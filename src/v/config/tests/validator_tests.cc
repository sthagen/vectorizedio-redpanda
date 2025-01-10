// Copyright 2022 Redpanda Data, Inc.
//
// Use of this software is governed by the Business Source License
// included in the file licenses/BSL.md
//
// As of the Change Date specified in that file, in accordance with
// the Business Source License, use of this software will be governed
// by the Apache License, Version 2.0

#include "config/validators.h"

#include <seastar/testing/thread_test_case.hh>

SEASTAR_THREAD_TEST_CASE(test_empty_string_vec) {
    using config::validate_non_empty_string_vec;
    BOOST_TEST(!(validate_non_empty_string_vec({"apple", "pear"}).has_value()));
    BOOST_TEST(validate_non_empty_string_vec({"apple", ""}).has_value());
    BOOST_TEST(validate_non_empty_string_vec({"", "pear"}).has_value());
    BOOST_TEST(
      validate_non_empty_string_vec({"apple", "", "pear"}).has_value());
}

SEASTAR_THREAD_TEST_CASE(test_empty_string_opt) {
    using config::validate_non_empty_string_opt;
    BOOST_TEST(!validate_non_empty_string_opt(std::nullopt).has_value());
    BOOST_TEST(!validate_non_empty_string_opt("apple").has_value());
    BOOST_TEST(validate_non_empty_string_opt("").has_value());
}

SEASTAR_THREAD_TEST_CASE(test_audit_event_types) {
    using config::validate_audit_event_types;
    BOOST_TEST(!validate_audit_event_types({"management",
                                            "produce",
                                            "consume",
                                            "describe",
                                            "heartbeat",
                                            "authenticate"})
                  .has_value());
    std::vector<ss::sstring> random_strings{"asdf", "fda", "hello", "world"};
    BOOST_TEST(validate_audit_event_types(random_strings).has_value());

    std::vector<ss::sstring> one_bad_apple{
      "management", "consume", "hello world", "heartbeat"};
    BOOST_TEST(validate_audit_event_types(one_bad_apple).has_value());
}
