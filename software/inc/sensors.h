#ifndef SENSORS_H
#define SENSORS_H

#include <velib/base/base.h>
#include <velib/types/ve_item.h>
#include <velib/types/ve_item_def.h>

#include "adc.h"
#include "values.h"

#define TANK_LEVEL_SENSOR_DIVIDER           (680) // ohms
#define EUR_MAX_TANK_LEVEL_RESISTANCE       (180) //ohms
#define USA_MAX_TANK_LEVEL_RESISTANCE       (240) //ohms
#define USA_MIN_TANK_LEVEL_RESISTANCE       (30) //ohms

// Temperature sensors settings
#define TEMP_SENS_VOLT_DIVID_R1             (10000) // ohms
#define TEMP_SENS_VOLT_DIVID_R2             (4700) // ohms

#define TEMP_SENS_MAX_ADCIN                 ADC_1p3VOLTS // ~400K
#define TEMP_SENS_MIN_ADCIN                 250

#define NUM_OF_SENSOR_SETTINGS_PARAMS       3
#define NUM_OF_PROD_ITEMS                   4
#define NUM_OF_SENSOR_OUTPUTS               2
#define NUM_OF_SENSOR_VARIANTS              NUM_OF_SENSOR_OUTPUTS + NUM_OF_SENSOR_SETTINGS_PARAMS
#define SENSORS_INFO_ARRAY_SIZE             NUM_OF_SENSOR_VARIANTS + NUM_OF_PROD_ITEMS

typedef enum
{
    capacity = 0,
    fluidType = 1,
    standard = 2,
    scale = 0,
    offset = 1,
    TempType = 2,
    num_of_parameters = 3
}parameter_name_t;

typedef enum
{
    index_tankLevel1 = 0,
    index_tankLevel2,
    index_tankLevel3,
    index_temperature1,
    index_temperature2,
    num_of_analog_sensors
}analog_sensors_index_t;

typedef struct
{
    VeItem name;
    VeItem id;
    VeItem instance;
    VeItem connected;
} ProductInfo;

typedef struct
{
   VeItem level;
   VeItem remaining;
   VeItem capacity;
   VeItem fluidType;
   VeItem standard;
}tank_level_sensor_item_t;

typedef struct
{
   VeItem temperature;
   VeItem scale;
   VeItem offset;
   VeItem temperatureType;
   VeItem spareParam;
}temperature_sensor_item_t;


typedef struct
{
   VeVariant level;
   VeVariant remaining;
   VeVariant capacity;
   VeVariant fluidType;
   VeVariant standard;
}tank_level_sensor_variant_t;

typedef struct
{
   VeVariant temperature;
   VeVariant scale;
   VeVariant offset;
   VeVariant temperatureType;
   VeVariant spareParam;
}temperature_sensor_variant_t;

typedef struct
{
    char        *productName;
    const char  *service;
}sensors_dbus_interface_t;

typedef struct
{
    ProductInfo     product;
    union
    {
        tank_level_sensor_item_t     tank_level;
        temperature_sensor_item_t    temperature;
    };
}sensors_items_t;

typedef struct
{
    union
    {
        tank_level_sensor_variant_t     tank_level;
        temperature_sensor_variant_t    temperature;
    };
}sensors_variants_t;

typedef enum
{
    tank_level_t = 0,
    temperature_t,
    num_of_sensorsTypes
}sensors_type_t;

typedef struct
{
    float scale;
    float offset;
}signal_correction_t;

typedef struct
{
    signal_correction_t sig_correct;
    filter_iir_lpf_t    filter_iir_lpf;
}signal_condition_t;

typedef struct
{
    const adc_analogPin_t       adc_pin;
    un32                        adc_sample;
    signal_condition_t          sig_cond;
    sensors_dbus_interface_t    dbus;
}sensors_interface_t;

/* sensor structure */
typedef struct
{
   const sensors_type_t   sensor_type;
   veBool                 valid;
   sensors_interface_t    interface;
   dbus_info_t            dbus_info[NUM_OF_SENSOR_SETTINGS_PARAMS];
   sensors_items_t        items;
   sensors_variants_t     variant;
}analog_sensor_t;

void sensor_init(VeItem *root, analog_sensors_index_t sensor_index);
void sensors_handle(void);

void sensors_dbusInit(analog_sensors_index_t sensor_index);
void values_dbus_service_addSettings(analog_sensor_t * sensor);
void sensors_dbusConnect(analog_sensor_t * sensor, analog_sensors_index_t sensor_index);
void sensors_dbusDisconnect(void);

//void valueChanged(struct VeItem *item);

