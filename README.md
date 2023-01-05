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

    # 2022-08-19: test 2021-10-29 on Pop!_OS 22.04 LTS
    # error: implicit declaration of function ‘Toupcam_AwbOnce’; did you mean ‘Toupcam_AwbInit’
    # was using: toupcamsdk_46.17309.2020.0616_2020-02-05
    # upgrade fixed

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
    # sudo cp linux/x64/libnncam.so /lib/x86_64-linux-gnu/
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

Branded SDK notes (Amcsope, Switcam, etc):
  * NOTE: 95% of the time you just want to get the unbranded Touptek SDK (compatible with all branded cameras)
  * But if not, carry on...
  * Locations to edit are marked with "XXX: SDK_BRANDING"
   * gsttoupcamsrc.h
   * makefile.am
  * If we were fancier we could probably add a --configure directive or similar

See the INSTALL file for advanced setup.

To import into the Eclipse IDE, use "existing code as Makefile project", and the file EclipseSymbolsAndIncludePaths.xml is included here
to import the library locations into the project (Properties -> C/C++ General -> Paths and symbols).

To get camera resolutions:

    GST_TOUPCAMSRC_INFO=Y gst-launch-1.0 toupcamsrc

Which will include something like:

  Still resolutions: 3
    esize 0
          5440 x 3648
          2.4 x 2.4 um
    esize 1
          2736 x 1824
          4.8 x 4.8 um
    esize 2
          1824 x 1216
          7.2 x 7.2 um

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

# Version history

0.0.0
 * SDK: 46.17309.2020.0616
 * Import from comissioned PoC

0.1.0
 * SDK: 46.17309.2020.0616
 * Add more controls
 * 16 bit PoC
 * Instructions

0.2.0
 * SDK: 50.19728.20211022
 * Documentation updates
 * Newer SDK support

0.3.0 (WIP)
 * SDK: 53.21907.20221217, 50.19728.20211022
 * Newer SDK support
 * Some rebranded SDK support
 * Add GST_TOUPCAMSRC_INFO

