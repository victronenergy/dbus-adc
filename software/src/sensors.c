#include <velib/base/base.h>
#include <velib/base/ve_string.h>
#include <velib/types/ve_item.h>
#include <velib/utils/ve_logger.h>
#include <velib/types/ve_dbus_item.h>
#include <velib/vecan/products.h>


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

// Local function prototypes
/**
 * @brief sensors_data_process
 * @param analog_sensors_index
 * @return
 */
veBool sensors_data_process(analog_sensors_index_t analog_sensors_index);
/**
 * @brief sensors_tankType_data_process
 * @param analog_sensors_index
 * @return
 */
veBool sensors_tankType_data_process(analog_sensors_index_t analog_sensors_index);
/**
 * @brief sensors_temperatureType_data_process
 * @param analog_sensors_index
 * @return
 */
veBool sensors_temperatureType_data_process(analog_sensors_index_t analog_sensors_index);

// Callbacks to be called when the paramters are changing
/**
 * @brief xxxChange
 * @param item
 * @param ctx
 * @param variant
 * @return
 */
veBool analogPinFuncChange(struct VeItem *item, void *ctx, VeVariant *variant);

veBool capacityChange(struct VeItem *item, void *ctx, VeVariant *variant);
veBool fluidTypeChange(struct VeItem *item, void *ctx, VeVariant *variant);
veBool standardChange(struct VeItem *item, void *ctx, VeVariant *variant);

veBool TempTypeChange(struct VeItem *item, void *ctx, VeVariant *variant);
veBool scaleChange(struct VeItem *item, void *ctx, VeVariant *variant);
veBool offsetChange(struct VeItem *item, void *ctx, VeVariant *variant);

//static varibles
/**
 * @brief analog_sensor - array of analog sensor structures
 */
analog_sensor_t analog_sensor[num_of_analog_sensors] = SENSORS_CONSTANT_DATA;

// potential divider for the tank level sender
const potential_divider_t sensor_tankLevel_pd = {TANK_LEVEL_SENSOR_DIVIDER, (POTENTIAL_DIV_MAX_SAMPLE - 1)};
const potential_divider_t sensor_temperature_pd = {TEMP_SENS_VOLT_DIVID_R1, TEMP_SENS_VOLT_DIVID_R2};

// instantiate a container structure for the interface with dbus APIś interface.
static VeVariantUnitFmt units = {9, ""};
// a pointers container for interfacing the sensor structure to the various dbus services
static ItemInfo const sensors_info[num_of_analog_sensors][SENSORS_INFO_ARRAY_SIZE] = SENSOR_ITEM_CONTAINER;

/**
 * @brief sensor_init - hook the sensor items to their dbus services
 * @param root - the service item root
 * @param sensor_index - the sensor index array number
 */
void sensor_init(VeItem *root, analog_sensors_index_t sensor_index)
{
    for (int i = 0; i < SENSORS_INFO_ARRAY_SIZE; i++)
    {
        if(sensors_info[sensor_index][i].item != NULL)
        {
            veItemAddChildByUid(root, sensors_info[sensor_index][i].id, sensors_info[sensor_index][i].item);
            veItemSetFmt(sensors_info[sensor_index][i].item, veVariantFmt, sensors_info[sensor_index][i].fmt);
            veItemSetTimeout(sensors_info[sensor_index][i].item, sensors_info[sensor_index][i].timeout);
            // Register the change items value callbacks.
            if (sensors_info[sensor_index][i].setValueCallback)
            {
                veItemSetSetter(sensors_info[sensor_index][i].item, sensors_info[sensor_index][i].setValueCallback, (void *)&analog_sensor[sensor_index]);
            }
        }
    }
}

/**
 * @brief updateValues - updates the dbus item values
 */
static void updateValues(void)
{
    for(analog_sensors_index_t sensor_index = 0; sensor_index < num_of_analog_sensors; sensor_index++)
    {
        // update only variables values
        for(snesor_items_container_items_t i = 0; i < num_of_container_items; i++)
        {
            if (sensors_info[sensor_index][i].local && veVariantIsValid(sensors_info[sensor_index][i].local))
            {
                veItemOwnerSet(sensors_info[sensor_index][i].item, sensors_info[sensor_index][i].local);
            }
        }
    }
}

/**
 * @brief sensors_handle - handles the sensors
 */
