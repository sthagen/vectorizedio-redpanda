/*
 * Copyright 2024 Redpanda Data, Inc.
 *
 * Licensed as a Redpanda Enterprise file under the Redpanda Community
 * License (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 * https://github.com/redpanda-data/redpanda/blob/master/licenses/rcl.md
 */
#include "datalake/table_creator.h"

namespace datalake {

std::ostream& operator<<(std::ostream& o, const table_creator::errc& e) {
    switch (e) {
    case table_creator::errc::incompatible_schema:
        return o << "table_creator::errc::incompatible_schema";
    case table_creator::errc::failed:
        return o << "table_creator::errc::failed";
    case table_creator::errc::shutting_down:
        return o << "table_creator::errc::shutting_down";
    }
}

} // namespace datalake
