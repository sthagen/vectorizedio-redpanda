load("@rules_proto//proto:defs.bzl", "proto_library")

filegroup(
    name = "schema_registry_proto_files",
    srcs = [
        "confluent/meta.proto",
        "confluent/types/decimal.proto",
        "google/type/calendar_period.proto",
        "google/type/color.proto",
        "google/type/date.proto",
        "google/type/datetime.proto",
        "google/type/dayofweek.proto",
        "google/type/decimal.proto",
        "google/type/expr.proto",
        "google/type/fraction.proto",
        "google/type/interval.proto",
        "google/type/latlng.proto",
        "google/type/localized_text.proto",
        "google/type/money.proto",
        "google/type/month.proto",
        "google/type/phone_number.proto",
        "google/type/postal_address.proto",
        "google/type/quaternion.proto",
        "google/type/timeofday.proto",
    ],
    visibility = ["//visibility:public"],
)

proto_library(
    name = "schema_registry_protos",
    srcs = [":schema_registry_proto_files"],
    strip_import_prefix = "/src/v/pandaproxy/schema_registry/protobuf/",
    visibility = ["//visibility:public"],
    deps = [
        "@protobuf//:any_proto",
        "@protobuf//:descriptor_proto",
        "@protobuf//:duration_proto",
        "@protobuf//:timestamp_proto",
        "@protobuf//:wrappers_proto",
    ],
)

cc_proto_library(
    name = "schema_registry_cc_proto",
    visibility = ["//visibility:public"],
    deps = [":schema_registry_protos"],
)
