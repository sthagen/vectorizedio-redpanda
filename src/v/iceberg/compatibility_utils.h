// Copyright 2024 Redpanda Data, Inc.
//
// Use of this software is governed by the Business Source License
// included in the file licenses/BSL.md
//
// As of the Change Date specified in that file, in accordance with
// the Business Source License, use of this software will be governed
// by the Apache License, Version 2.0

#pragma once

#include "base/vassert.h"
#include "iceberg/compatibility_types.h"
#include "iceberg/datatypes.h"
#include "iceberg/field_collecting_visitor.h"

#include <seastar/util/variant_utils.hh>

namespace iceberg {

namespace detail {

template<typename Fn, typename T>
concept VoidFieldFn = requires(T t, Fn fn) {
    { fn(t) } -> std::same_as<void>;
};

template<typename Fn, typename T>
concept CheckedFieldFn = requires(T t, Fn fn) {
    { fn(t) } -> std::same_as<schema_errc_result>;
};

template<typename Fn>
concept FieldPredicate = requires(const nested_field* t, Fn fn) {
    { fn(t) } -> std::same_as<bool>;
};

template<typename Fn, typename T>
concept FieldOp = VoidFieldFn<Fn, T> || CheckedFieldFn<Fn, T>;

template<typename T>
concept StructType = std::is_same_v<std::decay_t<T>, struct_type>;

template<typename T>
concept NestedFieldType = std::is_same_v<std::decay_t<T>, nested_field>;

template<NestedFieldType T>
requires std::is_same_v<std::decay_t<T>, nested_field>
schema_errc_result for_each_field_impl(
  T* field,
  detail::FieldOp<T*> auto&& fn,
  detail::FieldPredicate auto&& filter,
  chunked_vector<T*>& stk) {
    constexpr bool is_void_fn = detail::VoidFieldFn<decltype(fn), T*>;

    stk.emplace_back(field);
    while (!stk.empty()) {
        auto* dst = stk.back();
        stk.pop_back();
        if (dst == nullptr) {
            return schema_evolution_errc::null_nested_field;
        } else if (!std::invoke(filter, dst)) {
            continue;
        }
        if constexpr (is_void_fn) {
            std::invoke(fn, dst);
        } else if (auto res = std::invoke(fn, dst); res.has_error()) {
            return res.error();
        }

        if constexpr (std::is_const_v<T>) {
            std::visit(reverse_const_field_collecting_visitor{stk}, dst->type);
        } else {
            std::visit(reverse_field_collecting_visitor(stk), dst->type);
        }
    }
    return std::nullopt;
}

template<NestedFieldType T, StructType S>
schema_errc_result for_each_field_impl(
  S& s, detail::FieldOp<T*> auto&& fn, detail::FieldPredicate auto&& filter) {
    static_assert(std::is_const_v<S> == std::is_const_v<T>);
    chunked_vector<T*> stk;
    for (const auto& f : std::ranges::reverse_view(s.fields)) {
        if (f == nullptr) {
            return schema_evolution_errc::null_nested_field;
        } else if (!std::invoke(filter, f.get())) {
            continue;
        } else if (auto res = detail::for_each_field_impl<T>(
                     f.get(), fn, filter, stk);
                   res.has_error()) {
            return res.error();
        }
    }
    return std::nullopt;
}

} // namespace detail

/**
 * for_each_field - Apply some function to a field and all fields
 * nested under it in a depth first fashion.
 *
 * Statically enforces const correctness between input field/struct and the
 * supplied function.
 *
 * Returns an error if it encounters a nullptr nested_field at any point.
 *
 * The return type of the function must be either void or
 * checked<nullopt_t, schema_evolution_errc>. In the latter case, iteration
 * short-circuits if the function returns an error for some field.
 *
 * @param field   - Start here
 * @param fn      - Function to apply
 * @param filter  - Filter input fields on a predicate
 *
 * @return std::nullopt (indicating success) or an error code
 */
template<typename T>
requires std::is_same_v<std::decay_t<T>, nested_field>
schema_errc_result for_each_field(
  T& field,
  detail::FieldOp<T*> auto&& fn,
  detail::FieldPredicate auto&& filter) {
    chunked_vector<T*> stk{};
    return detail::for_each_field_impl(&field, fn, filter, stk);
}

/**
 * for_each_field - Overload providing a default filter (always true)
 */
template<detail::NestedFieldType T>
schema_errc_result for_each_field(T& field, detail::FieldOp<T*> auto&& fn) {
    chunked_vector<T*> stk{};
    return detail::for_each_field_impl(
      &field, fn, [](const nested_field*) { return true; }, stk);
}

/**
 * for_each_field - Overloads for providing a struct_type entry point.
 * Same semantics as the nested_field version.
 */
schema_errc_result for_each_field(
  struct_type& s,
  detail::FieldOp<nested_field*> auto&& fn,
  std::function<bool(const nested_field*)> filter) {
    return detail::for_each_field_impl<nested_field>(s, fn, filter);
}

schema_errc_result
for_each_field(struct_type& s, detail::FieldOp<nested_field*> auto&& fn) {
    return detail::for_each_field_impl<nested_field>(
      s, fn, [](const nested_field*) { return true; });
}

schema_errc_result for_each_field(
  const struct_type& s,
  detail::FieldOp<const nested_field*> auto&& fn,
  std::function<bool(const nested_field*)> filter) {
    return detail::for_each_field_impl<const nested_field>(s, fn, filter);
}

schema_errc_result for_each_field(
  const struct_type& s, detail::FieldOp<const nested_field*> auto&& fn) {
    return detail::for_each_field_impl<const nested_field>(
      s, fn, [](const nested_field*) { return true; });
}

} // namespace iceberg
