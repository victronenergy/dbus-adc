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
#include "serial_hal.h"
#include "sensors.h"
#include "dev_reg.h"

#define MODULE                      "VALUES"


/** dbus connection */
struct VeDbus *dbus[num_of_analog_sensors];

/** The root of the tree to locate values and format them etc */
static VeItem root[num_of_analog_sensors];

static VeItem processName[num_of_analog_sensors];
static VeItem processVersion[num_of_analog_sensors];
static VeItem connection[num_of_analog_sensors];

static un16 values_task_timer;
static un32 timeout;

static VeVariantUnitFmt none = {0, ""};

static char const *interface(void)
{
    return "ADC";
}

dbus_info_t     dbus_info;

const char      *settingsService = "com.victronenergy.settings";
VeItem          *input_root;
VeItem			*consumer;

VeItem * getConsumerRoot(void)
{
    return(consumer);
}

void valueChanged(struct VeItem *item)
{
    char buf[100];
    VeVariantUnitFmt fmt;
    fmt.decimals = 2;
    fmt.unit = "X";

    veVariantSprintf(&item->variant, &fmt, buf, sizeof(buf));
    printf("changed %s\n", buf);
}

void valuesInit(analog_sensors_index_t sensor_index)
{
    timeout = devReg.timeOut * 20;
    values_task_timer = VALUES_TASK_INTERVAL;
    sensor_init(&root[sensor_index], sensor_index);

    /* App info */
    veItemAddChildByUid(&root[sensor_index], "Mgmt/ProcessName", &processName[sensor_index]);
    veItemAddChildByUid(&root[sensor_index], "Mgmt/ProcessVersion", &processVersion[sensor_index]);
    veItemAddChildByUid(&root[sensor_index], "Mgmt/Connection", &connection[sensor_index]);
    veItemSetFmt(&processName[sensor_index], veVariantFmt, &none);
    veItemSetFmt(&processVersion[sensor_index], veVariantFmt, &none);
    veItemSetFmt(&connection[sensor_index], veVariantFmt, &none);

}

void values_dbus_service_connectSettings(void)
{
    input_root = veValueTree();

    if (! (dbus_info.connect = veDbusGetDefaultBus()) ) {
        printf("dbus connection failed\n");
        pltExit(5);
    }
    /* Listen to D-Bus.. */
    veDbusSetListeningDbus(dbus_info.connect);

    /* Connect to settings service */
    consumer = veItemGetOrCreateUid(input_root, settingsService);
    if (!veDbusAddRemoteService(settingsService, consumer, veTrue))
    {
        logE("task", "veDbusAddRemoteService failed");
        pltExit(1);
    }
}

void values_dbus_service_addSettings(analog_sensor_t * sensor)
{
    for(int i = 0; i < NUM_OF_SENSOR_SETTINGS_PARAMS; i++)
    {
        /* Connect to the dbus */
        if (! (sensor->dbus_info[i].connect = veDbusGetDefaultBus()) )
        {
           printf("dbus connection failed\n");
           pltExit(5);
        }
        /* Listen to D-Bus.. */
        veDbusSetListeningDbus(sensor->dbus_info[i].connect);

        /* Create an item pointing to our new setting */
        sensor->dbus_info[i].value = veItemGetOrCreateUid(consumer, sensor->dbus_info[i].path);
        /* Listen to changes in the item. Once the item is created 'valueChanged' will be called */
        //veItemSetChanged(sensor->dbus_info[i].value, valueChanged);

        /* Set the properties of the new settings */
        values_range_t  values_range;
        veVariantFloat(&values_range.def, sensor->dbus_info[i].def);
        veVariantFloat(&values_range.max, sensor->dbus_info[i].max);
        veVariantFloat(&values_range.min, sensor->dbus_info[i].min);

        if (!veDBusAddLocalSetting(sensor->dbus_info[i].value,"f",&values_range.def,&values_range.min, &values_range.max,veFalse))
        {
           logE("task", "veDBusAddLocalSetting failed");
           pltExit(1);
        }
        veItemValue(sensor->dbus_info[i].value, &sensor->dbus_info[i].value->variant);
    }
}

/** 50ms tick to invalidate items if there are not received in time */
void valuesTick(void)
{
    if(!(--values_task_timer))
    {
        values_task_timer = VALUES_TASK_INTERVAL;

        // update the settings
        for(analog_sensors_index_t sensor_index = 0; sensor_index < num_of_analog_sensors; sensor_index++)
        {
            veDbusItemUpdate(dbus[sensor_index]);
        }

        sensors_handle();
    }
}

void sensors_dbusConnect(analog_sensor_t * sensor, analog_sensors_index_t sensor_index)
{
    VeVariant variant;

    dbus[sensor_index] = veDbusConnect(DBUS_BUS_SYSTEM);
    if (!dbus[sensor_index])
    {
        logE(sensor->interface.dbus.service, "dbus connect failed");
        pltExit(1);
    }
    /* Device found */
    timeout = 0;

    veDbusItemInit(dbus[sensor_index], &root[sensor_index]);
    veDbusChangeName(dbus[sensor_index], sensor->interface.dbus.service);

    logI(sensor->interface.dbus.service, "connected to dbus");

    veItemOwnerSet(&processName[sensor_index], veVariantStr(&variant, pltProgramName()));
    veItemOwnerSet(&processVersion[sensor_index], veVariantStr(&variant, pltProgramVersion()));
    veItemOwnerSet(&connection[sensor_index], veVariantStr(&variant, interface()));
}

/* Stop service if connection lost */
void sensors_dbusDisconnect(void)
{
    logE(MODULE, "connection timeout");
    pltExit(130);
}

