#ifndef GC_GCANIM_H_
#define GC_GCANIM_H_

// The GameCube's own animation code where its data needs it in place of the Xbox's (docs/gc-data.md)

#include "../Hashcodes.h"

// The skin the level uses in place of skin: on the GameCube only two levels swap one, where the Xbox's
// FixupSkinHashcode swaps the "Spec" skins its bundles alone hold
HASHCODE GcFixupSkinHashcode(HASHCODE skin);

#endif // GC_GCANIM_H_
