#ifndef _VALUES_H_
#define _VALUES_H_

#include <velib/types/ve_item.h>
#include <velib/types/variant_print.h>

/**
 * To seperate gui logic / any other logic from the communication
 * the received values are stored in a tree in SI units. This struct
 * contains the field needed to create the tree.
 */
// tick interval definition for app ticking
#define DESIRED_VALUES_TASK_INTERVAL	100 // ms
#define VALUES_TASK_INTERVAL			DESIRED_VALUES_TASK_INTERVAL / 50 // 50ms base ticking

/***********************************************/
// Public function prototypes
void valuesTick(void);
struct VeItem *getConsumerRoot(void);

#endif
