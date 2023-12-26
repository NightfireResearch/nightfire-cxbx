#include <stdio.h>

// Never called - is our pointer overwritten, or are the functions not called?
void* ea_malloc(int amt, char* name) {
	printf("Alloc: %i of %s\n", amt, name);
	void* (*funcPtr)(int) = (void* (*)(int))(0x0e4f50);
	return funcPtr(amt);
}

