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

#define MODULE	"ADC"
#define MGRNUM	8

veBool adc_read(un32 * value, adc_analogPin_t pin)
{
    int fd;
    char buf[ADC_SYSFS_READ_BUF_SIZE];
    char val[4];
    snprintf(buf, sizeof(buf), "/sys/bus/iio/devices/iio\:device0/in_voltage%d_raw", pin);
    fd = open(buf, O_RDONLY);
    if (fd < 0)
    {
        return(veTrue);
        perror("adc/get-value");
    }

    read(fd, val, 4);
    close(fd);

    *value = (un32)atoi(val);
    return(veFalse);
}

float adc_sample2volts(un32 sample)
{
    return((float)sample*ADC_VREF/4095);
}

float adc_filter(float x, float *y, float Fc, float Fs, un16 FF)
{
    if(FF)
    {
        if(fabs(*y - x) > FF)
        {
            *y = x;
        }
    }
    if(Fc > 0)
    {
       return (*y = *y + (x - *y)*OMEGA*Fc/Fs);
    }
    return(x);
}


// converting the adc sample value to resistance
un32 adc_potDiv_calc(un32 sample, const potential_divider_t * pd, pd_calc_type_t type, un32 mltpty)
{
    un32 out;
    // function mltpty arg protection
    if(mltpty == 0)
    {
        mltpty = 1;
    }
    switch(type)
    {
        case calc_type_Vin:
        {
            // var1 = R1, var2 = R2
            out = mltpty * sample * (pd->var1 + pd->var2);
            out /= pd->var2;
            break;
        }
        case calc_type_R1:
        {
            // var1 = divider_supply, var2 = R2
            out = mltpty * pd->var2 * (pd->var1 - sample);
            out /= sample;
            out /= mltpty;
            break;
        }
        case calc_type_R2:
        {
            // var1 = R1, var2 = divider_supply
            sn32 diff = (sn32)(pd->var2 - sample);
            if(diff <= 0)
            {
                return(0);
            }
            out = mltpty * sample * pd->var1;
            out /= (un32)diff;
            out /= mltpty;
            break;
        }
        default:
        {
            break;
        }
    }
    return(out);
}