#define SENSORS_CONSTANT_DATA \
{		\
    {	\
        tank_level_t,\
        veFalse,\
        {\
            adc_pin4,\
            0,\
            {{},{1000,0.001,0}},\
            {\
                "Tank Level Sender 1",\
                "com.victronenergy.tank.builtin_adc4_di0"\
            }		\
        },		\
        {\
            {\
                0.2,\
                0,\
                1,\
                "Settings/tank/1/Capacity"\
            },\
            {\
                ' ',\
                0,\
                5,\
                "Settings/tank/1/FluidType"\
            },\
            {\
                0,\
                0,\
                1,\
                "Settings/tank/1/Standard"\
            }\
        }\
    },\
    {\
        tank_level_t,\
        veFalse,\
        {\
            adc_pin6,\
            0,\
            {{},{1000,0.001,0}},\
            {\
                "Tank Level Sender 2",\
                "com.victronenergy.tank.builtin_adc6_di1"\
            }\
        },\
        {\
            {\
                0.2,\
                0,\
                1,\
                "Settings/tank/2/Capacity"\
            },\
            {\
                ' ',\
                0,\
                5,\
                "Settings/tank/2/FluidType"\
            },\
            {\
                0,\
                0,\
                1,\
                "Settings/tank/2/Standard"\
            }\
        }\
    },\
    {\
        tank_level_t,\
        veFalse,\
        {\
            adc_pin2,\
            0,\
            {{},{1000,0.001,0}},\
            {\
                "Tank Level Sender 3",\
                "com.victronenergy.tank.builtin_adc2_di2"\
            }\
        },\
        {\
            {\
                0.2,\
                0,\
                1,\
                "Settings/tank/3/Capacity"\
            },\
            {\
                ' ',\
                0,\
                5,\
                "Settings/tank/3/FluidType"\
            },\
            {\
                0,\
                0,\
                1,\
                "Settings/tank/3/Standard"\
            }\
        }\
    },\
    {\
        temperature_t,\
        veFalse,\
        {\
            adc_pin5,\
            0,\
            {{},{100,0.01,0}},\
            {\
                "Temperature Sensor 1",\
                "com.victronenergy.temperature.builtin_adc5_di0"\
            }\
        },\
        {\
            {\
                1.00,\
                0.10,\
                10.00,\
                "Settings/Temperature/1/Scale"\
            },\
            {\
                0,\
                -100,\
                100,\
                "Settings/Temperature/1/Offset"\
            },\
            {\
                ' ',\
                0,\
                3,\
                "Settings/Temperature/1/TemperatureType"\
            }\
        }\
    },\
    {\
        temperature_t,\
        veFalse,\
        {\
            adc_pin3,\
            0,\
            {{},{100,0.01,0}},\
            {\
                "Temperature Sensor 2",\
                "com.victronenergy.temperature.builtin_adc3_di1"\
            }\
        },\
        {\
            {\
                1.00,\
                0.10,\
                10.00,\
                "Settings/Temperature/2/Scale"\
            },\
            {\
                0,\
                -100,\
                100,\
                "Settings/Temperature/2/Offset"\
            },\
            {\
                ' ',\
                0,\
                3,\
                "Settings/Temperature/2/TemperatureType"\
            }\
         }\
    }\
}


