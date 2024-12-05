load("@rules_oci//oci:defs.bzl", "oci_image", "oci_load")
load("@rules_pkg//pkg:mappings.bzl", "pkg_attributes", "pkg_files", "strip_prefix")
load("@rules_pkg//pkg:tar.bzl", "pkg_tar")

pkg_files(
    name = "redpanda_binary_with_runfiles",
    srcs = ["//src/v/redpanda"],
    attributes = pkg_attributes(
        mode = "0755",
    ),
    # Including runfiles has the effect of copying all of the shared libraries needed to run the binary.
    # Although they always retain the directory structure that is found in bazel build output, meaning
    # that `strip_prefix` has no effect on runfiles here (so we strip_prefix in the other targets).
    include_runfiles = True,
)

pkg_files(
    name = "redpanda_lib",
    srcs = [":redpanda_binary_with_runfiles"],
    # Exclude the redpanda binary, but this will still keep the runfiles, which for the redpanda
    # binary is the shared libraries.
    excludes = ["//src/v/redpanda"],
    prefix = "lib",
    # This flattens the files so that everything is directly in the `lib` folder.
    strip_prefix = strip_prefix.files_only(),
)

pkg_files(
    name = "redpanda_libexec",
    srcs = [
        "//src/go/rpk/cmd/rpk",
        "//src/v/redpanda",
    ],
    attributes = pkg_attributes(
        mode = "0755",
    ),
    prefix = "libexec",
    # This flattens the files so that everything is directly in the `libexec` folder.
    strip_prefix = strip_prefix.files_only(),
)

pkg_files(
    name = "redpanda_conf",
    srcs = ["//conf:redpanda.yaml"],
    prefix = "etc/redpanda",
    # This flattens the files so that everything is directly in the `etc/redpanda` folder.
    strip_prefix = strip_prefix.files_only(),
)

pkg_files(
    name = "redpanda_bin",
    srcs = [
        "//bazel/packaging/bin_wrapper:redpanda",
        "//bazel/packaging/bin_wrapper:rpk",
    ],
    attributes = pkg_attributes(
        mode = "0755",
    ),
    prefix = "bin",
    # This flattens the files so that everything is directly in the `bin` folder.
    strip_prefix = strip_prefix.files_only(),
)

# TODO(bazel): Pull in all the system libraries that are still depended on and not packaged yet.
# TODO(bazel): Add the other binaries we package (rp_util, hwloc, rpk, etc)
pkg_tar(
    name = "redpanda_tar",
    srcs = [
        ":redpanda_bin",
        ":redpanda_lib",
        ":redpanda_libexec",
    ],
    # This is also needed because it actually specifies compression.
    extension = ".tar.gz",
    package_file_name = "redpanda.tar.gz",
)

pkg_tar(
    name = "redpanda_image_broker_files",
    srcs = [
        ":redpanda_bin",
        ":redpanda_lib",
        ":redpanda_libexec",
    ],
    package_dir = "/opt/redpanda",
)

# This is the ID for the nonroot user in the distroless containers.
NONROOT_USER = "65532"

pkg_tar(
    name = "redpanda_image_config_files",
    srcs = [
        ":redpanda_conf",
    ],
    mode = "0o555",
    owner = NONROOT_USER + "." + NONROOT_USER,
    package_dir = "/etc/redpanda",
)

pkg_tar(
    name = "redpanda_image_writable_dirs",
    empty_dirs = [
        "/etc/redpanda",
        "/var/lib/redpanda",
    ],
    mode = "0o755",
    owner = NONROOT_USER + "." + NONROOT_USER,
)

oci_image(
    name = "image",
    base = "@distroless_cc_debian12",
    cmd = [
        "redpanda",
        "start",
        "--overprovisioned",
    ],
    entrypoint = ["/opt/redpanda/bin/rpk"],
    exposed_ports = [
        "8081",
        "8082",
        "9092",
        "9644",
    ],
    labels = {"org.opencontainers.image.authors": "Redpanda Data <hi@redpanda.com>"},
    tars = [
        ":redpanda_image_broker_files",
        ":redpanda_image_config_files",
        ":redpanda_image_writable_dirs",
    ],
    user = "nonroot",
    volumes = ["/var/lib/redpanda/data"],
)

oci_load(
    name = "image_load",
    image = ":image",
    repo_tags = ["redpandadata/redpanda-dev:latest"],
)
