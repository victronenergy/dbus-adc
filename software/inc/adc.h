#ifndef ADC_H
#define ADC_H

#include <velib/base/base.h>
#include <velib/types/ve_item.h>
#include <velib/types/ve_item_def.h>

#define ADC_SYSFS_READ_BUF_SIZE				50

#define ADC_VREF							(float)(1.8)

#define ADC_MAX_COUNT						4095
#define ADC_1p4VOLTS						3185
#define ADC_1p3VOLTS						2957
// -22.78 degrees, invalidate results below that temperature
#define ADC_0p8VOLTS						1820
// corresponding value when lm335 is connected in opposite polarity (forward zener diode voltage ~0.7V*DIVIDER = 0.22)
#define ADC_0p208VOLTS						473
#define ADC_0p15VOLTS						341

// Single pole iir low pass filter variables
typedef struct {
	float FF;
	float fc;
	float last;
} filter_iir_lpf_t;

// Public functions
veBool adc_read(un32 *value, int pin);
float adc_sample2volts(un32 sample);
float adc_filter(float x, filter_iir_lpf_t *f);

#endif // ADC_H
