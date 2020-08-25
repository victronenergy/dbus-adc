#ifndef SENSORS_H
#define SENSORS_H

#include <velib/base/base.h>
#include <velib/types/ve_item.h>

typedef enum {
	SENSOR_FUNCTION_NONE,
	SENSOR_FUNCTION_DEFAULT,
	SENSOR_FUNCTION_COUNT
} SensorFunction;

typedef enum {
	SENSOR_STATUS_OK,
	SENSOR_STATUS_NOT_CONNECTED,
	SENSOR_STATUS_SHORT,
	SENSOR_STATUS_REVERSE_POLARITY,
	SENSOR_STATUS_UNKNOWN,
} SensorStatus;

typedef enum {
	TANK_STANDARD_EU,
	TANK_STANDARD_US,
	TANK_STANDARD_CUSTOM,
	TANK_STANDARD_COUNT
} TankStandard;

typedef enum {
	TANK_SENSE_RESISTANCE,
	TANK_SENSE_VOLTAGE,
	TANK_SENSE_CURRENT,
	TANK_SENSE_COUNT,
} TankSenseType;

typedef enum {
	SENSOR_TYPE_TANK,
	SENSOR_TYPE_TEMP,
} SensorType;

typedef struct {
	char service[64];
	veBool connected;
} SensorDbusInterface;

// sensor signal correction parameters
typedef struct {
	float scale;
	float offset;
} SignalCorrection;

#define FILTER_LEN 64
#define FILTER_MASK (FILTER_LEN - 1)

typedef struct {
	float values[FILTER_LEN];
	float sum;
	unsigned len;
	unsigned head;
	unsigned tail;
} Filter;

// building a sensor signal conditioning structure
typedef struct {
	SignalCorrection sigCorrect;
	Filter filter;
} SignalCondition;

// building a sensor interface structure
typedef struct {
	int devfd;
	int adcPin;
	int gpio;
	float adcScale;
	float adcSample;
	float adcSampleRaw;
	SignalCondition sigCond;
	SensorDbusInterface dbus;
} SensorInterface;

// building a sensor structure
typedef struct {
	SensorType sensorType;
	int instance;
	veBool valid;
	SensorInterface interface;
	struct VeDbus *dbus;
	struct VeItem *root;
	struct VeItem *function;
	char ifaceName[32];
	char serial[32];
	struct VeItem *statusItem;
	struct VeItem *rawValueItem;
	struct VeItem *rawUnitItem;
	struct VeItem *filterLenItem;
} AnalogSensor;

#define TANK_SHAPE_MAX_POINTS 10

struct TankSensor {
	AnalogSensor sensor;
	TankSenseType senseType;
	int shapeMapLen;
	float shapeMap[TANK_SHAPE_MAX_POINTS + 2][2];
	struct VeItem *levelItem;
	struct VeItem *remaingItem;
	struct VeItem *capacityItem;
	struct VeItem *fluidTypeItem;
	struct VeItem *standardItem; /* tanksensor standard, EU vs US e.g. */
	struct VeItem *emptyRItem;
	struct VeItem *fullRItem;
	struct VeItem *shapeItem;
	struct VeItem *senseTypeItem;
};

struct TemperatureSensor {
	AnalogSensor sensor;
	struct VeItem *temperatureItem;
	struct VeItem *scaleItem;
	struct VeItem *offsetItem;
};

typedef struct {
	int devfd;
	int pin;
	int gpio;
	float scale;
	SensorType type;
	char dev[32];
	char label[32];
	char serial[32];
	int product_id;
	int func_def;
} SensorInfo;

AnalogSensor *sensorCreate(SensorInfo *s);
void sensorTick(void);

veBool adcRead(un32 *value, AnalogSensor *sensor);
float adcFilter(float x, Filter *f);
void adcFilterReset(Filter *f);
void adcFilterSetLen(Filter *f, unsigned len);

struct VeItem *getLocalSettings(void);
struct VeItem *getDbusRoot(void);

#endif
