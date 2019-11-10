#ifndef TASK_H
#define TASK_H

#include "sensors.h"

int add_sensor(int pin, float scale, int type);
void values_dbus_service_connectSettings(void);

#endif // TASK_H
