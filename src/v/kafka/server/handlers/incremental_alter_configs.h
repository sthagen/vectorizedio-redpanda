/*
 * Copyright 2021 Vectorized, Inc.
 *
 * Use of this software is governed by the Business Source License
 * included in the file licenses/BSL.md
 *
 * As of the Change Date specified in that file, in accordance with
 * the Business Source License, use of this software will be governed
 * by the Apache License, Version 2.0
 */
#pragma once
#include "kafka/protocol/incremental_alter_configs.h"
#include "kafka/server/handlers/handler.h"

namespace kafka {

using incremental_alter_configs_handler
  = handler<incremental_alter_configs_api, 0, 0>;

}
