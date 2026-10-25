#ifndef PS2_PS2CONVERT_H_
#define PS2_PS2CONVERT_H_

// The PS2's map blocks made the Xbox's as parsemap meets them (docs/ps2-data.md), in the game's heap, which is wiped
// with the level. Built only with NIGHTFIRE_ACTION_DATA_PS2.

#include "../engine/psiGraphics.h"
#include "../sound/psiSound.h"      // SampleHeaderData

// Block 0x12, a texture's palette, after its texels (block 0x16, which left their address in the header's
// textureInfo): the header is pointed at an Xbox texture header (KXT) of the decoded frames instead. hashcode is the
// texture's, from its palette header.
void Ps2_ConvertMapTexture(bmpHeaderEntry *header, uint32_t hashcode, const uint8_t *palette, uint32_t paletteBytes);

// Block 0x2b, a head's morph targets: kept for the mesh block after it
void Ps2_MorphData(const uint8_t *block, uint32_t blockBytes);

// Block 0x2d, a mesh: the Xbox's (a KXE) for the entity's geom_idx, or NULL when it has nothing to draw
ModelData *Ps2_ConvertModel(const uint8_t *block, uint32_t blockBytes);

// A sound bank just loaded (LoadSoundBank): its samples, SPU ADPCM in the .SBF with the SPU's pitch for a rate, made
// Xbox ADPCM in memory of their own with the rate in Hz and the loop's start in Xbox ADPCM bytes; the .SBF's memory is
// given back
void Ps2_ConvertSoundBank(SampleHeaderData *samples, int count);

#endif // PS2_PS2CONVERT_H_
