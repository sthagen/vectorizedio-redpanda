// Copyright 2024 Redpanda Data, Inc.
//
// Use of this software is governed by the Business Source License
// included in the file licenses/BSL.md
//
// As of the Change Date specified in that file, in accordance with
// the Business Source License, use of this software will be governed
// by the Apache License, Version 2.0

#pragma once

#include "base/outcome.h"
#include "base/seastarx.h"

#include <seastar/util/bool_class.hh>

namespace iceberg {

enum class compat_errc {
    mismatch,
};

using type_promoted = ss::bool_class<struct type_promoted_tag>;
using type_check_result = checked<type_promoted, compat_errc>;

} // namespace iceberg
