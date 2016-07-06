#include <velib/base/base.h>
#include <velib/base/ve_string.h>
#include <velib/types/ve_item.h>
#include <velib/utils/ve_logger.h>
#include <velib/types/ve_dbus_item.h>

#include "values.h"
#include "sensors.h"
#include "version.h"
#include "adc.h"

#include <string.h>
#ifdef WIN32
#include <stdio.h>
#else
#include <stdlib.h>
////#include <sys/stat.h>
////#include <fcntl.h>
////#include <unistd.h>
#endif

#define F_CONNECTED					1

#define CONNECTION_TIMEOUT			5*20	/* 50ms */

static un16 timeout;
const char version[] = VERSION_STR;

veBool capacityChange(struct VeItem *item, void *ctx, VeVariant *variant);
veBool fluidTypeChange(struct VeItem *item, void *ctx, VeVariant *variant);
veBool standardChange(struct VeItem *item, void *ctx, VeVariant *variant);
veBool scaleChange(struct VeItem *item, void *ctx, VeVariant *variant);
veBool offsetChange(struct VeItem *item, void *ctx, VeVariant *variant);
veBool filterStrengthChange(struct VeItem *item, void *ctx, VeVariant *variant);

//static varibles;
analog_sensor_t analog_sensor[num_of_analog_sensors] =
{
    // resistiveTank1
    {
        tank_level_t,                       // sensors_type_t sensors_type
        veFalse,                            // veBool valid
        {//sensors_interface_t interface
            adc_pin4,                       // const adc_analogPin_t adc_pin
            0,                              // un32 adc_sample
            {/*signal_condition_t sig_cond*/},
            {//sensors_dbus_interface_t dbus
                "Fuel Tank",                    // char *productName
                "com.victronenergy.tank.builtin_adc4_di0"    // const char *service
            }
        },
        {// dbus_info_t dbus_info[3]
            {
                0.2,
                0,
                1,
                "Settings/tank/1/Capacity"
            },
            {
                0,
                0,
                5,
                "Settings/tank/1/FluidType"
            },
            {
                0,
                0,
                1,
                "Settings/tank/1/Standard"
            }
        }
    },
    // resistiveTank2
    {
        tank_level_t,
        veFalse,
        {
            adc_pin6,
            0,
            {},
            {
                "Fuel Tank",                    // char *productName
                "com.victronenergy.tank.builtin_adc6_di1"
            }
        },
        {
            {
                0.2,
                0,
                1,
                "Settings/tank/2/Capacity"
            },
            {
                0,
                0,
                5,
                "Settings/tank/2/FluidType"
            },
            {
                0,
                0,
                1,
                "Settings/tank/2/Standard"
            }
        }
    },
    // resistiveTank3
    {
        tank_level_t,
        veFalse,
        {
            adc_pin2,
            0,
            {},
            {
                "Fuel Tank",                    // char *productName
                "com.victronenergy.tank.builtin_adc2_di2"
            }
        },
        {
            {
                0.2,
                0,
                1,
                "Settings/tank/3/Capacity"
            },
            {
                0,
                0,
                5,
                "Settings/tank/3/FluidType"
            },
            {
                0,
                0,
                1,
                "Settings/tank/3/Standard"
            }
        }
    },
    // vin_std_0_10
    {
        vin_std_0_10_t,
        veFalse,
        {
            adc_pin0,
            0,
            {},
            {
                "(0-10)Volts Analog Sensor",                    // char *productName
                "com.victronenergy.vin.builtin_adc0_di0"
            }
        },
        {
            {
                1,
                0.1,
                10,
                "Settings/vin/Scale/Value"
            },
            {
                0,
                0,
                100,
                "Settings/vin/Offset/Value"
            },
            {
                0.01,
                0.001,
                100,
                "Settings/vin/Fc/Value"
            }
        }
    },
    // TempSensor1
    {
        temperature_t,
        veFalse,
        {
            adc_pin5,
            0,
            {},
            {
                "Temperature Sensor 1",                    // char *productName
                "com.victronenergy.temperature.builtin_adc5_di0"
            }
        },
        {
            {
                1,
                0.1,
                10,
                "Settings/Temperature/1/Scale/Value"
            },
            {
                0,
                0,
                100,
                "Settings/Temperature/1/Offset/Value"
            },
            {
                0.01,
                0.001,
                100,
                "Settings/Temperature/1/Fc/Value"
            }
        }
    },
    // TempSensor2
    {
        temperature_t,
        veFalse,
        {
            adc_pin3,
            0,
            {},
            {
                "Temperature Sensor 2",                    // char *productName
                "com.victronenergy.temperature.builtin_adc3_di1"
            }
        },
        {
            {
                1,
                0.1,
                10,
                "Settings/Temperature/2/Scale/Value"
            },
            {
                0,
                0,
                100,
                "Settings/Temperature/2/Offset/Value"
            },
            {
                0.01,
                0.001,
                100,
                "Settings/Temperature/2/Fc/Value"
            }
        }
    },
    // vbat
    {
        vbat_t,
        veFalse,
        {
            adc_pin1,
            0,
            {},
            {
                "Battery Voltage",                    // char *productName
                "com.victronenergy.vbat.builtin_adc1_di0"
            }
        },
        {
            {
                1,
                0.1,
                10,
                "Settings/vbat/Scale/Value"
            },
            {
                0,
                0,
                100,
                "Settings/vbat/Offset/Value"
            },
            {
                0.01,
                0.001,
                100,
                "Settings/vbat/Fc/Value"
            }
        }
    } //vbat
};

