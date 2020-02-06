#ifndef SENSORS_H
#define SENSORS_H

#include <velib/base/base.h>
#include <velib/types/ve_item.h>
#include <velib/types/ve_item_def.h>

#define MAX_SENSORS							8
#define SAMPLE_RATE							10

// defines for the tank level sensor analog front end parameters
#define TANK_SENS_VREF						5.0
#define TANK_SENS_R1						680.0 // ohms
#define EUR_MAX_TANK_LEVEL_RESISTANCE		(180) //ohms
#define USA_MAX_TANK_LEVEL_RESISTANCE		(240) //ohms
#define USA_MIN_TANK_LEVEL_RESISTANCE		(30) //ohms

// defines for the temperature sensor analog front end parameters
#define TEMP_SENS_R1						10000.0 // ohms
#define TEMP_SENS_R2						4700.0  // ohms
#define TEMP_SENS_V_RATIO					((TEMP_SENS_R1 + TEMP_SENS_R2) / TEMP_SENS_R2)
#define TEMP_SENS_MAX_ADCIN					1.3 // ~400K
#define TEMP_SENS_MIN_ADCIN					0.8 // ~(-22) degrees C
#define TEMP_SENS_S_C_ADCIN					0.02
#define TEMP_SENS_INV_PLRTY_ADCIN			0.208 // 0.7 volts at divider input
#define TEMP_SENS_INV_PLRTY_ADCIN_BAND		0.15
#define TEMP_SENS_INV_PLRTY_ADCIN_LB		(TEMP_SENS_INV_PLRTY_ADCIN - TEMP_SENS_INV_PLRTY_ADCIN_BAND)
#define TEMP_SENS_INV_PLRTY_ADCIN_HB		(TEMP_SENS_INV_PLRTY_ADCIN + TEMP_SENS_INV_PLRTY_ADCIN_BAND)

// defines to tank level sensor filter parameters
#define TANK_SENSOR_IIR_LPF_FF_VALUE		0.4
#define TANK_SENSOR_CUTOFF_FREQ				(0.001 / SAMPLE_RATE)
// defines to temperature sensor filter parameters
#define TEMPERATURE_SENSOR_IIR_LPF_FF_VALUE	0.2
#define TEMPERATURE_SENSOR_CUTOFF_FREQ		(0.01 / SAMPLE_RATE)

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
	VeItem root;
	struct VeItem *processName;
	struct VeItem *processVersion;
	struct VeItem *connection;
	struct VeItem *function;
	char iface_name[32];
	VeItem *statusItem;
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

#endif // End of sensors.h file
