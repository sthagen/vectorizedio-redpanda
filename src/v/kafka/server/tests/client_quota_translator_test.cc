// Copyright 2024 Redpanda Data, Inc.
//
// Use of this software is governed by the Business Source License
// included in the file licenses/BSL.md
//
// As of the Change Date specified in that file, in accordance with
// the Business Source License, use of this software will be governed
// by the Apache License, Version 2.0

#include "base/seastarx.h"
#include "cluster/client_quota_serde.h"
#include "cluster/client_quota_store.h"
#include "config/configuration.h"
#include "kafka/server/client_quota_translator.h"

#include <seastar/testing/thread_test_case.hh>

#include <boost/test/auto_unit_test.hpp>
#include <boost/test/test_tools.hpp>
#include <boost/test/unit_test.hpp>

#include <variant>

using namespace kafka;

using cluster::client_quota::entity_key;
using cluster::client_quota::entity_value;

static const auto default_key = entity_key(
  entity_key::client_id_default_match{});

const ss::sstring test_client_id = "franz-go";
const tracker_key test_client_id_key = k_client_id{test_client_id};

constexpr auto P_DEF = 1111;
constexpr auto F_DEF = 2222;
constexpr auto PM_DEF = 3333;

// Helper for checking std::variant types for equality
const auto CHECK_VARIANT_EQ = [](auto expected, auto got) {
    BOOST_CHECK_EQUAL(expected, get<decltype(expected)>(got));
};

struct fixture {
    ss::sharded<cluster::client_quota::store> quota_store;
    kafka::client_quota_translator tr;

    fixture()
      : tr(std::ref(quota_store)) {
        quota_store.start().get();
    }

    ~fixture() { quota_store.stop().get(); }
};

SEASTAR_THREAD_TEST_CASE(quota_translator_default_test) {
    fixture f;

    auto default_limits = client_quota_limits{
      .produce_limit = std::nullopt,
      .fetch_limit = std::nullopt,
      .partition_mutation_limit = std::nullopt,
    };
    auto key = f.tr.find_quota_key(
      {client_quota_type::produce_quota, test_client_id});
    auto limits = f.tr.find_quota_value(key);
    BOOST_CHECK_EQUAL(test_client_id_key, key);
    BOOST_CHECK_EQUAL(default_limits, limits);
    BOOST_CHECK(f.tr.is_empty());
}

SEASTAR_THREAD_TEST_CASE(quota_translator_modified_default_test) {
    fixture f;

    auto default_values = entity_value{
      .producer_byte_rate = 1111,
      .consumer_byte_rate = 2222,
      .controller_mutation_rate = 3333,
    };
    f.quota_store.local().set_quota(default_key, default_values);

    auto expected_limits = client_quota_limits{
      .produce_limit = 1111,
      .fetch_limit = 2222,
      .partition_mutation_limit = 3333,
    };
    auto key = f.tr.find_quota_key(
      {client_quota_type::produce_quota, test_client_id});
    auto limits = f.tr.find_quota_value(key);
    BOOST_CHECK_EQUAL(test_client_id_key, key);
    BOOST_CHECK_EQUAL(expected_limits, limits);
    BOOST_CHECK(!f.tr.is_empty());
}

