#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "sensors.h"

/**
 * @brief adc_read - performs an adc sample read
 * @param value - a pointer to the variable which will store the result
 * @param sensor - pointer to sensor struct
 * @return - veTrue on success, veFalse on error
 */
veBool adc_read(un32 *value, analog_sensor_t *sensor)
{
	char file[64];
	char val[16];
	int fd;
	int n;

	snprintf(file, sizeof(file), "in_voltage%d_raw",
			 sensor->interface.adc_pin);

	fd = openat(sensor->interface.devfd, file, O_RDONLY);
	if (fd < 0) {
		perror(file);
		return veFalse;
	}

	n = read(fd, val, sizeof(val));
	close(fd);

	if (n <= 0)
		return veFalse;

	if (val[n - 1] != '\n')
		return veFalse;

	*value = strtoul(val, NULL, 0);

	return veTrue;
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
