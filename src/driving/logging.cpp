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
	*(char*)(0x001e4761) = 1;

    va_end(args);
}

void dbg_wprintf(char* format, ...)
{
    va_list args;
    va_start(args, format);

	char fmt2[256];
	snprintf(fmt2, sizeof(fmt2), "%s", format);

    vprintf(fmt2, args);
	*(char*)(0x001e4761) = 1;

    va_end(args);
}

void xapiDebugStringA(char* text) {

	printf("XAPIDebug: %s", text);
}


int game_main(int argc, char** argv) {
	int (*funcPtr)(int, char**) = (int (*)(int, char**))(0x0005a1b0);
	return funcPtr(argc, argv);
}

int preMain(int argc, char *argv[]) {

	printf("main launching with %i args: ", argc);
	for(int i = 0; i < argc; i++) {
		printf("%i: %s, ", i, argv[i]);
	}
	printf("\n");

	return game_main(argc, argv);
}
