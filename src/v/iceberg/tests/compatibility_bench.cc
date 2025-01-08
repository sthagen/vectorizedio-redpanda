// Copyright 2024 Redpanda Data, Inc.
//
// Use of this software is governed by the Business Source License
// included in the file licenses/BSL.md
//
// As of the Change Date specified in that file, in accordance with
// the Business Source License, use of this software will be governed
// by the Apache License, Version 2.0

#include "iceberg/compatibility.h"
#include "iceberg/datatypes.h"
#include "iceberg/field_collecting_visitor.h"
#include "iceberg/tests/test_schemas.h"

#include <seastar/testing/perf_tests.hh>

using namespace iceberg;

struct_type gen_nested_struct(int nesting_level, int n_at_each) {
    struct_type result;
    struct_type* curr = &result;
    for (auto i : std::views::iota(0, nesting_level)) {
        for (auto j : std::views::iota(0, n_at_each)) {
            auto idx = i * n_at_each + j;
            auto t = [](auto j) -> field_type {
                if (j == 0) {
                    return struct_type{};
                } else {
                    return int_type{};
                }
            }(j);
            curr->fields.emplace_back(nested_field::create(
              idx, std::to_string(idx), field_required::no, std::move(t)));
        }
        curr = &std::get<struct_type>(curr->fields.front()->type);
    }
    return result;
}

void run_basic_field_assign_bench(const struct_type& source) {
    auto dest = source.copy();

    checked<std::nullopt_t, schema_evolution_errc> errc{std::nullopt};

    perf_tests::start_measuring_time();
    chunked_vector<nested_field*> source_stack;
    source_stack.reserve(source.fields.size());
    for (auto& f : std::ranges::reverse_view(source.fields)) {
        source_stack.emplace_back(f.get());
    }
    chunked_vector<nested_field*> dest_stack;
    dest_stack.reserve(dest.fields.size());
    for (auto& f : std::ranges::reverse_view(dest.fields)) {
        dest_stack.emplace_back(f.get());
    }
    while (!source_stack.empty() && !dest_stack.empty()) {
        auto* dst = dest_stack.back();
        auto* src = source_stack.back();
        if (auto compatibility = check_types(src->type, dst->type);
            dst->name != src->name || dst->required != src->required
            || compatibility.has_error()) {
            errc = schema_evolution_errc::type_mismatch;
            break;
        }

        dst->id = src->id;
        dest_stack.pop_back();
        source_stack.pop_back();
        std::visit(reverse_field_collecting_visitor(dest_stack), dst->type);
        std::visit(reverse_field_collecting_visitor(source_stack), src->type);
    }
    perf_tests::stop_measuring_time();

    vassert(!errc.has_error(), "Expected success");
}

void run_describe_transform_bench(const struct_type& source) {
    auto dest = source.copy();

    perf_tests::start_measuring_time();
    auto res = annotate_schema_transform(source, dest);
    perf_tests::stop_measuring_time();

    vassert(!res.has_error(), "Expected success");
}

void run_apply_transform_bench(const struct_type& source) {
    auto dest = source.copy();
    auto xform = annotate_schema_transform(source, dest);
    vassert(!xform.has_error(), "Expected success");

    perf_tests::start_measuring_time();
    auto res = validate_schema_transform(dest);
    perf_tests::stop_measuring_time();

    vassert(!res.has_error(), "Expected success");
}

PERF_TEST(StructVisitation, BasicFieldAssign) {
    auto source = std::get<struct_type>(test_nested_schema_type());
    run_basic_field_assign_bench(source);
}

PERF_TEST(StructVisitation, DescribeTransform) {
    auto source = std::get<struct_type>(test_nested_schema_type());
    run_describe_transform_bench(source);
}

PERF_TEST(StructVisitation, ApplyTransform) {
    auto source = std::get<struct_type>(test_nested_schema_type());
    run_apply_transform_bench(source);
}

PERF_TEST(StructVisitation, BasicFieldAssignNested_5_1) {
    auto source = gen_nested_struct(5, 1);
    run_basic_field_assign_bench(source);
}

