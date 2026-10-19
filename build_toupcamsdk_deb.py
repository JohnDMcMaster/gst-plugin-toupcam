#!/usr/bin/env -S uv run
"""Build a local Debian package from a vendor Toupcam SDK ZIP."""

import argparse
import shutil
import tempfile
from pathlib import Path

from deb_common import (
    ARCHITECTURES,
    architecture_info,
    build_deb,
    extract_sdk_files,
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
            "Toupcam SDK ZIP. Defaults to the alphabetically highest *.zip "
            "in the toupcamsdk directory."
        ),
    )
    parser.add_argument(
        "--architecture",
        choices=sorted(ARCHITECTURES),
        help="Build only this Debian architecture (default: build all)",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=ROOT / "dist",
        help="Directory for the generated .deb (default: ./dist)",
    )
    return parser.parse_args()


def build_package(zip_path, vendor_version, architecture, output_dir):
    sdk_directory, multiarch = architecture_info(architecture)
    version = vendor_version
    sdk_output_dir = output_dir / f"toupcamsdk_{vendor_version}"
    output_path = (
        sdk_output_dir / f"toupcamsdk_{version}_{architecture}.deb"
    )

    print()
    print(f"Architecture: {architecture}")

    with tempfile.TemporaryDirectory(prefix="toupcamsdk-deb-") as temporary:
        temporary = Path(temporary)
        extracted = temporary / "sdk"
        package_root = temporary / "package"
        extract_sdk_files(zip_path, architecture, extracted)

        sdk_install_dir = package_root / "opt" / "toupcamsdk"
        sdk_include_dir = sdk_install_dir / "inc"
        sdk_library_dir = sdk_install_dir / "linux" / sdk_directory
        sdk_udev_dir = sdk_install_dir / "linux" / "udev"
        sdk_include_dir.mkdir(parents=True)
        sdk_library_dir.mkdir(parents=True)
        sdk_udev_dir.mkdir(parents=True)
        shutil.copy2(
            extracted / "inc" / "toupcam.h",
            sdk_include_dir / "toupcam.h",
        )
        shutil.copy2(
            extracted / "lib" / "libtoupcam.so",
            sdk_library_dir / "libtoupcam.so",
        )
        shutil.copy2(
            extracted / "99-toupcam.rules",
            sdk_udev_dir / "99-toupcam.rules",
        )

        include_dir = package_root / "usr" / "include"
        library_dir = package_root / "usr" / "lib" / multiarch
        udev_dir = package_root / "usr" / "lib" / "udev" / "rules.d"
        include_dir.mkdir(parents=True)
        library_dir.mkdir(parents=True)
        udev_dir.mkdir(parents=True)

        shutil.copy2(
            extracted / "inc" / "toupcam.h",
            include_dir / "toupcam.h",
        )
        shutil.copy2(
            extracted / "lib" / "libtoupcam.so",
            library_dir / "libtoupcam.so",
        )
        shutil.copy2(extracted / "99-toupcam.rules", udev_dir)

        write_text(
            library_dir / "pkgconfig" / "toupcam.pc",
            f"""prefix=/usr
exec_prefix=${{prefix}}
libdir=${{prefix}}/lib/{multiarch}
includedir=${{prefix}}/include

Name: toupcam
Description: ToupTek Toupcam camera SDK
Version: {vendor_version}
Libs: -L${{libdir}} -ltoupcam
Cflags: -I${{includedir}}
""",
        )
        write_text(
            package_root / "DEBIAN" / "control",
            f"""Package: toupcamsdk
Version: {version}
Section: non-free/libs
Priority: optional
Architecture: {architecture}
Depends: libc6
Maintainer: pyuscope maintainers <john@labsmore.com>
Description: ToupTek Toupcam camera SDK
 Proprietary runtime library, C header, pkg-config metadata, and udev rules
 used by applications that communicate with Toupcam-compatible cameras. The
 SDK is installed under /opt/toupcamsdk for compatibility with existing tools.
""",
        )
        write_text(
            package_root / "DEBIAN" / "triggers",
            "activate-noawait ldconfig\n",
        )
        write_text(
            package_root / "DEBIAN" / "postinst",
            """#!/bin/sh
set -e
if command -v udevadm >/dev/null 2>&1; then
    udevadm control --reload-rules || true
    udevadm trigger --subsystem-match=usb --action=add || true
fi
""",
            0o755,
        )
        write_text(
            package_root / "DEBIAN" / "postrm",
            """#!/bin/sh
set -e
if command -v udevadm >/dev/null 2>&1; then
    udevadm control --reload-rules || true
    udevadm trigger --subsystem-match=usb --action=add || true
fi
""",
            0o755,
        )
        write_package_docs(
            package_root,
            "toupcamsdk",
            version,
            f"Package vendor SDK version {vendor_version}.",
            """Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: ToupTek Toupcam SDK
Source: supplied locally by the package builder

Files: *
Copyright: 2022 ToupTek Photonics Co., Ltd.
License: Proprietary
 This package is generated from a user-supplied vendor SDK archive.
 The archive does not include a license file. Consult ToupTek for the terms
 governing use and redistribution before distributing this package.
""",
        )
        build_deb(package_root, output_path)


def main():
    args = parse_args()
    zip_path = select_sdk_zip(args.sdk_zip)
    vendor_version = sdk_version(zip_path)
    output_dir = args.output_dir.expanduser().resolve()
    architectures = (
        [args.architecture] if args.architecture else list(ARCHITECTURES)
    )

    print(f"SDK ZIP:     {zip_path}")
    print(f"SDK version: {vendor_version}")
    for architecture in architectures:
        build_package(zip_path, vendor_version, architecture, output_dir)


if __name__ == "__main__":
    main()
