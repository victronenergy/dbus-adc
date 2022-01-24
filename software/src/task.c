#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>

#include <velib/platform/plt.h>
#include <velib/types/ve_dbus_item.h>
#include <velib/types/ve_values.h>
#include <velib/utils/ve_logger.h>

#include "sensors.h"

#define SENSOR_TICKS	20		/* 1 s */

#define CONFIG_FILE	"/etc/venus/dbus-adc.conf"
#define CONFIG_DIR	"/run/dbus-adc.d"

#define VREF_MIN	1.0
#define VREF_MAX	10.0

#define SCALE_MIN	1023
#define SCALE_MAX	65535

static struct VeItem *localSettings;
static struct VeItem *root;

static void error(const char *file, int line, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "%s:%d: ", file, line);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	exit(1);
}

static char *token(char *buf, char **next, int q)
{
	char *end;

	while (isspace(*buf))
		buf++;

	if (!*buf)
		return NULL;

	if (q) {
		if (*buf == '"')
			buf++;
		else
			q = 0;
	}

	end = buf + 1;

	if (q) {
		while (*end && *end != '"')
			end++;
		if (!*end)
			return NULL;
	} else {
		while (*end && !isspace(*end))
			end++;
	}

	if (*end)
		*end++ = 0;

	*next = end;

	return buf;
}

static float getFloat(const char *p, float min, float max,
					   const char *file, int line)
{
	char *end;
	float v = strtof(p, &end);

	if (*end)
		error(file, line, "invalid number '%s'\n", p);

	if (!(v >= min && v <= max)) /* also catch NaN */
		error(file, line, "value out of range [%f, %f]\n", min, max);

	return v;
}

static unsigned getUint(const char *p, unsigned min, unsigned max,
						 const char *file, int line)
{
	char *end;
	unsigned v = strtoul(p, &end, 0);

	if (*end)
		error(file, line, "invalid number '%s'\n", p);

	if (v < min || v > max)
		error(file, line, "value out of range [%u, %u]\n", min, max);

	return v;
}

static int openDev(const char *dev, const char *file, int line)
{
	struct stat st;
	char buf[64];
	int err;
	int fd;

	snprintf(buf, sizeof(buf), "/dev/%s", dev);

	err = stat(buf, &st);
	if (err < 0) {
		if (errno != ENOENT)
			fprintf(stderr, "%s: %s\n", dev, strerror(errno));
		return err;
	}

	if (!S_ISCHR(st.st_mode))
		error(file, line, "not a character device: '%s'\n", buf);

	snprintf(buf, sizeof(buf), "/sys/bus/iio/devices/iio:device%d",
			 minor(st.st_rdev));

	fd = open(buf, O_RDONLY);
	if (fd < 0)
		error(file, line, "bad device '%s'\n", dev);

	return fd;
}