void sensors_handle(void)
{
    analog_sensors_index_t analog_sensors_index;
    // first read fast all the analog inputs and mark which read is valid
    // We reading always the same number of analog inputs to try to keep the timming of the system constant.
    for(analog_sensors_index = 0; analog_sensors_index < num_of_analog_sensors; analog_sensors_index++)
    {
        // reading all the analog inputs adc values
        if(!adc_read(&analog_sensor[analog_sensors_index].interface.adc_sample, analog_sensor[analog_sensors_index].interface.adc_pin) )
        {
            // validate the sample
            analog_sensor[analog_sensors_index].valid = veTrue;
        }
    }

    // Now handle the adc read to update the sensor
    for(analog_sensors_index = 0; analog_sensors_index < num_of_analog_sensors; analog_sensors_index++)
    {
        // proceed if the adc reading is valid
        if(analog_sensor[analog_sensors_index].valid == veTrue)
        {
            // filter the input ADC sample and stor it in adc var
            analog_sensor[analog_sensors_index].interface.adc_sample =
            adc_filter(
                        (float) (analog_sensor[analog_sensors_index].interface.adc_sample),
                        &analog_sensor[analog_sensors_index].interface.sig_cond.filter_iir_lpf.adc_mem,
                        analog_sensor[analog_sensors_index].interface.sig_cond.filter_iir_lpf.fc,
                        10, analog_sensor[analog_sensors_index].interface.sig_cond.filter_iir_lpf.FF);
            // reset the adc valid reading flag for next sampling cycle
            analog_sensor[analog_sensors_index].valid = veFalse;
            // check if the sensor function-if it needed at all?
            un32 sensor_analogpinFunc = (un32)analog_sensor[analog_sensors_index].dbus_info[analogpinFunc].value->variant.value.Float;
            switch(sensor_analogpinFunc)
            {
                case default_function:
                {
                    // check if dbus is disconnected and connect it
                    if(!analog_sensor[analog_sensors_index].interface.dbus.connected)
                    {
                        sensors_dbusConnect(&analog_sensor[analog_sensors_index], analog_sensors_index);
                    }
                    // need to proces the data
                    sensors_data_process(analog_sensors_index);
                    break;
                }
                case no_function:
                default:
                {
                    // check id dbus is connected and disconnect it
                    if(analog_sensor[analog_sensors_index].interface.dbus.connected)
                    {
                        sensors_dbusDisconnect(&analog_sensor[analog_sensors_index], analog_sensors_index);
                    }
                    break;
                }
            }
        }
        else
        {
            // adc reading error
        }
    }
    // call to update the dbus sservice with the new item values
    updateValues();
}

/**
 * @brief sensors_data_process - direct to the data processing algorithm per sensor type
 * @param analog_sensors_index - the sensor index array number
 * @return Boolean status veTrue-success, veFalse-fail
 */
veBool sensors_data_process(analog_sensors_index_t analog_sensors_index)
{
    // check the type of sensor before starting
    switch(analog_sensor[analog_sensors_index].sensor_type)
    {
        case tank_level_t:
        {
            sensors_tankType_data_process(analog_sensors_index);
            break;
        }
        case temperature_t:
        {
            sensors_temperatureType_data_process(analog_sensors_index);
            break;
        }
        default:
        {
            break;
        }
    }
    return veTrue;
}

/**
 * @brief sensors_tankType_data_process - proces the tank level sensor adc data (need to switch the oin function as when functions will be add)
 * @param analog_sensors_index - the sensor index array number
 * @return Boolean status veTrue-success, veFalse-fail
 */
