// Copyright 2024 Redpanda Data, Inc.
//
// Use of this software is governed by the Business Source License
// included in the file licenses/BSL.md
//
// As of the Change Date specified in that file, in accordance with
// the Business Source License, use of this software will be governed
// by the Apache License, Version 2.0

#include "iceberg/compatibility.h"
#include "iceberg/compatibility_utils.h"
#include "iceberg/datatypes.h"
#include "iceberg/tests/test_schemas.h"

#include <fmt/format.h>
#include <gtest/gtest.h>

using namespace iceberg;

TEST(CompatUtilsTests, CanForEachField) {
    auto s = std::get<struct_type>(test_nested_schema_type());

    {
        int n = 0;
        auto res = for_each_field(s, [&n](nested_field* f) {
            f->set_evolution_metadata(nested_field::is_new{});
            ++n;
        });
        ASSERT_FALSE(res.has_error());
        EXPECT_EQ(n, 17);
    }

    {
        const auto& s_ref = s;
        auto res = for_each_field(s_ref, [](const nested_field* f) {
            ASSERT_TRUE(f->has_evolution_metadata());
            EXPECT_TRUE(std::holds_alternative<nested_field::is_new>(f->meta));
        });
        ASSERT_FALSE(res.has_error());
    }

    {
        int n = 0;
        auto res = for_each_field(
          s,
          [&n](const nested_field*) { ++n; },
          [](const nested_field* f) -> bool {
              return !f->has_evolution_metadata();
          });
        ASSERT_FALSE(res.has_error());
        EXPECT_EQ(n, 0);
    }

    {
        int n = 0;
        const auto& s_ref = s;
        auto res = for_each_field(
          s_ref,
          [&n](const nested_field*)
            -> checked<std::nullopt_t, schema_evolution_errc> {
              ++n;
              return schema_evolution_errc::invalid_state;
          });
        ASSERT_TRUE(res.has_error());
        EXPECT_EQ(n, 1);
    }
}

TEST(CompatUtilsTests, ForEachFieldHandlesNullFields) {
    struct_type outer{};

    struct_type inner{};
    inner.fields.emplace_back(
      nested_field::create(0, "f1", field_required::no, int_type{}));
    inner.fields.emplace_back(nullptr);
    inner.fields.emplace_back(
      nested_field::create(0, "f1", field_required::no, int_type{}));

    outer.fields.emplace_back(
      nested_field::create(0, "inner", field_required::no, std::move(inner)));

    {
        auto f = nested_field::create(
          0, "outer", field_required::no, outer.copy());

        auto res = for_each_field(*f, [](nested_field* f) {
            // we shouldn't reach here when f is null
            ASSERT_NE(f, nullptr);
        });

        ASSERT_TRUE(res.has_error());
        EXPECT_EQ(res.error(), schema_evolution_errc::null_nested_field);
    }

    {
        outer.fields.emplace_back(nullptr);
        auto res = for_each_field(
          outer, [](nested_field* f) { ASSERT_NE(f, nullptr); });

        ASSERT_TRUE(res.has_error());
        EXPECT_EQ(res.error(), schema_evolution_errc::null_nested_field);
    }
}
