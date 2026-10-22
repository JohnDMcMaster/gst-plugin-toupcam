#!/usr/bin/env bash

set -ex

install_gst_plugin_toupcam() {
    if ! command -v uv >/dev/null 2>&1 ; then
        echo "uv is required to run the Debian package builders" >&2
        exit 1
    fi

    sudo apt-get install -y autoconf automake build-essential libtool \
        pkg-config dpkg-dev gstreamer1.0-tools libgstreamer1.0-dev \
        libgstreamer-plugins-base1.0-dev

    ./build_toupcamsdk_deb.py --architecture "$(dpkg --print-architecture)"
    sdk_deb=$(ls -t dist/toupcamsdk_*/toupcamsdk_*.deb | head -n 1)
    sudo apt-get install -y "$sdk_deb"

    ./build_deb.py
    plugin_deb=$(ls -t dist/toupcamsdk_*/gstreamer1.0-toupcam_*.deb | head -n 1)
    sudo apt-get install -y "$plugin_deb"
}

sudo apt-get update
install_gst_plugin_toupcam

