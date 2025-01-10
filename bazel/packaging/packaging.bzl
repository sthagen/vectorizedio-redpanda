"""
A rule to create a redpanda tarball given inputs from the build system.
"""

load("@bazel_tools//tools/cpp:toolchain_utils.bzl", "find_cpp_toolchain")

def _is_versioned_so(file):
    """ Return true if this file has a name like libfoo.so.N """
    parts = file.basename.rsplit(".", 3)
    if len(parts) != 3:
        return False
    if not parts[0].startswith("lib"):
        return False
    if parts[1] != "so":
        return False
    for c in parts[2].elems():
        if not c.isdigit():
            return False
    return True

def _impl(ctx):
    # Collect all shared libraries from the sysroot that we used.
    shared_libraries = []
    cc_toolchain = find_cpp_toolchain(ctx)
    if cc_toolchain.sysroot != None and ctx.attr.include_sysroot_libs:
        for cc_file in cc_toolchain.all_files.to_list():
            if cc_file.path.startswith(cc_toolchain.sysroot) and _is_versioned_so(cc_file):
                # TODO(bazel): To make this work properly in containers/tarballs without
                # vtools we need to run patchelf to set the interpreter to be the ld.so
                # from the sysroot (vtools does this for us now).
                shared_libraries.append(cc_file)

    # Collect all the shared libraries that we built as part of Redpanda.
    rp_runfiles = ctx.attr.redpanda_binary[DefaultInfo].default_runfiles.files.to_list()
    for solib in rp_runfiles:
        # Why the redpanda binary is marked as a runfile of itself? No idea...
        if solib == ctx.file.redpanda_binary:
            continue
        shared_libraries.append(solib)

    # Create the configuration file for the packaging tool
    cfg_file = ctx.actions.declare_file("%s.config.json" % ctx.attr.name)
    cfg = {
        "redpanda_binary": ctx.file.redpanda_binary.path,
        "rpk": ctx.file.rpk_binary.path if ctx.file.rpk_binary else None,
        "shared_libraries": [solib.path for solib in shared_libraries],
        "default_yaml_config": ctx.file.default_yaml_config.path if ctx.file.default_yaml_config else None,
        "bin_wrappers": [f.path for f in ctx.files.bin_wrappers],
        "owner": ctx.attr.owner,
    }
    ctx.actions.write(cfg_file, content = json.encode_indent(cfg))

    inputs = [cfg_file, ctx.file.redpanda_binary] + shared_libraries + ctx.files.bin_wrappers

    if ctx.file.rpk_binary:
        inputs.append(ctx.file.rpk_binary)
    if ctx.file.default_yaml_config:
        inputs.append(ctx.file.default_yaml_config)

    # run the packaging tool
    ctx.actions.run(
        outputs = [ctx.outputs.out],
        inputs = inputs,
        tools = [ctx.executable._tool],
        executable = ctx.executable._tool,
        arguments = [
            "-config",
            cfg_file.path,
            "-output",
            ctx.outputs.out.path,
        ],
        mnemonic = "BuildingRedpandaPackage",
        use_default_shell_env = False,
    )
    return [DefaultInfo(files = depset([ctx.outputs.out]))]

redpanda_package = rule(
    implementation = _impl,
    attrs = {
        "redpanda_binary": attr.label(
            allow_single_file = True,
            mandatory = True,
        ),
        "default_yaml_config": attr.label(
            allow_single_file = True,
        ),
        "bin_wrappers": attr.label_list(
            allow_files = True,
        ),
        "rpk_binary": attr.label(
            allow_single_file = True,
        ),
        "owner": attr.int(),
        "out": attr.output(
            mandatory = True,
        ),
        "include_sysroot_libs": attr.bool(),
        "_tool": attr.label(
            executable = True,
            allow_files = True,
            cfg = "exec",
            default = Label("//bazel/packaging:tool"),
        ),
        "_cc_toolchain": attr.label(
            default = Label("@bazel_tools//tools/cpp:current_cc_toolchain"),
        ),
    },
    toolchains = ["@bazel_tools//tools/cpp:toolchain_type"],
)
