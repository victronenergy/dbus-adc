#ifndef ADC_H
#define ADC_H

#define DEBUGING_APP

#include <velib/base/base.h>
#include <velib/types/ve_item.h>
#include <velib/types/ve_item_def.h>

#define ADC_SYSFS_READ_BUF_SIZE             50

#define ADC_VREF                            (float)(1.8)
#define OMEGA                               (float)6.283185307179586476925286766559

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

//typedef enum
//{
//    filter_splp_iir =0, // single pole lowpass infinite impulse response
//    num_of_filter_types
//}adc_filterTypes_t;

//typedef struct
//{
//    adc_filterTypes_t filterType;
//    float x;
//    float *y;
//    float Fc;
//    float Fs;
//    un16 FF;
//}adc_filterInfo_t;

veBool adc_read(un32 * value, adc_analogPin_t pin);
float adc_sample2volts(un32 sample);
float adc_filter(float x, float *y, float Fc, float Fs, un16 FF);
un32 adc_potDiv_calc(un32 sample, const potential_divider_t * pd, pd_calc_type_t type, un32 mltpty);

#endif // ADC_H
