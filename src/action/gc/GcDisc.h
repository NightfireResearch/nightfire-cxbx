#ifndef GC_GCDISC_H_
#define GC_GCDISC_H_

// The GameCube disc's layout (docs/gc-data.md): no archive, every file loose on the disc. The names the Xbox build
// asks its archives for become paths on the GameCube's disc here. Used by GcFs.cpp and by the offline checker, so
// it touches no game state.

#include <stddef.h>
#include <stdint.h>

// What a file is on the GameCube's disc, beside where it is
enum GcFileKind {
    GC_FILE_NONE,           // not on the GameCube's disc
    GC_FILE_AS_IS,          // the Xbox's bytes (text banks, woman.rle)
    GC_FILE_BUNDLE,         // a bundle (<hashcode>.bin), converted whole (GcBundle_Load)
    GC_FILE_SOUND,          // the sound banks, music and their tables: big-endian, another codec (not converted yet)
    GC_FILE_MOVIE,          // an HVQM4 movie where the Xbox has an XMV (Fmv.cpp plays it)
};

// The path on the GameCube's disc, from its root and with '\' for '/', of the file the Xbox build names (as its
// archives are asked: "07000002.bin", "uktxt.dat", "ENGLISH/SB_0/SB_0.SHF", "music/MFX_0/MFX_1.SSD", "07100001.xmv").
// GC_FILE_NONE, and path empty, for a file the GameCube's disc has no counterpart of (the streamed speech).
GcFileKind GcDisc_Path(const char *name, char *path, size_t pathSize);

// The bundle's hashcode from a bundle's name ("07000002.bin"), or 0 when the name is not a bundle's
uint32_t GcDisc_BundleHash(const char *name);

#endif // GC_GCDISC_H_
