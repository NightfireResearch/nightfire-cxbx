#ifndef __HELPERS_H__
#define __HELPERS_H__

#include <stdint.h>
#include <stdbool.h>

#define I32_AT(x) (*((int32_t*)x))
#define I16_AT(x) (*((int16_t*)x))
#define I8_AT(x) (*((int8_t*)x))
#define U32_AT(x) (*((uint32_t*)x))
#define U16_AT(x) (*((uint16_t*)x))
#define U8_AT(x) (*((uint8_t*)x))
#define U4_AT(x) (*((uint*)x))
#define FLOAT_AT(x) (*((float*)x))
#define PTR_AT(x) (*((void**)x))

typedef unsigned char   undefined;

typedef unsigned char    byte;
typedef unsigned char    uchar;
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

#endif // __HELPERS_H__