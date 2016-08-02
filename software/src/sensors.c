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

#endif

#define F_CONNECTED					1

#define CONNECTION_TIMEOUT			5*20	/* 50ms */

static un16 timeout;
const char version[] = VERSION_STR;
// Callbacks to be called when the paramters are changing
veBool capacityChange(struct VeItem *item, void *ctx, VeVariant *variant);
veBool fluidTypeChange(struct VeItem *item, void *ctx, VeVariant *variant);
veBool standardChange(struct VeItem *item, void *ctx, VeVariant *variant);

veBool TempTypeChange(struct VeItem *item, void *ctx, VeVariant *variant);
veBool scaleChange(struct VeItem *item, void *ctx, VeVariant *variant);
veBool offsetChange(struct VeItem *item, void *ctx, VeVariant *variant);

//static varibles;
analog_sensor_t analog_sensor[num_of_analog_sensors] = SENSORS_CONSTANT_DATA;

// potential divider for the tank level sender
const potential_divider_t sensor_tankLevel_pd = {TANK_LEVEL_SENSOR_DIVIDER, (POTENTIAL_DIV_MAX_SAMPLE - 1)};
const potential_divider_t sensor_temperature_pd = {TEMP_SENS_VOLT_DIVID_R1, TEMP_SENS_VOLT_DIVID_R2};

