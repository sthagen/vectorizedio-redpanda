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

#include "model/fundamental.h"
#include "storage/log.h"

#include <seastar/core/shared_ptr.hh>

namespace datalake::translation {

// Returns the equivalent log offset which can be considered translated by the
// datalake subsystem, while taking into account translator batch types, for a
// given kafka offset.
//
// Note that the provided kafka::offset o MUST be a valid offset, i.e one that
// has been produced to the log. This function will always return a value, and
// its correctness depends on the validity of the input offset.
//
// For example, in the following situation:
// Kaf offsets: [O]   .   .    .     [NKO]
// Log offsets: [K]  [C] [C] [C/TLO] [NKO]
// where O is the input offset, K is the last kafka record, C is a translator
// (Config) batch, TLO is the translated log offset, and NKO is the next
// expected kafka record. We should expect TLO to be equal to the offset of the
// last configuration batch before the next kafka record.
model::offset highest_log_offset_below_next(
  ss::shared_ptr<storage::log> log, kafka::offset o);

} // namespace datalake::translation
