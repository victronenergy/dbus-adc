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

## dbus paths

Tank:

```
com.victronenergy.tank


/analogpinFunc
/Level              0 to 100%
/Remaining          m3
/Status             0=Ok; 1=Disconnected; 2=Short circuited; 3=Unknown
/Capacity           m3
/FluidType          0=Fuel; 1=Fresh water; 2=Waste water; 3=Live well; 4=Oil; 5=Black water (sewage)
/Standard           0=European; 1=USA

Note that the FluidType enumeration is kept in sync with NMEA2000 definitions.
```

Temperature:

```
com.victronenergy.temperature

/analogpinFunc
/Temperature        degrees Celcius
/Status             0=Ok; 1=Disconnected; 2=Short circuited; 3=Reverse polarity; 4=Unknown
/Scale
/Offset
/TemperatureType    0=battery; 1=fridge; 2=generic
```
