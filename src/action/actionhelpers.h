#ifndef ACTIONHELPERS_H_
#define ACTIONHELPERS_H_

// Foward declarations prevent circular dependencies
typedef struct celglist_tag celglist_tag;
typedef struct level_tag level_tag;
typedef struct world_tag world_tag;
typedef struct cel_tag cel_tag;
typedef struct obj_tag obj_tag;
typedef struct HITDATA_tag HITDATA_tag;
typedef struct HUDINFO_tag HUDINFO_tag;
typedef struct HUDPANE_tag HUDPANE_tag;
typedef struct HUDPANECREATE_tag HUDPANECREATE_tag;

// Hashcode, SFX and translated text names
#include "assets.h"

// Ghidra types
#include "../helpers.h"

typedef short MallocFlags;

#include "util/LList.h"
#include "math/math.h"
#include "engine/Collide.h"
#include "game/obj/object.h" // for obj_tag needed by some autogen functions
#include "game/obj/control.h" // for Control_X functions
#include "game/obj/player.h" // for BLData
#include "gfx/Sprite.h"
#include "ui/ui.h"
#include "ui/HUD.h"

#endif //ACTIONHELPERS_H_