#define SENSOR_ITEM_CONTAINER \
{\
    {\
        {&analog_sensor[index_tankLevel1].items.product.connected,										NULL,											"Connected",		&units,	0},\
        {&analog_sensor[index_tankLevel1].items.product.name,											NULL,											"ProductName",		&units,	0},\
        {&analog_sensor[index_tankLevel1].items.product.id,												NULL,											"ProductId",		&units,	0},\
        {&analog_sensor[index_tankLevel1].items.product.instance,										NULL,											"DeviceInstance",	&units,	0},\
        {&analog_sensor[index_tankLevel1].items.tank_level.level,				&analog_sensor[index_tankLevel1].variant.tank_level.level,				"Level",  			&units,	5},\
        {&analog_sensor[index_tankLevel1].items.tank_level.remaining,			&analog_sensor[index_tankLevel1].variant.tank_level.remaining,			"Remaining",  		&units,	5},\
        {&analog_sensor[index_tankLevel1].items.tank_level.capacity,			&analog_sensor[index_tankLevel1].variant.tank_level.capacity,			"Capacity",			&units,	5, capacityChange},\
        {&analog_sensor[index_tankLevel1].items.tank_level.fluidType,			&analog_sensor[index_tankLevel1].variant.tank_level.fluidType,			"FluidType",  		&units,	5, fluidTypeChange},\
        {&analog_sensor[index_tankLevel1].items.tank_level.standard,			&analog_sensor[index_tankLevel1].variant.tank_level.standard,			"Standard",  		&units,	5, standardChange}\
    },\
    {\
        {&analog_sensor[index_tankLevel2].items.product.connected,										NULL,											"Connected",		&units,	0},\
        {&analog_sensor[index_tankLevel2].items.product.name,											NULL,											"ProductName",		&units,	0},\
        {&analog_sensor[index_tankLevel2].items.product.id,												NULL,											"ProductId",		&units,	0},\
        {&analog_sensor[index_tankLevel2].items.product.instance,										NULL,											"DeviceInstance",	&units,	0},\
        {&analog_sensor[index_tankLevel2].items.tank_level.level,				&analog_sensor[index_tankLevel2].variant.tank_level.level,				"Level",  			&units,	5},\
        {&analog_sensor[index_tankLevel2].items.tank_level.remaining,			&analog_sensor[index_tankLevel2].variant.tank_level.remaining,			"Remaining",  		&units,	5},\
        {&analog_sensor[index_tankLevel2].items.tank_level.capacity,			&analog_sensor[index_tankLevel2].variant.tank_level.capacity,			"Capacity",			&units,	5, capacityChange},\
        {&analog_sensor[index_tankLevel2].items.tank_level.fluidType,			&analog_sensor[index_tankLevel2].variant.tank_level.fluidType,			"FluidType",  		&units,	5, fluidTypeChange},\
        {&analog_sensor[index_tankLevel2].items.tank_level.standard,			&analog_sensor[index_tankLevel2].variant.tank_level.standard,			"Standard",  		&units,	5, standardChange}\
    },\
    {\
        {&analog_sensor[index_tankLevel3].items.product.connected,										NULL,											"Connected",		&units,	0},\
        {&analog_sensor[index_tankLevel3].items.product.name,											NULL,											"ProductName",		&units,	0},\
        {&analog_sensor[index_tankLevel3].items.product.id,												NULL,											"ProductId",		&units,	0},\
        {&analog_sensor[index_tankLevel3].items.product.instance,										NULL,											"DeviceInstance",	&units,	0},\
        {&analog_sensor[index_tankLevel3].items.tank_level.level,				&analog_sensor[index_tankLevel3].variant.tank_level.level,				"Level",  			&units,	5},\
        {&analog_sensor[index_tankLevel3].items.tank_level.remaining,			&analog_sensor[index_tankLevel3].variant.tank_level.remaining,			"Remaining",  		&units,	5},\
        {&analog_sensor[index_tankLevel3].items.tank_level.capacity,			&analog_sensor[index_tankLevel3].variant.tank_level.capacity,			"Capacity",			&units,	5, capacityChange},\
        {&analog_sensor[index_tankLevel3].items.tank_level.fluidType,			&analog_sensor[index_tankLevel3].variant.tank_level.fluidType,			"FluidType",  		&units,	5, fluidTypeChange},\
        {&analog_sensor[index_tankLevel3].items.tank_level.standard,			&analog_sensor[index_tankLevel3].variant.tank_level.standard,			"Standard",  		&units,	5, standardChange}\
    },\
    {\
        {&analog_sensor[index_temperature1].items.product.connected,									NULL,											"Connected",		&units,	0},\
        {&analog_sensor[index_temperature1].items.product.name,											NULL,											"ProductName",		&units,	0},\
        {&analog_sensor[index_temperature1].items.product.id,											NULL,											"ProductId",		&units,	0},\
        {&analog_sensor[index_temperature1].items.product.instance,										NULL,											"DeviceInstance",	&units,	0},\
        {&analog_sensor[index_temperature1].items.temperature.temperature,		&analog_sensor[index_temperature1].variant.temperature.temperature,		"Temperature",		&units,	5},\
        {&analog_sensor[index_temperature1].items.temperature.spareParam,                               NULL,                                   		"",                 &units,	5},\
        {&analog_sensor[index_temperature1].items.temperature.scale,			&analog_sensor[index_temperature1].variant.temperature.scale,			"Scale",			&units,	5, scaleChange},\
        {&analog_sensor[index_temperature1].items.temperature.offset,			&analog_sensor[index_temperature1].variant.temperature.offset,			"Offset",			&units,	5, offsetChange},\
        {&analog_sensor[index_temperature1].items.temperature.temperatureType,	&analog_sensor[index_temperature1].variant.temperature.temperatureType,	"TemperatureType",	&units,	5, TempTypeChange}\
    },\
    {\
        {&analog_sensor[index_temperature2].items.product.connected,									NULL,											"Connected",		&units,	0},\
        {&analog_sensor[index_temperature2].items.product.name,											NULL,											"ProductName",		&units,	0},\
        {&analog_sensor[index_temperature2].items.product.id,											NULL,											"ProductId",		&units,	0},\
        {&analog_sensor[index_temperature2].items.product.instance,										NULL,											"DeviceInstance",	&units,	0},\
        {&analog_sensor[index_temperature2].items.temperature.temperature,		&analog_sensor[index_temperature2].variant.temperature.temperature,		"Temperature",		&units,	5},\
        {&analog_sensor[index_temperature2].items.temperature.spareParam,                               NULL,                                   		"",                 &units,	5},\
        {&analog_sensor[index_temperature2].items.temperature.scale,			&analog_sensor[index_temperature2].variant.temperature.scale,			"Scale",			&units,	5, scaleChange},\
        {&analog_sensor[index_temperature2].items.temperature.offset,			&analog_sensor[index_temperature2].variant.temperature.offset,			"Offset",			&units,	5, offsetChange},\
        {&analog_sensor[index_temperature2].items.temperature.temperatureType,	&analog_sensor[index_temperature2].variant.temperature.temperatureType,	"TemperatureType",	&units,	5, TempTypeChange}\
    }\
}
#endif // End of sensors.h file
