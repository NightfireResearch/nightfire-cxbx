#include <stdarg.h>
#include <stdio.h>

void dbg_printf(char* format, ...)
{
    va_list args;
    va_start(args, format);

    // Driving log messages don't end with \n
	char fmt2[256];
	snprintf(fmt2, sizeof(fmt2), "%s\n", format);

    vprintf(fmt2, args);

    va_end(args);
}