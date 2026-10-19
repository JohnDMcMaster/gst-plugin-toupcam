#!/usr/bin/env -S uv run
"""Build the gst-plugin-toupcam Debian package."""

import argparse
import os
import re
import shlex
import shutil
import tempfile
from pathlib import Path

from deb_common import (
    architecture_info,
    build_deb,
    extract_sdk_files,
    host_architecture,
    require_commands,
    run,
    sdk_version,
    select_sdk_zip,
    write_package_docs,
    write_text,
)


ROOT = Path(__file__).resolve().parent


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "sdk_zip",
        nargs="?",
        help=(
            "Toupcam SDK ZIP used for headers and linking. Defaults to the "
            "alphabetically highest toupcamsdk/*.zip."
        ),
    )
    parser.add_argument(
        "--architecture",
        help="Debian target architecture (default: host architecture)",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=ROOT / "dist",
        help="Directory for the generated .deb (default: ./dist)",
    )
    parser.add_argument(
        "--distribution-tag",
        help=(
            "Override the package distribution tag "
            "(default: derive it from /etc/os-release)"
        ),
    )
    return parser.parse_args()


def plugin_version(distribution_tag):
    configure_ac = (ROOT / "configure.ac").read_text()
    match = re.search(
        r"AC_INIT\(\s*\[toupcam\]\s*,\s*\[([^\]]+)\]\s*\)", configure_ac
    )
    if not match:
        raise SystemExit("Could not determine plugin version from configure.ac")
    return f"{match.group(1)}+{distribution_tag}"


def distribution_tag(override=None):
    if override:
        tag = override
    else:
        values = {}
        try:
            lines = Path("/etc/os-release").read_text().splitlines()
        except OSError as error:
            raise SystemExit(f"Could not read /etc/os-release: {error}")
        for line in lines:
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            parsed = shlex.split(value)
            values[key] = parsed[0] if parsed else ""
        distribution = values.get("ID")
        release = values.get("VERSION_ID")
        if not distribution or not release:
            raise SystemExit(
                "/etc/os-release must define both ID and VERSION_ID"
            )
        tag = f"{distribution}{release}"

    tag = tag.lower()
    if not re.fullmatch(r"[a-z0-9][a-z0-9.]*", tag):
        raise SystemExit(
            f"Invalid distribution tag {tag!r}; use letters, numbers, and dots"
        )
    return tag


def copy_build_sources(destination):
    shutil.copy2(ROOT / "autogen.sh", destination)
    shutil.copy2(ROOT / "configure.ac", destination)
    shutil.copy2(ROOT / "Makefile.am", destination)
    source_dir = destination / "src"
    source_dir.mkdir()
    shutil.copy2(ROOT / "src" / "Makefile.am", source_dir)
    for pattern in ("*.c", "*.h"):
        for source in (ROOT / "src").glob(pattern):
            shutil.copy2(source, source_dir)


def main():
    args = parse_args()
    require_commands(
        "autoreconf",
        "cc",
        "make",
        "pkg-config",
        "libtoolize",
        "dpkg-deb",
        "strip",
    )

    architecture = args.architecture or host_architecture()
    current_architecture = host_architecture()
    if architecture != current_architecture:
        raise SystemExit(
            "Cross-compiling the plugin is not supported: requested "
            f"{architecture}, host is {current_architecture}"
        )
    _sdk_directory, multiarch = architecture_info(architecture)

    zip_path = select_sdk_zip(args.sdk_zip)
    selected_sdk_version = sdk_version(zip_path)
    sdk_package_version = selected_sdk_version
    selected_distribution = distribution_tag(args.distribution_tag)
    version = plugin_version(selected_distribution)
    output_dir = args.output_dir.expanduser().resolve()
    sdk_output_dir = output_dir / f"toupcamsdk_{selected_sdk_version}"
    output_path = (
        sdk_output_dir
        / f"gstreamer1.0-toupcam_{version}_{architecture}.deb"
    )

    print(f"SDK ZIP:        {zip_path}")
    print(f"SDK version:    {selected_sdk_version}")
    print(f"Plugin version: {version}")
    print(f"Distribution:   {selected_distribution}")
    print(f"Architecture:   {architecture}")

    with tempfile.TemporaryDirectory(prefix="gst-toupcam-deb-") as temporary:
        temporary = Path(temporary)
        sdk_root = temporary / "sdk"
        source_root = temporary / "source"
        package_root = temporary / "package"
        source_root.mkdir()

        extract_sdk_files(zip_path, architecture, sdk_root)
        copy_build_sources(source_root)

        build_environment = os.environ.copy()
        existing_library_path = build_environment.get("LD_LIBRARY_PATH")
        build_environment["LD_LIBRARY_PATH"] = os.pathsep.join(
            filter(None, [str(sdk_root / "lib"), existing_library_path])
        )

        run("./autogen.sh", cwd=source_root, env=build_environment)
        run(
            "make",
            f"TOUPCAM_CFLAGS=-I{sdk_root / 'inc'} -Werror",
            f"TOUPCAM_LIBS=-L{sdk_root / 'lib'} -ltoupcam",
            cwd=source_root,
            env=build_environment,
        )

        plugin_source = (
            source_root / "src" / ".libs" / "libgsttoupcamsrc.so"
        )
        if not plugin_source.is_file():
            raise SystemExit(f"Build did not produce {plugin_source}")
        plugin_dir = (
            package_root / "usr" / "lib" / multiarch / "gstreamer-1.0"
        )
        plugin_dir.mkdir(parents=True)
        packaged_plugin = plugin_dir / plugin_source.name
        shutil.copy2(plugin_source, packaged_plugin)
        run("strip", "--strip-unneeded", packaged_plugin)
        packaged_plugin.chmod(0o644)

        write_text(
            package_root / "DEBIAN" / "control",
            f"""Package: gstreamer1.0-toupcam
Version: {version}
Section: non-free/video
Priority: optional
Architecture: {architecture}
Depends: toupcamsdk (= {sdk_package_version}), libc6, libgstreamer1.0-0, libgstreamer-plugins-base1.0-0
Maintainer: pyuscope maintainers <john@labsmore.com>
X-Toupcam-SDK-Version: {selected_sdk_version}
Description: GStreamer source plugin for Toupcam-compatible cameras
 Provides the toupcamsrc GStreamer 1.0 source element using the proprietary
 ToupTek Toupcam camera SDK.
""",
        )
        write_package_docs(
            package_root,
            "gstreamer1.0-toupcam",
            version,
            f"Build against Toupcam SDK {selected_sdk_version}.",
            """Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: gst-plugin-toupcam
Source: https://github.com/JohnDMcMaster/gst-plugin-toupcam

Files: *
Copyright: 2022 Labsmore LLC
License: BSD-2-clause
 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:
 .
 1. Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.
 .
 2. Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.
 .
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 POSSIBILITY OF SUCH DAMAGE.
""",
        )
        build_deb(package_root, output_path)


if __name__ == "__main__":
    main()
