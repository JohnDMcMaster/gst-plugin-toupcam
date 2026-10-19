#!/usr/bin/env -S uv run
"""Build gst-plugin-toupcam packages for supported Debian distributions."""

import argparse
import os
import platform
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parent
CONTAINERFILE = ROOT / "Containerfile.deb"

DISTRIBUTIONS = {
    "ubuntu26.04": "ubuntu:26.04",
    "ubuntu24.04": "ubuntu:24.04",
    "ubuntu22.04": "ubuntu:22.04",
    "debian12": "debian:12",
    "debian13": "debian:13",
}


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--distribution",
        action="append",
        choices=DISTRIBUTIONS,
        dest="distributions",
        help="Build only this distribution; may be repeated (default: all)",
    )
    parser.add_argument(
        "--engine",
        choices=("auto", "podman", "docker"),
        default="auto",
        help="Container engine (default: prefer Podman, then Docker)",
    )
    parser.add_argument(
        "--rebuild-images",
        action="store_true",
        help="Rebuild cached toolchain images",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=ROOT / "dist",
        help="Host output directory (default: ./dist)",
    )
    return parser.parse_args()


def command(*args, cwd=None):
    printable = " ".join(os.fspath(arg) for arg in args)
    print(f"+ {printable}", flush=True)
    subprocess.run(
        [os.fspath(arg) for arg in args],
        cwd=cwd,
        check=True,
    )


def select_engine(requested):
    if requested != "auto":
        if shutil.which(requested) is None:
            raise SystemExit(f"Container engine not found: {requested}")
        return requested
    for candidate in ("podman", "docker"):
        if shutil.which(candidate):
            return candidate
    raise SystemExit("Neither Podman nor Docker was found")


def image_exists(engine, image):
    return (
        subprocess.run(
            [engine, "image", "inspect", image],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        ).returncode
        == 0
    )


def host_platform():
    machine = platform.machine().lower()
    platforms = {
        "amd64": "linux/amd64",
        "x86_64": "linux/amd64",
        "aarch64": "linux/arm64",
        "arm64": "linux/arm64",
        "armv7l": "linux/arm/v7",
        "armv6l": "linux/arm/v6",
        "i386": "linux/386",
        "i686": "linux/386",
    }
    try:
        return platforms[machine]
    except KeyError:
        raise SystemExit(f"Unsupported host machine architecture: {machine}")


def image_name(distribution, container_platform):
    architecture = container_platform.split("/", 1)[-1].replace("/", "-")
    return (
        f"localhost/gst-plugin-toupcam-deb-builder:"
        f"{distribution}-{architecture}"
    )


def build_image(
    engine, distribution, base_image, container_platform, rebuild
):
    image = image_name(distribution, container_platform)
    if rebuild or not image_exists(engine, image):
        command(
            engine,
            "build",
            "--platform",
            container_platform,
            "--build-arg",
            f"BASE_IMAGE={base_image}",
            "--tag",
            image,
            "--file",
            CONTAINERFILE,
            ROOT,
        )
    else:
        print(f"Using cached image {image}")
    return image


def selinux_enabled():
    enforce = Path("/sys/fs/selinux/enforce")
    try:
        return enforce.read_text().strip() == "1"
    except OSError:
        return False


def volume(path, container_path, read_only, use_selinux):
    options = "ro" if read_only else "rw"
    if use_selinux:
        options += ",z"
    return f"{path}:{container_path}:{options}"


def build_distribution(
    engine, distribution, image, output_dir, container_platform
):
    use_selinux = selinux_enabled()
    args = [
        engine,
        "run",
        "--rm",
        "--platform",
        container_platform,
        "--network",
        "none",
        "--env",
        "HOME=/tmp",
        "--env",
        "UV_CACHE_DIR=/tmp/uv-cache",
        "--volume",
        volume(ROOT, "/src", True, use_selinux),
        "--volume",
        volume(output_dir, "/out", False, use_selinux),
        "--workdir",
        "/src",
    ]
    if engine == "podman":
        args.extend(["--userns", "keep-id"])
    else:
        args.extend(["--user", f"{os.getuid()}:{os.getgid()}"])
    args.extend(
        [
            image,
            "./build_deb.py",
            "--distribution-tag",
            distribution,
            "--output-dir",
            "/out",
        ]
    )
    command(*args)


def main():
    args = parse_args()
    engine = select_engine(args.engine)
    container_platform = host_platform()
    distributions = args.distributions or list(DISTRIBUTIONS)
    output_dir = args.output_dir.expanduser().resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    print(f"Container engine: {engine}")
    print(f"Container platform: {container_platform}")
    print(f"Output directory: {output_dir}")
    for distribution in distributions:
        print(f"\n=== Building {distribution} ===", flush=True)
        image = build_image(
            engine,
            distribution,
            DISTRIBUTIONS[distribution],
            container_platform,
            args.rebuild_images,
        )
        build_distribution(
            engine,
            distribution,
            image,
            output_dir,
            container_platform,
        )

    print("\nBuilt packages:")
    for package in sorted(
        output_dir.glob("toupcamsdk_*/gstreamer1.0-toupcam_*.deb")
    ):
        print(package)


if __name__ == "__main__":
    main()
