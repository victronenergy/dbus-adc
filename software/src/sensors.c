#include <velib/base/base.h>
#include <velib/base/ve_string.h>
#include <velib/types/ve_item.h>
#include <velib/utils/ve_logger.h>
#include <velib/types/ve_dbus_item.h>
#include <velib/vecan/products.h>


#include "values.h"
#include "sensors.h"
#include "adc.h"

#include <string.h>
#ifdef WIN32
#include <stdio.h>
#else
#include <stdlib.h>
#endif

// Callbacks to be called when the parameters are changing
static veBool analogPinFuncChange(struct VeItem *item, void *ctx, VeVariant *variant);

static veBool capacityChange(struct VeItem *item, void *ctx, VeVariant *variant);
static veBool fluidTypeChange(struct VeItem *item, void *ctx, VeVariant *variant);
static veBool standardChange(struct VeItem *item, void *ctx, VeVariant *variant);

static veBool TempTypeChange(struct VeItem *item, void *ctx, VeVariant *variant);
static veBool scaleChange(struct VeItem *item, void *ctx, VeVariant *variant);
static veBool offsetChange(struct VeItem *item, void *ctx, VeVariant *variant);

static size_t statusFormatter(VeVariant *var, void const *ctx, char *buf, size_t len);
static size_t standardItemFormatter(VeVariant *var, void const *ctx, char *buf, size_t len);
static size_t fluidTypeFormatter(VeVariant *var, void const *ctx, char *buf, size_t len);

//static variables
/**
 * @brief analog_sensor - array of analog sensor structures
 */
analog_sensor_t analog_sensor[num_of_analog_sensors] = SENSORS_CONSTANT_DATA;

// potential divider for the tank level sender
const potential_divider_t sensor_tankLevel_pd = {TANK_LEVEL_SENSOR_DIVIDER, (POTENTIAL_DIV_MAX_SAMPLE - 1)};
const potential_divider_t sensor_temperature_pd = {TEMP_SENS_VOLT_DIVID_R1, TEMP_SENS_VOLT_DIVID_R2};

// instantiate a container structure for the interface with dbus API's interface.
static FormatInfo units = {{9, ""}, NULL};
static FormatInfo statusFormat = {{0, ""}, statusFormatter};
static FormatInfo fluidTypeFormat = {{0, ""}, fluidTypeFormatter};
static FormatInfo standardFormat = {{0, ""}, standardItemFormatter};

// a pointers container for interfacing the sensor structure to the various dbus services
static ItemInfo const sensors_info[num_of_analog_sensors][SENSORS_INFO_ARRAY_SIZE] = SENSOR_ITEM_CONTAINER;

/**
 * @brief sensor_init - hook the sensor items to their dbus services
 * @param sensor_index - the sensor index array number
 * @return Pointer to sensor struct
 */
analog_sensor_t *sensor_init(analog_sensors_index_t sensor_index)
{
	analog_sensor_t *sensor = &analog_sensor[sensor_index];

	sensor->info = sensors_info[sensor_index];

	for (int i = 0; i < SENSORS_INFO_ARRAY_SIZE; i++) {
		const ItemInfo *itemInfo = &sensor->info[i];

		if (itemInfo->item != NULL) {
			veItemAddChildByUid(&sensor->root, itemInfo->id, itemInfo->item);

			if (itemInfo->fmt->fun != NULL) {
				veItemSetFmt(itemInfo->item, itemInfo->fmt->fun, NULL);
			} else {
				veItemSetFmt(itemInfo->item, veVariantFmt, &itemInfo->fmt->unit);
			}

			veItemSetTimeout(itemInfo->item, itemInfo->timeout);

			// Register the change items value callbacks.
			if (itemInfo->setValueCallback) {
				veItemSetSetter(itemInfo->item, itemInfo->setValueCallback, (void *)sensor);
			}
		}
	}

	return sensor;
}

/**
 * @brief sensors_tankType_data_process - process the tank level sensor adc data (need to switch the oin function as when functions will be add)
 * @param sensor - pointer to the sensor struct
 * @return Boolean status veTrue - success, veFalse - fail
 */
