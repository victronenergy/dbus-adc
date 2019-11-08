#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <velib/platform/console.h>
#include <velib/utils/ve_logger.h>
#include <velib/utils/ve_arg.h>

struct option consoleOptions[] =
{
	{0,					0,					0,							0}
};

void consoleUsage(char*program)
{
	printf("%s\n", program);
	printf("\n");
	pltOptionsUsage();
	printf("Victron Energy B.V.\n");
}

veBool consoleOption(int flag)
{
	return veTrue;
}

veBool consoleArgs(int argc, char *argv[])
{
	VE_UNUSED(argv);

	if (argc != 0) {
		printf("error - no arguments are expected - missing a '-' or '--'?");
		return veFalse;
	}

	return veTrue;
}
