#ifndef SENSORS_H
#define SENSORS_H

#include <velib/base/base.h>
#include <velib/types/ve_item.h>
#include <velib/types/ve_item_def.h>

#include "adc.h"
#include "values.h"

#define NUM_OF_TANK_LEVEL_SENSOR            3
#define NUM_OF_TEMPERATURE_SENSOR           2
#define NUM_OF_VIN_STD_0_10_SENSOR          1
#define NUM_OF_VBAT_SENSOR                  1
// slected voltages in ADC counts
#define POTENTIAL_DIV_MAX_SAMPLE            11375 // = 4095 * 5/adc_vref
#define ADC_4_VOLTS                         9100
#define ADC_2p73VOLTS                       6211
#define ADC_70VOLTS                         159250
#define ADC_15VOLTS                         34125

#define TANK_LEVEL_SENSOR_DIVIDER           (680) // ohms
#define DEFAULT_MAX_TANK_LEVEL_RESISTANCE   (180) //ohms
#define TANK_LEVEL_PRCNT_MLTY               (1000) //ohms = multiplyer / decimal

// Temperature sensors settings
#define TEMP_SENS_VOLT_DIVID_R1             (51000 + 51000) // ohms
#define TEMP_SENS_VOLT_DIVID_R2             (22000) // ohms

#define TEMP_SENS_MAX_ADCIN                 1614 // ~127C
#define TEMP_SENS_MIN_ADCIN                 1100 // ~0C

#define VBAT_DIVID_R1                       (470000 + 10000) // ohms
#define VBAT_DIVID_R2                       (10000) // ohms

#define VIN_STD_0_10_DIVID_R1               (80000 + 12000) // ohms
#define VIN_STD_0_10_DIVID_R2               (8000) // ohms

#define NUM_OF_SENSOR_SETTINGS_PARAMS       6
#define NUM_OF_PROD_ITEMS                   6
#define NUM_OF_SENSOR_VARIANTS              3
#define SENSORS_INFO_ARRAY_SIZE             NUM_OF_SENSOR_VARIANTS + NUM_OF_PROD_ITEMS

typedef enum
{
    capacity,
}parameter_name_t;

typedef enum
{
    index_tankLevel1 = 0,
    index_tankLevel2,
    index_tankLevel3,
    index_vin_std_0_10,
    index_temperature1,
    index_temperature2,
    index_vbat,
    num_of_analog_sensors
}analog_sensors_index_t;

typedef struct
{
    VeItem name;
    VeItem id;
    VeItem instance;
    VeItem connected;
    VeItem hardwareRevision;
    VeItem firmwareVersion;
} ProductInfo;

typedef struct
{
    char        *productName;
    const char  *service;
}sensors_dbus_interface_t;

typedef struct
{
    ProductInfo	product;
    VeItem		item[NUM_OF_SENSOR_VARIANTS];
}sensors_items_t;

typedef enum
{
    tank_level_t = 0,
    vin_std_0_10_t,
    temperature_t,
    vbat_t,
    num_of_sensorsTypes
}sensors_type_t;

typedef struct
{
    float scale;
    float offset;
}signal_correction_t;

typedef struct
{
    signal_correction_t sig_correct;
    filter_iir_lpf_t    filter_iir_lpf;
}signal_condition_t;

typedef struct
{
    const adc_analogPin_t       adc_pin;
    un32                        adc_sample;
    signal_condition_t          sig_cond;
    sensors_dbus_interface_t    dbus;
}sensors_interface_t;
/* sensor structure */
typedef struct
{
   const sensors_type_t   sensor_type;
   veBool                 valid;
   sensors_interface_t    interface;
   dbus_info_t            dbus_info[NUM_OF_SENSOR_SETTINGS_PARAMS];
   VeVariant              var[NUM_OF_SENSOR_VARIANTS];
}analog_sensor_t;

void sensor_init(VeItem *root, analog_sensors_index_t sensor_index);
void sensors_handle(void);

void sensors_dbusInit(analog_sensors_index_t sensor_index);
void values_dbus_service_addSettings(analog_sensor_t * sensor);
void sensors_dbusConnect(analog_sensor_t * sensor, analog_sensors_index_t sensor_index);
void sensors_dbusDisconnect(void);


#endif // End of sensors.h file
