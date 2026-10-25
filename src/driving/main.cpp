#include <cstdio>

#include "engine/GameLoop.h"

int preMain(int argc, char *argv[]) {

	printf("main launching with %i args: ", argc);
	for(int i = 0; i < argc; i++) {
		printf("%i: %s, ", i, argv[i]);
	}
	printf("\n");

	// The game's main (0x0005a1b0), ours (src/driving/engine/GameLoop.cpp).
	return GameMain(argc, argv);
}