static void loadConfig(const char *file)
{
	SensorInfo s = { .devfd = -1 };
	FILE *f;
	char buf[128];
	float vref = 0;
	unsigned scale = 0;
	int line = 0;

	f = fopen(file, "r");
	if (!f)
		error(file, 0, "%s\n", strerror(errno));

	while (fgets(buf, sizeof(buf), f)) {
		char *cmd, *arg, *rest;
		char *p = buf;

		line++;

		if (!strchr(p, '\n'))
			error(file, line, "line too long\n");

		cmd = strchr(p, '#');
		if (cmd)
			*cmd = 0;

		cmd = token(p, &p, 0);
		if (!cmd)
			continue;

		arg = token(p, &p, 1);
		if (!arg)
			error(file, line, "missing value\n");

		rest = token(p, &p, 0);
		if (rest)
			error(file, line, "trailing junk\n");

		if (!strcmp(cmd, "product")) {
			s.product_id = getUint(arg, 0, UINT16_MAX, file, line);
			continue;
		}

		if (!strcmp(cmd, "serial")) {
			snprintf(s.serial, sizeof(s.serial), "%s", arg);
			continue;
		}

		if (!strcmp(cmd, "default")) {
			s.func_def = getUint(arg, 0, 1, file, line);
			continue;
		}

		if (!strcmp(cmd, "device")) {
			s.devfd = openDev(arg, file, line);
			snprintf(s.dev, sizeof(s.dev), "%s", arg);
			continue;
		}

		if (!strcmp(cmd, "vref")) {
			vref = getFloat(arg, VREF_MIN, VREF_MAX, file, line);
			continue;
		}

		if (!strcmp(cmd, "scale")) {
			scale = getUint(arg, SCALE_MIN, SCALE_MAX, file, line);
			continue;
		}

		if (!strcmp(cmd, "label")) {
			snprintf(s.label, sizeof(s.label), "%s", arg);
			continue;
		}

		if (!strcmp(cmd, "gpio")) {
			s.gpio = getUint(arg, 0, -1, file, line);
			continue;
		}

		if (!strcmp(cmd, "tank"))
			s.type = SENSOR_TYPE_TANK;
		else if (!strcmp(cmd, "temp"))
			s.type = SENSOR_TYPE_TEMP;
		else
			error(file, line, "unknown directive\n");

		if (!s.dev[0])
			error(file, line, "%s requires device\n", cmd);

		if (!vref)
			error(file, line, "%s requires vref\n", cmd);

		if (!scale)
			error(file, line, "%s requires scale\n", cmd);

		if (s.devfd < 0)
			continue;

		s.pin = getUint(arg, 0, -1u, file, line);
		s.scale = vref / scale;

		if (!sensorCreate(&s))
			error(file, line, "error adding sensor\n");

		s.label[0] = 0;
	}
}

static void loadConfigFiles(void)
{
	char buf[PATH_MAX];
	struct dirent *de;
	DIR *d;

	loadConfig(CONFIG_FILE);

	d = opendir(CONFIG_DIR);
	if (!d)
		return;

	while ((de = readdir(d)) != NULL) {
		char *dot = strrchr(de->d_name, '.');

		if (!dot || strcmp(dot, ".conf"))
			continue;

		snprintf(buf, sizeof(buf), "%s/%s", CONFIG_DIR, de->d_name);
		loadConfig(buf);
	}

	closedir(d);
}

static void connectToDbus(void)
{
	const char *settingsService = "com.victronenergy.settings";
	struct VeItem *inputRoot = veValueTree();
	struct VeDbus *dbus;
	int settingsTries = 10;

	if (!(dbus = veDbusGetDefaultBus())) {
		printf("dbus connection failed\n");
		pltExit(5);
	}
	veDbusSetListeningDbus(dbus);

	/* Connect to settings service */
	localSettings = veItemGetOrCreateUid(inputRoot, settingsService);

	while (settingsTries--) {
		if (veDbusAddRemoteService(settingsService, localSettings, veTrue))
			break;
		sleep(2);
	}

	if (settingsTries < 0) {
		logE("task", "error connecting to settings service");
		pltExit(1);
	}

	dbus = veDbusConnectString(veDbusGetDefaultConnectString());
	if (!dbus) {
		printf("dbus connection failed\n");
		pltExit(5);
	}

	root = veItemAlloc(NULL, "");
	veDbusItemInit(dbus, root);
	veDbusChangeName(dbus, "com.victronenergy.adc");
}

struct VeItem *getLocalSettings(void)
{
	return localSettings;
}

struct VeItem *getDbusRoot(void)
{
	return root;
}

void taskInit(void)
{
	pltExitOnOom();
	connectToDbus();
	loadConfigFiles();
}

void taskUpdate(void)
{
	// Not in use
}

/* 50 ms time update. */
void taskTick(void)
{
	static un16 sensorTimer = SENSOR_TICKS;

	if (--sensorTimer == 0) {
		sensorTimer = SENSOR_TICKS;
		sensorTick();
	}
}

char const *pltProgramVersion(void)
{
	return "1.35";
}
