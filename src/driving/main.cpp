#include <stdio.h>

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
