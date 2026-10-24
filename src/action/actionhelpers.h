#ifndef ACTIONHELPERS_H_
#define ACTIONHELPERS_H_

// Original is 640x480, 1280x960 is a clean 2x scaling.
//
// Higher resolutions now work - 1920x1080 runs at full speed with no measurable cost, and the window is
// created at whatever size is set here - but the menus and the positioned HUD sprites are still authored in
// 640x480 coordinates and do not move with it. See the longer note in src/inject_action.cpp before raising
// this.
#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480
// #define SCREEN_WIDTH 1280
// #define SCREEN_HEIGHT 960
// #define SCREEN_WIDTH 1920
// #define SCREEN_HEIGHT 1080

#include <stddef.h> // for offsetof
#include <stdint.h>

// Windows likes naming shit like this. ULONG is 32 bits, as driving/platform/Win32Types.h has it on every host.
typedef void* HANDLE;
typedef void* PVOID;
#if defined(_WIN32) || UINTPTR_MAX == 0xffffffffu
typedef unsigned long ULONG;
#else
typedef uint32_t ULONG;
#endif

typedef struct IO_STATUS_BLOCK IO_STATUS_BLOCK;

// Foward declarations prevent circular dependencies
typedef struct celglist_tag celglist_tag;
typedef struct level_tag level_tag;
typedef struct light_tag light_tag;
typedef struct world_tag world_tag;
typedef struct cel_tag cel_tag;
typedef struct obj_tag obj_tag;
typedef struct viewer_tag viewer_tag;
typedef struct HITDATA_tag HITDATA_tag;
typedef struct HITTEST_tag HITTEST_tag;
typedef struct HUDINFO_tag HUDINFO_tag;
typedef struct HUDPANE_tag HUDPANE_tag;
typedef struct HUDPANECREATE_tag HUDPANECREATE_tag;
typedef struct SCRIPTINFO SCRIPTINFO;
typedef struct map_tag map_tag;
typedef struct BLData BLData;
typedef struct weapon_definition_tag weapon_definition_tag;
typedef struct DYNAMICSOUNDS DYNAMICSOUNDS;
typedef struct AnimState AnimState;
typedef struct sAnimSet_tag sAnimSet_tag;
typedef struct sprite sprite;
typedef struct SpriteInfo SpriteInfo;
typedef struct _D3DMATRIX D3DMATRIX;
typedef struct MatrixChainNode MatrixChainNode;
typedef struct M_CONTROL M_CONTROL;
typedef struct M_ITEM M_ITEM;
typedef struct M_MANAGER M_MANAGER;
typedef struct M_PAGE M_PAGE;
// The multiplayer characters (Ghidra's enum): mp_characters' identifiers, a bot's skin, named after each row's
// CHAR_*_FULLNAME label
enum BotNum : unsigned int {
    BOT_BOND = 0,
    BOT_DRAKE = 1,
    BOT_ROOK = 2,
    BOT_KIKO = 3,
    BOT_ALURA = 4,
    BOT_DOMINIQUE = 5,
    BOT_GUARD = 6,
    BOT_BLACKOPS = 7,
    BOT_YAKUZA = 8,
    BOT_COMMANDO = 9,
    BOT_SOLDIER = 0xa,
    BOT_NINJA = 0xb,
    BOT_BONDTUX = 0xc,
    BOT_DRAKESUIT = 0xd,
    BOT_BONDSPACE = 0xe,
    BOT_GOLDFINGER = 0xf,
    BOT_RENARD = 0x10,
    BOT_SCARAMANGA = 0x11,
    BOT_GALORE = 0x12,
    BOT_XMASJONES = 0x13,
    BOT_WAILIN = 0x14,
    BOT_XENIA = 0x15,
    BOT_MAYDAY = 0x16,
    BOT_ELEKTRA = 0x17,
    BOT_JAWS = 0x18,
    BOT_SAMEDI = 0x19,
    BOT_ODDJOB = 0x1a,
    BOT_NICKNACK = 0x1b,
    BOT_ZORIN = 0x1c,
};
typedef struct M_MESSAGE M_MESSAGE;
typedef struct M_SCRIPT M_SCRIPT;
typedef struct M_KEYFRAME M_KEYFRAME;
typedef struct MENU_COMPONENT MENU_COMPONENT;
typedef struct MENU_COMPONENTPIECE MENU_COMPONENTPIECE;
typedef struct MENU_COMPONENTSET MENU_COMPONENTSET;
typedef struct REWARDINFO_tag REWARDINFO_tag;
typedef struct BOT_stats_t BOT_stats_t;
typedef struct DLISTINFO_tag DLISTINFO_tag;
typedef struct Drone_tag Drone_tag;
typedef struct DIVars_tag DIVars_tag;
typedef struct DCVars_tag DCVars_tag;
typedef struct MsgObject MsgObject;
typedef struct StateMachineInfo_tag StateMachineInfo_tag;
typedef struct AnimObj AnimObj;
typedef struct block_header_tag block_header_tag;
typedef struct TARGET_PLACEMENT TARGET_PLACEMENT;
typedef struct SPRITE_DRAW SPRITE_DRAW;
typedef struct edl_header_t edl_header_t;

