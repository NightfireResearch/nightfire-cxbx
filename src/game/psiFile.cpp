#include <stdio.h>
#include <stdint.h>
#include <windows.h>

#define DAT(x) (*((int*)x))

int FUN_000dc990(int a, int b, int c) {
	int (*funcPtr)(int, int, int) = (int (*)(int, int, int))(0x000dc990);
	return funcPtr(a, b, c);
}

int __cdecl psiFileOpen(int param_1)
{
  printf("hooked psiFileOpen: %s\n", (char*)param_1);
  
  DAT(0x002adf74) = 0;
  DAT(0x002adf78) = 0;
  DAT(0x002adf7c) = 0;
  DAT(0x002adf74) = FUN_000dc990(param_1,0x1204,0x002adf78);
  DAT(0x002adf7c) = 0;
  return (((uint32_t)DAT(0x002adf74) >> 8) << 8 | 1);
}


