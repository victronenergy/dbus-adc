#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "sensors.h"

/**
 * @brief performs an adc sample read
 * @param value - a pointer to the variable which will store the result
 * @param sensor - pointer to sensor struct
 * @return - veTrue on success, veFalse on error
 */
veBool adcRead(un32 *value, AnalogSensor *sensor)
{
	char file[64];
	char val[16];
	int fd;
	int n;

	snprintf(file, sizeof(file), "in_voltage%d_raw",
			 sensor->interface.adcPin);

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
 * @brief moving average filter
 * @param x - the current sample
 * @param f - filter parameters
 * @return the next filtered value (filter output)
 */
float adcFilter(float x, Filter *f)
{
	if (f->sum < 0) {
		for (int i = 0; i < FILTER_LEN; i++)
			f->values[i] = x;

		f->sum = f->len * x;
	}

	f->sum -= f->values[f->tail++];
	f->sum += f->values[f->head++] = x;
	f->head &= FILTER_MASK;
	f->tail &= FILTER_MASK;

	return f->sum / f->len;
}

void adcFilterSetLen(Filter *f, unsigned len)
{
	f->len = len;
	f->tail = (f->head - len) & FILTER_MASK;

	if (f->sum >= 0) {
		f->sum = 0;
		for (int i = f->tail; i != f->head; i = (i + 1) & FILTER_MASK)
			f->sum += f->values[i];
	}
}

void adcFilterReset(Filter *f)
{
	f->sum = -1;
}
