load("//bazel:test.bzl", "redpanda_cc_bench", "redpanda_cc_btest", "redpanda_cc_btest_no_seastar")

redpanda_cc_btest(
    name = "secure_hash_test",
    timeout = "short",
    srcs = [
        "secure_tests.cc",
    ],
    deps = [
        "//src/v/hashing:secure",
        "@boost//:test",
        "@seastar//:testing",
    ],
)

redpanda_cc_btest_no_seastar(
    name = "xx_hash_test",
    timeout = "short",
    srcs = [
        "xx_tests.cc",
    ],
    deps = [
        "//src/v/hashing:xx",
        "//src/v/utils:named_type",
    ],
)

redpanda_cc_bench(
    name = "hashing_bench_rpbench",
    srcs = [
        "hash_bench.cc",
    ],
    deps = [
        "//src/v/hashing:crc32c",
        "//src/v/hashing:xx",
        "//src/v/model",
        "//src/v/random:generators",
        "@abseil-cpp//absl/hash",
        "@abseil-cpp//absl/strings",
        "@boost//:crc",
        "@seastar",
        "@seastar//:benchmark",
    ],
)