veBool sensors_tankType_data_process(analog_sensors_index_t analog_sensors_index)
{
        // process the data of the analog input with respect to its function
        float level;
        un8 Std = (un8)analog_sensor[analog_sensors_index].variant.tank_level.standard.value.UN32;

        if(analog_sensor[analog_sensors_index].interface.adc_sample > ADC_1p4VOLTS)
        {
            // Sensor status: error- not connected
            veVariantUn32(&analog_sensor[analog_sensors_index].variant.tank_level.status, (un32)disconnected);
        }
        // this condition applies only for the US standard
        else if(Std && (analog_sensor[analog_sensors_index].interface.adc_sample < ADC_0p15VOLTS))
        {
            // Sensor status: error- short circuited
            veVariantUn32(&analog_sensor[analog_sensors_index].variant.tank_level.status, (un32)short_circuited);
        }
        else
        {
            // calculate the resistance of the tank level sensor from the adc pin sample
            float R2 = adc_potDiv_calc(analog_sensor[analog_sensors_index].interface.adc_sample, &sensor_tankLevel_pd, calc_type_R2, 100);
            // check if the integrity of the resistance
            if(R2>0)
            {   // clculate the tank level
                if(Std == european_std)
                { // tank level calculation in the case it is a European standard sensor
                    level = (R2 - USA_MIN_TANK_LEVEL_RESISTANCE) / (USA_MAX_TANK_LEVEL_RESISTANCE - USA_MIN_TANK_LEVEL_RESISTANCE);
                    if(level < 0)
                    {
                        level = 0;
                    }
                    level = 1-level;
                }
                else
                {   // tank level calculation in the case it is a American standard sensor
                    level = (R2 / EUR_MAX_TANK_LEVEL_RESISTANCE);
                }
                // is level biger than 100% ?
                if(level > 1)
                {
                    // saturate the level to 100%
                    level = 1;
                }
                // Sensor status: O.K.
                veVariantUn32(&analog_sensor[analog_sensors_index].variant.tank_level.status, (un32)ok);
            }
            else
            {
                // Sensor status: error- unknown value
                veVariantUn32(&analog_sensor[analog_sensors_index].variant.tank_level.status, (un32)unknown_value);
            }
        }
        // measure is ok and R2 resistance was correctlly calculated
        veVariantUn32(&analog_sensor[analog_sensors_index].variant.tank_level.analogpinFunc,
                (un32)analog_sensor[analog_sensors_index].dbus_info[analogpinFunc].value->variant.value.Float);
        // if status = o.k. publish valid value otherwise publish invalid value
        if(analog_sensor[analog_sensors_index].variant.tank_level.status.value.UN8 == (un8)ok)
        {
            veVariantUn32(&analog_sensor[analog_sensors_index].variant.tank_level.level, (un32)(100*level));
            veVariantFloat(&analog_sensor[analog_sensors_index].variant.tank_level.remaining,
                    level*analog_sensor[analog_sensors_index].dbus_info[capacity].value->variant.value.Float);
        }
        else
        {
            veVariantInvalidate(&analog_sensor[analog_sensors_index].variant.tank_level.level);
            veVariantInvalidate(&analog_sensor[analog_sensors_index].variant.tank_level.remaining);
            veItemOwnerSet(sensors_info[analog_sensors_index][level_item].item, sensors_info[analog_sensors_index][level_item].local);
            veItemOwnerSet(sensors_info[analog_sensors_index][remaining_item].item, sensors_info[analog_sensors_index][remaining_item].local);
        }

        veVariantFloat(&analog_sensor[analog_sensors_index].variant.tank_level.capacity,
                analog_sensor[analog_sensors_index].dbus_info[capacity].value->variant.value.Float);
        veVariantUn32(&analog_sensor[analog_sensors_index].variant.tank_level.fluidType,
                (un32)analog_sensor[analog_sensors_index].dbus_info[fluidType].value->variant.value.Float);
        veVariantUn32(&analog_sensor[analog_sensors_index].variant.tank_level.standard,
                (un32)analog_sensor[analog_sensors_index].dbus_info[standard].value->variant.value.Float);
    return veTrue;
}

/**
 * @brief sensors_tankType_data_process - proces the temperature sensor adc data (need to switch the oin function as when functions will be add)
 * @param analog_sensors_index - the sensor index array number
 * @return Boolean status veTrue-success, veFalse-fail
 */
veBool sensors_temperatureType_data_process(analog_sensors_index_t analog_sensors_index)
{
    float tempC;

    if(VALUE_BETWEEN(analog_sensor[analog_sensors_index].interface.adc_sample, TEMP_SENS_MIN_ADCIN, TEMP_SENS_MAX_ADCIN))
    {
        // calculate the output of the LM335 temperature sensor from the adc pin sample
        un32 divider_supply = adc_potDiv_calc(analog_sensor[analog_sensors_index].interface.adc_sample, &sensor_temperature_pd, calc_type_Vin, 1);
        // convert from Fahrenheit to Celsius
        tempC = ( 100 * adc_sample2volts(divider_supply) ) - 273;
        // Signal scale correction
        tempC *= (analog_sensor[analog_sensors_index].variant.temperature.scale.value.Float);
        // Signal offset correction
        tempC += (analog_sensor[analog_sensors_index].variant.temperature.offset.value.SN32);
        // update sensor status
        veVariantUn32(&analog_sensor[analog_sensors_index].variant.temperature.status, (un32)ok);
    }
    else if(analog_sensor[analog_sensors_index].interface.adc_sample > TEMP_SENS_MAX_ADCIN)
    {
        // open circuit error
        veVariantUn32(&analog_sensor[analog_sensors_index].variant.temperature.status, (un32)disconnected);
    }
    else if(analog_sensor[analog_sensors_index].interface.adc_sample < TEMP_SENS_S_C_ADCIN )
    {
        // short circuit error
        veVariantUn32(&analog_sensor[analog_sensors_index].variant.temperature.status, (un32)short_circuited);
    }
    else if(VALUE_BETWEEN(analog_sensor[analog_sensors_index].interface.adc_sample, TEMP_SENS_INV_PLRTY_ADCIN_LB, TEMP_SENS_INV_PLRTY_ADCIN_HB))
    {
        // lm335 probably connected in reverse polarity
        veVariantUn32(&analog_sensor[analog_sensors_index].variant.temperature.status, (un32)reverse_polarity);
    }
    else
    {
        // low temperature or unknown error
        veVariantUn32(&analog_sensor[analog_sensors_index].variant.temperature.status, (un32)unknown_value);
    }

    // if status = o.k. publish valid value otherwise publish invalid value
    if(analog_sensor[analog_sensors_index].variant.temperature.status.value.UN8 == (un8)ok)
    {
        veVariantSn32(&analog_sensor[analog_sensors_index].variant.temperature.temperature, (sn32)tempC);
    }
    else
    {
        veVariantInvalidate(&analog_sensor[analog_sensors_index].variant.temperature.temperature);
        veItemOwnerSet(sensors_info[analog_sensors_index][temperature_item].item, sensors_info[analog_sensors_index][temperature_item].local);
    }
    veVariantUn32(&analog_sensor[analog_sensors_index].variant.temperature.analogpinFunc,
            (un32)analog_sensor[analog_sensors_index].dbus_info[analogpinFunc].value->variant.value.Float);
    veVariantFloat(&analog_sensor[analog_sensors_index].variant.temperature.scale,
            analog_sensor[analog_sensors_index].dbus_info[scale].value->variant.value.Float);
    veVariantSn32(&analog_sensor[analog_sensors_index].variant.temperature.offset,
            (sn32)analog_sensor[analog_sensors_index].dbus_info[offset].value->variant.value.Float);
    veVariantSn32(&analog_sensor[analog_sensors_index].variant.temperature.temperatureType,
            (sn32)analog_sensor[analog_sensors_index].dbus_info[TempType].value->variant.value.Float);

    return veTrue;
}

