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

#include "kafka/server/client_quota_translator.h"

#include "cluster/client_quota_store.h"
#include "kafka/server/logger.h"

#include <seastar/core/shard_id.hh>
#include <seastar/util/variant_utils.hh>

#include <absl/algorithm/container.h>

#include <optional>

namespace kafka {

using cluster::client_quota::entity_key;
using cluster::client_quota::entity_value;

std::ostream& operator<<(std::ostream& os, const tracker_key& k) {
    ss::visit(
      k,
      [&os](const k_client_id& c) mutable {
          fmt::print(os, "k_client_id{{{}}}", c());
      },
      [&os](const k_group_name& g) mutable {
          fmt::print(os, "k_group_name{{{}}}", g());
      });
    return os;
}

std::ostream& operator<<(std::ostream& os, client_quota_type quota_type) {
    switch (quota_type) {
    case client_quota_type::produce_quota:
        return os << "produce_quota";
    case client_quota_type::fetch_quota:
        return os << "fetch_quota";
    case client_quota_type::partition_mutation_quota:
        return os << "partition_mutation_quota";
    }
}

std::ostream& operator<<(std::ostream& os, const client_quota_limits& l) {
    fmt::print(
      os,
      "limits{{produce_limit: {}, fetch_limit: {}, "
      "partition_mutation_limit: {}}}",
      l.produce_limit,
      l.fetch_limit,
      l.partition_mutation_limit);
    return os;
}

std::ostream&
operator<<(std::ostream& os, const client_quota_request_ctx& ctx) {
    fmt::print(
      os, "{{quota_type: {}, client_id: {}}}", ctx.q_type, ctx.client_id);
    return os;
}

std::ostream& operator<<(std::ostream& os, client_quota_rule r) {
    switch (r) {
    case client_quota_rule::not_applicable:
        return os << "not_applicable";
    case client_quota_rule::kafka_client_default:
        return os << "kafka_client_default";
    case client_quota_rule::kafka_client_prefix:
        return os << "kafka_client_prefix";
    case client_quota_rule::kafka_client_id:
        return os << "kafka_client_id";
    }
}

std::ostream& operator<<(std::ostream& os, client_quota_value value) {
    fmt::print(os, "{{limit: {}, rule: {}}}", value.limit, value.rule);
    return os;
}

client_quota_translator::client_quota_translator(
  ss::sharded<cluster::client_quota::store>& quota_store)
  : _quota_store(quota_store) {}

client_quota_value client_quota_translator::get_client_quota_value(
  const tracker_key& quota_id, client_quota_type qt) const {
    const auto accessor = [qt](const cluster::client_quota::entity_value& ev) {
        switch (qt) {
        case client_quota_type::produce_quota:
            return ev.producer_byte_rate;
        case client_quota_type::fetch_quota:
            return ev.consumer_byte_rate;
        case client_quota_type::partition_mutation_quota:
            return ev.controller_mutation_rate;
        }
    };
    return ss::visit(
      quota_id,
      [this, &accessor](const k_client_id& k) -> client_quota_value {
          auto exact_match_key = entity_key{entity_key::client_id_match{k}};
          auto exact_match_quota = _quota_store.local().get_quota(
            exact_match_key);
          if (exact_match_quota && accessor(*exact_match_quota)) {
              return client_quota_value{
                accessor(*exact_match_quota),
                client_quota_rule::kafka_client_id};
          }

          static const auto default_client_key = entity_key{
            entity_key::client_id_default_match{}};
          auto default_quota = _quota_store.local().get_quota(
            default_client_key);
          if (default_quota && accessor(*default_quota)) {
              return client_quota_value{
                accessor(*default_quota),
                client_quota_rule::kafka_client_default};
          }

          return client_quota_value{
            std::nullopt, client_quota_rule::not_applicable};
      },
      [this, &accessor](const k_group_name& k) -> client_quota_value {
          auto group_key = entity_key{entity_key::client_id_prefix_match{k}};
          auto group_quota = _quota_store.local().get_quota(group_key);
          if (group_quota && accessor(*group_quota)) {
              return client_quota_value{
                accessor(*group_quota), client_quota_rule::kafka_client_prefix};
          }

          return client_quota_value{
            std::nullopt, client_quota_rule::not_applicable};
      });
}

// If client is part of some group then client quota ID is a group
// else client quota ID is client_id
tracker_key client_quota_translator::find_quota_key(
  const client_quota_request_ctx& ctx) const {
    auto qt = ctx.q_type;
    const auto& client_id = ctx.client_id;
    const auto& quota_store = _quota_store.local();

    const auto checker = [qt](const entity_value val) {
        switch (qt) {
        case kafka::client_quota_type::produce_quota:
            return val.producer_byte_rate.has_value();
        case kafka::client_quota_type::fetch_quota:
            return val.consumer_byte_rate.has_value();
        case kafka::client_quota_type::partition_mutation_quota:
            return val.controller_mutation_rate.has_value();
        }
    };

    if (!client_id) {
        // requests without a client id are grouped into an anonymous group that
        // shares a default quota. the anonymous group is keyed on empty string.
        return tracker_key{std::in_place_type<k_client_id>, ""};
    }

    // Exact match quotas
    auto exact_match_key = entity_key{entity_key::client_id_match{*client_id}};
    auto exact_match_quota = quota_store.get_quota(exact_match_key);
    if (exact_match_quota && checker(*exact_match_quota)) {
        return tracker_key{std::in_place_type<k_client_id>, *client_id};
    }

    // Group quotas configured through the Kafka API
    auto group_quotas = quota_store.range(
      cluster::client_quota::store::prefix_group_filter(*client_id));
    for (auto& [gk, gv] : group_quotas) {
        if (checker(gv)) {
            for (auto& part : gk.parts) {
                using client_id_prefix_match
                  = entity_key::part::client_id_prefix_match;

                if (std::holds_alternative<client_id_prefix_match>(part.part)) {
                    auto& prefix_key_part = get<client_id_prefix_match>(
                      part.part);
                    return tracker_key{
                      std::in_place_type<k_group_name>, prefix_key_part.value};
                }
            }
        }
    }

    // Default quotas configured through the Kafka API
    return tracker_key{std::in_place_type<k_client_id>, *client_id};
}

std::pair<tracker_key, client_quota_value>
client_quota_translator::find_quota(const client_quota_request_ctx& ctx) const {
    auto key = find_quota_key(ctx);
    auto value = get_client_quota_value(key, ctx.q_type);
    return {std::move(key), value};
}

client_quota_limits
client_quota_translator::find_quota_value(const tracker_key& key) const {
    return client_quota_limits{
      .produce_limit
      = get_client_quota_value(key, client_quota_type::produce_quota).limit,
      .fetch_limit
      = get_client_quota_value(key, client_quota_type::fetch_quota).limit,
      .partition_mutation_limit
      = get_client_quota_value(key, client_quota_type::partition_mutation_quota)
          .limit,
    };
}

void client_quota_translator::watch(on_change_fn&& fn) {
    auto watcher = [fn = std::move(fn)]() { fn(); };
    _quota_store.local().watch(watcher);
}

bool client_quota_translator::is_empty() const {
    return _quota_store.local().size() == 0;
}

} // namespace kafka
