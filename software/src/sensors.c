#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <velib/platform/plt.h>
#include <velib/types/ve_dbus_item.h>
#include <velib/types/ve_item.h>
#include <velib/utils/ve_item_utils.h>
#include <velib/utils/ve_logger.h>
#include <velib/vecan/products.h>

#include "sensors.h"

#define MAX_SENSORS							8
#define SAMPLE_RATE							10

// defines for the tank level sensor analog front end parameters
#define TANK_SENS_VREF						5.0
#define TANK_SENS_R1						680.0 // ohms
#define EUR_MAX_TANK_LEVEL_RESISTANCE		(180) //ohms
#define USA_MAX_TANK_LEVEL_RESISTANCE		(240) //ohms
#define USA_MIN_TANK_LEVEL_RESISTANCE		(30) //ohms

// defines for the temperature sensor analog front end parameters
#define TEMP_SENS_R1						10000.0 // ohms
#define TEMP_SENS_R2						4700.0  // ohms
#define TEMP_SENS_V_RATIO					((TEMP_SENS_R1 + TEMP_SENS_R2) / TEMP_SENS_R2)
#define TEMP_SENS_MAX_ADCIN					1.3 // ~400K
#define TEMP_SENS_MIN_ADCIN					0.8 // ~(-22) degrees C
#define TEMP_SENS_S_C_ADCIN					0.02
#define TEMP_SENS_INV_PLRTY_ADCIN			0.208 // 0.7 volts at divider input
#define TEMP_SENS_INV_PLRTY_ADCIN_BAND		0.15
#define TEMP_SENS_INV_PLRTY_ADCIN_LB		(TEMP_SENS_INV_PLRTY_ADCIN - TEMP_SENS_INV_PLRTY_ADCIN_BAND)
#define TEMP_SENS_INV_PLRTY_ADCIN_HB		(TEMP_SENS_INV_PLRTY_ADCIN + TEMP_SENS_INV_PLRTY_ADCIN_BAND)

// defines to tank level sensor filter parameters
#define TANK_SENSOR_IIR_LPF_FF_VALUE		0.4
#define TANK_SENSOR_CUTOFF_FREQ				(0.001 / SAMPLE_RATE)

// defines to temperature sensor filter parameters
#define TEMPERATURE_SENSOR_IIR_LPF_FF_VALUE	0.2
#define TEMPERATURE_SENSOR_CUTOFF_FREQ		(0.01 / SAMPLE_RATE)

struct SettingProperties {
	VeDataBasicType type;
	VeVariant def;
	VeVariant min;
	VeVariant max;
};

static AnalogSensor *analog_sensor[MAX_SENSORS];
static int sensor_count;

static VeVariantUnitFmt veUnitVolume = {3, "m3"};
static VeVariantUnitFmt veUnitCelsius0Dec = {0, "C"};

/* Common */
static struct SettingProperties functionProps = {
	.type = VE_SN32,
	.def.value.SN32 = SENSOR_FUNCTION_DEFAULT,
	.max.value.SN32 = SENSOR_FUNCTION_COUNT - 1,
};

/* Tank sensor */
static struct SettingProperties tankCapacityProps = {
	.type = VE_FLOAT,
	.def.value.Float = 0.2f /* m3 */,
	.max.value.Float = 1000.0f,
};

static struct SettingProperties tankFluidType = {
	.type = VE_SN32,
	.max.value.SN32 = INT32_MAX - 3,
};

static struct SettingProperties tankStandardProps = {
	.type = VE_SN32,
	.max.value.SN32 = TANK_STANDARD_COUNT - 1,
};

/* Temperature sensor */
static struct SettingProperties scaleProps = {
	.type = VE_FLOAT,
	.def.value.Float = 1.0f,
	.min.value.Float = 0.1f,
	.max.value.Float = 10.0f,
};

static struct SettingProperties offsetProps = {
	.type = VE_FLOAT,
	.min.value.Float = -100.0f,
	.max.value.Float = 100.0f,
};

static struct SettingProperties temperatureType = {
	.type = VE_SN32,
	.max.value.SN32 = INT32_MAX - 3,
};

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

