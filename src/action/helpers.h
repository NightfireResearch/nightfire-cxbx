#ifndef __HELPERS_H__
#define __HELPERS_H__

#include <stdint.h>
#include <stdbool.h>

#include "assets.h"

#include "game/obj/object.h" // for obj_tag needed by some autogen functions

#define I32_AT(x) (*((int32_t*)x))
#define I16_AT(x) (*((int16_t*)x))
#define I8_AT(x) (*((int8_t*)x))
#define U32_AT(x) (*((uint32_t*)x))
#define U16_AT(x) (*((uint16_t*)x))
#define U8_AT(x) (*((uint8_t*)x))
#define FLOAT_AT(x) (*((float*)x))

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

// TODO: Unfinished
typedef struct {
    char pad[0x76];
} M_MANAGER;

// TODO: Unfinished
typedef struct {
    char pad[0x18];
    uint hashcode;
} M_CONTROL;

// Common between PS2 and Xbox
typedef struct {
    HASHCODE iconHashcode;
    Nightfire_TranslatedText title;
    Nightfire_TranslatedText description;
    uint identifier; // Identifier or index
    uint enabled; // 4-byte bool? Upper 3 bits seem unused
    Nightfire_TranslatedText descriptionWhenDisabled;
} M_ITEM;

// TODO: Unfinished
typedef struct {
    char unknown;
} celglist_tag;

// TODO: Unfinished
typedef struct {
    char unknown;
} level_tag;

#endif // __HELPERS_H__