// potential divider for the tank level sender
const potential_divider_t sensor_tankLevel_pd =
{
    TANK_LEVEL_SENSOR_DIVIDER,          // R1
    (POTENTIAL_DIV_MAX_SAMPLE - 1),
};
const potential_divider_t sensor_vin_std_0_10_pd =
{
    VIN_STD_0_10_DIVID_R1,
    VIN_STD_0_10_DIVID_R2
};
const potential_divider_t sensor_temperature_pd =
{
    TEMP_SENS_VOLT_DIVID_R1,
    TEMP_SENS_VOLT_DIVID_R2
};
const potential_divider_t sensor_vbat_pd =
{
    VBAT_DIVID_R1,
    VBAT_DIVID_R2
};

/* Container for VeItems */
static sensors_items_t       sensors_items[num_of_analog_sensors];

typedef enum
{
    percentage = 0,
    Celsius,
    volts,
    none,
    num_of_units
}units_t;

static VeVariantUnitFmt units[num_of_units] =
{
    {3,	" [%]"},
    {3,	" [C]"},
    {3,	" [v]"},
    {0, ""}
};
static ItemInfo const sensors_info[num_of_analog_sensors][SENSORS_INFO_ARRAY_SIZE] =
{
    {
        {&sensors_items[index_tankLevel1].product.connected,        NULL,							"Connected",		&units[none], 0},
        {&sensors_items[index_tankLevel1].product.name,				NULL,							"ProductName",		&units[none], 0},
        {&sensors_items[index_tankLevel1].product.id,               NULL,							"ProductId",		&units[none], 0},
        {&sensors_items[index_tankLevel1].product.firmwareVersion,	NULL,							"FirmwareVersion",	&units[none], 0},
        {&sensors_items[index_tankLevel1].product.instance,			NULL,							"DeviceInstance",	&units[none], 0},
        {&sensors_items[index_tankLevel1].item[0],	&analog_sensor[index_tankLevel1].var[0],		"Level",  			&units[none], 5},
        {&sensors_items[index_tankLevel1].item[1],	&analog_sensor[index_tankLevel1].var[1],		"Remaining",  		&units[none], 5},
        {&sensors_items[index_tankLevel1].item[2],	&analog_sensor[index_tankLevel1].var[2],		"Capacity",			&units[none], 5, capacityChange},
        {&sensors_items[index_tankLevel1].item[3],	&analog_sensor[index_tankLevel1].var[3],		"FluidType",  		&units[none], 5, fluidTypeChange},
        {&sensors_items[index_tankLevel1].item[4],	&analog_sensor[index_tankLevel1].var[4],		"Standard",  		&units[none], 5, standardChange}
    },
    {
        {&sensors_items[index_tankLevel2].product.connected,        NULL,							"Connected",		&units[none], 0},
        {&sensors_items[index_tankLevel2].product.name,				NULL,							"ProductName",		&units[none], 0},
        {&sensors_items[index_tankLevel2].product.id,               NULL,							"ProductId",		&units[none], 0},
        {&sensors_items[index_tankLevel2].product.firmwareVersion,	NULL,							"FirmwareVersion",	&units[none], 0},
        {&sensors_items[index_tankLevel2].product.instance,			NULL,							"DeviceInstance",	&units[none], 0},
        {&sensors_items[index_tankLevel2].item[0],	&analog_sensor[index_tankLevel2].var[0],		"Level",  			&units[none], 5},
        {&sensors_items[index_tankLevel2].item[1],	&analog_sensor[index_tankLevel2].var[1],		"Remaining",  		&units[none], 5},
        {&sensors_items[index_tankLevel2].item[2],	&analog_sensor[index_tankLevel2].var[2],		"Capacity",			&units[none], 5, capacityChange},
        {&sensors_items[index_tankLevel2].item[3],	&analog_sensor[index_tankLevel2].var[3],		"FluidType",  		&units[none], 5, fluidTypeChange},
        {&sensors_items[index_tankLevel2].item[4],	&analog_sensor[index_tankLevel2].var[4],		"Standard",  		&units[none], 5, standardChange}
    },
    {
        {&sensors_items[index_tankLevel3].product.connected,        NULL,							"Connected",		&units[none], 0},
        {&sensors_items[index_tankLevel3].product.name,				NULL,							"ProductName",		&units[none], 0},
        {&sensors_items[index_tankLevel3].product.id,               NULL,							"ProductId",		&units[none], 0},
        {&sensors_items[index_tankLevel3].product.firmwareVersion,	NULL,							"FirmwareVersion",	&units[none], 0},
        {&sensors_items[index_tankLevel3].product.instance,			NULL,							"DeviceInstance",	&units[none], 0},
        {&sensors_items[index_tankLevel3].item[0],	&analog_sensor[index_tankLevel3].var[0],		"Level",  			&units[none], 5},
        {&sensors_items[index_tankLevel3].item[1],	&analog_sensor[index_tankLevel3].var[1],		"Remaining",  		&units[none], 5},
        {&sensors_items[index_tankLevel3].item[2],	&analog_sensor[index_tankLevel3].var[2],		"Capacity",			&units[none], 5, capacityChange},
        {&sensors_items[index_tankLevel3].item[3],	&analog_sensor[index_tankLevel3].var[3],		"FluidType",  		&units[none], 5, fluidTypeChange},
        {&sensors_items[index_tankLevel3].item[4],	&analog_sensor[index_tankLevel3].var[4],		"Standard",  		&units[none], 5, standardChange}
    },
    {
        {&sensors_items[index_vin_std_0_10].product.connected,        	NULL,						"Connected",		&units[none], 0},
        {&sensors_items[index_vin_std_0_10].product.name,				NULL,						"ProductName",		&units[none], 0},
        {&sensors_items[index_vin_std_0_10].product.id,               	NULL,						"ProductId",		&units[none], 0},
        {&sensors_items[index_vin_std_0_10].product.firmwareVersion,	NULL,						"FirmwareVersion",	&units[none], 0},
        {&sensors_items[index_vin_std_0_10].product.instance,			NULL,						"DeviceInstance",	&units[none], 0},
        {&sensors_items[index_vin_std_0_10].item[0],	&analog_sensor[index_vin_std_0_10].var[0],	"Vin_std_0_10",		&units[none], 5},
        {&sensors_items[index_vin_std_0_10].item[1],	&analog_sensor[index_vin_std_0_10].var[1],	"dummy",			&units[none], 5},
        {&sensors_items[index_vin_std_0_10].item[2],	&analog_sensor[index_vin_std_0_10].var[2],	"scale",			&units[none], 5, scaleChange},
        {&sensors_items[index_vin_std_0_10].item[3],	&analog_sensor[index_vin_std_0_10].var[3],	"offset",			&units[none], 5, offsetChange},
        {&sensors_items[index_vin_std_0_10].item[4],	&analog_sensor[index_vin_std_0_10].var[4],	"FilterStrength",	&units[none], 5, filterStrengthChange}
    },
    {
        {&sensors_items[index_temperature1].product.connected,        	NULL,						"Connected",		&units[none], 0},
        {&sensors_items[index_temperature1].product.name,				NULL,						"ProductName",		&units[none], 0},
        {&sensors_items[index_temperature1].product.id,               	NULL,						"ProductId",		&units[none], 0},
        {&sensors_items[index_temperature1].product.firmwareVersion,	NULL,						"FirmwareVersion",	&units[none], 0},
        {&sensors_items[index_temperature1].product.instance,			NULL,						"DeviceInstance",	&units[none], 0},
        {&sensors_items[index_temperature1].item[0],	&analog_sensor[index_temperature1].var[0],	"Temperature",		&units[none], 5},
        {&sensors_items[index_temperature1].item[1],	&analog_sensor[index_temperature1].var[1],	"dummy",			&units[none], 5},
        {&sensors_items[index_temperature1].item[2],	&analog_sensor[index_temperature1].var[2],	"scale",			&units[none], 5, scaleChange},
        {&sensors_items[index_temperature1].item[3],	&analog_sensor[index_temperature1].var[3],	"offset",			&units[none], 5, offsetChange},
        {&sensors_items[index_temperature1].item[4],	&analog_sensor[index_temperature1].var[4],	"FilterStrength",	&units[none], 5, filterStrengthChange}
    },
    {
        {&sensors_items[index_temperature2].product.connected,        	NULL,						"Connected",		&units[none], 0},
        {&sensors_items[index_temperature2].product.name,				NULL,						"ProductName",		&units[none], 0},
        {&sensors_items[index_temperature2].product.id,               	NULL,						"ProductId",		&units[none], 0},
        {&sensors_items[index_temperature2].product.firmwareVersion,	NULL,						"FirmwareVersion",	&units[none], 0},
        {&sensors_items[index_temperature2].product.instance,			NULL,						"DeviceInstance",	&units[none], 0},
        {&sensors_items[index_temperature2].item[0],	&analog_sensor[index_temperature2].var[0],	"Temperature",		&units[none], 5},
        {&sensors_items[index_temperature2].item[1],	&analog_sensor[index_temperature2].var[1],	"dummy",			&units[none], 5},
        {&sensors_items[index_temperature2].item[2],	&analog_sensor[index_temperature2].var[2],	"scale",			&units[none], 5, scaleChange},
        {&sensors_items[index_temperature2].item[3],	&analog_sensor[index_temperature2].var[3],	"offset",			&units[none], 5, offsetChange},
        {&sensors_items[index_temperature2].item[4],	&analog_sensor[index_temperature2].var[4],	"FilterStrength",	&units[none], 5, filterStrengthChange}
    },
    {
        {&sensors_items[index_vbat].product.connected,        			NULL,						"Connected",		&units[none], 0},
        {&sensors_items[index_vbat].product.name,						NULL,                       "ProductName",		&units[none], 0},
        {&sensors_items[index_vbat].product.id,                         NULL,						"ProductId",		&units[none], 0},
        {&sensors_items[index_vbat].product.firmwareVersion,			NULL,                       "FirmwareVersion",	&units[none], 0},
        {&sensors_items[index_vbat].product.instance,					NULL,                       "DeviceInstance",	&units[none], 0},
        {&sensors_items[index_vbat].item[0],			&analog_sensor[index_vbat].var[0],          "vbat",				&units[none], 5},
        {&sensors_items[index_vbat].item[1],			&analog_sensor[index_vbat].var[1],          "dummy",			&units[none], 5},
        {&sensors_items[index_vbat].item[2],			&analog_sensor[index_vbat].var[2],			"scale",			&units[none], 5, scaleChange},
        {&sensors_items[index_vbat].item[3],			&analog_sensor[index_vbat].var[3],			"offset",			&units[none], 5, offsetChange},
        {&sensors_items[index_vbat].item[4],			&analog_sensor[index_vbat].var[4],			"FilterStrength",	&units[none], 5, filterStrengthChange}
    },
};

