#include <velib/base/base.h>
#include <velib/base/ve_string.h>
#include <velib/types/ve_item.h>
#include <velib/utils/ve_logger.h>
#include <velib/types/ve_dbus_item.h>
#include <velib/vecan/products.h>
#include <velib/utils/ve_item_utils.h>

#include "values.h"
#include "sensors.h"

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
static analog_sensor_t *analog_sensor[MAX_SENSORS];
static int sensor_count;

// instantiate a container structure for the interface with dbus API's interface.
static FormatInfo units = {{9, ""}, NULL};
static FormatInfo fluidTypeFormat = {{0, ""}, fluidTypeFormatter};
static FormatInfo standardFormat = {{0, ""}, standardItemFormatter};

static void init_item_info(ItemInfo *info, VeItem *item, VeVariant *local,
			const char *id, FormatInfo *fmt, int timeout,
			VeItemSetterFun *cb)
{
	info->item = item;
	info->local = local;
	info->id = id;
	info->fmt = fmt;
	info->timeout = timeout;
	info->setValueCallback = cb;
}

static struct VeItem *createEnumItem(analog_sensor_t *sensor, const char *id,
						   VeVariant *initial, VeItemValueFmt *fmt, VeItemSetterFun *cb)
{
	struct VeItem *item = veItemCreateBasic(&sensor->root, id, initial);
	veItemSetTimeout(item, 5);
	veItemSetSetter(item, cb, sensor);
	veItemSetFmt(item, fmt, NULL);

	return item;
}

static void sensor_item_info_init(analog_sensor_t *sensor)
{
	ProductInfo *prod = &sensor->items.product;
	ItemInfo *info = sensor->info;
	VeVariant v;

	init_item_info(&info[0], &prod->connected, NULL, "Connected",		&units, 0, NULL);
	init_item_info(&info[1], &prod->name,	   NULL, "ProductName",		&units, 0, NULL);
	init_item_info(&info[2], &prod->id,		   NULL, "ProductId",		&units, 0, NULL);
	init_item_info(&info[3], &prod->instance,  NULL, "DeviceInstance",	&units, 0, NULL);

	sensor->statusItem = createEnumItem(sensor, "Status", veVariantUn32(&v, SENSOR_STATUS_NCONN), statusFormatter, NULL);

#define IV(n) &item->n, &var->n

	if (sensor->sensor_type == SENSOR_TYPE_TANK) {
		tank_level_sensor_item_t *item = &sensor->items.tank_level;
		tank_level_sensor_variant_t *var = &sensor->variant.tank_level;

		init_item_info(&info[4], IV(level),			"Level",			&units,				5, NULL);
		init_item_info(&info[5], IV(remaining),		"Remaining",		&units,				5, NULL);
		init_item_info(&info[7], IV(analogpinFunc), "analogpinFunc",	&units,				5, analogPinFuncChange);
		init_item_info(&info[8], IV(capacity),		"Capacity",			&units,				5, capacityChange);
		init_item_info(&info[9], IV(fluidType),		"FluidType",		&fluidTypeFormat,	5, fluidTypeChange);
		init_item_info(&info[10],IV(standard),		"Standard",			&standardFormat,	5, standardChange);
	} else if (sensor->sensor_type == SENSOR_TYPE_TEMP) {
		temperature_sensor_item_t *item = &sensor->items.temperature;
		temperature_sensor_variant_t *var = &sensor->variant.temperature;

		init_item_info(&info[4], IV(temperature),	"Temperature",		&units,				5, NULL);
		init_item_info(&info[7], IV(analogpinFunc), "analogpinFunc",	&units,				5, analogPinFuncChange);
		init_item_info(&info[8], IV(scale),			"Scale",			&units,				5, scaleChange);
		init_item_info(&info[9], IV(offset),		"Offset",			&units,				5, offsetChange);
		init_item_info(&info[10],IV(temperatureType),"TemperatureType", &units,				5, TempTypeChange);
	}

#undef IV
}