static veBool sensors_tankType_data_process(analog_sensor_t *sensor)
{
	// process the data of the analog input with respect to its function
	float level;
	un8 Std = (un8)sensor->variant.tank_level.standard.value.UN32;

	if (sensor->interface.adc_sample > ADC_1p4VOLTS) {
		// Sensor status: error - not connected
		veVariantUn32(&sensor->variant.tank_level.status, (un32)disconnected);
	// this condition applies only for the US standard
	} else if (Std && (sensor->interface.adc_sample < ADC_0p15VOLTS)) {
		// Sensor status: error - short circuited
		veVariantUn32(&sensor->variant.tank_level.status, (un32)short_circuited);
	} else {
		// calculate the resistance of the tank level sensor from the adc pin sample
		float R2 = adc_potDiv_calc(sensor->interface.adc_sample, &sensor_tankLevel_pd, calc_type_R2, 100);

		// check the integrity of the resistance
		if (R2>0) { // calculate the tank level
			if (Std == american_std) { // tank level calculation in the case it is an American standard sensor
				level = (R2 - USA_MIN_TANK_LEVEL_RESISTANCE) / (USA_MAX_TANK_LEVEL_RESISTANCE - USA_MIN_TANK_LEVEL_RESISTANCE);
				if (level < 0) {
					level = 0;
				}
				level = 1-level;
			} else { // tank level calculation in the case it is a European standard sensor
				level = (R2 / EUR_MAX_TANK_LEVEL_RESISTANCE);
			}

			// is level bigger than 100% ?
			if (level > 1) {
				// saturate the level to 100%
				level = 1;
			}

			// Sensor status: O.K.
			veVariantUn32(&sensor->variant.tank_level.status, (un32)ok);
		} else {
			// Sensor status: error - unknown value
			veVariantUn32(&sensor->variant.tank_level.status, (un32)unknown_value);
		}
	}

	// measure is ok and R2 resistance was correctly calculated
	veVariantUn32(&sensor->variant.tank_level.analogpinFunc,
			(un32)sensor->dbus_info[analogpinFunc].value->variant.value.Float);

	// if status = o.k. publish valid value otherwise publish invalid value
	if (sensor->variant.tank_level.status.value.UN8 == (un8)ok) {
		veVariantUn32(&sensor->variant.tank_level.level, (un32)(100 * level));
		veVariantFloat(&sensor->variant.tank_level.remaining,
			level * sensor->dbus_info[capacity].value->variant.value.Float);
	} else {
		veVariantInvalidate(&sensor->variant.tank_level.level);
		veVariantInvalidate(&sensor->variant.tank_level.remaining);
		veItemOwnerSet(sensor->info[level_item].item, sensor->info[level_item].local);
		veItemOwnerSet(sensor->info[remaining_item].item, sensor->info[remaining_item].local);
	}

	veVariantFloat(&sensor->variant.tank_level.capacity,
			sensor->dbus_info[capacity].value->variant.value.Float);
	veVariantUn32(&sensor->variant.tank_level.fluidType,
			(un32)sensor->dbus_info[fluidType].value->variant.value.Float);
	veVariantUn32(&sensor->variant.tank_level.standard,
			(un32)sensor->dbus_info[standard].value->variant.value.Float);

	return veTrue;
}

/**
 * @brief sensors_tankType_data_process - process the temperature sensor adc data (need to switch the oin function as when functions will be add)
 * @param sensor - pointer to the sensor struct
 * @return Boolean status veTrue-success, veFalse-fail
 */