PERF_TEST(StructVisitation, DescribeTransformNested_5_1) {
    auto source = gen_nested_struct(5, 1);
    run_describe_transform_bench(source);
}

PERF_TEST(StructVisitation, ApplyTransformNested_5_1) {
    auto source = gen_nested_struct(5, 1);
    run_apply_transform_bench(source);
}

PERF_TEST(StructVisitation, BasicFieldAssignNested_10_1) {
    auto source = gen_nested_struct(10, 1);
    run_basic_field_assign_bench(source);
}

PERF_TEST(StructVisitation, DescribeTransformNested_10_1) {
    auto source = gen_nested_struct(10, 1);
    run_describe_transform_bench(source);
}

PERF_TEST(StructVisitation, ApplyTransformNested_10_1) {
    auto source = gen_nested_struct(10, 1);
    run_apply_transform_bench(source);
}

PERF_TEST(StructVisitation, BasicFieldAssignNested_20_1) {
    auto source = gen_nested_struct(20, 1);
    run_basic_field_assign_bench(source);
}

PERF_TEST(StructVisitation, DescribeTransformNested_20_1) {
    auto source = gen_nested_struct(20, 1);
    run_describe_transform_bench(source);
}

PERF_TEST(StructVisitation, ApplyTransformNested_20_1) {
    auto source = gen_nested_struct(20, 1);
    run_apply_transform_bench(source);
}

PERF_TEST(StructVisitation, BasicFieldAssignNested_5_5) {
    auto source = gen_nested_struct(5, 5);
    run_basic_field_assign_bench(source);
}

PERF_TEST(StructVisitation, DescribeTransformNested_5_5) {
    auto source = gen_nested_struct(5, 5);
    run_describe_transform_bench(source);
}

PERF_TEST(StructVisitation, ApplyTransformNested_5_5) {
    auto source = gen_nested_struct(5, 5);
    run_apply_transform_bench(source);
}

PERF_TEST(StructVisitation, BasicFieldAssignNested_10_5) {
    auto source = gen_nested_struct(10, 5);
    run_basic_field_assign_bench(source);
}

PERF_TEST(StructVisitation, DescribeTransformNested_10_5) {
    auto source = gen_nested_struct(10, 5);
    run_describe_transform_bench(source);
}

PERF_TEST(StructVisitation, ApplyTransformNested_10_5) {
    auto source = gen_nested_struct(10, 5);
    run_apply_transform_bench(source);
}

PERF_TEST(StructVisitation, BasicFieldAssignNested_20_5) {
    auto source = gen_nested_struct(20, 5);
    run_basic_field_assign_bench(source);
}

PERF_TEST(StructVisitation, DescribeTransformNested_20_5) {
    auto source = gen_nested_struct(20, 5);
    run_describe_transform_bench(source);
}

PERF_TEST(StructVisitation, ApplyTransformNested_20_5) {
    auto source = gen_nested_struct(20, 5);
    run_apply_transform_bench(source);
}

PERF_TEST(StructVisitation, BasicFieldAssignNested_100_5) {
    auto source = gen_nested_struct(100, 5);
    run_basic_field_assign_bench(source);
}

PERF_TEST(StructVisitation, DescribeTransformNested_100_5) {
    auto source = gen_nested_struct(100, 5);
    run_describe_transform_bench(source);
}

PERF_TEST(StructVisitation, ApplyTransformNested_100_5) {
    auto source = gen_nested_struct(100, 5);
    run_apply_transform_bench(source);
}

PERF_TEST(StructVisitation, BasicFieldAssignNested_100_20) {
    auto source = gen_nested_struct(100, 20);
    run_basic_field_assign_bench(source);
}

PERF_TEST(StructVisitation, DescribeTransformNested_100_20) {
    auto source = gen_nested_struct(100, 20);
    run_describe_transform_bench(source);
}

PERF_TEST(StructVisitation, ApplyTransformNested_100_20) {
    auto source = gen_nested_struct(100, 20);
    run_apply_transform_bench(source);
}
