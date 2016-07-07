#ifndef _VALUES_H_
#define _VALUES_H_

#include <velib/types/ve_item.h>
#include <velib/types/ve_item_def.h>
#include <velib/types/variant_print.h>



/**
 * To seperate gui logic / any other logic from the communication
 * the received values are stored in a tree in SI units. This struct
 * contains the field needed to create the tree.
 */

#define DESIRED_VALUES_TASK_INTERVAL    100 // ms
#define VALUES_TASK_INTERVAL            DESIRED_VALUES_TASK_INTERVAL / 50 // 50ms base ticking

typedef struct
{
    VeItem *		    item;
	VeVariant *			local;
	char const *		id;
	VeVariantUnitFmt *	fmt;
	un8					timeout;
    VeItemSetterFun	*	setValueCallback;
} ItemInfo;

typedef struct
{
    VeVariant def;
    VeVariant min;
    VeVariant max;
}values_range_t;

typedef struct
{
    const float     def;
    const float     min;
    const float     max;
    const char      *path;
    struct VeDbus   *connect;
    VeItem          *value;
}dbus_info_t;

void valuesTick(void);

VeItem * getConsumerRoot(void);

void valuesInvalidate(void);
void valuesAddItemByInfo(ItemInfo const *itemInfo);
void valuesDisconnectedEvent(void);

#endif