static void sensor_set_defaults_tank(analog_sensor_t *sensor)
{
	sensors_dbus_interface_t *dbus = &sensor->interface.dbus;
	filter_iir_lpf_t *lpf = &sensor->interface.sig_cond.filter_iir_lpf;
	dbus_info_t *dbi = sensor->dbus_info;

	static int tank_num = 1;

	lpf->FF = TANK_SENSOR_IIR_LPF_FF_VALUE;
	lpf->fc = TANK_SENSOR_CUTOFF_FREQ;
	lpf->last = HUGE_VALF;

	snprintf(dbus->service, sizeof(dbus->service),
			 "com.victronenergy.tank.builtin_adc%d", sensor->interface.adc_pin);

	snprintf(dbi[0].path, sizeof(dbi[0].path),
			 "Settings/AnalogInput/Resistive/%d/Function", tank_num);

	dbi[1].def = DEFAULT_TANK_CAPACITY;
	dbi[1].min = MIN_OF_TANK_CAPACITY;
	dbi[1].max = MAX_OF_TANK_CAPACITY;
	snprintf(dbi[1].path, sizeof(dbi[1].path),
			 "Settings/Tank/%d/Capacity", tank_num);

	dbi[2].def = DEFAULT_FLUID_TYPE;
	dbi[2].min = MIN_OF_FLUID_TYPE;
	dbi[2].max = MAX_OF_FLUID_TYPE;
	snprintf(dbi[2].path, sizeof(dbi[2].path),
			 "Settings/Tank/%d/FluidType", tank_num);

	dbi[3].def = european_std;
	dbi[3].min = european_std;
	dbi[3].max = num_of_stds - 1;
	snprintf(dbi[3].path, sizeof(dbi[3].path),
			 "Settings/Tank/%d/Standard", tank_num);

	snprintf(sensor->iface_name, sizeof(sensor->iface_name),
			 "Tank Level sensor input %d", tank_num);

	tank_num++;
}

static void sensor_set_defaults_temp(analog_sensor_t *sensor)
{
	sensors_dbus_interface_t *dbus = &sensor->interface.dbus;
	filter_iir_lpf_t *lpf = &sensor->interface.sig_cond.filter_iir_lpf;
	dbus_info_t *dbi = sensor->dbus_info;

	static int temp_num = 1;

	lpf->FF = TEMPERATURE_SENSOR_IIR_LPF_FF_VALUE;
	lpf->fc = TEMPERATURE_SENSOR_CUTOFF_FREQ;
	lpf->last = HUGE_VALF;

	snprintf(dbus->service, sizeof(dbus->service),
			 "com.victronenergy.temperature.builtin_adc%d", sensor->interface.adc_pin);

	snprintf(dbi[0].path, sizeof(dbi[0].path),
			 "Settings/AnalogInput/Temperature/%d/Function", temp_num);

	dbi[1].def = TEMPERATURE_SCALE;
	dbi[1].min = MIN_OF_TEMPERATURE_SCALE;
	dbi[1].max = MAX_OF_TEMPERATURE_SCALE;
	snprintf(dbi[1].path, sizeof(dbi[1].path),
			 "Settings/Temperature/%d/Scale", temp_num);

	dbi[2].def = TEMPERATURE_OFFSET;
	dbi[2].min = MIN_OF_TEMPERATURE_OFFSET;
	dbi[2].max = MAX_OF_TEMPERATURE_OFFSET;
	snprintf(dbi[2].path, sizeof(dbi[2].path),
			 "Settings/Temperature/%d/Offset", temp_num);

	dbi[3].def = DEFAULT_TEMPERATURE_TYPE;
	dbi[3].min = MIN_TEMPERATURE_TYPE;
	dbi[3].max = num_of_temperature_sensor_type - 1;
	snprintf(dbi[3].path, sizeof(dbi[3].path),
			 "Settings/Temperature/%d/TemperatureType", temp_num);

	snprintf(sensor->iface_name, sizeof(sensor->iface_name),
			 "Temperature sensor input %d", temp_num);

	temp_num++;
}

