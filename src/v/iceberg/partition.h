// Copyright 2024 Redpanda Data, Inc.
//
// Use of this software is governed by the Business Source License
// included in the file licenses/BSL.md
//
// As of the Change Date specified in that file, in accordance with
// the Business Source License, use of this software will be governed
// by the Apache License, Version 2.0
#pragma once

#include "container/fragmented_vector.h"
#include "iceberg/datatypes.h"
#include "iceberg/transform.h"
#include "utils/named_type.h"

#include <seastar/core/sstring.hh>

namespace iceberg {

struct unresolved_partition_spec {
    struct field {
        // Components of the nested source field name, in increasing depth
        // order.
        std::vector<ss::sstring> source_name;
        transform transform;
        ss::sstring name;

        friend std::ostream& operator<<(std::ostream&, const field&);
    };

    chunked_vector<field> fields;

    friend std::ostream&
    operator<<(std::ostream&, const unresolved_partition_spec&);
};

struct partition_field {
    using id_t = named_type<int32_t, struct field_id_tag>;
    nested_field::id_t source_id;
    id_t field_id;
    ss::sstring name;
    transform transform;

    friend bool operator==(const partition_field&, const partition_field&)
      = default;

    friend std::ostream& operator<<(std::ostream&, const partition_field&);
};

struct partition_spec {
    using id_t = named_type<int32_t, struct spec_id_tag>;
    id_t spec_id;
    chunked_vector<partition_field> fields;

    // NOTE: this function assumes that this is the first spec in the table.
    // Namely, the spec itself will get id 0, and partition fields will get
    // fresh ids starting from 1000.
    static std::optional<partition_spec>
    resolve(const unresolved_partition_spec&, const struct_type& schema_type);

    friend bool operator==(const partition_spec&, const partition_spec&)
      = default;

    friend std::ostream& operator<<(std::ostream&, const partition_spec&);

    partition_spec copy() const {
        return {
          .spec_id = spec_id,
          .fields = fields.copy(),
        };
    }
};

} // namespace iceberg