// What a script player calls on script event 15: the object of the stream before the event's, the event's
// argument, and the context it was given (FuseBox_Register)
typedef void ScriptPlayerCallback(obj_tag *obj, unsigned short arg, void *context);

// Hashcode, SFX and translated text names
#include "assets.h"

// Ghidra types
#include "../helpers.h"
#include "engine/Placement.h"   // every placement struct starts with one

#include "MallocFlags.h"

// QuickSort's comparison selector (Ghidra's COMP_FUNC). Only Compare_CustomFunction uses QuickSort's last argument.
typedef enum {
    Compare_Sprites        = 0,
    Compare_Unknown1       = 1, // Ghidra: Unknown1 - falls to QuickSort's default, CompLight
    Compare_HitData        = 2,
    Compare_DrawCels       = 3,
    Compare_AlphaObj       = 4,
    Compare_MsgTxt         = 5,
    Compare_TargetObj      = 6,
    Compare_MenuCtrl       = 7,
    Compare_Strings        = 8,
    Compare_BoxList        = 9,
    Compare_CustomFunction = 10,
    Compare_Force_U32      = 0x7FFFFFFF
} COMP_FUNC;
static_assert(sizeof(COMP_FUNC) == 4, "COMP_FUNC is a 32-bit argument");

#include "main.h"
#include "util/bin.h"
#include "util/hashtable.h"
#include "util/LList.h"
#include "util/DList.h"
#include "util/Stack.h"
#include "math/math.h"
#include "util/Random.h"
#include "engine/AINetwork.h"
#include "engine/Anim.h"
#include "game/mp/MPLimits.h"
#include "engine/Camera.h"
#include "engine/Collide.h"
#include "engine/EDL.h"
#include "engine/FS.h"
#include "engine/Inflate.h"
#include "engine/Loader.h"
#include "engine/Script.h"
#include "engine/Text.h"
#include "engine/Vision.h"
#include "engine/parsemap.h"
#include "engine/psiGraphics.h"
#include "engine/psiInput.h"
#include "engine/Woman.h"
#include "engine/XboxSettings.h"
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
#include "game/obj/Env.h"
#include "game/obj/Flicker.h"
#include "game/obj/FuseBox.h"
#include "game/obj/Grapple.h"
#include "game/obj/GT.h"
#include "game/obj/GunImp.h"
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
#include "game/obj/Pickup.h"
#include "game/obj/Player.h" // for BLData
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
#include "game/obj/PlayerWeapon.h"
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

// glb_viewer's views (Camera_CreateCameras): the players' are 0 to 3, by player number
enum ViewerIndex {
    VIEWER_SCRIPT = 4,          // a cut scene's camera
    VIEWER_HUD = 5,             // the HUD's sprites and 3D parts, in a world of its own
    VIEWER_FADE = 6,            // the game flow's fades
    VIEWER_MENU = 7,            // the menus' sprites, in a world of its own
    VIEWER_8 = 8,
    VIEWER_9 = 9,
    VIEWER_LOAD_SCREEN = 10,    // draws the loading screen (LoadScreen_Draw)
    NUM_VIEWERS
};

// globals and constants
extern viewer_tag *glb_viewer[NUM_VIEWERS];
extern world_tag *glb_world;
extern BLData *glb_blokes[NUM_PLAYERS];
extern obj_tag *glb_players[NUM_PLAYERS];

extern _VECTOR CONST_UP_VECTOR;
extern _VECTOR CONST_ZERO_VECTOR;
extern _VECTOR GRAVITY_VECTOR;
extern _VECTOR CONST_X_VECTOR;
extern _MATRIX MAT_IDENTITY;

extern uint32_t VIDEO_FRAME_RATE;
extern uint32_t FRAME_RATE_INT;
extern float _FRAME_RATE;
extern float FRAME_RATE_DIV;
extern float FRAME_RATE_MUL;
extern float REC_FRAME_RATE;

#include <stdio.h>

#ifdef _WIN32
#define NF_DEBUG_BREAK() __debugbreak()
#else
#define NF_DEBUG_BREAK() __builtin_trap()
#endif

#define NF_ASSERT(expr, msg)                                                \
    do {                                                                    \
        if (!(expr)) {                                                      \
            printf("Assertion failed: %s (%s)\nFile: %s\nLine: %d\n",       \
                #expr, #msg, __FILE__, __LINE__);                           \
            NF_DEBUG_BREAK();                                               \
            while(1);                                                       \
        }                                                                   \
    } while (0)

// The GameCube build's debug checks, which the Xbox build compiled out: a printf and carry on, exactly as there
// (docs/gamecube-checks.md lists them all, and which of our functions have them). The message is the GameCube's
// own. NfWarnMuted (main.cpp) silences them, for the shadow tests that feed bad input on purpose.
extern int NfWarnMuted;
#define NF_WARN(...)                                                        \
    do {                                                                    \
        if (!NfWarnMuted)                                                   \
            printf(__VA_ARGS__);                                            \
    } while (0)
#define NF_WARN_IF(cond, ...)                                               \
    do {                                                                    \
        if (cond)                                                           \
            NF_WARN(__VA_ARGS__);                                           \
    } while (0)

#endif //ACTIONHELPERS_H_