static void sensor_set_defaults(analog_sensor_t *sensor)
{
	dbus_info_t *dbi = sensor->dbus_info;

	dbi[0].def = default_function;
	dbi[0].min = no_function;
	dbi[0].max = num_of_functions - 1;

	if (sensor->sensor_type == SENSOR_TYPE_TANK)
		sensor_set_defaults_tank(sensor);
	else if (sensor->sensor_type == SENSOR_TYPE_TEMP)
		sensor_set_defaults_temp(sensor);
}

/**
 * @brief sensor_init - hook the sensor items to their dbus services
 * @param devfd - file descriptor of ADC device sysfs directory
 * @param pin - ADC pin number
 * @param scale - ADC scale in volts / unit
 * @param type - type of sensor
 * @return Pointer to sensor struct
 */
analog_sensor_t *sensor_init(int devfd, int pin, float scale, sensor_type_t type)
{
	analog_sensor_t *sensor;

	if (sensor_count == MAX_SENSORS)
		return NULL;

	if (type == SENSOR_TYPE_TANK)
		sensor = calloc(1, sizeof(struct TankSensor));
	else if (type == SENSOR_TYPE_TEMP)
		sensor = calloc(1, sizeof(struct TemperatureSensor));
	else
		return NULL;

	if (!sensor)
		return NULL;

	analog_sensor[sensor_count++] = sensor;

	sensor->interface.devfd = devfd;
	sensor->interface.adc_pin = pin;
	sensor->interface.adc_scale = scale;
	sensor->sensor_type = type;

	sensor_set_defaults(sensor);
	sensor_item_info_init(sensor);

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
	sensor_status_t status;
	VeVariant v;

	if (sensor->interface.adc_sample > 1.4) {
		// Sensor status: error - not connected
		status = SENSOR_STATUS_NCONN;
	// this condition applies only for the US standard
	} else if (Std && (sensor->interface.adc_sample < 0.15)) {
		// Sensor status: error - short circuited
		status = SENSOR_STATUS_SHORT;
	} else {
		// calculate the resistance of the tank level sensor from the adc pin sample
		float vdiff = TANK_SENS_VREF - sensor->interface.adc_sample;
		float R2 = TANK_SENS_R1 * sensor->interface.adc_sample / vdiff;

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
			status = SENSOR_STATUS_OK;
		} else {
			// Sensor status: error - unknown value
			status = SENSOR_STATUS_UNKNOWN;
		}
	}

	// measure is ok and R2 resistance was correctly calculated
	veVariantUn32(&sensor->variant.tank_level.analogpinFunc,
			(un32)sensor->dbus_info[analogpinFunc].value->variant.value.Float);

	veItemOwnerSet(sensor->statusItem, veVariantUn32(&v, status));

	// if status = o.k. publish valid value otherwise publish invalid value
	if (status == SENSOR_STATUS_OK) {
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
	sensor_status_t status;
	float adc_sample = sensor->interface.adc_sample;
	VeVariant v;

	if (adc_sample > TEMP_SENS_MIN_ADCIN && adc_sample < TEMP_SENS_MAX_ADCIN) {
		// calculate the output of the LM335 temperature sensor from the adc pin sample
		float v_sens = adc_sample * TEMP_SENS_V_RATIO;
		// convert from Kelvin to Celsius
		tempC = 100 * v_sens - 273;
		// Signal scale correction
		tempC *= (sensor->variant.temperature.scale.value.Float);
		// Signal offset correction
		tempC += (sensor->variant.temperature.offset.value.SN32);

		status = SENSOR_STATUS_OK;
	} else if (adc_sample > TEMP_SENS_MAX_ADCIN) {
		// open circuit error
		status = SENSOR_STATUS_NCONN;
	} else if (adc_sample < TEMP_SENS_S_C_ADCIN ) {
		// short circuit error
		status = SENSOR_STATUS_SHORT;
	} else if (adc_sample > TEMP_SENS_INV_PLRTY_ADCIN_LB && adc_sample < TEMP_SENS_INV_PLRTY_ADCIN_HB) {
		// lm335 probably connected in reverse polarity
		status = SENSOR_STATUS_REVPOL;
	} else {
		// low temperature or unknown error
		status = SENSOR_STATUS_UNKNOWN;
	}

	veItemOwnerSet(sensor->statusItem, veVariantUn32(&v, status));

	// if status = o.k. publish valid value otherwise publish invalid value
	if (status == SENSOR_STATUS_OK) {
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
	case SENSOR_TYPE_TANK:
		sensors_tankType_data_process(sensor);
		break;

	case SENSOR_TYPE_TEMP:
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
	for (int sensor_index = 0; sensor_index < sensor_count; sensor_index++) {
		analog_sensor_t *sensor = analog_sensor[sensor_index];

		// update only variables values
		for (sensor_items_container_items_t i = 0; i < num_of_container_items; i++) {
			const ItemInfo *itemInfo = &sensor->info[i];

			if (itemInfo->local && veVariantIsValid(itemInfo->local)) {
				veItemOwnerSet(itemInfo->item, itemInfo->local);
			}
		}
	}
}

/**
 * @brief sensors_handle - handles the sensors
 */
void sensors_handle(void)
{
	int analog_sensors_index;

	// first read fast all the analog inputs and mark which read is valid
	// We reading always the same number of analog inputs to try to keep the timing of the system constant.
	for (analog_sensors_index = 0; analog_sensors_index < sensor_count; analog_sensors_index++) {
		analog_sensor_t *sensor = analog_sensor[analog_sensors_index];
		un32 val;

		sensor->valid = adc_read(&val, sensor);
		if (sensor->valid)
			sensor->interface.adc_sample = val * sensor->interface.adc_scale;
	}

	// Now handle the adc read to update the sensor
	for (analog_sensors_index = 0; analog_sensors_index < sensor_count; analog_sensors_index++) {
		analog_sensor_t *sensor = analog_sensor[analog_sensors_index];
		filter_iir_lpf_t *filter = &sensor->interface.sig_cond.filter_iir_lpf;

		if (!sensor->valid)
			continue;

		// filter the input ADC sample and store it in adc var
		sensor->interface.adc_sample = adc_filter(sensor->interface.adc_sample, filter);

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
	}

	// call to update the dbus service with the new item values
	updateValues();
}

/**
 * @brief sensors_dbusInit - connect sensor items to their dbus services
 * @param sensor - pointer to the sensor struct
 */
void sensors_dbusInit(analog_sensor_t *sensor)
{
	VeVariant variant;
	static un8 instance = 20;

	veItemOwnerSet(&sensor->items.product.connected, veVariantUn32(&variant, veTrue));
	veItemOwnerSet(&sensor->items.product.instance, veVariantUn8(&variant, instance++));

	/* Product info */
	if (sensor->sensor_type == SENSOR_TYPE_TANK) {
		veItemOwnerSet(&sensor->items.product.id, veVariantUn16(&variant, VE_PROD_ID_TANK_SENSOR_INPUT));
		veItemOwnerSet(&sensor->items.product.name, veVariantStr(&variant, veProductGetName(VE_PROD_ID_TANK_SENSOR_INPUT)));
	} else if (sensor->sensor_type == SENSOR_TYPE_TEMP) {
		veItemOwnerSet(&sensor->items.product.id, veVariantUn16(&variant, VE_PROD_ID_TEMPERATURE_SENSOR_INPUT));
		veItemOwnerSet(&sensor->items.product.name, veVariantStr(&variant, veProductGetName(VE_PROD_ID_TEMPERATURE_SENSOR_INPUT)));
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