static struct VeItem *createEnumItem(AnalogSensor *sensor, const char *id,
						   VeVariant *initial, VeItemValueFmt *fmt, VeItemSetterFun *cb)
{
	struct VeItem *item = veItemCreateBasic(sensor->root, id, initial);
	veItemSetTimeout(item, 5);
	veItemSetSetter(item, cb, sensor);
	veItemSetFmt(item, fmt, NULL);

	return item;
}

/* sensor -> localsettings */
static veBool forwardToLocalsettings(struct VeItem *item, void *ctx, VeVariant *variant)
{
	struct VeItem *settingsItem = (struct VeItem *) ctx;
	VE_UNUSED(item);

	return veItemSet(settingsItem, variant);
}

/* localsettings -> sensor */
static void onSettingChanged(struct VeItem *item)
{
	struct VeItem *sensorItem = (struct VeItem *) veItemCtx(item)->ptr;
	VeVariant v;

	veItemLocalValue(item, &v);
	veItemOwnerSet(sensorItem, &v);
}

/*
 * The settings of a sensor service are stored in localsettings, so when
 * the sensor value changes, send it to localsettings and if the setting
 * in localsettings changed, also update the sensor value.
 */
static struct VeItem *createSettingsProxy(AnalogSensor *sensor, char const *prefix,
										  char *id, VeItemValueFmt *fmt, void *fmtCtx,
										  struct SettingProperties *properties)
{
	struct VeItem *localSettings = getLocalSettings();
	struct VeItem *settingPrefixItem, *settingItem, *sensorItem;
	VeVariant v;

	properties->def.type.tp = properties->type;
	properties->min.type.tp = properties->type;
	properties->max.type.tp = properties->type;

	settingPrefixItem = veItemGetOrCreateUid(localSettings, prefix);
	settingItem = veItemGetOrCreateUid(settingPrefixItem, id);
	sensorItem = veItemGetOrCreateUid(sensor->root, id);

	veItemCtx(settingItem)->ptr = sensorItem;
	veItemSetChanged(settingItem, onSettingChanged);

	veItemSetSetter(sensorItem, forwardToLocalsettings, settingItem);
	veItemSetFmt(sensorItem, fmt, fmtCtx);
	veItemLocalSet(sensorItem, veVariantInvalidType(&v, properties->type));
	veItemSetMax(sensorItem, &properties->max);
	veItemSetMin(sensorItem, &properties->min);
	veItemSetDefault(sensorItem, &properties->def);

	if (!veDBusAddLocalSetting(settingItem, &properties->def, &properties->min, &properties->max, veFalse)) {
		logE("task", "veDBusAddLocalSetting failed");
		pltExit(1);
	}
	return sensorItem;
}

static struct VeItem *createFunctionProxy(AnalogSensor *sensor, const char *prefixFormat)
{
	char prefix[VE_MAX_UID_SIZE];

	snprintf(prefix, sizeof(prefix), prefixFormat, sensor->number);
	return createSettingsProxy(sensor, prefix, "Function", veVariantFmt, &veUnitNone, &functionProps);
}

