#include <stdio.h>
#include <stdint.h>
#include <windows.h>

#define I32_AT(x) (*((int*)x))
#define I16_AT(x) (*((short*)x))
#define I8_AT(x) (*((char*)x))
#define U32_AT(x) (*((unsigned int*)x))
#define U16_AT(x) (*((unsigned short*)x))
#define U8_AT(x) (*((unsigned char*)x))

typedef unsigned char   undefined;

typedef unsigned char    byte;
typedef unsigned int    dword;

typedef long long    longlong;
typedef unsigned int    uint;
typedef unsigned long    ulong;
typedef unsigned long long    ulonglong;
typedef unsigned char    undefined1;
typedef unsigned short    undefined2;
typedef unsigned int    undefined4;
typedef unsigned long long    undefined6;
typedef unsigned long long    undefined8;
typedef unsigned short    ushort;

typedef unsigned short    word;


int FUN_000dc990(int a, int b, int c) {
	int (*funcPtr)(int, int, int) = (int (*)(int, int, int))(0x000dc990);
	return funcPtr(a, b, c);
}

int allocateAndLoadFileWithinArchive(char* a, unsigned short b, int* c) {
	int (*funcPtr)(char*, unsigned short, int*) = (int (*)(char*, unsigned short, int*))(0x000dc990);
	return funcPtr(a, b, c);
}

int __cdecl FUN_000eed6b(int param_1) {
	int (*funcPtr)(int) = (int (*)(int))(0x000eed6b);
	return funcPtr(param_1);
}

int __cdecl psiFileOpen(int param_1)
{
  printf("hooked psiFileOpen: %s\n", (char*)param_1);

  U32_AT(0x002adf74) = 0;
  U32_AT(0x002adf78) = 0;
  U32_AT(0x002adf7c) = 0;
  U32_AT(0x002adf74) = FUN_000dc990(param_1,0x1204,0x002adf78);
  U32_AT(0x002adf7c) = 0;
  return (((uint32_t)U32_AT(0x002adf74) >> 8) << 8 | 1);
}

#define SingleFileMode U8_AT(0x002adf70)
#define DirFileLen U32_AT(0x00279174)
#define dirFileBuf U32_AT(0x00279168)

int ** __cdecl psiFileLoad(char *filename, unsigned short allocType, int *sizeOut)
{
  int **ppiVar1;

  printf("psiFileLoad: %s - 0x%04x\n", filename, allocType);

  if (SingleFileMode == '\0') {
    ppiVar1 = (int **)allocateAndLoadFileWithinArchive(filename,allocType,sizeOut);
    return ppiVar1;
  }
  if (sizeOut != (int *)0x0) {
    *sizeOut = DirFileLen;
  }
  return (int**)dirFileBuf;
}
