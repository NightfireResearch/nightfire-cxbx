#ifndef ACTIONHELPERS_H_
#define ACTIONHELPERS_H_

// Foward declarations prevent circular dependencies
typedef struct celglist_tag celglist_tag;
typedef struct level_tag level_tag;
typedef struct world_tag world_tag;
typedef struct cel_tag cel_tag;
typedef struct obj_tag obj_tag;
typedef struct viewer_tag viewer_tag;
typedef struct HITDATA_tag HITDATA_tag;
typedef struct HUDINFO_tag HUDINFO_tag;
typedef struct HUDPANE_tag HUDPANE_tag;
typedef struct HUDPANECREATE_tag HUDPANECREATE_tag;
typedef struct SCRIPTINFO SCRIPTINFO;
typedef struct map_tag map_tag;
typedef struct BLData BLData;
typedef struct DYNAMICSOUNDS DYNAMICSOUNDS;
typedef struct AnimState AnimState;

// Hashcode, SFX and translated text names
#include "assets.h"

// Ghidra types
#include "../helpers.h"

typedef short MallocFlags;

#include "util/hashtable.h"
#include "util/LList.h"
#include "util/Stack.h"
#include "math/math.h"
#include "engine/Camera.h"
#include "engine/Collide.h"
#include "engine/Loader.h"
#include "game/Autoaim.h"
#include "game/obj/Break.h"
#include "game/obj/object.h" // for obj_tag needed by some autogen functions
#include "game/obj/control.h" // for Control_X functions
#include "game/obj/player.h" // for BLData
#include "game/weapon_stats.h"
#include "gfx/Sprite.h"
#include "ui/ui.h"
#include "ui/HUD.h"
#include "engine/Woman.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#endif //ACTIONHELPERS_H_
