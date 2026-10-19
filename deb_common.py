#!/usr/bin/env python3
"""Shared helpers for the local Toupcam Debian package builders."""

import os
import re
import shutil
import subprocess
import gzip
from datetime import datetime, timezone
from email.utils import format_datetime
from pathlib import Path
from zipfile import ZipFile


ROOT = Path(__file__).resolve().parent
SDK_ZIP_DIR = ROOT / "toupcamsdk"

ARCHITECTURES = {
    "amd64": ("x64", "x86_64-linux-gnu"),
    "i386": ("x86", "i386-linux-gnu"),
    "arm64": ("arm64", "aarch64-linux-gnu"),
    "armhf": ("armhf", "arm-linux-gnueabihf"),
    "armel": ("armel", "arm-linux-gnueabi"),
}


def run(*args, cwd=None, env=None):
    print("+", " ".join(os.fspath(arg) for arg in args))
    subprocess.run(
        [os.fspath(arg) for arg in args],
        cwd=cwd,
        env=env,
        check=True,
    )


def require_commands(*commands):
    missing = [command for command in commands if shutil.which(command) is None]
    if missing:
        raise SystemExit(
            "Missing required command(s): "
            + ", ".join(missing)
            + ". Install the Debian packaging/build prerequisites first."
        )


def host_architecture():
    require_commands("dpkg")
    return subprocess.check_output(
        ["dpkg", "--print-architecture"], text=True
    ).strip()


def architecture_info(architecture):
    try:
        return ARCHITECTURES[architecture]
    except KeyError:
        supported = ", ".join(sorted(ARCHITECTURES))
        raise SystemExit(
            f"Unsupported architecture {architecture!r}; supported: {supported}"
        )


def select_sdk_zip(value=None):
    if value:
        path = Path(value).expanduser().resolve()
        if not path.is_file():
            raise SystemExit(f"Toupcam SDK ZIP does not exist: {path}")
        return path

    candidates = sorted(SDK_ZIP_DIR.glob("*.zip"), key=lambda path: path.name)
    if not candidates:
        raise SystemExit(f"No SDK ZIP files found in {SDK_ZIP_DIR}")
    return candidates[-1].resolve()


def sdk_version(zip_path):
    match = re.fullmatch(r"toupcamsdk[_-](.+)\.zip", zip_path.name, re.I)
    if not match:
        raise SystemExit(
            "Cannot derive the SDK version from "
            f"{zip_path.name!r}; expected toupcamsdk_<version>.zip"
        )
    version = match.group(1).replace("_", ".")
    if not re.fullmatch(r"[0-9][0-9A-Za-z.+:~\-]*", version):
        raise SystemExit(f"Invalid Debian version derived from ZIP: {version!r}")
    return version


def copy_zip_member(archive, member, destination, mode=None):
    try:
        source = archive.open(member)
    except KeyError:
        raise SystemExit(f"SDK ZIP is missing required file: {member}")

    destination.parent.mkdir(parents=True, exist_ok=True)
    with source, destination.open("wb") as output:
        shutil.copyfileobj(source, output)
    if mode is not None:
        destination.chmod(mode)


def extract_sdk_files(zip_path, architecture, destination):
    sdk_directory, _multiarch = architecture_info(architecture)
    with ZipFile(zip_path) as archive:
        copy_zip_member(
            archive,
            "inc/toupcam.h",
            destination / "inc" / "toupcam.h",
            0o644,
        )
        copy_zip_member(
            archive,
            f"linux/{sdk_directory}/libtoupcam.so",
            destination / "lib" / "libtoupcam.so",
            0o644,
        )
        copy_zip_member(
            archive,
            "linux/udev/99-toupcam.rules",
            destination / "99-toupcam.rules",
            0o644,
        )


def write_text(path, contents, mode=0o644):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(contents)
    path.chmod(mode)


def write_package_docs(
    package_root, package_name, version, change_description, copyright_text
):
    doc_dir = package_root / "usr" / "share" / "doc" / package_name
    doc_dir.mkdir(parents=True, exist_ok=True)
    timestamp = format_datetime(datetime.now(timezone.utc))
    changelog = f"""{package_name} ({version}) unstable; urgency=medium

  * {change_description}

 -- pyuscope maintainers <john@labsmore.com>  {timestamp}
"""
    with gzip.open(
        doc_dir / "changelog.Debian.gz", "wt", encoding="utf-8"
    ) as output:
        output.write(changelog)
    (doc_dir / "changelog.Debian.gz").chmod(0o644)
    write_text(doc_dir / "copyright", copyright_text)


def build_deb(package_root, output_path):
    require_commands("dpkg-deb")
    package_root.chmod(0o755)
    for directory in (
        path for path in package_root.rglob("*") if path.is_dir()
    ):
        directory.chmod(0o755)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    if output_path.exists():
        output_path.unlink()
    run(
        "dpkg-deb",
        "--build",
        "--root-owner-group",
        package_root,
        output_path,
    )
    print(f"Built {output_path}")