veBool capacityChange(struct VeItem *item, void *ctx, VeVariant *variant)
{
    analog_sensor_t * p_analog_sensor = (analog_sensor_t *)ctx;

    veItemOwnerSet(item, variant);
    veItemOwnerSet(getConsumerRoot(), variant);

    VeItem *settingsItem = veItemGetOrCreateUid(getConsumerRoot(), p_analog_sensor->dbus_info[capacity].path);
    veItemSet(settingsItem, variant);

    p_analog_sensor->var[NUM_OF_SENSOR_OUTPUTS+capacity].value.Float = variant->value.Float;
    return veTrue;
}

veBool fluidTypeChange(struct VeItem *item, void *ctx, VeVariant *variant)
{
    analog_sensor_t * p_analog_sensor = (analog_sensor_t *)ctx;

    veItemOwnerSet(item, variant);
    veItemOwnerSet(getConsumerRoot(), variant);

    VeItem *settingsItem = veItemGetOrCreateUid(getConsumerRoot(), p_analog_sensor->dbus_info[fluidType].path);
    veItemSet(settingsItem, variant);

    p_analog_sensor->var[NUM_OF_SENSOR_OUTPUTS+fluidType].value.UN32 = variant->value.UN32;
    return veTrue;
}

veBool standardChange(struct VeItem *item, void *ctx, VeVariant *variant)
{
    analog_sensor_t * p_analog_sensor = (analog_sensor_t *)ctx;

    veItemOwnerSet(item, variant);
    veItemOwnerSet(getConsumerRoot(), variant);

    VeItem *settingsItem = veItemGetOrCreateUid(getConsumerRoot(), p_analog_sensor->dbus_info[standard].path);
    veItemSet(settingsItem, variant);

    p_analog_sensor->var[NUM_OF_SENSOR_OUTPUTS+standard].value.Float = variant->value.Float;
    return veTrue;
}

