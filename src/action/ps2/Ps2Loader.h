#ifndef PS2_PS2LOADER_H_
#define PS2_PS2LOADER_H_

// The PS2 splits a level over bundles (docs/ps2-data.md): its animation sequences, which the Xbox keeps in the level's
// own bundle, are in the variant 0x07F0xxxx. PS2 ResetMap_Load loads it before the level (LoaderLoad's mode 0, which
// sets bits 20-23 to 0xF). Built only with NIGHTFIRE_ACTION_DATA_PS2.

#include <stdint.h>

// Loads the level's sequence bundle whole, as LoaderLoad loads any bundle, if the disc has one
void Ps2_LoadLevelSequences(uint32_t level);

// LoadableLoad and LoadableReload (Loader.h) are the PS2's here: a cutscene's loadable bundle (0x0780xxxx-0x07A0xxxx,
// its sequences and script) loaded in place of the level's sequences, into the same memory, and the sequences loaded
// back the same way by LoadableReload. The Xbox's, which load nothing (their bundles hold everything), are left out of
// this build.

#endif // PS2_PS2LOADER_H_
