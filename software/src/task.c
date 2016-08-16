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
/**
 * @brief taskInit
 * initiate the system and enable the interrupts to start ticking the app
 */
void taskInit(void)
{
#ifdef DEBUGING_APP
        logI("DebugMsg", "init tasks");
#endif

     // Connect to settings service to dbus
    values_dbus_service_connectSettings();
    // brief hook the sensor items to their dbus services
    for(analog_sensors_index_t sensor_index = 0; sensor_index < num_of_analog_sensors; sensor_index++)
    {
        valuesInit(sensor_index);
    }
     // Connect sensor items to their dbus services
    for(analog_sensors_index_t sensor_index = 0; sensor_index < num_of_analog_sensors; sensor_index++)
    {
        sensors_dbusInit(sensor_index);
    }
    // Interrupt enable now
    pltInterruptEnable();
}

void taskUpdate(void)
{
// Not in use
}
/**
 * @brief taskTick- will be executed every 50 ms and this is the app tick.
 */
void taskTick(void)
{
    // got to handle the sensors and update the dbus items
    valuesTick();
}

/**
 * @brief version
 */
static char const version[] = VERSION_STR;
/**
 * @brief pltProgramVersion
 * @return the firmware version
 */
char const *pltProgramVersion(void)
{
	return version;
}
