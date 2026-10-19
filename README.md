toupcamsrc

#  What it is

This is a source element for GStreamer 1.0 for live acquisition from a camera that uses the 
toupcam SDK (http://www.touptek.com/download/showdownload.php?lang=en&id=32).

Intended to be used with https://github.com/JohnDMcMaster/pyuscope/

Why? Here's some background on v4l vs gstreamer plugin: https://uvicrec.blogspot.com/2021/12/migrating-from-kernel-to-gstreamer.html


# Install

Reference system:
* Ubuntu 20.04 x64
    * Other Linux distro should work (ex: ARM)
    * MacOS might work
    * Windows won't work
 * SDK: see Version section
 * Camera: E3ISPM20000KPA
    * Other cameras should work

Run the following in a terminal:

```
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

# Verify plugin is installed
GST_PLUGIN_PATH=$PWD/src/.libs/ gst-inspect-1.0 toupcamsrc
# View camera debug info
GST_PLUGIN_PATH=$PWD/src/.libs/ GST_TOUPCAMSRC_INFO=Y gst-launch-1.0 toupcamsrc num-buffers=0 ! fakesink
# Display live feed
GST_PLUGIN_PATH=$PWD/src/.libs/ gst-launch-1.0 toupcamsrc ! videoconvert ! xvimagesink
# Take a snapshot
GST_PLUGIN_PATH=$PWD/src/.libs/ gst-launch-1.0 toupcamsrc ! videoconvert ! pngenc snapshot=true ! filesink location=/tmp/test.png
# Stream to local video out (e.g. 2nd HDMI on raspberry pi 4)
GST_PLUGIN_PATH=$PWD/src/.libs/ gst-launch-1.0 toupcamsrc ! videoconvert ! autovideosink
# Change resolution, set some parameters
GST_PLUGIN_PATH=$PWD/src/.libs/ gst-launch-1.0 toupcamsrc auto_exposure=1 expotime=100000 esize=2 ! videoconvert ! xvimagesink

# if all is good
sudo make install
echo "export GST_PLUGIN_PATH=/usr/local/lib/gstreamer-1.0" >> ~/.profile

# Debug internal error
GST_DEBUG=3,toupcamsrc:5 gst-launch-1.0 toupcamsrc ! queue ! videoconvert ! queue ! xvimagesink
```

See the INSTALL file for advanced setup.

# Debian packages

The SDK and plugin packages can be built independently. The builders never
download the proprietary SDK. Put vendor ZIP files in `toupcamsdk/`; when no
ZIP is specified, each builder uses the alphabetically highest `*.zip`
filename in that directory.

The SDK ZIP can be downloaded from the
[official ToupTek SDK download page](http://www.touptek.com/download/showdownload.php?lang=en&id=32).
Download it manually because the package builders do not fetch SDK files.
An older copy was mirrored at
[microwiki.org](https://microwiki.org/media/touptek/toupcamsdk_53.21907.20221217.zip).

Install the plugin build prerequisites:

```
sudo apt-get install -y autoconf automake libtool pkg-config build-essential \
    dpkg-dev gstreamer1.0-tools libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev
```

Build SDK packages for every architecture present in the vendor archive:

```
./build_toupcamsdk_deb.py
# Or build only one architecture:
./build_toupcamsdk_deb.py --architecture arm64
# Or select an SDK explicitly:
./build_toupcamsdk_deb.py /path/to/toupcamsdk_<version>.zip
```

The SDK package preserves the existing layout under `/opt/toupcamsdk`.
Debian-path header and library copies are also installed so normal compiler
and runtime library discovery continue to work.

Build only the GStreamer plugin package:

```
./build_deb.py
# Or select an SDK explicitly:
./build_deb.py /path/to/toupcamsdk_<version>.zip
```

The plugin package version and filename include the build distribution from
`/etc/os-release`, for example
`gstreamer1.0-toupcam_0.5.0+ubuntu20.04_amd64.deb`. Use
`--distribution-tag` to override automatic detection when building in a
custom environment. The package metadata records and requires the exact
Toupcam SDK version used for the build.

To build packages for every supported distribution locally, install Podman
or Docker and run:

```
./build_distribution_debs.py
```

The default matrix is Ubuntu 26.04, 24.04, and 22.04 plus Debian 12 and 13.
The script prefers Podman and falls back to Docker. Toolchain container images
are cached locally; use `--rebuild-images` to refresh them. Individual
distributions can be selected when needed:

```
./build_distribution_debs.py --distribution ubuntu24.04
./build_distribution_debs.py --engine docker --distribution debian13
```

Packages are grouped by SDK version under
`dist/toupcamsdk_<version>/`. The plugin package declares an exact dependency
on the SDK version used to build it, so install the generated SDK package
before the plugin package:

```
sdk_version=53.21907.20221217
sudo apt install ./dist/toupcamsdk_${sdk_version}/toupcamsdk_${sdk_version}_$(dpkg --print-architecture).deb
# Select the plugin matching the installed distribution:
sudo apt install ./dist/toupcamsdk_${sdk_version}/gstreamer1.0-toupcam_*+ubuntu24.04_$(dpkg --print-architecture).deb
```

# Usage

## Supported resolutions

To get camera resolutions (ie for "esize" enum):

    GST_TOUPCAMSRC_INFO=Y gst-launch-1.0 toupcamsrc

Which will include something like (E3ISPM20000KPA):

```
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
```


# Development

## SDK

Branded SDK notes (Amcsope, Switcam, etc):
  * NOTE: 95% of the time you just want to get the unbranded Touptek SDK (compatible with all branded cameras)
  * But if not, carry on...
  * Locations to edit are marked with "XXX: SDK_BRANDING"
   * gsttoupcamsrc.h
   * makefile.am
  * If we were fancier we could probably add a --configure directive or similar


## Eclipse

To import into the Eclipse IDE, use "existing code as Makefile project", and the file EclipseSymbolsAndIncludePaths.xml is included here
to import the library locations into the project (Properties -> C/C++ General -> Paths and symbols).


## File locations

Potential gstreamer plugin locations:
  * /usr/local/lib/gstreamer-1.0
    * Ubuntu 20.04 x64
  * /usr/lib/i386-linux-gnu/gstreamer-1.0
  * /usr/lib/arm-linux-gnueabihf/gstreamer-1.0/
    * Raspberry Pi


# Compatbility

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

## Camera inventory

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

0.3.0
 * SDK: 53.21907.20221217, 50.19728.20211022
 * Newer SDK support
 * Some rebranded SDK support
 * Add GST_TOUPCAMSRC_INFO

0.4.0
 * GST_TOUPCAMSRC_INFO: tweak output format
 * Add gain control

0.5.0
 * Significant properties added (in lieu of expanding GST_TOUPCAMSRC_INFO hacks)
