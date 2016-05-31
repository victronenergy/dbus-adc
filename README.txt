dbus-adc
========

daemon on ccgx beaglebone black that reads analog sensors info from a the adc 
module publishes it on dbus.

Building depens on:
 - python
 - dbus libs + headers
 - libevent libs + headers

For crosscompiling, installing the ccgx sdk is sufficient, it
contains above dependencies. For a normal compile, to run 
the project on a pc, Google will help you :).


To build and compile for CCGX:

# create the make file
  cd software
  ./ext/velib/mk/init_build.sh
  
# now, there are two options. One build on a pc, to run on a pc:
export CC=gcc
make

# or, option two, setup the environment for cross compiling:
. /opt/bpp3/current/environment-setup-armv7a-vfp-neon-bpp3-linux-gnueabi
export CC=gcc
export CROSS_COMPILE=arm-bpp3-linux-gnueabi-
export TARGET=ccgx

# and then compile:
make
#####################################################################################################################################################################

#############################
To build and compile for bbb:
#############################
## Cross compiling for the bbb

# First, browse to the folder that contains the makefile- for this example: cd dev/dbus-adc/software 
# than run the following command lines

source /opt/venus/v1.40-bbb/environment-setup-cortexa8hf-vfp-neon-ve-linux-gnueabi
export CC=gcc
export CROSS_COMPILE=arm-ve-linux-gnueabi-
export TARGET=ccgx
export CFLAGS="-march=armv7-a -mfloat-abi=hard -mfpu=neon -mtune=cortex-a8 --sysroot=/opt/venus/v1.40-bbb/sysroots/cortexa8hf-vfp-neon-ve-linux-gnueabi/"
export LDFLAGS="-march=armv7-a -mfloat-abi=hard -mfpu=neon -mtune=cortex-a8 --sysroot=/opt/venus/v1.40-bbb/sysroots/cortexa8hf-vfp-neon-ve-linux-gnueabi/"
make
qtcreator

#######################################################################################################################################################################
# then scp the end result to the beaglebone, and run it on the beaglebone
# (add -vvv to see some minimal output)
# So will be something like:

scp ./obj/ccgx-linux-arm-gnueabi-release/gps_dbus root@192.168.51.64:~/
root@192.168.51.64

and then in that shell execute the binary with:
./gps-dbus -vvv


# Or option 2) start qtcreator, and compile, link, deploy and then debug from there:
~/Qt/Tools/QtCreator/bin/qtcreator
