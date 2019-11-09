#include <velib/base/base.h>
#include <velib/base/ve_string.h>
#include <velib/types/ve_item.h>
#include <velib/utils/ve_logger.h>

#include <string.h>
#ifdef WIN32
#include <stdio.h>
#else
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include "values.h"
#include "adc.h"
#endif

/**
 * @brief adc_read - performs an adc sample read
 * @param value - a pointer to the variable which will store the result
 * @param pin - which adc interface pin to sample
 * @return - Boolean status veFalse-success, veTrue-fail
 */
veBool adc_read(un32 *value, int pin)
{
	int fd;
	char buf[ADC_SYSFS_READ_BUF_SIZE];
	char val[4];
	snprintf(buf, sizeof(buf), "/sys/bus/iio/devices/iio:device0/in_voltage%d_raw", pin);
	fd = open(buf, O_RDONLY);
	if (fd < 0) {
		return veTrue;
		perror("adc/get-value");
	}

	read(fd, val, 4);
	close(fd);

	*value = (un32)atoi(val);
	return veFalse;
}
/**
 * @brief adc_sample2volts - converts the adc sample from counts to volts
 * @param sample - adc sample in counts
 * @return - the value in volts
 */
float adc_sample2volts(un32 sample)
{
	return ((float)sample*ADC_VREF/ADC_MAX_COUNT);
}
/**
 * @brief adc_filter - a single pole IIR low pass filter
 * @param x - the current sample
 * @param f - filter parameters
 * @return the next filtered value (filter output)
 */
float adc_filter(float x, filter_iir_lpf_t *f)
{
	if (f->FF && fabs(f->last - x) > f->FF)
		f->last = x;

	return f->last += (x - f->last) * 2 * M_PI * f->fc;
}