static veBool sensors_temperatureType_data_process(analog_sensor_t *sensor)
{
	float tempC;

	if (VALUE_BETWEEN(sensor->interface.adc_sample, TEMP_SENS_MIN_ADCIN, TEMP_SENS_MAX_ADCIN)) {
		// calculate the output of the LM335 temperature sensor from the adc pin sample
		un32 divider_supply = adc_potDiv_calc(sensor->interface.adc_sample, &sensor_temperature_pd, calc_type_Vin, 1);
		// convert from Fahrenheit to Celsius
		tempC = ( 100 * adc_sample2volts(divider_supply) ) - 273;
		// Signal scale correction
		tempC *= (sensor->variant.temperature.scale.value.Float);
		// Signal offset correction
		tempC += (sensor->variant.temperature.offset.value.SN32);
		// update sensor status
		veVariantUn32(&sensor->variant.temperature.status, (un32)ok);
	} else if (sensor->interface.adc_sample > TEMP_SENS_MAX_ADCIN) {
		// open circuit error
		veVariantUn32(&sensor->variant.temperature.status, (un32)disconnected);
	} else if (sensor->interface.adc_sample < TEMP_SENS_S_C_ADCIN ) {
		// short circuit error
		veVariantUn32(&sensor->variant.temperature.status, (un32)short_circuited);
	} else if (VALUE_BETWEEN(sensor->interface.adc_sample, TEMP_SENS_INV_PLRTY_ADCIN_LB, TEMP_SENS_INV_PLRTY_ADCIN_HB)) {
		// lm335 probably connected in reverse polarity
		veVariantUn32(&sensor->variant.temperature.status, (un32)reverse_polarity);
	} else {
		// low temperature or unknown error
		veVariantUn32(&sensor->variant.temperature.status, (un32)unknown_value);
	}

	// if status = o.k. publish valid value otherwise publish invalid value
	if (sensor->variant.temperature.status.value.UN8 == (un8)ok) {
		veVariantSn32(&sensor->variant.temperature.temperature, (sn32)tempC);
	} else {
		veVariantInvalidate(&sensor->variant.temperature.temperature);
		veItemOwnerSet(sensor->info[temperature_item].item, sensor->info[temperature_item].local);
	}

	veVariantUn32(&sensor->variant.temperature.analogpinFunc,
			(un32)sensor->dbus_info[analogpinFunc].value->variant.value.Float);
	veVariantFloat(&sensor->variant.temperature.scale,
			sensor->dbus_info[scale].value->variant.value.Float);
	veVariantSn32(&sensor->variant.temperature.offset,
			(sn32)sensor->dbus_info[offset].value->variant.value.Float);
	veVariantSn32(&sensor->variant.temperature.temperatureType,
			(sn32)sensor->dbus_info[TempType].value->variant.value.Float);

	return veTrue;
}

/**
 * @brief sensors_data_process - direct to the data processing algorithm per sensor type
 * @param sensor - the sensor struct
 * @return Boolean status veTrue-success, veFalse-fail
 */
static veBool sensors_data_process(analog_sensor_t *sensor)
{
	// check the type of sensor before starting
	switch (sensor->sensor_type) {
	case tank_level_t:
		sensors_tankType_data_process(sensor);
		break;

	case temperature_t:
		sensors_temperatureType_data_process(sensor);
		break;

	default:
		break;
	}

	return veTrue;
}

/**
 * @brief updateValues - updates the dbus item values
 */
static void updateValues(void)
{
	for (analog_sensors_index_t sensor_index = 0; sensor_index < num_of_analog_sensors; sensor_index++) {
		analog_sensor_t *sensor = &analog_sensor[sensor_index];

		// update only variables values
		for (sensor_items_container_items_t i = 0; i < num_of_container_items; i++) {
			const ItemInfo *itemInfo = &sensor->info[i];

			if (itemInfo->local && veVariantIsValid(itemInfo->local)) {
				veItemOwnerSet(itemInfo->item, itemInfo->local);
			}
		}

		veDbusItemUpdate(sensor->dbus);
	}
}

/**
 * @brief sensors_handle - handles the sensors
 */
void sensors_handle(void)
{
	analog_sensors_index_t analog_sensors_index;

	// first read fast all the analog inputs and mark which read is valid
	// We reading always the same number of analog inputs to try to keep the timing of the system constant.
	for (analog_sensors_index = 0; analog_sensors_index < num_of_analog_sensors; analog_sensors_index++) {
		analog_sensor_t *sensor = &analog_sensor[analog_sensors_index];

		// reading all the analog inputs adc values
		if (!adc_read(&sensor->interface.adc_sample, sensor->interface.adc_pin)) {
			// validate the sample
			sensor->valid = veTrue;
		}
	}

	// Now handle the adc read to update the sensor
	for (analog_sensors_index = 0; analog_sensors_index < num_of_analog_sensors; analog_sensors_index++) {
		analog_sensor_t *sensor = &analog_sensor[analog_sensors_index];

		// proceed if the adc reading is valid
		if (sensor->valid == veTrue) {
			filter_iir_lpf_t *filter = &sensor->interface.sig_cond.filter_iir_lpf;

			// filter the input ADC sample and store it in adc var
			sensor->interface.adc_sample = adc_filter(
				(float)sensor->interface.adc_sample,
				&filter->adc_mem,
				filter->fc,
				10, filter->FF);

			// reset the adc valid reading flag for next sampling cycle
			sensor->valid = veFalse;

			// check if the sensor function - if it needed at all?
			un32 sensor_analogpinFunc = (un32)sensor->dbus_info[analogpinFunc].value->variant.value.Float;

			switch (sensor_analogpinFunc) {
			case default_function:
				// check if dbus is disconnected and connect it
				if (!sensor->interface.dbus.connected) {
					sensors_dbusConnect(sensor);
				}

				// need to proces the data
				sensors_data_process(sensor);
				break;

			case no_function:
			default:
				// check if dbus is connected and disconnect it
				if (sensor->interface.dbus.connected) {
					sensors_dbusDisconnect(sensor);
				}
				break;
			}
		} else {
			// adc reading error
		}
	}

	// call to update the dbus service with the new item values
	updateValues();
}

