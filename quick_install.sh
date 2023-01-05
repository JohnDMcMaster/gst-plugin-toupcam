#!/usr/bin/env bash
# Script to quickly rebuild after /opt/toupcamsdk link changed

set -ex

pushd /opt/toupcamsdk/
sudo cp linux/udev/99-toupcam.rules  /etc/udev/rules.d/
# disconnect your camera
sudo udevadm control --reload-rules
# you may now re-connect your camera :)

# on x86_64
sudo cp linux/x64/libtoupcam.so /lib/x86_64-linux-gnu/
# sudo cp linux/x64/libnncam.so /lib/x86_64-linux-gnu/
# on raspberry pi
# sudo cp linux/armhf/libtoupcam.so /lib/arm-linux-gnueabihf/

popd
sudo ldconfig


./configure
make
sudo make install

