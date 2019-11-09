#include <stdio.h>
#include <stdlib.h>

#include <velib/base/base.h>
#include <velib/base/ve_string.h>
#include <velib/types/ve_item.h>
#include <velib/types/variant_print.h>
#include <velib/types/ve_dbus_item.h>
#include <velib/utils/ve_logger.h>
#include <velib/platform/console.h>
#include <velib/platform/plt.h>

#include <velib/types/ve_values.h>
#include <velib/types/ve_item_def.h>

#include "values.h"
#include "sensors.h"

// timer divider for the app ticking
static un16 values_task_timer = VALUES_TASK_INTERVAL;

static VeVariantUnitFmt none = {0, ""};

static int sensor_pins[] = {
	4, 6, 2, 5, 3,
};

static int sensor_types[] = {
	SENSOR_TYPE_TANK, SENSOR_TYPE_TANK, SENSOR_TYPE_TANK,
	SENSOR_TYPE_TEMP, SENSOR_TYPE_TEMP,
};

VeItem *consumer;

/**
 * @brief getConsumerRoot
 * @return a pointer to the dbus consumer item root
 */
VeItem *getConsumerRoot(void)
{
	return consumer;
}

/**
 * @brief valuesInit
 * @param sensor_index - the sensor index array number
 */
void valuesInit(int sensor_index)
{
	analog_sensor_t *sensor;

	sensor = sensor_init(sensor_pins[sensor_index],
						 ADC_VREF / ADC_MAX_COUNT,
						 sensor_types[sensor_index]);

	/* App info */
	veItemAddChildByUid(&sensor->root, "Mgmt/ProcessName", &sensor->processName);
	veItemAddChildByUid(&sensor->root, "Mgmt/ProcessVersion", &sensor->processVersion);
	veItemAddChildByUid(&sensor->root, "Mgmt/Connection", &sensor->connection);
	veItemSetFmt(&sensor->processName, veVariantFmt, &none);
	veItemSetFmt(&sensor->processVersion, veVariantFmt, &none);
	veItemSetFmt(&sensor->connection, veVariantFmt, &none);

	values_dbus_service_addSettings(sensor);
	sensors_dbusInit(sensor);
}

/**
 * @brief values_dbus_service_connectSettings
 */
void values_dbus_service_connectSettings(void)
{
	const char *settingsService = "com.victronenergy.settings";
	VeItem *input_root = veValueTree();
	struct VeDbus *dbus;

	if (!(dbus = veDbusGetDefaultBus())) {
		printf("dbus connection failed\n");
		pltExit(5);
	}
	/* Listen to D-Bus.. */
	veDbusSetListeningDbus(dbus);

	/* Connect to settings service */
	consumer = veItemGetOrCreateUid(input_root, settingsService);
	if (!veDbusAddRemoteService(settingsService, consumer, veTrue)) {
		logE("task", "veDbusAddRemoteService failed");
		pltExit(1);
	}
}

/**
 * @brief values_dbus_service_addSettings
 * @param sensor - the pointer to the sensor structure array element
 */
void values_dbus_service_addSettings(analog_sensor_t *sensor)
{
	for (int i = 0; i < NUM_OF_SENSOR_SETTINGS_PARAMS; i++) {
		/* Connect to the dbus */
		if (!(sensor->dbus_info[i].connect = veDbusGetDefaultBus())) {
			printf("dbus connection failed\n");
			pltExit(5);
		}
		/* Listen to D-Bus.. */
		veDbusSetListeningDbus(sensor->dbus_info[i].connect);
		/* Create an item pointing to our new setting */
		sensor->dbus_info[i].value = veItemGetOrCreateUid(consumer, sensor->dbus_info[i].path);
		/* Set the properties of the new settings */
		values_range_t values_range;
		veVariantFloat(&values_range.def, sensor->dbus_info[i].def);
		veVariantFloat(&values_range.max, sensor->dbus_info[i].max);
		veVariantFloat(&values_range.min, sensor->dbus_info[i].min);

		if (!veDBusAddLocalSetting(sensor->dbus_info[i].value, &values_range.def, &values_range.min, &values_range.max, veFalse)) {
			logE("task", "veDBusAddLocalSetting failed");
			pltExit(1);
		}
		veItemValue(sensor->dbus_info[i].value, &sensor->dbus_info[i].value->variant);
	}
}

/** 50ms tick to invalidate items if there are not received in time */
/**
 * @brief valuesTick - divide the tick frequency.
 * call to handle the sensors
 * call to update the dbus items
 */
void valuesTick(void)
{
	// division of the ticking frequency
	if (!(--values_task_timer)) {
		values_task_timer = VALUES_TASK_INTERVAL;
		// handle the sensors - sample the adc, check sensor status, filter raw data and process raw data
		sensors_handle();
	}
}

/**
 * @brief sensors_dbusConnect -connects sensor to dbus
 * @param sensor - pointer the the sensor structure array element
 */
void sensors_dbusConnect(analog_sensor_t *sensor)
{
	VeVariant variant;

	sensor->dbus = veDbusConnect(DBUS_BUS_SYSTEM);
	if (!sensor->dbus) {
		logE(sensor->interface.dbus.service, "dbus connect failed");
		pltExit(1);
	}
	sensor->interface.dbus.connected = veTrue;
	/* Device found */

	veDbusItemInit(sensor->dbus, &sensor->root);
	veDbusChangeName(sensor->dbus, sensor->interface.dbus.service);

	logI(sensor->interface.dbus.service, "connected to dbus");

	veItemOwnerSet(&sensor->processName, veVariantStr(&variant, pltProgramName()));
	veItemOwnerSet(&sensor->processVersion, veVariantStr(&variant, pltProgramVersion()));
	veItemOwnerSet(&sensor->connection, veVariantStr(&variant, sensor->iface_name));
}

/**
 * @brief sensors_dbusDisconnect
 * @param sensor - pointer the the sensor structure array element
 */
void sensors_dbusDisconnect(analog_sensor_t *sensor)
{
	veDbusDisconnect(sensor->dbus);
	sensor->interface.dbus.connected = veFalse;
}

