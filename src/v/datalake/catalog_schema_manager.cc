/*
 * Copyright 2024 Redpanda Data, Inc.
 *
 * Licensed as a Redpanda Enterprise file under the Redpanda Community
 * License (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 * https://github.com/redpanda-data/redpanda/blob/master/licenses/rcl.md
 */
#include "datalake/catalog_schema_manager.h"

#include "base/vlog.h"
#include "datalake/logger.h"
#include "datalake/table_definition.h"
#include "iceberg/compatibility.h"
#include "iceberg/datatypes.h"
#include "iceberg/field_collecting_visitor.h"
#include "iceberg/table_identifier.h"
#include "iceberg/transaction.h"

namespace datalake {

namespace {
schema_manager::errc log_and_convert_catalog_err(
  iceberg::catalog::errc e, std::string_view log_msg) {
    switch (e) {
    case iceberg::catalog::errc::shutting_down:
        vlog(datalake_log.debug, "{}: {}", log_msg, e);
        return schema_manager::errc::shutting_down;
    case iceberg::catalog::errc::timedout:
    case iceberg::catalog::errc::not_found:
    case iceberg::catalog::errc::io_error:
    case iceberg::catalog::errc::unexpected_state:
    case iceberg::catalog::errc::already_exists:
        vlog(datalake_log.warn, "{}: {}", log_msg, e);
        return schema_manager::errc::failed;
    }
}
enum class fill_errc {
    // There is a mismatch in a field's type, name, or required.
    invalid_schema,
    // We couldn't fill all the columns, but the ones we could all matched.
    // Or one or more columns were type-promoted (legally), so we'll need to
    // push an update into the catalog.
    schema_evolution_needed,
};

// Performs a simultaneous, depth-first iteration through fields of the two
// schemas, filling dest's field IDs with those from the source. Returns
// successfully if all the field IDs in the destination type are filled.
checked<std::nullopt_t, fill_errc>
fill_field_ids(iceberg::struct_type& dest, const iceberg::struct_type& source) {
    using namespace iceberg;
    chunked_vector<nested_field*> dest_stack;
    dest_stack.reserve(dest.fields.size());
    for (auto& f : std::ranges::reverse_view(dest.fields)) {
        dest_stack.emplace_back(f.get());
    }
    chunked_vector<nested_field*> source_stack;
    source_stack.reserve(source.fields.size());
    for (auto& f : std::ranges::reverse_view(source.fields)) {
        source_stack.emplace_back(f.get());
    }
    bool has_primitive_type_promotion{false};
    while (!source_stack.empty() && !dest_stack.empty()) {
        auto* dst = dest_stack.back();
        auto* src = source_stack.back();
        if (auto compatibility = check_types(src->type, dst->type);
            dst->name != src->name || dst->required != src->required
            || compatibility.has_error()) {
            return fill_errc::invalid_schema;
        } else if (compatibility.value() == type_promoted::yes) {
            has_primitive_type_promotion = true;
        }

        dst->id = src->id;
        dest_stack.pop_back();
        source_stack.pop_back();
        std::visit(reverse_field_collecting_visitor(dest_stack), dst->type);
        std::visit(reverse_field_collecting_visitor(source_stack), src->type);
    }

    if (!dest_stack.empty() || has_primitive_type_promotion) {
        // There are more fields to fill.
        return fill_errc::schema_evolution_needed;
    }
    // We successfully filled all the fields in the destination.
    return std::nullopt;
}
} // namespace

std::ostream& operator<<(std::ostream& o, const schema_manager::errc& e) {
    switch (e) {
    case schema_manager::errc::not_supported:
        return o << "schema_manager::errc::not_supported";
    case schema_manager::errc::failed:
        return o << "schema_manager::errc::failed";
    case schema_manager::errc::shutting_down:
        return o << "schema_manager::errc::shutting_down";
    }
}

bool schema_manager::table_info::fill_registered_ids(
  iceberg::struct_type& type) {
    auto fill_res = fill_field_ids(type, schema.schema_struct);
    return !fill_res.has_error();
}

ss::future<checked<std::nullopt_t, schema_manager::errc>>
simple_schema_manager::ensure_table_schema(
  const iceberg::table_identifier& table_id,
  const iceberg::struct_type& desired_type) {
    iceberg::schema s{
      .schema_struct = desired_type.copy(),
      .schema_id = {},
      .identifier_field_ids = {},
    };
    s.assign_fresh_ids();

    // TODO: check schema compatibility
    table_info_by_id.insert_or_assign(
      table_id.copy(),
      table_info{
        .id = table_id.copy(),
        .schema = std::move(s),
        .partition_spec = hour_partition_spec(),
      });

    co_return std::nullopt;
}

ss::future<checked<schema_manager::table_info, schema_manager::errc>>
simple_schema_manager::get_table_info(
  const iceberg::table_identifier& table_id) {
    auto it = table_info_by_id.find(table_id);
    if (it == table_info_by_id.end()) {
        co_return errc::failed;
    }
    co_return table_info{
      .id = it->second.id.copy(),
      .schema = it->second.schema.copy(),
      .partition_spec = it->second.partition_spec.copy(),
    };
}

ss::future<checked<std::nullopt_t, schema_manager::errc>>
catalog_schema_manager::ensure_table_schema(
  const iceberg::table_identifier& table_id,
  const iceberg::struct_type& desired_type) {
    auto load_res = co_await catalog_.load_or_create_table(
      table_id, desired_type, hour_partition_spec());
    if (load_res.has_error()) {
        co_return log_and_convert_catalog_err(
          load_res.error(), fmt::format("Error loading table {}", table_id));
    }

    // Check schema compatibility
    auto type_copy = desired_type.copy();
    auto get_res = get_ids_from_table_meta(
      table_id, load_res.value(), type_copy);
    if (get_res.has_error()) {
        co_return get_res.error();
    }
    if (get_res.value()) {
        // Success! Schema already matches what we need.
        co_return std::nullopt;
    }

    // The current table schema is a prefix of the desired schema. Add the
    // schema to the table.
    iceberg::transaction txn(std::move(load_res.value()));
    auto update_res = co_await txn.set_schema(iceberg::schema{
      .schema_struct = desired_type.copy(),
      .schema_id = iceberg::schema::unassigned_id,
      .identifier_field_ids = {},
    });
    if (update_res.has_error()) {
        auto msg = fmt::format(
          "Failed trying to apply schema update to table {}: {}",
          table_id,
          update_res.error());
        switch (update_res.error()) {
        case iceberg::action::errc::shutting_down:
            vlog(datalake_log.debug, "{}", msg);
            co_return errc::shutting_down;
        case iceberg::action::errc::io_failed:
        case iceberg::action::errc::unexpected_state:
            vlog(datalake_log.warn, "{}", msg);
            co_return errc::failed;
        }
    }
    auto commit_res = co_await catalog_.commit_txn(table_id, std::move(txn));
    if (commit_res.has_error()) {
        co_return log_and_convert_catalog_err(
          commit_res.error(),
          fmt::format(
            "Error while committing schema update to table {}", table_id));
    }
    co_return std::nullopt;
}

ss::future<checked<schema_manager::table_info, schema_manager::errc>>
catalog_schema_manager::get_table_info(
  const iceberg::table_identifier& table_id) {
    auto load_res = co_await catalog_.load_table(table_id);
    if (load_res.has_error()) {
        co_return log_and_convert_catalog_err(
          load_res.error(),
          fmt::format(
            "Error while reloading table {} after schema update", table_id));
    }
    const auto& table = load_res.value();

    auto cur_schema = table.get_schema(table.current_schema_id);
    if (!cur_schema) {
        vlog(
          datalake_log.error,
          "Cannot find current schema {} in table {}",
          table.current_schema_id,
          table_id);
        co_return errc::failed;
    }

    auto cur_spec = table.get_partition_spec(table.default_spec_id);
    if (!cur_spec) {
        vlog(
          datalake_log.error,
          "Cannot find default partition spec {} in table {}",
          table.default_spec_id,
          table_id);
        co_return errc::failed;
    }

    co_return table_info{
      .id = table_id.copy(),
      .schema = cur_schema->copy(),
      .partition_spec = cur_spec->copy(),
    };
}

checked<bool, schema_manager::errc>
catalog_schema_manager::get_ids_from_table_meta(
  const iceberg::table_identifier& table_id,
  const iceberg::table_metadata& table_meta,
  iceberg::struct_type& dest_type) {
    auto schema_iter = std::ranges::find(
      table_meta.schemas,
      table_meta.current_schema_id,
      &iceberg::schema::schema_id);
    if (schema_iter == table_meta.schemas.end()) {
        vlog(
          datalake_log.error,
          "Cannot find current schema {} in table {}",
          table_meta.current_schema_id,
          table_id);
        return errc::failed;
    }
    auto fill_res = fill_field_ids(dest_type, schema_iter->schema_struct);
    if (fill_res.has_error()) {
        switch (fill_res.error()) {
        case fill_errc::invalid_schema:
            vlog(datalake_log.warn, "Type mismatch with table {}", table_id);
            return errc::not_supported;
        case fill_errc::schema_evolution_needed:
            return false;
        }
    }
    return true;
}

} // namespace datalake