void run_quota_translator_client_group_test(fixture& f) {
    // Stage 1 - Start by checking that tracker_key's are correctly detected
    // for various client ids
    auto get_produce_key = [&f](auto client_id) {
        return f.tr.find_quota_key(
          {client_quota_type::produce_quota, client_id});
    };
    auto get_fetch_key = [&f](auto client_id) {
        return f.tr.find_quota_key({client_quota_type::fetch_quota, client_id});
    };
    auto get_mutation_key = [&f](auto client_id) {
        return f.tr.find_quota_key(
          {client_quota_type::partition_mutation_quota, client_id});
    };

    // Check keys for produce
    CHECK_VARIANT_EQ(k_group_name{"franz-go"}, get_produce_key("franz-go"));
    CHECK_VARIANT_EQ(
      k_group_name{"not-franz-go"}, get_produce_key("not-franz-go"));
    CHECK_VARIANT_EQ(k_client_id{"unknown"}, get_produce_key("unknown"));
    CHECK_VARIANT_EQ(k_client_id{""}, get_produce_key(std::nullopt));

    // Check keys for fetch
    CHECK_VARIANT_EQ(k_group_name{"franz-go"}, get_fetch_key("franz-go"));
    CHECK_VARIANT_EQ(
      k_group_name{"not-franz-go"}, get_fetch_key("not-franz-go"));
    CHECK_VARIANT_EQ(k_client_id{"unknown"}, get_fetch_key("unknown"));
    CHECK_VARIANT_EQ(k_client_id{""}, get_fetch_key(std::nullopt));

    // Check keys for partition mutations
    CHECK_VARIANT_EQ(k_client_id{"franz-go"}, get_mutation_key("franz-go"));
    CHECK_VARIANT_EQ(
      k_client_id{"not-franz-go"}, get_mutation_key("not-franz-go"));
    CHECK_VARIANT_EQ(k_client_id{"unknown"}, get_mutation_key("unknown"));
    CHECK_VARIANT_EQ(k_client_id{""}, get_mutation_key(std::nullopt));

    // Stage 2 - Next verify that the correct quota limits apply to the
    // various tracker_key's being tested
    // Check limits for the franz-go groups
    auto franz_go_limits = client_quota_limits{
      .produce_limit = 4096,
      .fetch_limit = 4097,
      .partition_mutation_limit = {},
    };
    BOOST_CHECK_EQUAL(
      franz_go_limits, f.tr.find_quota_value(k_group_name{"franz-go"}));

    // Check limits for the not-franz-go groups
    auto not_franz_go_limits = client_quota_limits{
      .produce_limit = 2048,
      .fetch_limit = 2049,
      .partition_mutation_limit = {},
    };
    BOOST_CHECK_EQUAL(
      not_franz_go_limits, f.tr.find_quota_value(k_group_name{"not-franz-go"}));

    // Check limits for the non-client-group keys
    auto default_limits = client_quota_limits{
      .produce_limit = P_DEF,
      .fetch_limit = F_DEF,
      .partition_mutation_limit = PM_DEF,
    };
    BOOST_CHECK_EQUAL(
      default_limits, f.tr.find_quota_value(k_client_id{"unknown"}));
    BOOST_CHECK_EQUAL(default_limits, f.tr.find_quota_value(k_client_id{""}));
    BOOST_CHECK_EQUAL(
      default_limits, f.tr.find_quota_value(k_client_id{"franz-go"}));
    BOOST_CHECK_EQUAL(
      default_limits, f.tr.find_quota_value(k_client_id{"not-franz-go"}));
}

SEASTAR_THREAD_TEST_CASE(quota_translator_store_client_group_test) {
    fixture f;

    auto default_values = entity_value{
      .producer_byte_rate = P_DEF,
      .consumer_byte_rate = F_DEF,
      .controller_mutation_rate = PM_DEF,
    };

    auto franz_go_key = entity_key{
      entity_key::client_id_prefix_match{"franz-go"}};
    auto franz_go_values = entity_value{
      .producer_byte_rate = 4096,
      .consumer_byte_rate = 4097,
    };

    auto not_franz_go_key = entity_key{
      entity_key::client_id_prefix_match{"not-franz-go"}};
    auto not_franz_go_values = entity_value{
      .producer_byte_rate = 2048,
      .consumer_byte_rate = 2049,
    };

    f.quota_store.local().set_quota(default_key, default_values);
    f.quota_store.local().set_quota(franz_go_key, franz_go_values);
    f.quota_store.local().set_quota(not_franz_go_key, not_franz_go_values);

    run_quota_translator_client_group_test(f);
}