/**
 * @brief sensors_dbusInit - connect sensor items to their dbus services
 * @param sensor_index - the sensor index array number
 */
void sensors_dbusInit(analog_sensors_index_t sensor_index)
{
    VeVariant variant;
    static veBool flags[num_of_analog_sensors];

    timeout = CONNECTION_TIMEOUT;

    if (flags[sensor_index] & F_CONNECTED)
        return;

    flags[sensor_index] |= F_CONNECTED;

    static un8 instance = 20;

    veItemOwnerSet(&analog_sensor[sensor_index].items.product.connected, veVariantUn32(&variant, veTrue));
    veItemOwnerSet(&analog_sensor[sensor_index].items.product.instance, veVariantUn8(&variant, instance++));
    /* Product info */
    if(analog_sensor[sensor_index].sensor_type == tank_level_t)
    {
        veItemOwnerSet(&analog_sensor[sensor_index].items.product.id, veVariantUn16(&variant, VE_PROD_ID_TANK_SENSOR_INPUT));
        veItemOwnerSet(&analog_sensor[sensor_index].items.product.name, veVariantStr(&variant, veProductGetName(VE_PROD_ID_TANK_SENSOR_INPUT)));
    }
    else if(analog_sensor[sensor_index].sensor_type == temperature_t)
    {
        veItemOwnerSet(&analog_sensor[sensor_index].items.product.id, veVariantUn16(&variant, VE_PROD_ID_TEMPERATURE_SENSOR_INPUT));
        veItemOwnerSet(&analog_sensor[sensor_index].items.product.name, veVariantStr(&variant, veProductGetName(VE_PROD_ID_TEMPERATURE_SENSOR_INPUT)));
    }
    else
    {

    }

    values_dbus_service_addSettings(&analog_sensor[sensor_index]);
    if(!analog_sensor[sensor_index].interface.dbus.connected)
    {
        sensors_dbusConnect(&analog_sensor[sensor_index], sensor_index);
    }
}

/**
 * @brief xxxChange - is a callbeck that called when an item was changed
 * @param item - a pointer to the chanched item
 * @param ctx - a preloaded void pointer to some desired variable- in our case, a pointer the the sensor structure array element
 * @param variant - the changed variant in the item
 * @return Boolean status veTrue -success, veFalse -fail
 */

// Callback when the sensor function is changing
veBool analogPinFuncChange(struct VeItem *item, void *ctx, VeVariant *variant)
{
    analog_sensor_t * p_analog_sensor = (analog_sensor_t *)ctx;

    veItemOwnerSet(item, variant);
    veItemOwnerSet(getConsumerRoot(), variant);

    VeItem *settingsItem = veItemGetOrCreateUid(getConsumerRoot(), p_analog_sensor->dbus_info[analogpinFunc].path);
    veItemSet(settingsItem, variant);

    p_analog_sensor->variant.tank_level.analogpinFunc.value.Float = variant->value.Float;
    return veTrue;
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

    p_analog_sensor->variant.tank_level.fluidType.value.UN32 = variant->value.UN32;
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
