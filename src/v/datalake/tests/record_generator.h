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

#include "base/seastarx.h"
#include "bytes/iobuf.h"
#include "container/chunked_hash_map.h"
#include "model/timestamp.h"
#include "pandaproxy/schema_registry/types.h"
#include "serde/avro/tests/data_generator.h"
#include "serde/protobuf/tests/data_generator.h"
#include "storage/record_batch_builder.h"
#include "utils/absl_sstring_hash.h"
#include "utils/named_type.h"

#include <seastar/core/future.hh>

namespace schema {
class registry;
} // namespace schema

namespace datalake::tests {

class record_generator {
public:
    explicit record_generator(schema::registry* sr)
      : _sr(sr) {}
    using error = named_type<ss::sstring, struct error_tag>;

    // Registers the given schema with the given name.
    ss::future<checked<std::nullopt_t, error>>
    register_avro_schema(std::string_view name, std::string_view schema);

    // Registers the given schema with the given name.
    ss::future<checked<std::nullopt_t, error>>
    register_protobuf_schema(std::string_view name, std::string_view schema);

    // Adds a record of the given schema to the builder.
    ss::future<checked<std::nullopt_t, error>> add_random_avro_record(
      storage::record_batch_builder&,
      std::string_view schema_name,
      std::optional<iobuf> key,
      testing::avro_generator_config config = {});

    // Adds a record of the given schema to the builder.
    ss::future<checked<std::nullopt_t, error>> add_random_protobuf_record(
      storage::record_batch_builder&,
      std::string_view schema_name,
      const std::vector<int32_t>& message_index,
      std::optional<iobuf> key,
      testing::protobuf_generator_config config = {});

private:
    chunked_hash_map<
      ss::sstring,
      pandaproxy::schema_registry::schema_id,
      sstring_hash,
      sstring_eq>
      _id_by_name;
    schema::registry* _sr;
};

} // namespace datalake::tests
