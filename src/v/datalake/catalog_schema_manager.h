/*
 * Copyright 2024 Redpanda Data, Inc.
 *
 * Licensed as a Redpanda Enterprise file under the Redpanda Community
 * License (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 * https://github.com/redpanda-data/redpanda/blob/master/licenses/rcl.md
 */
#pragma once

#include "iceberg/catalog.h"
#include "iceberg/datatypes.h"
#include "iceberg/partition.h"
#include "iceberg/table_identifier.h"

namespace datalake {

class schema_manager {
public:
    enum class errc {
        // The requested operation is not supported (e.g. unsupported schema
        // evolution).
        not_supported,
        // The operation failed because of a subsystem failure.
        failed,
        // The system is shutting down.
        shutting_down,
    };
    friend std::ostream& operator<<(std::ostream&, const errc&);

    virtual ss::future<checked<std::nullopt_t, errc>> ensure_table_schema(
      const iceberg::table_identifier&,
      const iceberg::struct_type& desired_type,
      const iceberg::unresolved_partition_spec&)
      = 0;

    struct table_info {
        iceberg::table_identifier id;
        iceberg::schema schema;
        iceberg::partition_spec partition_spec;

        // Fills the field IDs of the given type with those in the current
        // schema. Returns true on success.
        bool fill_registered_ids(iceberg::struct_type&);
    };

    virtual ss::future<checked<table_info, errc>>
    get_table_info(const iceberg::table_identifier&) = 0;

    virtual ~schema_manager() = default;
};

// Used in unit tests
class simple_schema_manager : public schema_manager {
public:
    ss::future<checked<std::nullopt_t, schema_manager::errc>>
    ensure_table_schema(
      const iceberg::table_identifier&,
      const iceberg::struct_type& desired_type,
      const iceberg::unresolved_partition_spec&) override;

    ss::future<checked<table_info, schema_manager::errc>>
    get_table_info(const iceberg::table_identifier&) override;

private:
    chunked_hash_map<iceberg::table_identifier, table_info> table_info_by_id;
};

// Manages interactions with the catalog when reconciling the current schema of
// a given table. This is where Redpanda should make decisions about schema
// evolution.
class catalog_schema_manager : public schema_manager {
public:
    explicit catalog_schema_manager(iceberg::catalog& catalog)
      : catalog_(catalog) {}

    // Create the table with a desired schema, or, if the table exists and its
    // current schema doesn't include all of the fields (e.g. we are going from
    // the schemaless schema to a schema containing user fields), the table's
    // schema is updated to the desired type.
    ss::future<checked<std::nullopt_t, schema_manager::errc>>
    ensure_table_schema(
      const iceberg::table_identifier&,
      const iceberg::struct_type& desired_type,
      const iceberg::unresolved_partition_spec&) override;

    // Loads the table metadata for the given topic.
    ss::future<checked<table_info, schema_manager::errc>>
    get_table_info(const iceberg::table_identifier&) override;

private:
    // Attempts to fill the field ids in the given type with those from the
    // current schema of the given table metadata.
    //
    // Returns true if successful, false if the fill is incomplete because the
    // table schema does not have all the necessary fields. The latter is a
    // signal that the caller needs to add the schema to the table.
    checked<bool, errc> get_ids_from_table_meta(
      const iceberg::table_identifier&,
      const iceberg::table_metadata&,
      iceberg::struct_type&);

    iceberg::catalog& catalog_;
};

} // namespace datalake