veBool scaleChange(struct VeItem *item, void *ctx, VeVariant *variant)
{
    analog_sensor_t * p_analog_sensor = (analog_sensor_t *)ctx;

    veItemOwnerSet(item, variant);
    veItemOwnerSet(getConsumerRoot(), variant);

    VeItem *settingsItem = veItemGetOrCreateUid(getConsumerRoot(), p_analog_sensor->dbus_info[scale].path);
    veItemSet(settingsItem, variant);

    p_analog_sensor->var[NUM_OF_SENSOR_OUTPUTS+scale].value.Float = variant->value.Float;
    return veTrue;
}

veBool offsetChange(struct VeItem *item, void *ctx, VeVariant *variant)
{
    analog_sensor_t * p_analog_sensor = (analog_sensor_t *)ctx;

    veItemOwnerSet(item, variant);
    veItemOwnerSet(getConsumerRoot(), variant);

    VeItem *settingsItem = veItemGetOrCreateUid(getConsumerRoot(), p_analog_sensor->dbus_info[offset].path);
    veItemSet(settingsItem, variant);

    p_analog_sensor->var[NUM_OF_SENSOR_OUTPUTS+offset].value.Float = variant->value.Float;
    return veTrue;
}

veBool filterStrengthChange(struct VeItem *item, void *ctx, VeVariant *variant)
{
    analog_sensor_t * p_analog_sensor = (analog_sensor_t *)ctx;

    veItemOwnerSet(item, variant);
    veItemOwnerSet(getConsumerRoot(), variant);

    VeItem *settingsItem = veItemGetOrCreateUid(getConsumerRoot(), p_analog_sensor->dbus_info[filterStrength].path);
    veItemSet(settingsItem, variant);

    p_analog_sensor->var[NUM_OF_SENSOR_OUTPUTS+filterStrength].value.Float = variant->value.Float;
    return veTrue;
}

