#ifndef XBOXPATHS_H_
#define XBOXPATHS_H_

#include <stddef.h>

// ---------------------------------------------------------------------------------------------------------------
// Xbox drive letters to host paths.
//
// The game asks for files by Xbox path - "d:\eurocom\filesys.d00", "z:\state.bin" - which on real hardware the
// kernel resolved through its own mount table, and which under CXBX resolves through CXBX's (it mounts D: to
// whichever directory the XBE it was handed lives in). Replacing the file I/O with Win32 calls means doing that
// resolution ourselves, and this is the one place it happens.
//
//   d:\...   the game disc         -> the DiscPath setting, "../disc" by default
//   t:\...   title save data       -> "saves/"
//   u:\...   the save enumeration root, which psiSave already walks -> "saves/"
//   z:\...   the per-boot cache partition, scratch space the game expects to be able to write and re-read,
//            and which it never expects to survive a restart -> "cache/"
//
// Paths with no recognised drive letter are passed through unchanged, so the plain relative paths the project
// already uses ("patch/...", "saves/...", "dump/...") keep working through the same function.
//
// Separators are normalised to forward slashes. Win32 accepts either, but it keeps logs readable and matches
// how the rest of the project writes paths.
// ---------------------------------------------------------------------------------------------------------------

// Writes the host path for an Xbox path into out. Returns false only if the arguments are unusable or the
// result would not fit, in which case out is left as an empty string.
bool Xbox_ResolvePath(const char *xboxPath, char *out, size_t outSize);

// The host directory D: resolves to. From settings.ini's DiscPath, defaulting to "../disc" - which, with the
// working directory being the one the executables live in, is a "disc" folder beside it.
const char *Xbox_GetDiscRoot(void);

#endif // XBOXPATHS_H_
