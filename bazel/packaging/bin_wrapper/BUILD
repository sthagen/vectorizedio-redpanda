load("@rules_go//go:def.bzl", "go_binary")

# gazelle:ignore

go_binary(
    name = "rpk",
    srcs = ["main.go"],
    visibility = ["//bazel/packaging:__pkg__"],
    x_defs = {
        "BinaryPath": "/opt/redpanda/libexec/rpk",
    },
)

go_binary(
    name = "redpanda",
    srcs = ["main.go"],
    visibility = ["//bazel/packaging:__pkg__"],
    x_defs = {
        "BinaryPath": "/opt/redpanda/libexec/redpanda",
        "LDLibraryPath": "/opt/redpanda/lib",
    },
)
