#include <stdio.h>

#include "eagl/View.h"

// Never called - is our pointer overwritten, or are the functions not called?
void* ea_malloc(int amt, char* name) {
	printf("Alloc: %i of %s\n", amt, name);
	return eagl_alloc(amt, name);
}
