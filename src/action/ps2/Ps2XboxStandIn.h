#ifndef PS2_PS2XBOXSTANDIN_H_
#define PS2_PS2XBOXSTANDIN_H_

// INTERIM, until milestone 7 of docs/ps2-data.md (the PS2's skinned meshes): what the PS2 data has but this build
// cannot yet convert is taken from the Xbox disc's copy of the same level (settings.ini XboxDiscPath). Skins and
// skeletons are byte for byte the same on both discs, and every skinned mesh has its Xbox twin, under the same
// hashcode, in the pack of the same name, whose textures are listed in the same order; so the Xbox's mesh drops in
// with the PS2's textures. Built only with NIGHTFIRE_ACTION_DATA_PS2. Remove it with milestone 7.

#include "../engine/psiGraphics.h"

// The Xbox's mesh (a KXE, in the game's heap) of the entity with this hashcode in the level being loaded, or NULL
// when there is no Xbox disc or no such entity
ModelData *Ps2_XboxStandInModel(uint32_t entityHashcode);

#endif // PS2_PS2XBOXSTANDIN_H_
