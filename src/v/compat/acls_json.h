/*
 * Copyright 2022 Redpanda Data, Inc.
 *
 * Use of this software is governed by the Business Source License
 * included in the file licenses/BSL.md
 *
 * As of the Change Date specified in that file, in accordance with
 * the Business Source License, use of this software will be governed
 * by the Apache License, Version 2.0
 */
#pragma once

#include "compat/json.h"
#include "security/acl.h"

namespace json {

inline void read_value(const json::Value& rd, security::acl_host& host) {
    ss::sstring address;
    read_member(rd, "address", address);
    host = security::acl_host(address);
}

inline void rjson_serialize(
  json::Writer<json::StringBuffer>& w, const security::acl_host& host) {
    w.StartObject();
    std::stringstream ss;
    vassert(host.address(), "Unset optional address unexpected.");
    ss << host.address().value();
    if (!ss.good()) {
        throw std::runtime_error(fmt_with_ctx(
          fmt::format,
          "failed to serialize acl_host, state: {}",
          ss.rdstate()));
    }
    w.Key("address");
    rjson_serialize(w, std::string_view{ss.str()});
    w.EndObject();
}

inline void
read_value(const json::Value& rd, security::acl_principal& principal) {
    security::principal_type type{};
    read_member(rd, "type", type);
    ss::sstring name;
    read_member(rd, "name", name);
    principal = security::acl_principal(type, std::move(name));
}

inline void rjson_serialize(
  json::Writer<json::StringBuffer>& w,
  const security::acl_principal& principal) {
    w.StartObject();
    w.Key("type");
    rjson_serialize(w, principal.type());
    w.Key("name");
    rjson_serialize(w, principal.name());
    w.EndObject();
}

inline void read_value(const json::Value& rd, security::acl_entry& entry) {
    security::acl_principal principal;
    security::acl_host host;
    security::acl_operation operation{};
    security::acl_permission permission{};
    read_member(rd, "principal", principal);
    read_member(rd, "host", host);
    read_member(rd, "operation", operation);
    read_member(rd, "permission", permission);
    entry = security::acl_entry(
      std::move(principal), host, operation, permission);
}

inline void rjson_serialize(
  json::Writer<json::StringBuffer>& w, const security::acl_entry& entry) {
    w.StartObject();
    w.Key("principal");
    rjson_serialize(w, entry.principal());
    w.Key("host");
    rjson_serialize(w, entry.host());
    w.Key("operation");
    rjson_serialize(w, entry.operation());
    w.Key("permission");
    rjson_serialize(w, entry.permission());
    w.EndObject();
}

inline void
read_value(const json::Value& rd, security::resource_pattern& pattern) {
    ss::sstring name;
    security::resource_type resource{};
    security::pattern_type pattern_type{};
    read_member(rd, "resource", resource);
    read_member(rd, "name", name);
    read_member(rd, "pattern", pattern_type);
    pattern = security::resource_pattern(
      resource, std::move(name), pattern_type);
}

inline void rjson_serialize(
  json::Writer<json::StringBuffer>& w,
  const security::resource_pattern& pattern) {
    w.StartObject();
    w.Key("resource");
    rjson_serialize(w, pattern.resource());
    w.Key("name");
    rjson_serialize(w, pattern.name());
    w.Key("pattern");
    rjson_serialize(w, pattern.pattern());
    w.EndObject();
}

inline void read_value(const json::Value& rd, security::acl_binding& binding) {
    security::resource_pattern pattern;
    security::acl_entry entry;
    read_member(rd, "pattern", pattern);
    read_member(rd, "entry", entry);
    binding = security::acl_binding(std::move(pattern), std::move(entry));
}

inline void rjson_serialize(
  json::Writer<json::StringBuffer>& w, const security::acl_binding& data) {
    w.StartObject();
    w.Key("pattern");
    rjson_serialize(w, data.pattern());
    w.Key("entry");
    rjson_serialize(w, data.entry());
    w.EndObject();
}

inline void read_value(
  const json::Value& rd,
  security::resource_pattern_filter::pattern_filter_type& pattern_filter) {
    using type = security::resource_pattern_filter::serialized_pattern_type;
    type ser_filter{};
    read_member(rd, "pattern_filter", ser_filter);
    switch (ser_filter) {
    case type::literal:
        pattern_filter = security::pattern_type::literal;
        break;
    case type::prefixed:
        pattern_filter = security::pattern_type::prefixed;
        break;
    case type::match:
        pattern_filter = security::resource_pattern_filter::pattern_match{};
        break;
    default:
        vassert(
          false, "unexpected serialized pattern filter type {}", ser_filter);
    }
}

inline void rjson_serialize(
  json::Writer<json::StringBuffer>& w,
  const security::resource_pattern_filter::pattern_filter_type&
    pattern_filter) {
    security::resource_pattern_filter::serialized_pattern_type out{};
    if (std::holds_alternative<
          security::resource_pattern_filter::pattern_match>(pattern_filter)) {
        out = security::resource_pattern_filter::serialized_pattern_type::match;
    } else {
        auto source_pattern = std::get<security::pattern_type>(pattern_filter);
        out = security::resource_pattern_filter::to_pattern(source_pattern);
    }
    w.StartObject();
    w.Key("pattern_filter");
    rjson_serialize(w, out);
    w.EndObject();
}

inline void
read_value(const json::Value& rd, security::resource_pattern_filter& pattern) {
    std::optional<security::resource_type> resource;
    std::optional<ss::sstring> name;
    std::optional<security::resource_pattern_filter::pattern_filter_type>
      pattern_filter_type;
    read_member(rd, "name", name);
    read_member(rd, "resource", resource);
    read_member(rd, "pattern", pattern_filter_type);
    pattern = security::resource_pattern_filter(
      resource, std::move(name), pattern_filter_type);
}

inline void rjson_serialize(
  json::Writer<json::StringBuffer>& w,
  const security::resource_pattern_filter& pattern) {
    w.StartObject();
    w.Key("resource");
    rjson_serialize(w, pattern.resource());
    w.Key("name");
    rjson_serialize(w, pattern.name());
    w.Key("pattern");
    rjson_serialize(w, pattern.pattern());
    w.EndObject();
}

inline void
read_value(const json::Value& rd, security::acl_entry_filter& filter) {
    std::optional<security::acl_principal> principal;
    std::optional<security::acl_host> host;
    std::optional<security::acl_operation> operation;
    std::optional<security::acl_permission> permission;
    read_member(rd, "principal", principal);
    read_member(rd, "host", host);
    read_member(rd, "operation", operation);
    read_member(rd, "permission", permission);
    filter = security::acl_entry_filter(
      std::move(principal), host, operation, permission);
}

inline void rjson_serialize(
  json::Writer<json::StringBuffer>& w,
  const security::acl_entry_filter& filter) {
    w.StartObject();
    w.Key("principal");
    rjson_serialize(w, filter.principal());
    w.Key("host");
    rjson_serialize(w, filter.host());
    w.Key("operation");
    rjson_serialize(w, filter.operation());
    w.Key("permission");
    rjson_serialize(w, filter.permission());
    w.EndObject();
}

inline void rjson_serialize(
  json::Writer<json::StringBuffer>& w,
  const cluster::delete_acls_result& data) {
    w.StartObject();
    w.Key("error");
    rjson_serialize(w, data.error);
    w.Key("bindings");
    rjson_serialize(w, data.bindings);
    w.EndObject();
}

inline void
read_value(const json::Value& rd, security::acl_binding_filter& binding) {
    security::resource_pattern_filter pattern_filter;
    security::acl_entry_filter entry_filter;
    read_member(rd, "pattern", pattern_filter);
    read_member(rd, "entry", entry_filter);
    binding = security::acl_binding_filter(pattern_filter, entry_filter);
}

inline void rjson_serialize(
  json::Writer<json::StringBuffer>& w,
  const security::acl_binding_filter& data) {
    w.StartObject();
    w.Key("pattern");
    rjson_serialize(w, data.pattern());
    w.Key("entry");
    rjson_serialize(w, data.entry());
    w.EndObject();
}

inline void
read_value(const json::Value& rd, cluster::delete_acls_cmd_data& data) {
    read_member(rd, "filters", data.filters);
}

inline void rjson_serialize(
  json::Writer<json::StringBuffer>& w,
  const cluster::delete_acls_cmd_data& data) {
    w.StartObject();
    w.Key("filters");
    rjson_serialize(w, data.filters);
    w.EndObject();
}

} // namespace json
