#ifndef DRIVING_EAGL_EAGLORIGINALS_H_
#define DRIVING_EAGL_EAGLORIGINALS_H_

// The originals EAGL's port still calls by address, each defined once (on one line, so tools/function_coverage.py
// sees the cast and the address together). Everything else it calls is ours and called directly: the D3D8 seam
// (../gfx/D3D8.h), the platform layer (../platform/), the game's operator delete (../engine/UMemory.hpp), and EAGL
// itself.

#include <stdarg.h>
#include <stdint.h>

#include "Transform.h"

// ---- not ported: the engine's maths (game side) and the C/C++ runtime

// Transform::BuildQT (thiscall: the transform in ECX; the int is EDX's unused slot): a rotation quaternion and a
// translation.
#define Transform_BuildQT ((void (__fastcall *)(Transform *, int, float, float, float, float, float, float, float))0x000162e0)
#define AngleBetween ((double (*)(const float *a, const float *b))0x00016530)   // atan2(|a x b|, a.b), in ST0
#define QuatMultiply ((void (*)(const float *a, const float *b, float *out))0x00016820)
#define CRT_free ((void (__cdecl *)(void *))0x001331dc)
#define CRT_malloc ((void *(__cdecl *)(uint32_t))0x001340e3)
#define CrtVsnprintf ((int (*)(char *buffer, int size, const char *format, va_list arguments))0x001340f5)
#define VectorDestructorIterator ((void (__stdcall *)(void *array, uint32_t size, int32_t count, void *destructor))0x0013332e)   // ??_M

#endif // DRIVING_EAGL_EAGLORIGINALS_H_
