/*
 * Copyright 2024 Redpanda Data, Inc.
 *
 * Use of this software is governed by the Business Source License
 * included in the file licenses/BSL.md
 *
 * As of the Change Date specified in that file, in accordance with
 * the Business Source License, use of this software will be governed
 * by the Apache License, Version 2.0
 */

#include "serde/parquet/value.h"

#include "utils/base64.h"

#include <seastar/util/variant_utils.hh>

auto fmt::formatter<serde::parquet::value>::format(
  const serde::parquet::value& val,
  fmt::format_context& ctx) const -> decltype(ctx.out()) {
    using namespace serde::parquet;
    return ss::visit(
      val,
      [&ctx](const null_value&) { return fmt::format_to(ctx.out(), "NULL"); },
      [&ctx](const boolean_value& v) {
          return fmt::format_to(ctx.out(), "{}", v.val);
      },
      [&ctx](const int32_value& v) {
          return fmt::format_to(ctx.out(), "{}", v.val);
      },
      [&ctx](const int64_value& v) {
          return fmt::format_to(ctx.out(), "{}", v.val);
      },
      [&ctx](const float32_value& v) {
          return fmt::format_to(ctx.out(), "{}", v.val);
      },
      [&ctx](const float64_value& v) {
          return fmt::format_to(ctx.out(), "{}", v.val);
      },
      [&ctx](const decimal_value& v) {
          return fmt::format_to(ctx.out(), "{}", v.val);
      },
      [&ctx](const date_value& v) {
          return fmt::format_to(ctx.out(), "{}", v.val);
      },
      [&ctx](const time_value& v) {
          return fmt::format_to(ctx.out(), "{}", v.val);
      },
      [&ctx](const timestamp_value& v) {
          return fmt::format_to(ctx.out(), "{}", v.val);
      },
      [&ctx](const string_value& v) {
          ss::sstring raw;
          for (const auto& e : v.val) {
              raw.append(e.get(), e.size());
          }
          return fmt::format_to(ctx.out(), "{}", raw);
      },
      [&ctx](const uuid_value& v) {
          return fmt::format_to(ctx.out(), "{}", v.val);
      },
      [&ctx](const byte_array_value& v) {
          return fmt::format_to(ctx.out(), "{}", iobuf_to_base64(v.val));
      },
      [&ctx](const fixed_byte_array_value& v) {
          return fmt::format_to(ctx.out(), "{}", iobuf_to_base64(v.val));
      },
      [&ctx](const group_value& v) {
          return fmt::format_to(ctx.out(), "({})", fmt::join(v, ", "));
      },
      [&ctx](const repeated_value& v) {
          return fmt::format_to(ctx.out(), "[{}]", fmt::join(v, ", "));
      });
}

auto fmt::formatter<serde::parquet::group_member>::format(
  const serde::parquet::group_member& f,
  fmt::format_context& ctx) const -> decltype(ctx.out()) {
    return fmt::format_to(ctx.out(), "{}", f.field);
}

auto fmt::formatter<serde::parquet::repeated_element>::format(
  const serde::parquet::repeated_element& e,
  fmt::format_context& ctx) const -> decltype(ctx.out()) {
    return fmt::format_to(ctx.out(), "{}", e.element);
}
