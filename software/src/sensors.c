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
#define EUR_MAX_TANK_LEVEL_RESISTANCE		180 // ohms
#define USA_MAX_TANK_LEVEL_RESISTANCE		240 // ohms
#define USA_MIN_TANK_LEVEL_RESISTANCE		30 // ohms

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

static AnalogSensor *sensors[MAX_SENSORS];
static int sensorCount;

static VeVariantUnitFmt veUnitVolume = {3, "m3"};
static VeVariantUnitFmt veUnitCelsius0Dec = {0, "C"};

/* Common */
static struct VeSettingProperties functionProps = {
	.type = VE_SN32,
	.def.value.SN32 = SENSOR_FUNCTION_DEFAULT,
	.max.value.SN32 = SENSOR_FUNCTION_COUNT - 1,
};

/* Tank sensor */
static struct VeSettingProperties tankCapacityProps = {
	.type = VE_FLOAT,
	.def.value.Float = 0.2f /* m3 */,
	.max.value.Float = 1000.0f,
};

static struct VeSettingProperties tankFluidType = {
	.type = VE_SN32,
	.max.value.SN32 = INT32_MAX - 3,
};

static struct VeSettingProperties tankStandardProps = {
	.type = VE_SN32,
	.max.value.SN32 = TANK_STANDARD_COUNT - 1,
};

/* Temperature sensor */
static struct VeSettingProperties scaleProps = {
	.type = VE_FLOAT,
	.def.value.Float = 1.0f,
	.min.value.Float = 0.1f,
	.max.value.Float = 10.0f,
};

static struct VeSettingProperties offsetProps = {
	.type = VE_FLOAT,
	.min.value.Float = -100.0f,
	.max.value.Float = 100.0f,
};

static struct VeSettingProperties temperatureType = {
	.type = VE_SN32,
	.max.value.SN32 = INT32_MAX - 3,
};

VeVariantEnumFmt const statusDef = VE_ENUM_DEF("Ok", "Disconnected",  "Short circuited",
												   "Reverse polarity", "Unknown");
VeVariantEnumFmt const fluidTypeDef = VE_ENUM_DEF("Fuel", "Fresh water", "Waste water",
													  "Live well", "Oil", "Black water (sewage)");
VeVariantEnumFmt const standardDef = VE_ENUM_DEF("European", "American");

static struct VeItem *createEnumItem(AnalogSensor *sensor, const char *id,
						   VeVariant *initial, VeVariantEnumFmt const *fmt, VeItemSetterFun *cb)
{
	struct VeItem *item = veItemCreateBasic(sensor->root, id, initial);
	veItemSetTimeout(item, 5);
	veItemSetSetter(item, cb, sensor);
	if (fmt)
		veItemSetFmt(item, veVariantEnumFmt, fmt);

	return item;
}

/*
 * The settings of a sensor service are stored in localsettings, so when
 * the sensor value changes, send it to localsettings and if the setting
 * in localsettings changed, also update the sensor value.
 */
static struct VeItem *createSettingsProxy(AnalogSensor *sensor, char const *prefix,
										  char *id, VeItemValueFmt *fmt, void const *fmtCtx,
										  struct VeSettingProperties *properties)
{
	struct VeItem *localSettings = getLocalSettings();
	struct VeItem *sensorItem;

