toupcamsrc
=======

What it is
----------

This is a source element for GStreamer 1.0 for live acquisition from a camera that uses the 
toupcam SDK (http://www.touptek.com/download/showdownload.php?lang=en&id=32).

Intended to be used with https://github.com/JohnDMcMaster/pyuscope/

Why? Here's some background on v4l vs gstreamer plugin: https://uvicrec.blogspot.com/2021/12/migrating-from-kernel-to-gstreamer.html

Install
-------

    # Ubuntu 20.04, tested on 2020-08-12
    # Linux dell 5.4.0-26-generic #30-Ubuntu SMP Mon Apr 20 16:58:30 UTC 2020 x86_64 x86_64 x86_64 GNU/Linux
    # Note 1.8.3 tested working on reference laptop, but 1.16.2 on 20.04
    # Raspbian GNU/Linux 10 (buster), tested on 14th Aug 2020
    sudo apt-get install -y autoconf
    sudo apt-get install -y libtool
    sudo apt-get install -y dpkg-dev devscripts
    # Takes a while
    sudo apt-get install -y gstreamer1.0-tools
    sudo apt-get install -y libgstreamer-plugins-base1.0-dev

    cd ~
    # get sdk from here: http://www.touptek.com/download/showdownload.php?lang=en&id=32
    mkdir toupcamsdk
    cd toupcamsdk
    unzip ../toupcamsdk.zip
    cd ..
    sudo mv toupcamsdk /opt/
    pushd /opt/toupcamsdk/
    sudo cp linux/udev/99-toupcam.rules  /etc/udev/rules.d/
    # disconnect your camera
    sudo udevadm control --reload-rules
    # you may now re-connect your camera :)

    # on x86_64
    sudo cp linux/x64/libtoupcam.so /lib/x86_64-linux-gnu/
    # on raspberry pi
    sudo cp linux/armhf/libtoupcam.so /lib/arm-linux-gnueabihf/

    popd
    sudo ldconfig

    cd ~
    git clone https://github.com/JohnDMcMaster/gst-plugin-toupcam.git
    cd gst-plugin-toupcam
    ./autogen.sh
    make

    # test
    GST_DEBUG=0 GST_PLUGIN_PATH=$PWD/src/.libs/ gst-inspect-1.0 toupcamsrc
    GST_DEBUG=0 GST_PLUGIN_PATH=$PWD/src/.libs/ gst-launch-1.0 toupcamsrc ! videoconvert ! xvimagesink
    # take a snapshot
    GST_PLUGIN_PATH=$PWD/src/.libs/ gst-launch-1.0 toupcamsrc ! videoconvert ! pngenc snapshot=true ! filesink location=/tmp/test.png
    # stream to local video out (e.g. 2nd HDMI on raspberry pi 4)
    GST_PLUGIN_PATH=$PWD/src/.libs/ gst-launch-1.0 toupcamsrc ! videoconvert ! autovideosink
    # Change resolution, set some parameters
    GST_PLUGIN_PATH=$PWD/src/.libs/ gst-launch-1.0 toupcamsrc auto_exposure=1 expotime=100000 esize=2 ! videoconvert ! xvimagesink

    # if all is good
    sudo make install
    echo "export GST_PLUGIN_PATH=/usr/local/lib/gstreamer-1.0" >> ~/.profile

Tested versions:
  * toupcamsdk_46.16880.2020.0330
  * toupcamsdk_46.17309.2020.0616

See the INSTALL file for advanced setup.

To import into the Eclipse IDE, use "existing code as Makefile project", and the file EclipseSymbolsAndIncludePaths.xml is included here
to import the library locations into the project (Properties -> C/C++ General -> Paths and symbols).

Locations
---------

Potential gstreamer plugin locations:
  * /usr/local/lib/gstreamer-1.0
    * Ubuntu 20.04 x64
  * /usr/lib/i386-linux-gnu/gstreamer-1.0
  * /usr/lib/arm-linux-gnueabihf/gstreamer-1.0/
    * Raspberry Pi

Compatbility
---------

These cameras can include:
  * ToupTek
    * All USB cameras
    * Not including networked cameras
  * ScopeTek
    * All USB cameras?
    * Unclear exact relationship to ToupTek
  * AmScope
    * All USB cameras?
    * MU series
      * Ex: AmScope MU800 => ToupTek UCMOS08000KPB
      * Ex: AmScope MU1403 => ToupTek U3CMOS14000KPA
    * MD series
      * Ex: AmScope MD1800 => ScopeTek DCM800
  * Tucsen
      * All USB cameras?
      * SDK is rebranded
      * Do they work with ToupTek SDK as well?

Camera inventory
---------

Quick refernece for cameras that developers are testing:
  * E3ISPM20000KPA
    * McMaster current camera
    * 16 bit capture is partially supported
  * MU800
    * McMaster original camera