static void sensor_item_info_init(AnalogSensor *sensor)
{
	VeVariant v;
	struct VeItem *root = sensor->root;
	char prefix[VE_MAX_UID_SIZE];

	veItemCreateBasic(root, "Connected", veVariantUn32(&v, veTrue));
	veItemCreateBasic(root, "DeviceInstance", veVariantUn32(&v, sensor->instance));
	sensor->statusItem = createEnumItem(sensor, "Status", veVariantUn32(&v, SENSOR_STATUS_NOT_CONNECTED), statusFormatter, NULL);

	if (sensor->sensor_type == SENSOR_TYPE_TANK) {
		struct TankSensor *tank = (struct TankSensor *) sensor;

		veItemCreateProductId(root, VE_PROD_ID_TANK_SENSOR_INPUT);
		veItemCreateBasic(root, "ProductName", veVariantStr(&v, veProductGetName(VE_PROD_ID_TANK_SENSOR_INPUT)));

		tank->levelItem = veItemCreateQuantity(root, "Level", veVariantInvalidType(&v, VE_UN32), &veUnitPercentage);
		tank->remaingItem = veItemCreateQuantity(root, "Remaining", veVariantInvalidType(&v, VE_FLOAT), &veUnitVolume);

		snprintf(prefix, sizeof(prefix), "Settings/Tank/%d", sensor->number);
		tank->capacityItem = createSettingsProxy(sensor, prefix, "Capacity", veVariantFmt, &veUnitVolume, &tankCapacityProps);
		tank->fluidTypeItem = createSettingsProxy(sensor, prefix, "FluidType", fluidTypeFormatter, NULL, &tankFluidType);
		tank->standardItem = createSettingsProxy(sensor, prefix, "Standard", standardItemFormatter, NULL, &tankStandardProps);

		sensor->function = createFunctionProxy(sensor, "/Settings/AnalogInput/Resistive/%d");

	} else if (sensor->sensor_type == SENSOR_TYPE_TEMP) {
		struct TemperatureSensor *temperature = (struct TemperatureSensor *) sensor;

		veItemCreateProductId(root, VE_PROD_ID_TEMPERATURE_SENSOR_INPUT);
		veItemCreateBasic(root, "ProductName", veVariantStr(&v, veProductGetName(VE_PROD_ID_TEMPERATURE_SENSOR_INPUT)));

		temperature->temperatureItem = veItemCreateQuantity(root, "Temperature", veVariantInvalidType(&v, VE_SN32), &veUnitCelsius0Dec);

		snprintf(prefix, sizeof(prefix), "Settings/Temperature/%d", sensor->number);
		temperature->scaleItem = createSettingsProxy(sensor, prefix, "Scale", veVariantFmt, &veUnitNone, &scaleProps);
		temperature->offsetItem = createSettingsProxy(sensor, prefix, "Offset", veVariantFmt, &veUnitNone, &offsetProps);
		createSettingsProxy(sensor, prefix, "TemperatureType", veVariantFmt, &veUnitNone, &temperatureType);

		sensor->function = createFunctionProxy(sensor, "/Settings/AnalogInput/Temperature/%d");
	}
}

static void sensor_set_defaults_tank(AnalogSensor *sensor)
{
	SensorDbusInterface *dbus = &sensor->interface.dbus;
	FilerIirLpf *lpf = &sensor->interface.sig_cond.filterIirLpf;

	static int tank_num = 1;

	lpf->FF = TANK_SENSOR_IIR_LPF_FF_VALUE;
	lpf->fc = TANK_SENSOR_CUTOFF_FREQ;
	lpf->last = HUGE_VALF;

	snprintf(dbus->service, sizeof(dbus->service),
			 "com.victronenergy.tank.builtin_adc%d", sensor->interface.adc_pin);

	snprintf(sensor->iface_name, sizeof(sensor->iface_name),
			 "Tank Level sensor input %d", tank_num);

	sensor->number = tank_num;
	tank_num++;
}

static void sensor_set_defaults_temp(AnalogSensor *sensor)
{
	SensorDbusInterface *dbus = &sensor->interface.dbus;
	FilerIirLpf *lpf = &sensor->interface.sig_cond.filterIirLpf;

	static int temp_num = 1;

	lpf->FF = TEMPERATURE_SENSOR_IIR_LPF_FF_VALUE;
	lpf->fc = TEMPERATURE_SENSOR_CUTOFF_FREQ;
	lpf->last = HUGE_VALF;

	snprintf(dbus->service, sizeof(dbus->service),
			 "com.victronenergy.temperature.builtin_adc%d", sensor->interface.adc_pin);

	snprintf(sensor->iface_name, sizeof(sensor->iface_name),
			 "Temperature sensor input %d", temp_num);

	sensor->number = temp_num;
	temp_num++;
}

