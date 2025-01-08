// Copyright 2024 Redpanda Data, Inc.
//
// Use of this software is governed by the Business Source License
// included in the file licenses/BSL.md
//
// As of the Change Date specified in that file, in accordance with
// the Business Source License, use of this software will be governed
// by the Apache License, Version 2.0

#include "iceberg/compatibility_types.h"

namespace iceberg {

std::string_view to_string_view(schema_evolution_errc ec) {
    switch (ec) {
    case schema_evolution_errc::type_mismatch:
        return "schema_evolution_errc::type_mismatch";
    case schema_evolution_errc::incompatible:
        return "schema_evolution_errc::incompatible";
    case schema_evolution_errc::ambiguous:
        return "schema_evolution_errc::ambiguous";
    case schema_evolution_errc::violates_map_key_invariant:
        return "schema_evolution_errc::violates_map_key_invariant";
    case schema_evolution_errc::new_required_field:
        return "schema_evolution_errc::new_required_field";
    case schema_evolution_errc::null_nested_field:
        return "schema_evolution_errc::null_nested_field";
    case schema_evolution_errc::invalid_state:
        return "schema_evolution_errc::invalid_state";
    }
}

schema_transform_state&
operator+=(schema_transform_state& lhs, const schema_transform_state& rhs) {
    lhs.n_removed += rhs.n_removed;
    lhs.n_added += rhs.n_added;
    lhs.n_promoted += rhs.n_promoted;
    return lhs;
}

} // namespace iceberg

auto fmt::formatter<iceberg::schema_evolution_errc>::format(
  iceberg::schema_evolution_errc ec,
  format_context& ctx) const -> format_context::iterator {
    return formatter<string_view>::format(iceberg::to_string_view(ec), ctx);
}
