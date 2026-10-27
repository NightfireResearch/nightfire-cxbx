#ifndef DRIVING_GC_GCDATA_H_
#define DRIVING_GC_GCDATA_H_

// The driving engine on the GameCube US release's data (docs/gc-driving.md): what the game files' seams call when
// the build has NIGHTFIRE_DRIVING_DATA_GC. The GameCube's files are read from DiscPath; the Xbox's render methods
// (misc.viv's eaglrm.o and bondrm.o) from the Xbox disc named by settings.ini's XboxDiscPath.

#include <stddef.h>
#include <stdint.h>

#include "../platform/FileSys.h"   // FileMallocFn, FileFreeFn

class DynamicLoader;

// The GameCube disc's name for a file the game asks for (.xsh and .xfn are .gsh and .gfn there): `path` itself, or
// `mapped`.
const char *Gc_FileName(const char *path, char *mapped, size_t size);

// A file just loaded (and unpacked), turned into what the Xbox's code reads: data groups (CARP, galleries) by record
// type, image containers (SHPG to SHPX), fonts (FNTG to FNTX), controller configurations (the GameCube pad's names),
// the animation lookup. In place, or in a new block from `allocate` (the old one given to `release`); anything else
// is returned as it is. `size`, if not NULL, is the file's and follows.
void *Gc_ConvertFile(void *data, int *size, const char *path, int flags, FileMallocFn allocate, FileFreeFn release);

// A stream's SCDl chunk (snd/Streams.cpp DataChunk) as it is about to be played: its frame count and its channels'
// offsets are big-endian on the GameCube, the tag and size little-endian as the Xbox's
void Gc_ConvertStreamChunk(uint32_t *chunk, int channels);

// One of the render-method libraries (data\render\eaglrm.o, bondrm.o), from the Xbox disc's misc.viv, in a block
// UMemory knows the size of; the GameCube render methods' names are registered on the first call
// (GcRenderMethods.cpp).
void *Gc_LoadXboxRenderMethods(const char *path);

// A GameCube EAGL object, its relocations applied and its constructors not yet run: every symbol's data turned into
// the Xbox's order by its class, with what its models' pointers lead to (GcObject.cpp). The Xbox's render-method
// libraries are left alone.
void Gc_ConvertObject(DynamicLoader *loader);

// Every model of a loaded GameCube object, its constructors run: each GeoPrim of its draw list replaced by an Xbox
// GeoPrim of the render method GcRenderMethods.cpp maps it to, or taken out of the list if none (GcModel.cpp).
void Gc_ConvertModels(DynamicLoader *loader);

#endif // DRIVING_GC_GCDATA_H_
