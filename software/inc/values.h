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
// tick intervall definition for app ticking
#define DESIRED_VALUES_TASK_INTERVAL	100 // ms
#define VALUES_TASK_INTERVAL			DESIRED_VALUES_TASK_INTERVAL / 50 // 50ms base ticking

typedef struct {
	VeVariantUnitFmt unit;
	VeItemValueFmt *fun;
} FormatInfo;

// information for interfacing to dbus service
typedef struct {
	VeItem *item;
	VeVariant *local;
	char const *id;
	FormatInfo *fmt;
	un8 timeout;
	VeItemSetterFun *setValueCallback;
} ItemInfo;

// values variables structure for dbus settings parameters
typedef struct {
	VeVariant def;
	VeVariant min;
	VeVariant max;
} values_range_t;

// structure to hold the onformation required to dbus unterfacing
typedef struct {
	const float def;
	const float min;
	const float max;
	const char *path;
	struct VeDbus *connect;
	VeItem *value;
} dbus_info_t;

/***********************************************/
// Public function prototypes
/**
 * @brief valuesTick
 */
void valuesTick(void);

/**
 * @brief getConsumerRoot
 * @return
 */
VeItem *getConsumerRoot(void);

/**
 * @brief valuesInvalidate
 */
void valuesInvalidate(void);

/**
 * @brief valuesAddItemByInfo
 * @param itemInfo
 */
void valuesAddItemByInfo(ItemInfo const *itemInfo);

/**
 * @brief valuesDisconnectedEvent
 */
void valuesDisconnectedEvent(void);

#endif