/**
 * @brief sensors_addSettings - connect sensor items to their dbus services
 * @param sensor_index - the sensor index array number
 */
void sensors_addSettings(analog_sensors_index_t sensor_index)
{
	analog_sensor_t *sensor = &analog_sensor[sensor_index];

	values_dbus_service_addSettings(sensor);
}

/**
 * @brief sensors_dbusInit - connect sensor items to their dbus services
 * @param sensor_index - the sensor index array number
 */
void sensors_dbusInit(analog_sensors_index_t sensor_index)
{
	analog_sensor_t *sensor = &analog_sensor[sensor_index];
	VeVariant variant;
	static un8 instance = 20;

	veItemOwnerSet(&sensor->items.product.connected, veVariantUn32(&variant, veTrue));
	veItemOwnerSet(&sensor->items.product.instance, veVariantUn8(&variant, instance++));

	/* Product info */
	if (sensor->sensor_type == tank_level_t) {
		veItemOwnerSet(&sensor->items.product.id, veVariantUn16(&variant, VE_PROD_ID_TANK_SENSOR_INPUT));
		veItemOwnerSet(&sensor->items.product.name, veVariantStr(&variant, veProductGetName(VE_PROD_ID_TANK_SENSOR_INPUT)));
	} else if (sensor->sensor_type == temperature_t) {
		veItemOwnerSet(&sensor->items.product.id, veVariantUn16(&variant, VE_PROD_ID_TEMPERATURE_SENSOR_INPUT));
		veItemOwnerSet(&sensor->items.product.name, veVariantStr(&variant, veProductGetName(VE_PROD_ID_TEMPERATURE_SENSOR_INPUT)));
	} else {

	}
}

/**
 * @brief xxxChange - is a callback that called when an item was changed
 * @param item - a pointer to the chanched item
 * @param ctx - a preloaded void pointer to some desired variable - in our case, a pointer the the sensor structure array element
 * @param variant - the changed variant in the item
 * @return Boolean status veTrue - success, veFalse - fail
 */

// Callback when the sensor function is changing
static veBool analogPinFuncChange(struct VeItem *item, void *ctx, VeVariant *variant)
{
	analog_sensor_t *p_analog_sensor = (analog_sensor_t *)ctx;

	veItemOwnerSet(item, variant);
	veItemOwnerSet(getConsumerRoot(), variant);

	VeItem *settingsItem = veItemGetOrCreateUid(getConsumerRoot(), p_analog_sensor->dbus_info[analogpinFunc].path);
	veItemSet(settingsItem, variant);

	p_analog_sensor->variant.tank_level.analogpinFunc.value.Float = variant->value.Float;

	return veTrue;
}

// Callback when the capacity is changing
static veBool capacityChange(struct VeItem *item, void *ctx, VeVariant *variant)
{
	analog_sensor_t *p_analog_sensor = (analog_sensor_t *)ctx;

	veItemOwnerSet(item, variant);
	veItemOwnerSet(getConsumerRoot(), variant);

	VeItem *settingsItem = veItemGetOrCreateUid(getConsumerRoot(), p_analog_sensor->dbus_info[capacity].path);
	veItemSet(settingsItem, variant);

	p_analog_sensor->variant.tank_level.capacity.value.Float = variant->value.Float;

	return veTrue;
}

// Callback when the fluid type is changing
static veBool fluidTypeChange(struct VeItem *item, void *ctx, VeVariant *variant)
{
	analog_sensor_t *p_analog_sensor = (analog_sensor_t *)ctx;

	veItemOwnerSet(item, variant);
	veItemOwnerSet(getConsumerRoot(), variant);

	VeItem *settingsItem = veItemGetOrCreateUid(getConsumerRoot(), p_analog_sensor->dbus_info[fluidType].path);
	veItemSet(settingsItem, variant);

	p_analog_sensor->variant.tank_level.fluidType.value.UN32 = variant->value.UN32;

	return veTrue;
}

