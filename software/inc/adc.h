#ifndef ADC_H
#define ADC_H

#include <velib/base/base.h>
#include <velib/types/ve_item.h>
#include <velib/types/ve_item_def.h>

// Single pole iir low pass filter variables
typedef struct {
	float FF;
	float fc;
	float last;
} filter_iir_lpf_t;

// Public functions
veBool adc_read(un32 *value, int pin);
float adc_filter(float x, filter_iir_lpf_t *f);

#endif // ADC_H