// instantiate a container structure for the interface with dbus APIś interface.
static VeVariantUnitFmt units = {9, ""};
static ItemInfo const sensors_info[num_of_analog_sensors][SENSORS_INFO_ARRAY_SIZE] = SENSOR_ITEM_CONTAINER;

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
            adc_filter(
                        (float) (analog_sensor[analog_sensors_index].interface.adc_sample),
                        &analog_sensor[analog_sensors_index].interface.sig_cond.filter_iir_lpf.adc_mem,
                        analog_sensor[analog_sensors_index].interface.sig_cond.filter_iir_lpf.fc,
                        10, analog_sensor[analog_sensors_index].interface.sig_cond.filter_iir_lpf.FF);

            switch(analog_sensor[analog_sensors_index].sensor_type)
            {
                case tank_level_t:
                {
                    un8 Std = (un8)analog_sensor[analog_sensors_index].variant.tank_level.standard.value.UN32;

                    if(analog_sensor[analog_sensors_index].interface.adc_sample > ADC_1p3VOLTS)
                    {
                        veVariantStr(&analog_sensor[analog_sensors_index].variant.tank_level.level, "O.C.");
                    }
                    // this condition applies only for the US standard
                    else if(Std && (analog_sensor[analog_sensors_index].interface.adc_sample < ADC_0p15VOLTS))
                    {
                        veVariantStr(&analog_sensor[analog_sensors_index].variant.tank_level.level, "S.C.");
                    }
                    else
                    {
                        float R2 = adc_potDiv_calc(analog_sensor[analog_sensors_index].interface.adc_sample, &sensor_tankLevel_pd, calc_type_R2, 100);
                        if(R2>0)
                        {
                            float level;

                            if(Std)
                            {
                                level = (R2 - USA_MIN_TANK_LEVEL_RESISTANCE) / (USA_MAX_TANK_LEVEL_RESISTANCE - USA_MIN_TANK_LEVEL_RESISTANCE);
                                if(level < 0)
                                {
                                    level = 0;
                                }
                                level = 1-level;
                            }
                            else
                            {
                                level = (R2 / EUR_MAX_TANK_LEVEL_RESISTANCE);
                            }
                            if(level > 1)
                            {
                                level = 1;
                                // Fault sensor
                            }
                            veVariantUn32(&analog_sensor[analog_sensors_index].variant.tank_level.level, (un32)(100*level));
                            veVariantFloat(&analog_sensor[analog_sensors_index].variant.tank_level.remaining,
                                    level*analog_sensor[analog_sensors_index].dbus_info[capacity].value->variant.value.Float);

                            veVariantFloat(&analog_sensor[analog_sensors_index].variant.tank_level.capacity,
                                    analog_sensor[analog_sensors_index].dbus_info[capacity].value->variant.value.Float);

                            veVariantUn32(&analog_sensor[analog_sensors_index].variant.tank_level.fluidType,
                                    (un32)analog_sensor[analog_sensors_index].dbus_info[fluidType].value->variant.value.Float);

                            veVariantUn32(&analog_sensor[analog_sensors_index].variant.tank_level.standard,
                                    (un32)analog_sensor[analog_sensors_index].dbus_info[standard].value->variant.value.Float);
                        }
                        else
                        {
                            veVariantStr(&analog_sensor[analog_sensors_index].variant.tank_level.level, "err");
                        }
                    }
                    break;
                }
                case temperature_t:
                {
                    // sensor connectivity check
                    if(analog_sensor[analog_sensors_index].interface.adc_sample > TEMP_SENS_MAX_ADCIN)
                    {
                        veVariantStr(&analog_sensor[analog_sensors_index].variant.temperature.temperature, "O.C.");
                    }
                    else if( analog_sensor[analog_sensors_index].interface.adc_sample < (TEMP_SENS_MIN_ADCIN/4) )
                    {
                        veVariantStr(&analog_sensor[analog_sensors_index].variant.temperature.temperature, "S.C.");
                    }
                    // Value ok
                    else
                    {
                        un32 divider_supply = adc_potDiv_calc(analog_sensor[analog_sensors_index].interface.adc_sample, &sensor_temperature_pd, calc_type_Vin, 1);
                        float tempC = ( 100 * adc_sample2volts(divider_supply) ) - 273;
                        tempC *= (analog_sensor[analog_sensors_index].variant.temperature.scale.value.Float);
                        tempC += (analog_sensor[analog_sensors_index].variant.temperature.offset.value.SN32);
                        veVariantSn32(&analog_sensor[analog_sensors_index].variant.temperature.temperature, (sn32)tempC);
                    }

                    veVariantFloat(&analog_sensor[analog_sensors_index].variant.temperature.scale,
                            analog_sensor[analog_sensors_index].dbus_info[scale].value->variant.value.Float);

                    veVariantSn32(&analog_sensor[analog_sensors_index].variant.temperature.offset,
                            (sn32)analog_sensor[analog_sensors_index].dbus_info[offset].value->variant.value.Float);

                    veVariantSn32(&analog_sensor[analog_sensors_index].variant.temperature.temperatureType,
                            (sn32)analog_sensor[analog_sensors_index].dbus_info[TempType].value->variant.value.Float);

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

    veItemOwnerSet(&analog_sensor[sensor_index].items.product.connected, veVariantUn32(&variant, veTrue));
    veItemOwnerSet(&analog_sensor[sensor_index].items.product.instance, veVariantUn8(&variant, instance++));
    veItemOwnerSet(&analog_sensor[sensor_index].items.product.firmwareVersion, veVariantStr(&variant, version));
    veItemOwnerSet(&analog_sensor[sensor_index].items.product.name, veVariantStr(&variant, analog_sensor[sensor_index].interface.dbus.productName));
    veItemOwnerSet(&analog_sensor[sensor_index].items.product.id, veVariantStr(&variant, "41312"));

    values_dbus_service_addSettings(&analog_sensor[sensor_index]);
    sensors_dbusConnect(&analog_sensor[sensor_index], sensor_index);
}

// Callback when the capacity is changing
veBool capacityChange(struct VeItem *item, void *ctx, VeVariant *variant)
{
    analog_sensor_t * p_analog_sensor = (analog_sensor_t *)ctx;

    veItemOwnerSet(item, variant);
    veItemOwnerSet(getConsumerRoot(), variant);

    VeItem *settingsItem = veItemGetOrCreateUid(getConsumerRoot(), p_analog_sensor->dbus_info[capacity].path);
    veItemSet(settingsItem, variant);

    p_analog_sensor->variant.tank_level.capacity.value.Float = variant->value.Float;
    return veTrue;
}

// Callback when the fluid type is changing
veBool fluidTypeChange(struct VeItem *item, void *ctx, VeVariant *variant)
{
    analog_sensor_t * p_analog_sensor = (analog_sensor_t *)ctx;

    veItemOwnerSet(item, variant);
    veItemOwnerSet(getConsumerRoot(), variant);

    VeItem *settingsItem = veItemGetOrCreateUid(getConsumerRoot(), p_analog_sensor->dbus_info[fluidType].path);
    veItemSet(settingsItem, variant);

    p_analog_sensor->variant.tank_level.capacity.value.UN32 = variant->value.UN32;
    return veTrue;
}

// Callback when the standard is changing
veBool standardChange(struct VeItem *item, void *ctx, VeVariant *variant)
{
    analog_sensor_t * p_analog_sensor = (analog_sensor_t *)ctx;

    veItemOwnerSet(item, variant);
    veItemOwnerSet(getConsumerRoot(), variant);

    VeItem *settingsItem = veItemGetOrCreateUid(getConsumerRoot(), p_analog_sensor->dbus_info[standard].path);
    veItemSet(settingsItem, variant);

    p_analog_sensor->variant.tank_level.standard.value.Float = variant->value.Float;
    return veTrue;
}


// Callback when the scale is changing
veBool scaleChange(struct VeItem *item, void *ctx, VeVariant *variant)
{
    analog_sensor_t * p_analog_sensor = (analog_sensor_t *)ctx;

    veItemOwnerSet(item, variant);
    veItemOwnerSet(getConsumerRoot(), variant);

    VeItem *settingsItem = veItemGetOrCreateUid(getConsumerRoot(), p_analog_sensor->dbus_info[scale].path);
    veItemSet(settingsItem, variant);

    p_analog_sensor->variant.temperature.scale.value.Float = variant->value.Float;
    return veTrue;
}

// Callback when the offset is changing
veBool offsetChange(struct VeItem *item, void *ctx, VeVariant *variant)
{
    analog_sensor_t * p_analog_sensor = (analog_sensor_t *)ctx;

    veItemOwnerSet(item, variant);
    veItemOwnerSet(getConsumerRoot(), variant);

    VeItem *settingsItem = veItemGetOrCreateUid(getConsumerRoot(), p_analog_sensor->dbus_info[offset].path);
    veItemSet(settingsItem, variant);

    p_analog_sensor->variant.temperature.offset.value.SN32 = variant->value.SN32;

    return veTrue;
}

//
veBool TempTypeChange(struct VeItem *item, void *ctx, VeVariant *variant)
{
    analog_sensor_t * p_analog_sensor = (analog_sensor_t *)ctx;

    veItemOwnerSet(item, variant);
    veItemOwnerSet(getConsumerRoot(), variant);

    VeItem *settingsItem = veItemGetOrCreateUid(getConsumerRoot(), p_analog_sensor->dbus_info[TempType].path);
    veItemSet(settingsItem, variant);

    p_analog_sensor->variant.temperature.temperatureType.value.SN32 = variant->value.SN32;
    return veTrue;
}
