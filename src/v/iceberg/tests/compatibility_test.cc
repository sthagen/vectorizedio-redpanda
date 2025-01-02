// Copyright 2024 Redpanda Data, Inc.
//
// Use of this software is governed by the Business Source License
// included in the file licenses/BSL.md
//
// As of the Change Date specified in that file, in accordance with
// the Business Source License, use of this software will be governed
// by the Apache License, Version 2.0

#include "iceberg/compatibility.h"
#include "iceberg/compatibility_types.h"
#include "iceberg/compatibility_utils.h"
#include "iceberg/datatypes.h"
#include "random/generators.h"

#include <absl/container/btree_map.h>
#include <fmt/format.h>
#include <gtest/gtest.h>

#include <iostream>
#include <unordered_set>
#include <vector>

using namespace iceberg;

namespace {

void reset_field_ids(struct_type& type) {
    std::ignore = for_each_field(type, [](nested_field* f) {
        f->id = nested_field::id_t{0};
        f->meta = std::nullopt;
    });
}

/**
 * Less strict than an equality check.
 *   - Collects the fields for each param in sorted order by ID
 *   - Checks that IDs are unique in both input structs
 *   - Checks that input structs have the same number of fields
 *   - Checks that corresponding (by ID) lhs fields are equivalent to rhs
 *     fields, matching name, type, nullability
 */
bool structs_equivalent(const struct_type& lhs, const struct_type& rhs) {
    using field_map_t
      = absl::btree_map<nested_field::id_t, const nested_field*>;

    auto collect_fields =
      [](const struct_type& s) -> std::pair<field_map_t, bool> {
        field_map_t fields;
        bool unique_ids = true;
        std::ignore = for_each_field(
          s, [&fields, &unique_ids](const nested_field* f) {
              auto res = fields.emplace(f->id, f);
              unique_ids = unique_ids && res.second;
          });
        return std::make_pair(std::move(fields), unique_ids);
    };

    auto [lhs_fields, lhs_uniq] = collect_fields(lhs);
    auto [rhs_fields, rhs_uniq] = collect_fields(rhs);

    if (!lhs_uniq || !rhs_uniq) {
        return false;
    }
    if (lhs_fields.size() != rhs_fields.size()) {
        return false;
    }

    static constexpr auto fields_equivalent =
      [](const nested_field* lf, const nested_field* rf) {
          if (
            lf->id != rf->id || lf->name != rf->name
            || lf->required != rf->required) {
              return false;
          }
          auto res = check_types(lf->type, rf->type);
          return !res.has_error() && res.value() == type_promoted::no;
      };

    return std::ranges::all_of(lhs_fields, [&rhs_fields](const auto lhs_pr) {
        auto rhs_it = rhs_fields.find(lhs_pr.first);
        if (rhs_it == rhs_fields.end()) {
            return false;
        }
        return fields_equivalent(lhs_pr.second, rhs_it->second);
    });
}

struct unique_id_generator {
    static constexpr int max = 100000;

    nested_field::id_t get_one() {
        int id = random_generators::get_int(1, max);
        while (used.contains(id)) {
            id = random_generators::get_int(1, max);
        }
        used.insert(id);
        return nested_field::id_t{id};
    }
    std::unordered_set<int> used;
};

bool updated(const nested_field& src, const nested_field& dest) {
    return std::holds_alternative<nested_field::src_info>(dest.meta)
           && std::get<nested_field::src_info>(dest.meta).id == dest.id
           && dest.id == src.id;
}

bool updated(const nested_field& dest) {
    return std::holds_alternative<nested_field::src_info>(dest.meta);
}

bool added(const nested_field& f) {
    return std::holds_alternative<nested_field::is_new>(f.meta);
}

bool removed(const nested_field& f) {
    return std::holds_alternative<nested_field::removed>(f.meta)
           && std::get<nested_field::removed>(f.meta)
                == nested_field::removed::yes;
}

template<typename T>
T& get(const nested_field_ptr& f) {
    vassert(
      std::holds_alternative<T>(f->type),
      "Unexpected variant type: {}",
      f->type.index());
    return std::get<T>(f->type);
}

using compat = ss::bool_class<struct compat_tag>;

struct field_test_case {
    field_test_case(
      field_type source, field_type dest, type_check_result expected)
      : source(std::move(source))
      , dest(std::move(dest))
      , expected(expected) {}

    field_test_case(const field_test_case& other)
      : source(make_copy(other.source))
      , dest(make_copy(other.dest))
      , expected(
          other.expected.has_error()
            ? type_check_result{other.expected.error()}
            : type_check_result{other.expected.value()}) {}

    field_test_case(field_test_case&&) = default;
    field_test_case& operator=(const field_test_case& other) = delete;
    field_test_case& operator=(field_test_case&&) = delete;
    ~field_test_case() = default;

    field_type source;
    field_type dest;
    type_check_result expected{compat_errc::mismatch};
};

std::ostream& operator<<(std::ostream& os, const field_test_case& ftc) {
    fmt::print(
      os,
      "{}->{} [expected: {}]",
      ftc.source,
      ftc.dest,
      ftc.expected.has_error()
        ? std::string{"ERROR"}
        : fmt::format("promoted={}", ftc.expected.value()));
    return os;
}
} // namespace

