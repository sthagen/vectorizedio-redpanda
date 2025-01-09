/*
 * Copyright 2024 Redpanda Data, Inc.
 *
 * Licensed as a Redpanda Enterprise file under the Redpanda Community
 * License (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 * https://github.com/redpanda-data/redpanda/blob/master/licenses/rcl.md
 */
#include "datalake/partitioning_writer.h"

#include "base/vlog.h"
#include "datalake/data_writer_interface.h"
#include "datalake/logger.h"
#include "datalake/table_definition.h"
#include "iceberg/struct_accessor.h"

#include <exception>

namespace datalake {

std::ostream&
operator<<(std::ostream& o, const partitioning_writer::partitioned_file& f) {
    fmt::print(
      o,
      "{{local_file: {}, schema_id: {}, partition_spec_id: {}}}",
      f.local_file,
      f.schema_id,
      f.partition_spec_id);
    return o;
}

ss::future<writer_error>
partitioning_writer::add_data(iceberg::struct_value val, int64_t approx_size) {
    iceberg::partition_key pk;
    try {
        pk = iceberg::partition_key::create(val, accessors_, spec_);
    } catch (...) {
        vlog(
          datalake_log.error,
          "Error {} while partitioning value: {}",
          std::current_exception(),
          val);
        co_return writer_error::parquet_conversion_error;
    }
    auto writer_iter = writers_.find(pk);
    if (writer_iter == writers_.end()) {
        auto writer_res = co_await writer_factory_.create_writer(type_);
        if (writer_res.has_error()) {
            vlog(
              datalake_log.error,
              "Failed to create new writer: {}",
              writer_res.error());
            co_return writer_res.error();
        }
        auto new_iter = writers_.emplace(
          pk.copy(), std::move(writer_res.value()));
        writer_iter = new_iter.first;
    }
    auto& writer = writer_iter->second;
    auto write_res = co_await writer->add_data_struct(
      std::move(val), approx_size);
    if (write_res != writer_error::ok) {
        vlog(datalake_log.error, "Failed to add data: {}", write_res);
        co_return write_res;
    }
    co_return write_res;
}

ss::future<
  result<chunked_vector<partitioning_writer::partitioned_file>, writer_error>>
partitioning_writer::finish() && {
    chunked_vector<partitioned_file> files;
    auto first_error = writer_error::ok;
    // TODO: parallelize me!
    for (auto& [pk, writer] : writers_) {
        auto file_res = co_await writer->finish();
        if (file_res.has_error()) {
            vlog(
              datalake_log.error,
              "Failed to finish writer: {}",
              file_res.error());
            if (first_error == writer_error::ok) {
                first_error = file_res.error();
            }
            // Even on error, move on so that we can close all the writers.
            continue;
        }

        files.push_back(partitioned_file{
          .local_file = std::move(file_res.value()),
          .schema_id = schema_id_,
          .partition_spec_id = spec_.spec_id,
          .partition_key = std::move(pk),
        });
    }
    if (first_error != writer_error::ok) {
        co_return first_error;
    }
    co_return files;
}

} // namespace datalake
