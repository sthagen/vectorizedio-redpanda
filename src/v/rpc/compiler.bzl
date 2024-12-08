"""
This module contains functions for working with Redpanda RPC system.
"""

load("//bazel:build.bzl", "redpanda_cc_library")

def redpanda_cc_rpc_library(name, src, out = None, deps = [], include_prefix = None, visibility = None):
    """
    Generate Redpanda RPC library.

    Args:
      name: name of the library
      src: rpc specification json file
      out: output header name. defaults to src_service.h (without .json extension)
      deps: dependencies defined in the json src file
      include_prefix: include_prefix of generated header
      visibility: visibility setting
    """
    if not src.endswith(".json"):
        fail(src, "expected to have .json suffix")

    # the convention is to generate x_service.h from x.json
    if not out:
        out = src.removesuffix(".json") + "_service.h"

    native.genrule(
        name = name + "_genrule",
        srcs = [src],
        outs = [out],
        cmd = "$(execpath //src/v/rpc:compiler) --service_file $< --output_file $@",
        tools = ["//src/v/rpc:compiler"],
    )

    rpc_template_deps = [
        "//src/v/config",
        "//src/v/metrics",
        "//src/v/rpc",
        "//src/v/finjector",
        "//src/v/random:fast_prng",
    ]

    redpanda_cc_library(
        name = name,
        hdrs = [out],
        deps = rpc_template_deps + deps,
        include_prefix = include_prefix,
        visibility = visibility,
    )
