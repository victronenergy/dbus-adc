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

// Single pole iir low pass filter variables
typedef struct {
	float FF;
	float fc;
	float last;
} FilerIirLpf;

// building a sensor signal conditioning structure
typedef struct {
	SignalCorrection sigCorrect;
	FilerIirLpf filterIirLpf;
} SignalCondition;

// building a sensor interface structure
typedef struct {
	int devfd;
	int adcPin;
	float adcScale;
	float adcSample;
	float adcSampleRaw;
	SignalCondition sigCond;
	SensorDbusInterface dbus;
} SensorInterface;

// building a sensor structure
typedef struct {
	SensorType sensorType;
	int number; /* per type */
	int instance;
	veBool valid;
	SensorInterface interface;
	struct VeDbus *dbus;
	struct VeItem *root;
	struct VeItem *function;
	char ifaceName[32];
	struct VeItem *statusItem;
	struct VeItem *rawValueItem;
} AnalogSensor;

#define TANK_SHAPE_MAX_POINTS 10

struct TankSensor {
	AnalogSensor sensor;
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
	float scale;
	SensorType type;
	char dev[32];
} SensorInfo;

AnalogSensor *sensorCreate(SensorInfo *s);
void sensorTick(void);

veBool adcRead(un32 *value, AnalogSensor *sensor);
float adcFilter(float x, FilerIirLpf *f);

struct VeItem *getLocalSettings(void);

#endif
