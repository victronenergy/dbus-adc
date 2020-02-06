#ifndef SENSORS_H
#define SENSORS_H

#include <velib/base/base.h>
#include <velib/types/ve_item.h>

// analog input function
typedef enum {
	no_function = 0,
	default_function,
	num_of_functions
} sensor_function_t;

// sensor statuses
typedef enum {
	SENSOR_STATUS_OK = 0,
	SENSOR_STATUS_NCONN,
	SENSOR_STATUS_SHORT,
	SENSOR_STATUS_REVPOL,
	SENSOR_STATUS_UNKNOWN,
} sensor_status_t;

// tank level sensor standards that the app can handle
typedef enum {
	european_std = 0,
	american_std,
	num_of_stds
} tank_sensor_std_t;

// types of sensors that the app can handle
typedef enum {
	SENSOR_TYPE_TANK = 0,
	SENSOR_TYPE_TEMP,
} sensor_type_t;

// parameters to interface the sensor to dbus service
typedef struct {
	char service[64];
	veBool connected;
} sensors_dbus_interface_t;

// sensor signal correction parameters
typedef struct {
	float scale;
	float offset;
} signal_correction_t;

// Single pole iir low pass filter variables
typedef struct {
	float FF;
	float fc;
	float last;
} filter_iir_lpf_t;

// building a sensor signal conditioning structure
typedef struct {
	signal_correction_t sig_correct;
	filter_iir_lpf_t filter_iir_lpf;
} signal_condition_t;

// building a sensor interface structure
typedef struct {
	int devfd;
	int adc_pin;
	float adc_scale;
	float adc_sample;
	signal_condition_t sig_cond;
	sensors_dbus_interface_t dbus;
} sensors_interface_t;

// building a sensor structure
typedef struct {
	sensor_type_t sensor_type;
	int number; /* per type */
	int instance;
	veBool valid;
	sensors_interface_t interface;
	struct VeDbus *dbus;
	struct VeItem *root;
	struct VeItem *processName;
	struct VeItem *processVersion;
	struct VeItem *connection;
	struct VeItem *function;
	char iface_name[32];
	struct VeItem *statusItem;
} analog_sensor_t;

struct TankSensor {
	analog_sensor_t sensor;
	struct VeItem *levelItem;
	struct VeItem *remaingItem;
	struct VeItem *capacityItem;
	struct VeItem *fluidTypeItem;
	struct VeItem *standardItem; /* tanksensor standard, EU vs US e.g. */
};

struct TemperatureSensor {
	analog_sensor_t sensor;
	struct VeItem *temperatureItem;
	struct VeItem *scaleItem;
	struct VeItem *offsetItem;
};

analog_sensor_t *sensor_init(int devfd, int pin, float scale, sensor_type_t type);
void sensors_handle(void);
int add_sensor(int devfd, int pin, float scale, int type);
veBool adc_read(un32 *value, analog_sensor_t *sensor);
float adc_filter(float x, filter_iir_lpf_t *f);

struct VeItem *getLocalSettings(void);

#endif // End of sensors.h file
