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
#include <velib/utils/ve_item_utils.h>

#include <velib/types/ve_values.h>
#include <velib/types/ve_item_def.h>

#include "values.h"
#include "sensors.h"

// timer divider for the app ticking
static un16 values_task_timer = VALUES_TASK_INTERVAL;

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
 * @brief add_sensor
 * @param devfd - file descriptor of ADC device sysfs directory
 * @param pin - ADC pin number
 * @param scale - ADC scale in volts / unit
 * @param type - type of sensor
 * @return 0 on success, -1 on error
 */
int add_sensor(int devfd, int pin, float scale, int type)
{
	analog_sensor_t *sensor;
	VeVariant v;

	sensor = sensor_init(devfd, pin, scale, type);
	if (!sensor)
		return -1;

	/* App info */
	sensor->processName = veItemCreateBasic(&sensor->root, "Mgmt/ProcessName", veVariantStr(&v, pltProgramName()));
	sensor->processVersion = veItemCreateBasic(&sensor->root, "Mgmt/ProcessVersion", veVariantStr(&v, pltProgramVersion()));
	sensor->connection = veItemCreateBasic(&sensor->root, "Mgmt/Connection", veVariantStr(&v, sensor->iface_name));

	values_dbus_service_addSettings(sensor);

	return 0;
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

