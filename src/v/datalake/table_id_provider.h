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

#include "iceberg/table_identifier.h"
#include "model/fundamental.h"

namespace datalake {

class table_id_provider {
public:
    static iceberg::table_identifier table_id(model::topic t) {
        return {
          // TODO: namespace as a topic property? Keep it in the table metadata?
          .ns = {"redpanda"},
          .table = std::move(t),
        };
    }

    static iceberg::table_identifier dlq_table_id(const model::topic& t) {
        return {
          // TODO: namespace as a topic property? Keep it in the table metadata?
          .ns = {"redpanda"},
          .table = fmt::format("{}~dlq", t()),
        };
    }
};

} // namespace datalake