void sensor_init(VeItem *root, analog_sensors_index_t sensor_index)
{
    for (int i = 0; i < SENSORS_INFO_ARRAY_SIZE; i++)
    {
        veItemAddChildByUid(root, sensors_info[sensor_index][i].id, sensors_info[sensor_index][i].item);
        veItemSetFmt(sensors_info[sensor_index][i].item, veVariantFmt, sensors_info[sensor_index][i].fmt);
        veItemSetTimeout(sensors_info[sensor_index][i].item, sensors_info[sensor_index][i].timeout);
        if (sensors_info[sensor_index][i].setValueCallback)
        {
            veItemSetSetter(sensors_info[sensor_index][i].item, sensors_info[sensor_index][i].setValueCallback, (void *)&analog_sensor[sensor_index]);
        }
        analog_sensor[sensor_index].interface.sig_cond.filter_iir_lpf.FF = 1000;
    }

}

static void updateValues(void)
{
    for(analog_sensors_index_t sensor_index = 0; sensor_index < num_of_analog_sensors; sensor_index++)
    {
        for(un8 i = 0; i < SENSORS_INFO_ARRAY_SIZE; i++)
        {
            if (sensors_info[sensor_index][i].local && veVariantIsValid(sensors_info[sensor_index][i].local))
                veItemOwnerSet(sensors_info[sensor_index][i].item, sensors_info[sensor_index][i].local);
        }
    }
}

