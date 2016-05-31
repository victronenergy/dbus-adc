#include <stdio.h>
#include <stdlib.h>

#include "values.h"
#include "version.h"
#include "adc.h"
#include "task.h"

#include <velib/types/ve_values.h>
#include <velib/platform/console.h>
#include <velib/types/variant_print.h>
#include <velib/types/ve_item.h>
#include <velib/types/ve_item_def.h>
#include <velib/types/ve_dbus_item.h>
#include <velib/utils/ve_logger.h>

#include <velib/platform/plt.h>

void taskInit(void)
{
#ifdef DEBUGING_APP
        logI("DebugMsg", "init tasks");
#endif

    values_dbus_service_connectSettings();
    for(analog_sensors_index_t sensor_index = 0; sensor_index < num_of_analog_sensors; sensor_index++)
    {
        valuesInit(sensor_index);
    }
    for(analog_sensors_index_t sensor_index = 0; sensor_index < num_of_analog_sensors; sensor_index++)
    {
        sensors_dbusInit(sensor_index);
    }
    pltInterruptEnable();
}

void taskUpdate(void)
{

}

void taskTick(void)
{
    valuesTick();
}

static char const version[] = VERSION_STR;
char const *pltProgramVersion(void)
{
	return version;
}