	sensorItem = veItemCreateSettingsProxy(localSettings, prefix, sensor->root,
										   id, fmt, fmtCtx, properties);
	if (!sensorItem) {
		logE("task", "veItemCreateSettingsProxy failed");
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

static void createItems(AnalogSensor *sensor)
{
	VeVariant v;
	struct VeItem *root = sensor->root;
	char prefix[VE_MAX_UID_SIZE];

	/* App info */
	sensor->processName = veItemCreateBasic(root, "Mgmt/ProcessName", veVariantStr(&v, pltProgramName()));
	sensor->processVersion = veItemCreateBasic(root, "Mgmt/ProcessVersion", veVariantStr(&v, pltProgramVersion()));
	sensor->connection = veItemCreateBasic(root, "Mgmt/Connection", veVariantStr(&v, sensor->ifaceName));

	veItemCreateBasic(root, "Connected", veVariantUn32(&v, veTrue));
	veItemCreateBasic(root, "DeviceInstance", veVariantUn32(&v, sensor->instance));
	sensor->statusItem = createEnumItem(sensor, "Status", veVariantUn32(&v, SENSOR_STATUS_NOT_CONNECTED), &statusDef, NULL);

	if (sensor->sensorType == SENSOR_TYPE_TANK) {
		struct TankSensor *tank = (struct TankSensor *) sensor;

		veItemCreateProductId(root, VE_PROD_ID_TANK_SENSOR_INPUT);
		veItemCreateBasic(root, "ProductName", veVariantStr(&v, veProductGetName(VE_PROD_ID_TANK_SENSOR_INPUT)));

		tank->levelItem = veItemCreateQuantity(root, "Level", veVariantInvalidType(&v, VE_UN32), &veUnitPercentage);
		tank->remaingItem = veItemCreateQuantity(root, "Remaining", veVariantInvalidType(&v, VE_FLOAT), &veUnitVolume);

		snprintf(prefix, sizeof(prefix), "Settings/Tank/%d", sensor->number);
		tank->capacityItem = createSettingsProxy(sensor, prefix, "Capacity", veVariantFmt, &veUnitVolume, &tankCapacityProps);
		tank->fluidTypeItem = createSettingsProxy(sensor, prefix, "FluidType", veVariantEnumFmt, &fluidTypeDef, &tankFluidType);
		tank->standardItem = createSettingsProxy(sensor, prefix, "Standard", veVariantEnumFmt, &standardDef, &tankStandardProps);

		sensor->function = createFunctionProxy(sensor, "/Settings/AnalogInput/Resistive/%d");

	} else if (sensor->sensorType == SENSOR_TYPE_TEMP) {
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

static void tankInit(AnalogSensor *sensor)
{
	SensorDbusInterface *dbus = &sensor->interface.dbus;
	FilerIirLpf *lpf = &sensor->interface.sigCond.filterIirLpf;

	static int tankNum = 1;

	lpf->FF = TANK_SENSOR_IIR_LPF_FF_VALUE;
	lpf->fc = TANK_SENSOR_CUTOFF_FREQ;
	lpf->last = HUGE_VALF;

	snprintf(dbus->service, sizeof(dbus->service),
			 "com.victronenergy.tank.builtin_adc%d", sensor->interface.adcPin);

	snprintf(sensor->ifaceName, sizeof(sensor->ifaceName),
			 "Tank Level sensor input %d", tankNum);

	sensor->number = tankNum;
	tankNum++;
}

static void temperatureInit(AnalogSensor *sensor)
{
	SensorDbusInterface *dbus = &sensor->interface.dbus;
	FilerIirLpf *lpf = &sensor->interface.sigCond.filterIirLpf;

	static int tempNum = 1;

	lpf->FF = TEMPERATURE_SENSOR_IIR_LPF_FF_VALUE;
	lpf->fc = TEMPERATURE_SENSOR_CUTOFF_FREQ;
	lpf->last = HUGE_VALF;

	snprintf(dbus->service, sizeof(dbus->service),
			 "com.victronenergy.temperature.builtin_adc%d", sensor->interface.adcPin);

	snprintf(sensor->ifaceName, sizeof(sensor->ifaceName),
			 "Temperature sensor input %d", tempNum);

	sensor->number = tempNum;
	tempNum++;
}

/**
 * @brief hook the sensor items to their dbus services
 * @param devfd - file descriptor of ADC device sysfs directory
 * @param pin - ADC pin number
 * @param scale - ADC scale in volts / unit
 * @param type - type of sensor
 * @return Pointer to sensor struct
 */
AnalogSensor *sensorCreate(int devfd, int pin, float scale, SensorType type)
{
	AnalogSensor *sensor;
	static un8 instance = 20;

	if (sensorCount == MAX_SENSORS)
		return NULL;

	if (type == SENSOR_TYPE_TANK)
		sensor = calloc(1, sizeof(struct TankSensor));
	else if (type == SENSOR_TYPE_TEMP)
		sensor = calloc(1, sizeof(struct TemperatureSensor));
	else
		return NULL;

	if (!sensor)
		return NULL;

	sensors[sensorCount++] = sensor;

	sensor->interface.devfd = devfd;
	sensor->interface.adcPin = pin;
	sensor->interface.adcScale = scale;
	sensor->sensorType = type;
	sensor->instance = instance++;
	sensor->root = veItemAlloc(NULL, "");

	if (sensor->sensorType == SENSOR_TYPE_TANK)
		tankInit(sensor);
	else if (sensor->sensorType == SENSOR_TYPE_TEMP)
		temperatureInit(sensor);

	createItems(sensor);

	return sensor;
}

/**
 * @brief process the tank level sensor adc data
 * @param sensor - pointer to the sensor struct
 * @return Boolean status veTrue - success, veFalse - fail
 */
static veBool updateTank(AnalogSensor *sensor)
{
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

	if (sensor->interface.adcSample > 1.4) {
		// Sensor status: error - not connected
		status = SENSOR_STATUS_NOT_CONNECTED;
	// this condition applies only for the US standard
	} else if (standard && (sensor->interface.adcSample < 0.15)) {
		// Sensor status: error - short circuited
		status = SENSOR_STATUS_SHORT;
	} else {
		// calculate the resistance of the tank level sensor from the adc pin sample
		float vdiff = TANK_SENS_VREF - sensor->interface.adcSample;
		float r2 = TANK_SENS_R1 * sensor->interface.adcSample / vdiff;

		// check the integrity of the resistance
		if (r2 > 0) { // calculate the tank level
			if (standard == TANK_STANDARD_US) { // tank level calculation in the case it is an American standard sensor
				level = (r2 - USA_MIN_TANK_LEVEL_RESISTANCE) / (USA_MAX_TANK_LEVEL_RESISTANCE - USA_MIN_TANK_LEVEL_RESISTANCE);
				if (level < 0)
					level = 0;
				level = 1 - level;
			} else { // tank level calculation in the case it is a European standard sensor
				level = r2 / EUR_MAX_TANK_LEVEL_RESISTANCE;
			}

			// clamp at 100%
			if (level > 1)
				level = 1;

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
 * @brief process the temperature sensor adc data
 * @param sensor - pointer to the sensor struct
 * @return Boolean status veTrue-success, veFalse-fail
 */
static veBool updateTemperature(AnalogSensor *sensor)
{
	float tempC, offset, scale;
	SensorStatus status;
	float adcSample = sensor->interface.adcSample;
	struct TemperatureSensor *temperature = (struct TemperatureSensor *) sensor;
	VeVariant v;

	if (!veVariantIsValid(veItemLocalValue(temperature->offsetItem, &v)))
		return veFalse;
	offset = v.value.Float;

	if (!veVariantIsValid(veItemLocalValue(temperature->scaleItem, &v)))
		return veFalse;
	scale = v.value.Float;

	if (adcSample > TEMP_SENS_MIN_ADCIN && adcSample < TEMP_SENS_MAX_ADCIN) {
		// calculate the output of the LM335 temperature sensor from the adc pin sample
		float vSense = adcSample * TEMP_SENS_V_RATIO;
		// convert from Kelvin to Celsius
		tempC = 100 * vSense - 273;
		// Signal scale correction
		tempC *= scale;
		// Signal offset correction
		tempC += offset;

		status = SENSOR_STATUS_OK;
	} else if (adcSample > TEMP_SENS_MAX_ADCIN) {
		// open circuit error
		status = SENSOR_STATUS_NOT_CONNECTED;
	} else if (adcSample < TEMP_SENS_S_C_ADCIN ) {
		// short circuit error
		status = SENSOR_STATUS_SHORT;
	} else if (adcSample > TEMP_SENS_INV_PLRTY_ADCIN_LB && adcSample < TEMP_SENS_INV_PLRTY_ADCIN_HB) {
		// lm335 probably connected in reverse polarity
		status = SENSOR_STATUS_REVERSE_POLARITY;
	} else {
		// low temperature or unknown error
		status = SENSOR_STATUS_UNKNOWN;
	}

	veItemOwnerSet(sensor->statusItem, veVariantUn32(&v, status));
	if (status == SENSOR_STATUS_OK)
		veItemOwnerSet(temperature->temperatureItem, veVariantSn32(&v, tempC));
	else
		veItemInvalidate(temperature->temperatureItem);

	return veTrue;
}

static void sensorDbusConnect(AnalogSensor *sensor)
{
	sensor->dbus = veDbusConnectString(veDbusGetDefaultConnectString());
	if (!sensor->dbus) {
		logE(sensor->interface.dbus.service, "dbus connect failed");
		pltExit(1);
	}
	sensor->interface.dbus.connected = veTrue;

	veDbusItemInit(sensor->dbus, sensor->root);
	veDbusChangeName(sensor->dbus, sensor->interface.dbus.service);

	logI(sensor->interface.dbus.service, "connected to dbus");
}

static void sensorDbusDisconnect(AnalogSensor *sensor)
{
	veDbusDisconnect(sensor->dbus);
	sensor->interface.dbus.connected = veFalse;
}

void sensorTick(void)
{
	int i;
	VeVariant v;

	// first read fast all the analog inputs and mark which read is valid
	// We reading always the same number of analog inputs to try to keep the timing of the system constant.
	for (i = 0; i < sensorCount; i++) {
		AnalogSensor *sensor = sensors[i];
		un32 val;

		sensor->valid = adcRead(&val, sensor);
		if (sensor->valid)
			sensor->interface.adcSample = val * sensor->interface.adcScale;
	}

	// Now handle the adc read to update the sensor
	for (i = 0; i < sensorCount; i++) {
		AnalogSensor *sensor = sensors[i];
		FilerIirLpf *filter = &sensor->interface.sigCond.filterIirLpf;

		if (!sensor->valid)
			continue;

		// filter the input ADC sample and store it in adc var
		sensor->interface.adcSample = adcFilter(sensor->interface.adcSample, filter);

		// check if the sensor function - if it needed at all?
		veItemLocalValue(sensor->function, &v);
		if (!veVariantIsValid(&v))
			continue;

		switch (v.value.UN32) {
		case SENSOR_FUNCTION_DEFAULT:
			// check if dbus is disconnected and connect it
			if (!sensor->interface.dbus.connected)
				sensorDbusConnect(sensor);

			switch (sensor->sensorType) {
			case SENSOR_TYPE_TANK:
				updateTank(sensor);
				break;

			case SENSOR_TYPE_TEMP:
				updateTemperature(sensor);
				break;
			}
			break;

		case SENSOR_FUNCTION_NONE:
		default:
			// check if dbus is connected and disconnect it
			if (sensor->interface.dbus.connected)
				sensorDbusDisconnect(sensor);
			break;
		}
	}
}