void sensors_handle(void)
{
    analog_sensors_index_t analog_sensors_index;

    for(analog_sensors_index = 0; analog_sensors_index < num_of_analog_sensors; analog_sensors_index++)
    {
        // reading all the analog inputs adc values
        if(!adc_read(&analog_sensor[analog_sensors_index].interface.adc_sample, analog_sensor[analog_sensors_index].interface.adc_pin) )
        {
            // validate the sample
            analog_sensor[analog_sensors_index].valid = veTrue;
        }
    }

    for(analog_sensors_index = 0; analog_sensors_index < num_of_analog_sensors; analog_sensors_index++)
    {
        // reading all the analog inputs adc values
        if(analog_sensor[analog_sensors_index].valid == veTrue)
        {
            // filter the input ADC sample and stor it in adc var

            analog_sensor[analog_sensors_index].interface.adc_sample =
            (un32) adc_filter(
                        (float) (analog_sensor[analog_sensors_index].interface.adc_sample),
                        &analog_sensor[analog_sensors_index].interface.sig_cond.filter_iir_lpf.adc_mem,
                        analog_sensor[analog_sensors_index].interface.sig_cond.filter_iir_lpf.fc,
                        10, analog_sensor[analog_sensors_index].interface.sig_cond.filter_iir_lpf.FF);

            switch(analog_sensor[analog_sensors_index].sensor_type)
            {
                case tank_level_t:
                {
                    if(analog_sensor[analog_sensors_index].interface.adc_sample > 3600)
                    {
                        veVariantStr(&analog_sensor[analog_sensors_index].var[0], "O.C.");
                    }
                    else if(analog_sensor[analog_sensors_index].interface.adc_sample < 15)
                    {
                        veVariantStr(&analog_sensor[analog_sensors_index].var[0], "S.C.");
                    }
                    else
                    {
                        float R2 = adc_potDiv_calc(analog_sensor[analog_sensors_index].interface.adc_sample, &sensor_tankLevel_pd, calc_type_R2, 100);
                        if(R2>0)
                        {
                            float level;
                            un8 Std = (un8)analog_sensor[analog_sensors_index].var[NUM_OF_SENSOR_OUTPUTS + standard].value.UN32;
                            if(Std)
                            {
                                level = (R2 / USA_MAX_TANK_LEVEL_RESISTANCE);
                            }
                            else
                            {
                                level = (R2 / EUR_MAX_TANK_LEVEL_RESISTANCE);
                            }
                            level += analog_sensor[analog_sensors_index].interface.sig_cond.sig_correct.offset;
                            if(level > 1)
                            {
                                level = 1;
                                // Fault sensor
                            }
                            veVariantUn32(&analog_sensor[analog_sensors_index].var[0], (un32)(100*level));
                            veVariantFloat(&analog_sensor[analog_sensors_index].var[1],
                                    level*analog_sensor[analog_sensors_index].dbus_info[0].value->variant.value.Float);
                            veVariantFloat(&analog_sensor[analog_sensors_index].var[NUM_OF_SENSOR_OUTPUTS + capacity],
                                    analog_sensor[analog_sensors_index].dbus_info[capacity].value->variant.value.Float);
                            veVariantUn32(&analog_sensor[analog_sensors_index].var[NUM_OF_SENSOR_OUTPUTS + fluidType],
                                    (un32)analog_sensor[analog_sensors_index].dbus_info[fluidType].value->variant.value.Float);
                            veVariantUn32(&analog_sensor[analog_sensors_index].var[NUM_OF_SENSOR_OUTPUTS + standard],
                                    (un32)analog_sensor[analog_sensors_index].dbus_info[standard].value->variant.value.Float);
                        }
                        else
                        {
                            veVariantStr(&analog_sensor[analog_sensors_index].var[0], "err");
                        }
                    }
                    break;
                }
                case vin_std_0_10_t:
                {
                    un32 divider_supply =
                            adc_potDiv_calc(analog_sensor[analog_sensors_index].interface.adc_sample, &sensor_vin_std_0_10_pd, calc_type_Vin, 1);
                    // sensor connectivity check
                    if(divider_supply < 10)
                    {
                        veVariantStr(&analog_sensor[analog_sensors_index].var[0], "S.C.");
                    }
                    else if(divider_supply > ADC_15VOLTS)
                    {
                        veVariantStr(&analog_sensor[analog_sensors_index].var[0], "0.C.");
                    }
                    // Value ok
                    else
                    {
                        float voltage = adc_sample2volts(divider_supply);
                        voltage *= analog_sensor[analog_sensors_index].interface.sig_cond.sig_correct.scale;
                        voltage += analog_sensor[analog_sensors_index].interface.sig_cond.sig_correct.offset;
                        veVariantFloat(&analog_sensor[analog_sensors_index].var[0], voltage);
                    }
                    break;
                }
                case temperature_t:
                {
                    // sensor connectivity check
                    if(analog_sensor[analog_sensors_index].interface.adc_sample > 2*TEMP_SENS_MAX_ADCIN)
                    {
                        veVariantStr(&analog_sensor[analog_sensors_index].var[0], "O.C.");
                    }
                    else if(analog_sensor[analog_sensors_index].interface.adc_sample > TEMP_SENS_MAX_ADCIN)
                    {
                        veVariantStr(&analog_sensor[analog_sensors_index].var[0], "HIGH TEMP.");
                    }
                    else if( analog_sensor[analog_sensors_index].interface.adc_sample < (TEMP_SENS_MIN_ADCIN/4) )
                    {
                        veVariantStr(&analog_sensor[analog_sensors_index].var[0], "S.C.");
                    }
                    else if(analog_sensor[analog_sensors_index].interface.adc_sample < TEMP_SENS_MIN_ADCIN)
                    {
                        veVariantStr(&analog_sensor[analog_sensors_index].var[0], "LOW TEMP.");
                    }
                    // Value ok
                    else
                    {
                        un32 divider_supply = adc_potDiv_calc(analog_sensor[analog_sensors_index].interface.adc_sample, &sensor_temperature_pd, calc_type_Vin, 1);
                        float tempC = ( 100 * adc_sample2volts(divider_supply) ) - 273;
                        tempC *= analog_sensor[analog_sensors_index].interface.sig_cond.sig_correct.scale;
                        tempC += analog_sensor[analog_sensors_index].interface.sig_cond.sig_correct.offset;
                        veVariantUn32(&analog_sensor[analog_sensors_index].var[0], (un32)tempC);
                    }
                    break;
                }
                case vbat_t:
                {
                    un32 divider_supply =
                            adc_potDiv_calc(analog_sensor[analog_sensors_index].interface.adc_sample, &sensor_vbat_pd, calc_type_Vin, 1);
                    if(divider_supply < 10)
                    {
                        veVariantStr(&analog_sensor[analog_sensors_index].var[0], "S.C.");
                    }
                    else if(divider_supply > ADC_70VOLTS)
                    {
                        veVariantStr(&analog_sensor[analog_sensors_index].var[0], "0.C.");
                    }
                    // Value ok
                    else
                    {
                        float voltage = adc_sample2volts(divider_supply);
                        voltage *= analog_sensor[analog_sensors_index].interface.sig_cond.sig_correct.scale;
                        voltage += analog_sensor[analog_sensors_index].interface.sig_cond.sig_correct.offset;
                        veVariantFloat(&analog_sensor[analog_sensors_index].var[0], voltage);
                    }
                    break;
                }
                default:
                {
                    break;
                }
            }
            analog_sensor[analog_sensors_index].valid = veFalse;
        }
    }
    updateValues();
}


void sensors_dbusInit(analog_sensors_index_t sensor_index)
{
    VeVariant variant;
    static veBool flags[num_of_analog_sensors];

    timeout = CONNECTION_TIMEOUT;

    if (flags[sensor_index] & F_CONNECTED)
        return;

    flags[sensor_index] |= F_CONNECTED;

    static un8 instance = 0;

    veItemOwnerSet(&sensors_items[sensor_index].product.connected, veVariantUn32(&variant, veTrue));
    veItemOwnerSet(&sensors_items[sensor_index].product.instance, veVariantUn8(&variant, instance++));
    veItemOwnerSet(&sensors_items[sensor_index].product.firmwareVersion, veVariantStr(&variant, version));
    veItemOwnerSet(&sensors_items[sensor_index].product.name, veVariantStr(&variant, analog_sensor[sensor_index].interface.dbus.productName));
    veItemOwnerSet(&sensors_items[sensor_index].product.id, veVariantStr(&variant, "TLS001"));

    values_dbus_service_addSettings(&analog_sensor[sensor_index]);
    sensors_dbusConnect(&analog_sensor[sensor_index], sensor_index);
}