static void sensor_set_defaults(AnalogSensor *sensor)
{
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
AnalogSensor *sensor_init(int devfd, int pin, float scale, SensorType type)
{
	AnalogSensor *sensor;
	static un8 instance = 20;

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
	sensor->instance = instance++;
	sensor->root = veItemAlloc(NULL, "");

	sensor_set_defaults(sensor);
	sensor_item_info_init(sensor);

	return sensor;
}

/**
 * @brief sensors_tankType_data_process - process the tank level sensor adc data (need to switch the oin function as when functions will be add)
 * @param sensor - pointer to the sensor struct
 * @return Boolean status veTrue - success, veFalse - fail
 */
static veBool sensors_tankType_data_process(AnalogSensor *sensor)
{
	// process the data of the analog input with respect to its function
	float level, capacity;
	TankStandard standard;
	SensorStatus status;
	VeVariant v;
	struct TankSensor *tank = (struct TankSensor *) sensor;

	if (!veVariantIsValid(veItemLocalValue(tank->standardItem, &v)))
		return veFalse;
	standard = (TankStandard) v.value.UN32;

	if (!veVariantIsValid(veItemLocalValue(tank->capacityItem, &v)))
		return veFalse;
	capacity = v.value.Float;

	if (sensor->interface.adc_sample > 1.4) {
		// Sensor status: error - not connected
		status = SENSOR_STATUS_NOT_CONNECTED;
	// this condition applies only for the US standard
	} else if (standard && (sensor->interface.adc_sample < 0.15)) {
		// Sensor status: error - short circuited
		status = SENSOR_STATUS_SHORT;
	} else {
		// calculate the resistance of the tank level sensor from the adc pin sample
		float vdiff = TANK_SENS_VREF - sensor->interface.adc_sample;
		float R2 = TANK_SENS_R1 * sensor->interface.adc_sample / vdiff;

		// check the integrity of the resistance
		if (R2>0) { // calculate the tank level
			if (standard == TANK_STANDARD_US) { // tank level calculation in the case it is an American standard sensor
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

	veItemOwnerSet(sensor->statusItem, veVariantUn32(&v, status));

	// if status = o.k. publish valid value otherwise publish invalid value
	if (status == SENSOR_STATUS_OK) {
		veItemOwnerSet(tank->levelItem, veVariantUn32(&v, 100 * level));
		veItemOwnerSet(tank->remaingItem, veVariantFloat(&v, level * capacity));
	} else {
		veItemInvalidate(tank->levelItem);
		veItemInvalidate(tank->remaingItem);
	}

	return veTrue;
}

/**
 * @brief sensors_tankType_data_process - process the temperature sensor adc data (need to switch the oin function as when functions will be add)
 * @param sensor - pointer to the sensor struct
 * @return Boolean status veTrue-success, veFalse-fail
 */
static veBool sensors_temperatureType_data_process(AnalogSensor *sensor)
{
	float tempC, offset, scale;
	SensorStatus status;
	float adc_sample = sensor->interface.adc_sample;
	struct TemperatureSensor *temperature = (struct TemperatureSensor *) sensor;
	VeVariant v;

	if (!veVariantIsValid(veItemLocalValue(temperature->offsetItem, &v)))
		return veFalse;
	offset = v.value.Float;

	if (!veVariantIsValid(veItemLocalValue(temperature->scaleItem, &v)))
		return veFalse;
	scale = v.value.Float;

	if (adc_sample > TEMP_SENS_MIN_ADCIN && adc_sample < TEMP_SENS_MAX_ADCIN) {
		// calculate the output of the LM335 temperature sensor from the adc pin sample
		float v_sens = adc_sample * TEMP_SENS_V_RATIO;
		// convert from Kelvin to Celsius
		tempC = 100 * v_sens - 273;
		// Signal scale correction
		tempC *= scale;
		// Signal offset correction
		tempC += offset;

		status = SENSOR_STATUS_OK;
	} else if (adc_sample > TEMP_SENS_MAX_ADCIN) {
		// open circuit error
		status = SENSOR_STATUS_NOT_CONNECTED;
	} else if (adc_sample < TEMP_SENS_S_C_ADCIN ) {
		// short circuit error
		status = SENSOR_STATUS_SHORT;
	} else if (adc_sample > TEMP_SENS_INV_PLRTY_ADCIN_LB && adc_sample < TEMP_SENS_INV_PLRTY_ADCIN_HB) {
		// lm335 probably connected in reverse polarity
		status = SENSOR_STATUS_REVERSE_POLARITY;
	} else {
		// low temperature or unknown error
		status = SENSOR_STATUS_UNKNOWN;
	}

	veItemOwnerSet(sensor->statusItem, veVariantUn32(&v, status));

	// if status = o.k. publish valid value otherwise publish invalid value
	if (status == SENSOR_STATUS_OK) {
		veItemOwnerSet(temperature->temperatureItem, veVariantSn32(&v, tempC));
	} else {
		veItemInvalidate(temperature->temperatureItem);
	}

	return veTrue;
}

/**
 * @brief sensors_data_process - direct to the data processing algorithm per sensor type
 * @param sensor - the sensor struct
 * @return Boolean status veTrue-success, veFalse-fail
 */
static veBool sensors_data_process(AnalogSensor *sensor)
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

static void sensors_dbusConnect(AnalogSensor *sensor)
{
	sensor->dbus = veDbusConnectString(veDbusGetDefaultConnectString());
	if (!sensor->dbus) {
		logE(sensor->interface.dbus.service, "dbus connect failed");
		pltExit(1);
	}
	sensor->interface.dbus.connected = veTrue;
	/* Device found */

	veDbusItemInit(sensor->dbus, sensor->root);
	veDbusChangeName(sensor->dbus, sensor->interface.dbus.service);

	logI(sensor->interface.dbus.service, "connected to dbus");
}

static void sensors_dbusDisconnect(AnalogSensor *sensor)
{
	veDbusDisconnect(sensor->dbus);
	sensor->interface.dbus.connected = veFalse;
}

void sensors_handle(void)
{
	int analog_sensors_index;
	VeVariant v;

	// first read fast all the analog inputs and mark which read is valid
	// We reading always the same number of analog inputs to try to keep the timing of the system constant.
	for (analog_sensors_index = 0; analog_sensors_index < sensor_count; analog_sensors_index++) {
		AnalogSensor *sensor = analog_sensor[analog_sensors_index];
		un32 val;

		sensor->valid = adc_read(&val, sensor);
		if (sensor->valid)
			sensor->interface.adc_sample = val * sensor->interface.adc_scale;
	}

	// Now handle the adc read to update the sensor
	for (analog_sensors_index = 0; analog_sensors_index < sensor_count; analog_sensors_index++) {
		AnalogSensor *sensor = analog_sensor[analog_sensors_index];
		FilerIirLpf *filter = &sensor->interface.sig_cond.filterIirLpf;

		if (!sensor->valid)
			continue;

		// filter the input ADC sample and store it in adc var
		sensor->interface.adc_sample = adc_filter(sensor->interface.adc_sample, filter);

		// check if the sensor function - if it needed at all?
		veItemLocalValue(sensor->function, &v);
		if (!veVariantIsValid(&v))
			continue;

		switch (v.value.UN32) {
		case SENSOR_FUNCTION_DEFAULT:
			// check if dbus is disconnected and connect it
			if (!sensor->interface.dbus.connected) {
				sensors_dbusConnect(sensor);
			}

			// need to proces the data
			sensors_data_process(sensor);
			break;

		case SENSOR_FUNCTION_NONE:
		default:
			// check if dbus is connected and disconnect it
			if (sensor->interface.dbus.connected) {
				sensors_dbusDisconnect(sensor);
			}
			break;
		}
	}
}

/**
 * @brief add_sensor
 * @param devfd - file descriptor of ADC device sysfs directory
 * @param pin - ADC pin number
 * @param scale - ADC scale in volts / unit
 * @param type - type of sensor
 * @return 0 on success, -1 on error
 */
int add_sensor(int devfd, int pin, float scale, int type)
{
	AnalogSensor *sensor;
	VeVariant v;

	sensor = sensor_init(devfd, pin, scale, type);
	if (!sensor)
		return -1;

	/* App info */
	sensor->processName = veItemCreateBasic(sensor->root, "Mgmt/ProcessName", veVariantStr(&v, pltProgramName()));
	sensor->processVersion = veItemCreateBasic(sensor->root, "Mgmt/ProcessVersion", veVariantStr(&v, pltProgramVersion()));
	sensor->connection = veItemCreateBasic(sensor->root, "Mgmt/Connection", veVariantStr(&v, sensor->iface_name));

	return 0;
}