SEASTAR_THREAD_TEST_CASE(quota_translator_priority_order) {
    fixture f;

    using cluster::client_quota::entity_key;
    using cluster::client_quota::entity_value;

    auto check_produce = [&f](
                           auto client_id,
                           auto expected_key,
                           auto expected_value,
                           auto expected_rule) {
        auto [k, value] = f.tr.find_quota(
          {.q_type = kafka::client_quota_type::produce_quota,
           .client_id = client_id});
        CHECK_VARIANT_EQ(expected_key, k);
        BOOST_CHECK_EQUAL(expected_value, value.limit);
        BOOST_CHECK_EQUAL(expected_rule, value.rule);
    };
    auto check_fetch = [&f](
                         auto client_id,
                         auto expected_key,
                         auto expected_value,
                         auto expected_rule) {
        auto [k, value] = f.tr.find_quota(
          {.q_type = kafka::client_quota_type::fetch_quota,
           .client_id = client_id});
        CHECK_VARIANT_EQ(expected_key, k);
        BOOST_CHECK_EQUAL(expected_value, value.limit);
        BOOST_CHECK_EQUAL(expected_rule, value.rule);
    };
    auto check_pm = [&f](
                      auto client_id,
                      auto expected_key,
                      auto expected_value,
                      auto expected_rule) {
        auto [k, value] = f.tr.find_quota(
          {.q_type = kafka::client_quota_type::partition_mutation_quota,
           .client_id = client_id});
        CHECK_VARIANT_EQ(expected_key, k);
        BOOST_CHECK_EQUAL(expected_value, value.limit);
        BOOST_CHECK_EQUAL(expected_rule, value.rule);
    };

    // This test walks through the priority levels of the various ways of
    // configuring quotas in increasing order and asserts that each successive
    // priority level overwrites the previous one. The quota values XY mean
    // priority level X and Y = {1, 2, 3} for produce/fetch/partition mutation
    // quotas respectively to check that their values are independent.

    // 2. Next: default client quota
    auto default_key = entity_key{entity_key::client_id_default_match{}};
    auto default_values = entity_value{
      .producer_byte_rate = 11,
      .consumer_byte_rate = 12,
      .controller_mutation_rate = 13,
    };
    f.quota_store.local().set_quota(default_key, default_values);

    check_produce(
      "franz-go",
      k_client_id{"franz-go"},
      11,
      client_quota_rule::kafka_client_default);
    check_fetch(
      "franz-go",
      k_client_id{"franz-go"},
      12,
      client_quota_rule::kafka_client_default);
    check_pm(
      "franz-go",
      k_client_id{"franz-go"},
      13,
      client_quota_rule::kafka_client_default);

    // 4. Next: client id prefix quota store
    auto franz_go_prefix_key = entity_key{
      entity_key::client_id_prefix_match{"franz-go"}};
    auto franz_go_prefix_values = entity_value{
      .producer_byte_rate = 21,
      .consumer_byte_rate = 22,
      .controller_mutation_rate = 23,
    };
    f.quota_store.local().set_quota(
      franz_go_prefix_key, franz_go_prefix_values);

    check_produce(
      "franz-go",
      k_group_name{"franz-go"},
      21,
      client_quota_rule::kafka_client_prefix);
    check_fetch(
      "franz-go",
      k_group_name{"franz-go"},
      22,
      client_quota_rule::kafka_client_prefix);
    check_pm(
      "franz-go",
      k_group_name{"franz-go"},
      23,
      client_quota_rule::kafka_client_prefix);

    // 5. Finally: client id exact match quota store
    auto franz_go_exact_key = entity_key{
      entity_key::client_id_match{"franz-go"}};
    auto franz_go_exact_values = entity_value{
      .producer_byte_rate = 31,
      .consumer_byte_rate = 32,
      .controller_mutation_rate = 33,
    };
    f.quota_store.local().set_quota(franz_go_exact_key, franz_go_exact_values);

    check_produce(
      "franz-go",
      k_client_id{"franz-go"},
      31,
      client_quota_rule::kafka_client_id);
    check_fetch(
      "franz-go",
      k_client_id{"franz-go"},
      32,
      client_quota_rule::kafka_client_id);
    check_pm(
      "franz-go",
      k_client_id{"franz-go"},
      33,
      client_quota_rule::kafka_client_id);
}

SEASTAR_THREAD_TEST_CASE(quota_translator_watch_test) {
    fixture f;

    bool first_called = false;
    bool second_called = false;

    f.tr.watch([&first_called]() mutable { first_called = true; });
    f.tr.watch([&second_called]() mutable { second_called = true; });

    auto val = entity_value{.producer_byte_rate = P_DEF};
    f.quota_store.local().set_quota(default_key, val);

    BOOST_CHECK(first_called);
    BOOST_CHECK(second_called);
}
