#!/usr/bin/env bash

set -ex

# flavor        version                 toupcam.h md5
# toupcamsdk    53.21907.20221217       9afe63898896d094a41f5adc40a541df
WANT_TOUPCAM_MD5=9afe63898896d094a41f5adc40a541df

install_toupcam_sdk() {
    if [ -n "$TOUPCAMSDK_SKIP_INSTALL" ] ; then
        echo "Skipping SDK install on TOUPCAMSDK_SKIP_INSTALL"
        return
    fi

    # TODO: check if the expected version is installed
    if [  -d /opt/toupcamsdk ] ; then
        echo "toupcamsdk: already installed. Checking..."
        md5=$(md5sum /opt/toupcamsdk/inc/toupcam.h |cut -d' ' -f1)
        echo "Want header MD5: $WANT_TOUPCAM_MD5"
        echo "Got header MD5:  $md5"
        if [ "$md5" != "$WANT_TOUPCAM_MD5" ]; then
            echo "ERROR: expected SDK mismatch. Either delete /opt/toupcamsdk to update or set TOUPCAMSDK_SKIP_INSTALL=Y"
            exit 1
        fi
        return
    fi

    echo "toupcamsdk: installing"
    mkdir -p download
    pushd download
    # 2023-03-21: latest version on site is old (50.x)
    # Get the newer one
    # wget -c http://www.touptek.com/upload/download/toupcamsdk.zip
    wget -c https://microwiki.org/media/touptek/toupcamsdk_53.21907.20221217.zip
    mv toupcamsdk_53.21907.20221217.zip toupcamsdk.zip
    mkdir toupcamsdk
    pushd toupcamsdk
    unzip ../toupcamsdk.zip
    popd
    sudo mv toupcamsdk /opt/
    popd

    pushd /opt/toupcamsdk/
    sudo cp linux/udev/99-toupcam.rules  /etc/udev/rules.d/
    sudo udevadm control --reload-rules
    sudo cp linux/x64/libtoupcam.so /lib/x86_64-linux-gnu/
    sudo ldconfig
    popd
}

# FIXME: move to plugin
install_gst_plugin_toupcam() {
    # Forcing install each time now to ensure updated
    if [ -f "/usr/local/lib/gstreamer-1.0/libgsttoupcamsrc.so" ] ; then
        echo "gst-plugin-toupcam: already installed => reinstalling"
    else
        echo "gst-plugin-toupcam: not installed"
    fi

    sudo apt-get install -y autoconf libtool dpkg-dev devscripts gstreamer1.0-tools libgstreamer-plugins-base1.0-dev

    install_toupcam_sdk

    ./autogen.sh
    make
    sudo make install

    # Token effor to prevent path from stacking up and/or conflicts
    if [[ -z "${GST_PLUGIN_PATH}" ]]; then
        echo "export GST_PLUGIN_PATH=/usr/local/lib/gstreamer-1.0" >> ~/.profile
        echo "export GST_PLUGIN_PATH=/usr/local/lib/gstreamer-1.0" >> ~/.bashrc
    else
        echo "GST_PLUGIN_PATH already set, skipping"
    fi
}

sudo apt-get update
install_gst_plugin_toupcam

