#ifndef ACTIONHELPERS_H_
#define ACTIONHELPERS_H_

// Original is 640x480, this is a clean 2x scaling
#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 960

#include <stddef.h> // for offsetof

// Foward declarations prevent circular dependencies
typedef struct celglist_tag celglist_tag;
typedef struct level_tag level_tag;
typedef struct light_tag light_tag;
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
typedef struct sprite sprite;
typedef struct SpriteInfo SpriteInfo;
typedef struct _D3DMATRIX D3DMATRIX;
typedef struct M_CONTROL M_CONTROL;
typedef struct M_ITEM M_ITEM;
typedef struct M_MANAGER M_MANAGER;
typedef struct BOT_stats_t BOT_stats_t;
typedef struct DLISTINFO_tag DLISTINFO_tag;
typedef struct Drone_tag Drone_tag;
typedef struct DIVars_tag DIVars_tag;
typedef struct DCVars_tag DCVars_tag;
typedef struct MsgObject MsgObject;
typedef struct AnimObj AnimObj;
typedef struct block_header_tag block_header_tag;
typedef struct TARGET_PLACEMENT TARGET_PLACEMENT;
typedef struct SPRITE_DRAW SPRITE_DRAW;


typedef void* ScriptPlayerCallback; // FIXME: Function pointer signature

// Hashcode, SFX and translated text names
#include "assets.h"

// Ghidra types
#include "../helpers.h"

typedef short MallocFlags;

#include "util/bin.h"
#include "util/hashtable.h"
#include "util/LList.h"
#include "util/DList.h"
#include "util/Stack.h"
#include "math/math.h"
#include "util/Random.h"
#include "engine/AINetwork.h"
#include "engine/Anim.h"
#include "engine/Camera.h"
#include "engine/Collide.h"
#include "engine/Loader.h"
#include "engine/Text.h"
#include "engine/Vision.h"
#include "engine/parsemap.h"
#include "engine/psiGraphics.h"
#include "engine/psiInput.h"
#include "engine/Woman.h"
#include "game/Autoaim.h"
#include "game/Upgrade.h"
#include "game/drone/Drone.h"
#include "game/drone/NDrone2.h"
#include "game/obj/Apocalypse.h"
#include "game/obj/Break.h"
#include "game/obj/build.h"
#include "game/obj/CamSubject.h"
#include "game/obj/Cloud.h"
#include "game/obj/Copter.h"
#include "game/obj/control.h" // for Control_X functions
#include "game/obj/Corona.h"
#include "game/obj/Creature.h"
#include "game/obj/CreepWall.h"
#include "game/obj/Destroy.h"
#include "game/obj/Door.h"
#include "game/obj/Drone_AIHints.h"
#include "game/obj/DroneSpawner.h"
#include "game/obj/Env.h"
#include "game/obj/Flicker.h"
#include "game/obj/FuseBox.h"
#include "game/obj/Grapple.h"
#include "game/obj/GT.h"
#include "game/obj/Hint.h"
#include "game/obj/Hurt.h"
#include "game/obj/Ladder.h"
#include "game/obj/Lightning.h"
#include "game/obj/Lock.h"
#include "game/obj/Mine.h"
#include "game/obj/MiniSub.h"
#include "game/obj/Monitor.h"
#include "game/obj/object.h" // for obj_tag needed by some autogen functions
#include "game/obj/OneSided.h"
#include "game/obj/PCQWorm.h"
#include "game/obj/player.h" // for BLData
#include "game/obj/RainBox.h"
#include "game/obj/Rotor.h"
#include "game/obj/ScriptPlayer.h"
#include "game/obj/Searchlight.h"
#include "game/obj/Sensor.h"
#include "game/obj/Shooter.h"
#include "game/obj/SpaceLaser.h"
#include "game/obj/SpaceMissile.h"
#include "game/obj/SS.h"
#include "game/obj/Sub.h"
#include "game/obj/ThirdCam.h"
#include "game/obj/ThirdIcon.h"
#include "game/obj/Trigger.h"
#include "game/obj/Wire.h"
#include "game/weapon_stats.h"
#include "game/sp/Locks.h"
#include "game/sp/Mission.h"
#include "game/sp/PlayerStats.h"
#include "game/sp/SwitchChannels.h"
#include "gfx/Sprite.h"
#include "ui/ui.h"
#include "ui/HUD.h"
#include "ui/Menu.h"
#include "sound/Sound.h"


#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

// globals and constants
#define glb_viewer (*(viewer_tag*(*)[11])0x001f661c)
#define glb_world (*(world_tag**)0x001f6674)
#define glb_blokes (*(BLData*(*)[4])(0x002774b8))
#define glb_players (*(obj_tag*(*)[4])(0x001f6654))

#define CONST_UP_VECTOR (*(_VECTOR*)0x0029d71c)
#define CONST_ZERO_VECTOR (*((_VECTOR*)0x0029d728))
#define GRAVITY_VECTOR (*((_VECTOR*)0x001f6648))
#define MAYBE_CONST_FORWARD_VECTOR (*((_VECTOR*)0x0029d6d4))
#define MAT_IDENTITY (*((_MATRIX*)0x0029d6e0))

#define VIDEO_FRAME_RATE U32_AT(0x0017c0f0)
#define FRAME_RATE_INT U32_AT(0x0017c0f4)
#define _FRAME_RATE FLOAT_AT(0x0017c0f8)
#define FRAME_RATE_DIV FLOAT_AT(0x0017c0fc)
#define FRAME_RATE_MUL FLOAT_AT(0x0017c100)
#define REC_FRAME_RATE FLOAT_AT(0x0017c104)

#include <stdio.h>

#define NF_ASSERT(expr, msg)                                                \
    do {                                                                    \
        if (!(expr)) {                                                      \
            printf("Assertion failed: %s (%s)\nFile: %s\nLine: %d\n",       \
                #expr, #msg, __FILE__, __LINE__);                           \
            __debugbreak();                                                 \
            while(1);                                                       \
        }                                                                   \
    } while (0)



typedef struct {
    char tbc[0x2c];
} ObjectCreationData_Basic; // This might be related to TARGET_PLACEMENT but not exactly the same?

#endif //ACTIONHELPERS_H_
