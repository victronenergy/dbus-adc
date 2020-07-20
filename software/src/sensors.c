#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include <velib/platform/plt.h>
#include <velib/types/ve_dbus_item.h>
#include <velib/types/ve_item.h>
#include <velib/utils/ve_item_utils.h>
#include <velib/utils/ve_logger.h>
#include <velib/vecan/products.h>

#include "sensors.h"

#define INSTANCE_BASE						20

#define MAX_SENSORS							8
#define SAMPLE_RATE							10

// defines for the tank level sensor analog front end parameters
#define TANK_SENS_VREF						5.0
#define TANK_SENS_R1						680.0 // ohms
#define TANK_MAX_RESISTANCE					264 // ohms

#define EUR_MIN_TANK_LEVEL_RESISTANCE		0 // ohms
#define EUR_MAX_TANK_LEVEL_RESISTANCE		180 // ohms
#define USA_MIN_TANK_LEVEL_RESISTANCE		240 // ohms
#define USA_MAX_TANK_LEVEL_RESISTANCE		30 // ohms

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
static VeVariantUnitFmt unitRes0Dec = {0, "ohm"};
static VeVariantUnitFmt veUnitVolts = {2, "V"};

/* Common */
static struct VeSettingProperties functionProps = {
	.type = VE_SN32,
	.def.value.SN32 = SENSOR_FUNCTION_NONE,
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

static struct VeSettingProperties emptyStrType = {
	.type = VE_HEAP_STR,
	.def.value.Ptr = "",
};

static struct VeSettingProperties tankResistanceProps = {
	.type = VE_SN32,
	.max.value.SN32 = TANK_MAX_RESISTANCE,
};

VeVariantEnumFmt const statusDef =
		VE_ENUM_DEF("Ok", "Disconnected",  "Short circuited",
					"Reverse polarity", "Unknown");
VeVariantEnumFmt const fluidTypeDef =
		VE_ENUM_DEF("Fuel", "Fresh water", "Waste water", "Live well",
					"Oil", "Black water (sewage)");
VeVariantEnumFmt const standardDef =
		VE_ENUM_DEF("European", "American", "Custom");
VeVariantEnumFmt const functionDef = VE_ENUM_DEF("None", "Default");

static struct VeItem *createEnumItem(AnalogSensor *sensor, const char *id,
		VeVariant *initial, const VeVariantEnumFmt *fmt, VeItemSetterFun *cb)
{
	struct VeItem *item = veItemCreateBasic(sensor->root, id, initial);
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
static struct VeItem *createSettingsProxy(struct VeItem *root,
		const char *prefix, char *settingsId, VeItemValueFmt *fmt,
		const void *fmtCtx, struct VeSettingProperties *props, char *serviceId)
{
	struct VeItem *localSettings = getLocalSettings();
	struct VeItem *sensorItem;

	if (serviceId == NULL)
		serviceId = settingsId;

	sensorItem = veItemCreateSettingsProxyId(localSettings, prefix, root,
			settingsId, fmt, fmtCtx, props, serviceId);
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
	return createSettingsProxy(sensor->root, prefix, "Function2", veVariantEnumFmt, &functionDef, &functionProps, "Function");
}

/*
 * Keep the settings in sync. The gui shouldn't allow changing the
 * resistance settings when not in custom mode, but external might
 * try. So always makes sure they match.
 */
static void onTankResConfigChanged(struct VeItem *item)
{
	struct TankSensor *tank = (struct TankSensor *) veItemCtx(item)->ptr;
	VeVariant standard, v;
	sn32 tankEmptyR, tankFullR;
	struct VeItem *settingsItem;

	if (!veVariantIsValid(veItemLocalValue(tank->standardItem, &standard)))
		return;

	switch (standard.value.SN32) {
	case TANK_STANDARD_EU:
		tankEmptyR = EUR_MIN_TANK_LEVEL_RESISTANCE;
		tankFullR = EUR_MAX_TANK_LEVEL_RESISTANCE;
		break;
	case TANK_STANDARD_US:
		tankEmptyR = USA_MIN_TANK_LEVEL_RESISTANCE;
		tankFullR = USA_MAX_TANK_LEVEL_RESISTANCE;
		break;
	default:
		return;
	}

	settingsItem = (struct VeItem *) veItemCtxSet(tank->emptyRItem);
	if (veVariantIsValid(veItemLocalValue(settingsItem, &v)) &&
		v.value.SN32 != tankEmptyR)
		veItemSet(settingsItem, veVariantSn32(&v, tankEmptyR));

	settingsItem = veItemCtxSet(tank->fullRItem);
	if (veVariantIsValid(veItemLocalValue(settingsItem, &v)) &&
		v.value.SN32 != tankFullR)
		veItemSet(settingsItem, veVariantSn32(&v, tankFullR));
}

static void onTankShapeChanged(struct VeItem *item)
{
	struct TankSensor *tank = (struct TankSensor *) veItemCtx(item)->ptr;
	VeVariant shape;
	const char *map;
	int i;

	if (!veVariantIsValid(veItemLocalValue(tank->shapeItem, &shape))) {
		logE("tank", "invalid shape value");
		goto reset;
	}

	map = shape.value.Ptr;

	if (!map[0])
		goto reset;

	tank->shapeMap[0][0] = 0;
	tank->shapeMap[0][1] = 0;
	i = 1;

	while (i < TANK_SHAPE_MAX_POINTS) {
		unsigned int s, l;

		if (sscanf(map, "%u:%u", &s, &l) < 2) {
			logE("tank", "malformed shape spec");
			goto reset;
		}

		if (s < 1 || s > 99 || l < 1 || l > 99) {
			logE("tank", "shape level out of range 1-99");
			goto reset;
		}

		if (s <= tank->shapeMap[i - 1][0] ||
			l <= tank->shapeMap[i - 1][1]) {
			logE("tank", "shape level non-increasing");
			goto reset;
		}

		tank->shapeMap[i][0] = s / 100.0;
		tank->shapeMap[i][1] = l / 100.0;
		i++;

		map = strchr(map, ',');
		if (!map)
			break;

		map++;
	}

	tank->shapeMap[i][0] = 1;
	tank->shapeMap[i][1] = 1;
	tank->shapeMapLen = i + 1;

	return;

reset:
	tank->shapeMapLen = 0;
}

static void createItems(AnalogSensor *sensor, const char *devid)
{
	VeVariant v;
	struct VeItem *root = sensor->root;
	char prefix[VE_MAX_UID_SIZE];

	snprintf(prefix, sizeof(prefix), "Settings/Devices/%s", devid);

	/* App info */
	veItemCreateBasic(root, "Mgmt/ProcessName",
					  veVariantStr(&v, pltProgramName()));
	veItemCreateBasic(root, "Mgmt/ProcessVersion",
					  veVariantStr(&v, pltProgramVersion()));
	veItemCreateBasic(root, "Mgmt/Connection",
					  veVariantStr(&v, sensor->ifaceName));

	veItemCreateBasic(root, "Connected", veVariantUn32(&v, veTrue));
	veItemCreateBasic(root, "DeviceInstance",
					  veVariantUn32(&v, sensor->instance));
	sensor->statusItem = createEnumItem(sensor, "Status",
			veVariantUn32(&v, SENSOR_STATUS_NOT_CONNECTED), &statusDef, NULL);

	createSettingsProxy(root, prefix, "CustomName", veVariantFmt, &veUnitNone,
						&emptyStrType, NULL);

	if (sensor->sensorType == SENSOR_TYPE_TANK) {
		struct TankSensor *tank = (struct TankSensor *) sensor;

		veItemCreateProductId(root, VE_PROD_ID_TANK_SENSOR_INPUT);
		veItemCreateBasic(root, "ProductName", veVariantStr(&v, veProductGetName(VE_PROD_ID_TANK_SENSOR_INPUT)));

		tank->levelItem = veItemCreateQuantity(root, "Level",
				veVariantInvalidType(&v, VE_UN32), &veUnitPercentage);
		tank->remaingItem = veItemCreateQuantity(root, "Remaining",
				veVariantInvalidType(&v, VE_FLOAT), &veUnitVolume);
		sensor->rawValueItem = veItemCreateQuantity(root, "Resistance",
				veVariantInvalidType(&v, VE_FLOAT), &unitRes0Dec);

		tank->capacityItem = createSettingsProxy(root, prefix, "Capacity",
				veVariantFmt, &veUnitVolume, &tankCapacityProps, NULL);
		tank->fluidTypeItem = createSettingsProxy(root, prefix, "FluidType2",
				veVariantEnumFmt, &fluidTypeDef, &tankFluidType, "FluidType");

		/* The callback will make sure these are kept in sync */
		tank->emptyRItem = createSettingsProxy(root, prefix, "ResistanceWhenEmpty",
				veVariantFmt, &unitRes0Dec, &tankResistanceProps,  NULL);
		veItemCtx(tank->emptyRItem)->ptr = tank;
		veItemSetChanged(tank->emptyRItem, onTankResConfigChanged);

		tank->fullRItem = createSettingsProxy(root, prefix, "ResistanceWhenFull",
				veVariantFmt, &unitRes0Dec, &tankResistanceProps, NULL);
		veItemCtx(tank->fullRItem)->ptr = tank;
		veItemSetChanged(tank->fullRItem, onTankResConfigChanged);

		tank->standardItem = createSettingsProxy(root, prefix, "Standard2",
				veVariantEnumFmt, &standardDef, &tankStandardProps, "Standard");
		veItemCtx(tank->standardItem)->ptr = tank;
		veItemSetChanged(tank->standardItem, onTankResConfigChanged);

		tank->shapeItem = createSettingsProxy(root, prefix, "Shape",
				veVariantFmt, &veUnitNone, &emptyStrType, NULL);
		veItemCtx(tank->shapeItem)->ptr = tank;
		veItemSetChanged(tank->shapeItem, onTankShapeChanged);

		sensor->function = createFunctionProxy(sensor, "Settings/AnalogInput/Resistive/%d");

	} else if (sensor->sensorType == SENSOR_TYPE_TEMP) {
		struct TemperatureSensor *temp = (struct TemperatureSensor *) sensor;

		veItemCreateProductId(root, VE_PROD_ID_TEMPERATURE_SENSOR_INPUT);
		veItemCreateBasic(root, "ProductName", veVariantStr(&v, veProductGetName(VE_PROD_ID_TEMPERATURE_SENSOR_INPUT)));

		temp->temperatureItem = veItemCreateQuantity(root, "Temperature",
				veVariantInvalidType(&v, VE_SN32), &veUnitCelsius0Dec);

		temp->scaleItem = createSettingsProxy(root, prefix, "Scale",
				veVariantFmt, &veUnitNone, &scaleProps, NULL);
		temp->offsetItem = createSettingsProxy(root, prefix, "Offset",
				veVariantFmt, &veUnitNone, &offsetProps, NULL);
		createSettingsProxy(root, prefix, "TemperatureType2",
				veVariantFmt, &veUnitNone, &temperatureType, "TemperatureType");

		sensor->function = createFunctionProxy(sensor, "Settings/AnalogInput/Temperature/%d");
	}
}

static void tankInit(AnalogSensor *sensor, const char *devid)
{
	SensorDbusInterface *dbus = &sensor->interface.dbus;
	FilerIirLpf *lpf = &sensor->interface.sigCond.filterIirLpf;

	static int tankNum = 1;

	lpf->FF = TANK_SENSOR_IIR_LPF_FF_VALUE;
	lpf->fc = TANK_SENSOR_CUTOFF_FREQ;
	lpf->last = HUGE_VALF;

	snprintf(dbus->service, sizeof(dbus->service),
			 "com.victronenergy.tank.%s", devid);

	sensor->number = tankNum;
	tankNum++;
}

static void temperatureInit(AnalogSensor *sensor, const char *devid)
{
	SensorDbusInterface *dbus = &sensor->interface.dbus;
	FilerIirLpf *lpf = &sensor->interface.sigCond.filterIirLpf;

	static int tempNum = 1;

	lpf->FF = TEMPERATURE_SENSOR_IIR_LPF_FF_VALUE;
	lpf->fc = TEMPERATURE_SENSOR_CUTOFF_FREQ;
	lpf->last = HUGE_VALF;

	snprintf(dbus->service, sizeof(dbus->service),
			 "com.victronenergy.temperature.%s", devid);

	sensor->number = tempNum;
	tempNum++;
}

/**
 * @brief hook the sensor items to their dbus services
 * @param s - struct with sensor parameters
 * @return Pointer to sensor struct
 */
AnalogSensor *sensorCreate(SensorInfo *s)
{
	AnalogSensor *sensor;
	char devid[64];
	char *p;

	if (sensorCount == MAX_SENSORS)
		return NULL;

	if (s->type == SENSOR_TYPE_TANK)
		sensor = calloc(1, sizeof(struct TankSensor));
	else if (s->type == SENSOR_TYPE_TEMP)
		sensor = calloc(1, sizeof(struct TemperatureSensor));
	else
		return NULL;

	if (!sensor)
		return NULL;

	snprintf(devid, sizeof(devid), "%s_%d", s->dev, s->pin);
	for (p = devid; *p; p++)
		if (!isalnum(*p))
			*p = '_';

	sensors[sensorCount++] = sensor;

	sensor->interface.devfd = s->devfd;
	sensor->interface.adcPin = s->pin;
	sensor->interface.adcScale = s->scale;
	sensor->sensorType = s->type;
	sensor->instance =
		veDbusGetVrmDeviceInstance(devid, "analog", INSTANCE_BASE);
	sensor->root = veItemAlloc(NULL, "");

	if (s->label[0])
		snprintf(sensor->ifaceName, sizeof(sensor->ifaceName), "%s", s->label);
	else
		snprintf(sensor->ifaceName, sizeof(sensor->ifaceName), "Analog input %s:%d", s->dev, s->pin);

	if (sensor->sensorType == SENSOR_TYPE_TANK)
		tankInit(sensor, devid);
	else if (sensor->sensorType == SENSOR_TYPE_TEMP)
		temperatureInit(sensor, devid);

	createItems(sensor, devid);

	return sensor;
}

/**
 * @brief process the tank level sensor adc data
 * @param sensor - pointer to the sensor struct
 * @return Boolean status veTrue - success, veFalse - fail
 */
static void updateTank(AnalogSensor *sensor)
{
	float level, capacity;
	SensorStatus status = SENSOR_STATUS_UNKNOWN;
	VeVariant v;
	struct TankSensor *tank = (struct TankSensor *) sensor;
	float tankEmptyR, tankFullR, tankR, tankRRaw, tankMinR;
	float vMeas = sensor->interface.adcSample;
	float vMeasRaw = sensor->interface.adcSampleRaw;
	int i;

	tankR = vMeas / (TANK_SENS_VREF - vMeas) * TANK_SENS_R1;
	tankRRaw = vMeasRaw / (TANK_SENS_VREF - vMeasRaw) * TANK_SENS_R1;

	veItemOwnerSet(sensor->rawValueItem, veVariantFloat(&v, tankRRaw));

	if (!veVariantIsValid(veItemLocalValue(tank->emptyRItem, &v)))
		goto errorState;
	tankEmptyR = v.value.SN32;

	if (!veVariantIsValid(veItemLocalValue(tank->fullRItem, &v)))
		goto errorState;
	tankFullR = v.value.SN32;

	if (!veVariantIsValid(veItemLocalValue(tank->capacityItem, &v)))
		goto errorState;
	capacity = v.value.Float;

	/* prevent division by zero, configuration issue */
	if (tankFullR == tankEmptyR)
		goto errorState;

	/* If the resistance is higher then the max supported; assume not connected */
	if (tankR > fmax(tankEmptyR, tankFullR) * 1.05) {
		status = SENSOR_STATUS_NOT_CONNECTED;
		goto errorState;
	}

	/* Detect short, but only if not allow by the spec and a bit significant */
	tankMinR = fmin(tankEmptyR, tankFullR);
	if (tankMinR > 20 && tankR < 0.9 * tankMinR) {
		status = SENSOR_STATUS_SHORT;
		goto errorState;
	}

	status = SENSOR_STATUS_OK;
	level = (tankR - tankEmptyR) / (tankFullR - tankEmptyR);
	if (level < 0)
		level = 0;
	if (level > 1)
		level = 1;

	for (i = 1; i < tank->shapeMapLen; i++) {
		if (tank->shapeMap[i][0] >= level) {
			float s0 = tank->shapeMap[i - 1][0];
			float s1 = tank->shapeMap[i    ][0];
			float l0 = tank->shapeMap[i - 1][1];
			float l1 = tank->shapeMap[i    ][1];
			level = l0 + (level - s0) / (s1 - s0) * (l1 - l0);
			break;
		}
	}

	VeVariant oldRemaining;
	float newRemaing = level * capacity;
	float minRemainingChange = capacity / 5000.0f;

	veItemLocalValue(tank->remaingItem, &oldRemaining);
	if (veVariantIsValid(&oldRemaining) &&
		fabsf(oldRemaining.value.Float - newRemaing) < minRemainingChange)
		return;

	veItemOwnerSet(sensor->statusItem, veVariantUn32(&v, status));
	veItemOwnerSet(tank->levelItem, veVariantUn32(&v, 100 * level));
	veItemOwnerSet(tank->remaingItem, veVariantFloat(&v, level * capacity));

	return;

errorState:
	veItemOwnerSet(sensor->statusItem, veVariantUn32(&v, status));
	veItemInvalidate(tank->levelItem);
	veItemInvalidate(tank->remaingItem);
}

/**
 * @brief process the temperature sensor adc data
 * @param sensor - pointer to the sensor struct
 * @return Boolean status veTrue-success, veFalse-fail
 */
static void updateTemperature(AnalogSensor *sensor)
{
	float tempC, offset, scale;
	SensorStatus status = SENSOR_STATUS_UNKNOWN;
	float adcSample = sensor->interface.adcSample;
	float adcSampleRaw = sensor->interface.adcSampleRaw;
	struct TemperatureSensor *temperature = (struct TemperatureSensor *) sensor;
	VeVariant v;

	// calculate the output of the LM335 temperature sensor from the adc pin sample
	float vSense = adcSample * TEMP_SENS_V_RATIO;
	float vSenseRaw = adcSampleRaw * TEMP_SENS_V_RATIO;

	if (!veVariantIsValid(veItemLocalValue(temperature->offsetItem, &v)))
		goto updateState;
	offset = v.value.Float;

	if (!veVariantIsValid(veItemLocalValue(temperature->scaleItem, &v)))
		goto updateState;
	scale = v.value.Float;

	if (adcSample > TEMP_SENS_MIN_ADCIN && adcSample < TEMP_SENS_MAX_ADCIN) {
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
	} else if (adcSample > TEMP_SENS_INV_PLRTY_ADCIN_LB &&
			   adcSample < TEMP_SENS_INV_PLRTY_ADCIN_HB) {
		// lm335 probably connected in reverse polarity
		status = SENSOR_STATUS_REVERSE_POLARITY;
	} else {
		// low temperature or unknown error
		status = SENSOR_STATUS_UNKNOWN;
	}

updateState:
	veItemOwnerSet(sensor->statusItem, veVariantUn32(&v, status));
	if (status == SENSOR_STATUS_OK)
		veItemOwnerSet(temperature->temperatureItem, veVariantSn32(&v, tempC));
	else
		veItemInvalidate(temperature->temperatureItem);
	veItemOwnerSet(sensor->rawValueItem, veVariantFloat(&v, vSenseRaw));
}

static void sensorDbusConnect(AnalogSensor *sensor)
{
	sensor->dbus = veDbusConnectString(veDbusGetDefaultConnectString());
	if (!sensor->dbus) {
		logE(sensor->interface.dbus.service, "dbus connect failed");
		pltExit(1);
	}

	veDbusItemInit(sensor->dbus, sensor->root);
	veDbusChangeName(sensor->dbus, sensor->interface.dbus.service);

	logI(sensor->interface.dbus.service, "connected to dbus");
}

void sensorTick(void)
{
	int i;
	VeVariant v;
	static int secCounter;
	veBool isSec = veFalse;

	if (++secCounter == 10) {
		isSec = veTrue;
		secCounter = 0;
	}

	/* Read the ADC values */
	for (i = 0; i < sensorCount; i++) {
		AnalogSensor *sensor = sensors[i];
		un32 val;

		sensor->valid = adcRead(&val, sensor);
		if (sensor->valid)
			sensor->interface.adcSampleRaw = val * sensor->interface.adcScale;
	}

	/* Handle ADC values */
	for (i = 0; i < sensorCount; i++) {
		AnalogSensor *sensor = sensors[i];
		FilerIirLpf *filter = &sensor->interface.sigCond.filterIirLpf;

		if (!sensor->valid)
			continue;

		/* filter the input ADC sample, high rate */
		sensor->interface.adcSample =
			adcFilter(sensor->interface.adcSampleRaw, filter);

		/* dbus update part can be at a lower rate */
		if (!isSec)
			continue;

		if (!veVariantIsValid(veItemLocalValue(sensor->function, &v)))
			continue;

		switch (v.value.SN32) {
		case SENSOR_FUNCTION_DEFAULT:
			if (!sensor->interface.dbus.connected) {
				sensorDbusConnect(sensor);
				sensor->interface.dbus.connected = veTrue;
			}

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
			if (sensor->interface.dbus.connected) {
				veDbusDisconnect(sensor->dbus);
				sensor->interface.dbus.connected = veFalse;
			}
			break;
		}
	}
}