std::vector<field_test_case> generate_test_cases() {
    std::vector<field_test_case> test_data{};

    test_data.emplace_back(int_type{}, long_type{}, type_promoted::yes);
    test_data.emplace_back(int_type{}, boolean_type{}, compat_errc::mismatch);

    test_data.emplace_back(date_type{}, timestamp_type{}, type_promoted::yes);
    test_data.emplace_back(date_type{}, long_type{}, compat_errc::mismatch);

    test_data.emplace_back(float_type{}, double_type{}, type_promoted::yes);
    test_data.emplace_back(
      float_type{}, fixed_type{.length = 64}, compat_errc::mismatch);

    test_data.emplace_back(
      decimal_type{.precision = 10, .scale = 2},
      decimal_type{.precision = 20, .scale = 2},
      type_promoted::yes);
    test_data.emplace_back(
      decimal_type{.precision = 10, .scale = 2},
      decimal_type{.precision = 10, .scale = 2},
      type_promoted::no);
    test_data.emplace_back(
      decimal_type{.precision = 20, .scale = 2},
      decimal_type{.precision = 10, .scale = 2},
      compat_errc::mismatch);

    test_data.emplace_back(
      fixed_type{.length = 32}, fixed_type{.length = 32}, type_promoted::no);
    test_data.emplace_back(
      fixed_type{.length = 32},
      fixed_type{.length = 64},
      compat_errc::mismatch);
    test_data.emplace_back(
      fixed_type{.length = 64},
      fixed_type{.length = 32},
      compat_errc::mismatch);

    struct_type s1{};
    struct_type s2{};
    s2.fields.emplace_back(
      nested_field::create(0, "foo", field_required::yes, int_type{}));
    field_type l1 = list_type::create(0, field_required::yes, int_type{});
    field_type l2 = list_type::create(0, field_required::no, string_type{});
    field_type m1 = map_type::create(
      0, int_type{}, 0, field_required::yes, date_type{});
    field_type m2 = map_type::create(
      0, string_type{}, 0, field_required::no, timestamptz_type{});

    // NOTE: basic type check doesn't descend into non-primitive types
    // Checking stops at type ID - i.e. compat(struct, struct) == true,
    // compat(struct, list) == false.
    test_data.emplace_back(s1.copy(), s1.copy(), type_promoted::no);
    test_data.emplace_back(s1.copy(), s2.copy(), type_promoted::no);
    test_data.emplace_back(make_copy(l1), make_copy(l1), type_promoted::no);
    test_data.emplace_back(make_copy(l1), make_copy(l2), type_promoted::no);
    test_data.emplace_back(make_copy(m1), make_copy(m1), type_promoted::no);
    test_data.emplace_back(make_copy(m1), make_copy(m2), type_promoted::no);

    std::vector<field_type> non_promotable_types;
    non_promotable_types.emplace_back(boolean_type{});
    non_promotable_types.emplace_back(long_type{});
    non_promotable_types.emplace_back(double_type{});
    non_promotable_types.emplace_back(time_type{});
    non_promotable_types.emplace_back(timestamp_type{});
    non_promotable_types.emplace_back(timestamptz_type{});
    non_promotable_types.emplace_back(string_type{});
    non_promotable_types.emplace_back(uuid_type{});
    non_promotable_types.emplace_back(binary_type{});
    non_promotable_types.emplace_back(s1.copy());
    non_promotable_types.emplace_back(make_copy(l1));
    non_promotable_types.emplace_back(make_copy(m1));

    for (const auto& fta : non_promotable_types) {
        for (const auto& ftb : non_promotable_types) {
            if (fta == ftb) {
                continue;
            }
            test_data.emplace_back(
              make_copy(fta), make_copy(ftb), compat_errc::mismatch);
        }
    }

    return test_data;
}

template<typename T>
struct CompatibilityTest
  : ::testing::Test
  , testing::WithParamInterface<T> {};

using PrimitiveCompatibilityTest = CompatibilityTest<field_test_case>;

INSTANTIATE_TEST_SUITE_P(
  PrimitiveTypeCompatibilityTest,
  PrimitiveCompatibilityTest,
  ::testing::ValuesIn(generate_test_cases()));

TEST_P(PrimitiveCompatibilityTest, CompatibleTypesAreCompatible) {
    const auto& p = GetParam();

    auto res = check_types(p.source, p.dest);
    ASSERT_EQ(res.has_error(), p.expected.has_error());
    if (res.has_error()) {
        ASSERT_EQ(res.error(), p.expected.error());
    } else {
        ASSERT_EQ(res.value(), p.expected.value());
    }
}

namespace {

struct_type nested_test_struct() {
    unique_id_generator ids{};

    struct_type nested_struct;
    struct_type key_struct;
    key_struct.fields.emplace_back(nested_field::create(
      ids.get_one(), "baz", field_required::yes, int_type{}));

    struct_type nested_value_struct;
    nested_value_struct.fields.emplace_back(nested_field::create(
      ids.get_one(), "nmv1", field_required::yes, int_type{}));
    nested_value_struct.fields.emplace_back(nested_field::create(
      ids.get_one(), "nmv2", field_required::yes, string_type{}));

    nested_struct.fields.emplace_back(nested_field::create(
      ids.get_one(),
      "quux",
      field_required::yes,
      map_type::create(
        ids.get_one(),
        std::move(key_struct),
        ids.get_one(),
        field_required::yes,
        map_type::create(
          ids.get_one(),
          string_type{},
          ids.get_one(),
          field_required::yes,
          std::move(nested_value_struct)))));

    struct_type location_struct;
    location_struct.fields.emplace_back(nested_field::create(
      ids.get_one(), "latitude", field_required::yes, float_type{}));
    location_struct.fields.emplace_back(nested_field::create(
      ids.get_one(), "longitude", field_required::yes, float_type{}));
    nested_struct.fields.emplace_back(nested_field::create(
      ids.get_one(),
      "location",
      field_required::yes,
      list_type::create(
        ids.get_one(), field_required::yes, std::move(location_struct))));

    return nested_struct;
}
} // namespace
