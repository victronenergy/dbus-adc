# dbus-adc

A daemon, part of [Venus](https://github.com/victronenergy/venus/), that reads
analog sensors info from the adc module and publishes it on dbus. Either as
tank level information or temperature readings.

## building

This is a [velib](https://github.com/victronenergy/velib/) project.

Besides on velib, it also depends on:

- python
- dbus libs + headers
- libevent libs + headers

After cloning, and getting the velib submodule, create the Makefile:      

    cd software
    ./ext/velib/mk/init_build.sh

More information about this is in velib/doc/README_make.txt

For cross-compiling for a Venus device, see
[here](https://www.victronenergy.com/live/open_source:ccgx:setup_development_environment).
And then especially the section about velib projects.

