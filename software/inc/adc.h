#ifndef ADC_H
#define ADC_H

#define DEBUGING_APP

#include <velib/base/base.h>
#include <velib/types/ve_item.h>
#include <velib/types/ve_item_def.h>

#define ADC_SYSFS_READ_BUF_SIZE             50

#define ADC_VREF                            (float)(1.8)
#define OMEGA                               (float)6.283185307179586476925286766559

#define POTENTIAL_DIV_MAX_SAMPLE            11375 // = 4095 * 5/adc_vref
#define ADC_4_VOLTS                         9100
#define ADC_2p73VOLTS                       6211
#define ADC_1p6VOLTS                        3640
#define ADC_0p15VOLTS                       341

typedef enum
{
    adc_pin0 = 0,
    adc_pin1,
    adc_pin2,
    adc_pin3,
    adc_pin4,
    adc_pin5,
    adc_pin6,
    num_of_adc_pins
}adc_analogPin_t;


typedef enum
{
    calc_type_Vin = 0,
    calc_type_R1,
    calc_type_R2,
    max_calc_type
}pd_calc_type_t;

typedef struct
{
    un32 var1;
    un32 var2;
}potential_divider_t;


typedef struct
{
   un32     FF;
   float    fc;
   float    adc_mem;
}filter_iir_lpf_t;

veBool adc_read(un32 * value, adc_analogPin_t pin);
float adc_sample2volts(un32 sample);
float adc_filter(float x, float *y, float Fc, float Fs, un16 FF);
un32 adc_potDiv_calc(un32 sample, const potential_divider_t * pd, pd_calc_type_t type, un32 mltpty);

#endif // ADC_H