// Callback when the standard is changing
static veBool standardChange(struct VeItem *item, void *ctx, VeVariant *variant)
{
	analog_sensor_t *p_analog_sensor = (analog_sensor_t *)ctx;

	veItemOwnerSet(item, variant);
	veItemOwnerSet(getConsumerRoot(), variant);

	VeItem *settingsItem = veItemGetOrCreateUid(getConsumerRoot(), p_analog_sensor->dbus_info[standard].path);
	veItemSet(settingsItem, variant);

	p_analog_sensor->variant.tank_level.standard.value.Float = variant->value.Float;

	return veTrue;
}

// Callback when the scale is changing
static veBool scaleChange(struct VeItem *item, void *ctx, VeVariant *variant)
{
	analog_sensor_t *p_analog_sensor = (analog_sensor_t *)ctx;

	veItemOwnerSet(item, variant);
	veItemOwnerSet(getConsumerRoot(), variant);

	VeItem *settingsItem = veItemGetOrCreateUid(getConsumerRoot(), p_analog_sensor->dbus_info[scale].path);
	veItemSet(settingsItem, variant);

	p_analog_sensor->variant.temperature.scale.value.Float = variant->value.Float;

	return veTrue;
}

// Callback when the offset is changing
static veBool offsetChange(struct VeItem *item, void *ctx, VeVariant *variant)
{
	analog_sensor_t *p_analog_sensor = (analog_sensor_t *)ctx;

	veItemOwnerSet(item, variant);
	veItemOwnerSet(getConsumerRoot(), variant);

	VeItem *settingsItem = veItemGetOrCreateUid(getConsumerRoot(), p_analog_sensor->dbus_info[offset].path);
	veItemSet(settingsItem, variant);

	p_analog_sensor->variant.temperature.offset.value.SN32 = variant->value.SN32;

	return veTrue;
}

//
static veBool TempTypeChange(struct VeItem *item, void *ctx, VeVariant *variant)
{
	analog_sensor_t *p_analog_sensor = (analog_sensor_t *)ctx;

	veItemOwnerSet(item, variant);
	veItemOwnerSet(getConsumerRoot(), variant);

	VeItem *settingsItem = veItemGetOrCreateUid(getConsumerRoot(), p_analog_sensor->dbus_info[TempType].path);
	veItemSet(settingsItem, variant);

	p_analog_sensor->variant.temperature.temperatureType.value.SN32 = variant->value.SN32;

	return veTrue;
}

/** Formats enum values. The content of `var` will converted to an integer. The integer will be used
  * as index to pick a string from options. This string is copied to buf.
  * If `var` is invalid or its value exceeds `optionCount`, buf will be an empty string
  * @returns The size of the selected option (or 0 if no option could be selected).
  */
static size_t enumFormatter(VeVariant *var, char *buf, size_t len, const char **options,
					 un32 optionCount)
{
	if (var->type.tp != VE_UNKNOWN) {
		un32 optionIndex = 0;
		const char *option = NULL;

		veVariantToN32(var);
		optionIndex = var->value.UN32;

		if (optionIndex < optionCount) {
			option = options[optionIndex];
			strncpy(buf, option, len);
			return strlen(option);
		}
	}

	/* Invalid or unknown value, set an empty string if possible */
	if (len > 0) {
		*buf = 0;
	}

	return 0;
}

/** Used to format the /Status D-Bus entry */
static size_t statusFormatter(VeVariant *var, void const *ctx, char *buf, size_t len)
{
	const char *options[] = { "Ok", "Disconnected", "Short circuited", "Reverse polarity",
							  "Unknown" };
	VE_UNUSED(ctx);
	return enumFormatter(var, buf, len, options, sizeof(options)/sizeof(options[0]));
}

/** Used to format the /FluidType D-Bus entry */
static size_t fluidTypeFormatter(VeVariant *var, void const *ctx, char *buf, size_t len)
{
	const char *options[] = { "Fuel", "Fresh water", "Waste water", "Live well", "Oil",
							  "Black water (sewage)" };
	VE_UNUSED(ctx);
	return enumFormatter(var, buf, len, options, sizeof(options)/sizeof(options[0]));
}

/** Used to format the /Standard D-Bus entry */
static size_t standardItemFormatter(VeVariant *var, void const *ctx, char *buf, size_t len)
{
	const char *options[] = { "European", "American" };
	VE_UNUSED(ctx);
	return enumFormatter(var, buf, len, options, sizeof(options)/sizeof(options[0]));
}
