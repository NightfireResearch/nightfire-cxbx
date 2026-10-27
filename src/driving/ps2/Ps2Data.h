#ifndef DRIVING_PS2_PS2DATA_H_
#define DRIVING_PS2_PS2DATA_H_

// The driving engine on the PS2 release's data (docs/ps2-driving.md): what the game files' seams call when the
// build has NIGHTFIRE_DRIVING_DATA_PS2. The PS2's files are read from DiscPath; the Xbox's render methods (misc.viv's
// eaglrm.o and bondrm.o) from the Xbox disc named by settings.ini's XboxDiscPath.

#include <stddef.h>

#include "../platform/FileSys.h"   // FileMallocFn, FileFreeFn

class DynamicLoader;

// The PS2 disc's name for a file the game asks for (.xsh, .xfn and .mad are .ssh, .sfn and .mpc there): `path`
// itself, or `mapped`.
const char *Ps2_FileName(const char *path, char *mapped, size_t size);

// A file just loaded (and unpacked): a PS2 image container (SHPS) becomes the Xbox's (SHPX), in a new block from
// `allocate` (the old one given to `release`); anything else is returned as it is. `size`, if not NULL, follows.
void *Ps2_ConvertFile(void *data, int *size, const char *path, int flags, FileMallocFn allocate,
                      FileFreeFn release);

// One of the render-method libraries (data\render\eaglrm.o, bondrm.o), from the Xbox disc's misc.viv, in a block
// UMemory knows the size of; the PS2 render methods' names are registered on the first call (Ps2RenderMethods.cpp).
void *Ps2_LoadXboxRenderMethods(const char *path);

// Every model of a loaded object, its relocations applied: each PS2 GeoPrim of its draw list replaced by an Xbox
// GeoPrim of the render method Ps2RenderMethods.cpp maps it to, or taken out of the list if none.
void Ps2_ConvertModels(DynamicLoader *loader);

#endif // DRIVING_PS2_PS2DATA_H_
