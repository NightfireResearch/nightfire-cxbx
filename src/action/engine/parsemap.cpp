#include "parsemap.h"
#include "../game/obj/Break.h"

#include "psiFile.h"
#include "psiGraphics.h"
#include "../memory.h"
#include "../game/mp/multiplayer.h"
#include "../game/obj/car.h"
#include "../game/obj/DroneSpawner.h"
#include "../game/obj/DynamicObject.h"
#include "../game/obj/Emitter.h"
#include "../game/obj/GunImp.h"
#include "../game/obj/LeafGen.h"
#include "../game/obj/Light.h"
#include "../game/obj/Pickup.h"
#include "../game/obj/RigidBody.h"
#include "../game/obj/Ripples.h"
#include "../game/obj/Rotor.h"
#include "../game/obj/Switch.h"
#include "../game/view.h"
#include "../game/obj/build.h"
#include "../util/bin.h"
#include "../util/hashtable.h"
#include "AIPath.h"
#include "Loader.h"
#include "celglist.h"
#include "viewer.h"

#include <string.h>
#include "../sound/Sound.h"

// XBE_GLOBAL(0x00274c80, 0x4)
celglist_tag *pCurrCelList;
// XBE_GLOBAL(0x00274c70, 0x4)
uint *FileNextBlock;
// XBE_GLOBAL(0x00274c68, 0x1)
static uint8_t StartingNewMap;
// XBE_GLOBAL(0x00274b50, 0x4)
map_tag *m_pmap;

typedef struct {
    uint32_t size; // maybe block_header_tag?
    HASHCODE hashcode;
    uint32_t applyFlagsToObject;
    float boundSphereX;
    float boundSphereY;
    float boundSphereZ;
    float boundSphereRadius;
    _VECTOR extentMin;
    _VECTOR extentMax;
    char name[32]; // unsure on size, at least 32 bytes long
} block_entity_data;

static_assert(sizeof(block_entity_data) == 0x54, "Bad size for block_entity_data");

// XBE_GLOBAL(0x00274b34, 0x8)
block_header_tag DynamicBH;
extern void *DynamicPtr;
// XBE_GLOBAL(0x00274c88, 0x4)
static uint32_t MapHashCode;
// XBE_GLOBAL(0x00274ca4, 0x4)
static uint32_t ParseMap_State;
// XBE_GLOBAL(0x00274b58, 0x100)
static char filename[0x100];
extern uint8_t *FileLastBlock;
extern uint8_t *FileDiscard;
// XBE_GLOBAL(0x00274c6c, 0x4)
uchar *FileBase;
// XBE_GLOBAL(0x00274c64, 0x4)
uint32_t BinFileVersion;
// XBE_GLOBAL(0x00274c5c, 0x4)
uint *pBinFileHeader;

// A map file loaded whole for parsing: its first word, then its blocks
// AUTOINJECT
void psiFileLoadForParse(char *name) {
    int size;
    uint *file = (uint *)psiFileLoad(name, 0x1704, &size);
    FileNextBlock = file;
    FileBase = (uchar *)file;
    FileLastBlock = (uint8_t *)file + size;
    BinFileVersion = *FileNextBlock++;
    pBinFileHeader = FileNextBlock;
}

// AUTOINJECT
bool parsemap_parsemap(uint hashcode, bool secondPass) {

    switch(ParseMap_State) {
        case 0: {
            // Load the file into RAM?
            const char* filePath = "";
            sprintf(filename, "%s%8.8X.bin", filePath, hashcode);
            psiFileLoadForParse(filename);
            MapHashCode = hashcode;
            DynamicBH.size = 0;
            DynamicPtr = NULL;
            StartingNewMap = 1;
            ParseMap_State = 1;
            return true;
        }
        case 1: {
            // Run parsemap_parsenextblock until completion
            bool complete = false;
            do {
                complete = parsemap_parsenextblock(secondPass);
            } while(!complete);

            ParseMap_State = 2;
            return true;
        }
        case 2: {
            // If in the second pass, do stuff with dynamic data?
            if(!secondPass) {
                parsemap_block_map_data_dynamic(&DynamicBH, (uchar*)DynamicPtr, false);
            }
            ParseMap_State = 3;
            return true;
        }
        case 3: {
            // If in the second pass, shrink memory?
            if(!secondPass) {
                Mem_Shrink((void**)&FileDiscard, (uint)(FileLastBlock - FileDiscard));
            }
            ParseMap_State = 0;
            break;
        }

    }

    return false; // Nothing more to do
}

// AUTOINJECT
void parsemap_block_entity_params(void) {

    // Load the given cel from the file

    celglist_tag* currentCelGlist = pCurrCelList;

    block_entity_data* data = (block_entity_data*)FileNextBlock;

    currentCelGlist->applyFlagsToObject = data->applyFlagsToObject;
    currentCelGlist->boundSphere.x = data->boundSphereX;
    currentCelGlist->boundSphere.y = data->boundSphereY;
    currentCelGlist->boundSphere.z = data->boundSphereZ;
    currentCelGlist->boundSphere.r = data->boundSphereRadius;
    currentCelGlist->extentMin.x = data->extentMin.x;
    currentCelGlist->extentMin.y = data->extentMin.y;
    currentCelGlist->extentMin.z = data->extentMin.z;
    currentCelGlist->extentMax.x = data->extentMax.x;
    currentCelGlist->extentMax.y = data->extentMax.y;
    currentCelGlist->extentMax.z = data->extentMax.z;
    // Unsure on the size, this is a null-terminated char array, contains a debug name
    // for each object / script.
    currentCelGlist->name = data->name;

    // If loading a hashcode-referenced piece of geometry, add it to the hashmap
    if(data->hashcode != 0xFFFFFFFF) {
        hashtable_additem(data->hashcode, currentCelGlist);
    }

    // Map textures
    if(StartingNewMap) {
        psiCreateMapTextures(m_pmap);
        StartingNewMap = false;
    }

    psiCreateEntityGfx(pCurrCelList, m_pmap, MemType);

    pCurrCelList++;

}

// The header of the block currently being parsed, filled in by parsemap_parsenextblock (still original).
// XBE_GLOBAL(0x00274b40, 0x8)
block_header_tag CurrentBH;

// Running total of the "old style" (identifier 5) collision blocks' leading size word. Only this function touches it.
// XBE_GLOBAL(0x00274ca0, 0x4)
static uint32_t OldCollisionSize;

// Collision block identifiers: 5 is the older layout, with 16 bytes (a size word and padding) before the data
#define COLL_BLOCK_OLD 5

// How far each collision box is grown on every side, so that a point exactly on a face counts as inside
static const float kCollBoxInflate = 0.01f; // the original's float at 0x0015d31c

#pragma pack(push, 1)
typedef struct {
    uint32_t size;          // 0x0 - block header
    uint32_t identifier;    // 0x4 - 4, or COLL_BLOCK_OLD
    ushort numCollBoxes;    // 0x8
    ushort countB;          // 0xa
    ushort countC;          // 0xc
    ushort _pad;            // 0xe
    // 0x10: the data - [countC] collDataC (64 bytes each), [numCollBoxes] COLLBOX_tag, [countB] collDataB
    // (8 bytes each), then the rest ("D"). An identifier-5 block has 16 more bytes first, the first word of
    // which is added to OldCollisionSize.
} block_coll_data_header;
#pragma pack(pop)

static_assert(offsetof(block_coll_data_header, numCollBoxes) == 0x8, "Bad offset of numCollBoxes");
static_assert(offsetof(block_coll_data_header, countC) == 0xc, "Bad offset of countC");
static_assert(sizeof(block_coll_data_header) == 0x10, "Bad size for block_coll_data_header");

// Points the current cel's collision data at the arrays inside the loaded block (no copying), allocating the
// small COLLDATA_tag header the first time a cel gets collision.
//
// Quirk kept from the original (and the PS2 build): the loop meant to grow every box by kCollBoxInflate never
// advances its pointer, so the FIRST box is grown numCollBoxes times over and the others not at all.
//
// AUTOINJECT
void parsemap_block_Coll_Data_New(void) {

    block_coll_data_header *header = (block_coll_data_header *)FileNextBlock;
    // GC check (0x800595dc): the collision block versions this code reads (it parses on regardless)
    NF_WARN_IF(header->identifier != 4 && header->identifier != COLL_BLOCK_OLD,
               ">>>FATAL<<<< : COLLISION VERSION MIS-MATCH %d\n", header->identifier);
    ushort numCollBoxes = header->numCollBoxes;
    ushort countB = header->countB;
    uint sizeofC = (uint)header->countC * sizeof(collDataC);

    uchar *data = (uchar *)(header + 1);
    if(header->identifier == COLL_BLOCK_OLD) {
        OldCollisionSize += *(uint32_t *)data;
        data += 0x10;
    }

    celglist_tag *cel = pCurrCelList;
    if(cel->colldata == NULL) {
        if(numCollBoxes == 0) {
            // Returns before the MemStats update below
            cel->colldata = NULL;
            return;
        }
        // Rounded up to 4 (0x18, not the 0x16 of COLLDATA_tag on 32-bit), as the original
        cel->colldata = (COLLDATA_tag *)Mem_Malloc((sizeof(COLLDATA_tag) + 3) & ~(size_t)3, (MallocFlags)0x1a04, 0);
    }

    COLLDATA_tag *coll = cel->colldata;

    coll->dataStartC = (collDataC *)data;
    data += sizeofC;
    coll->sizeofC = (short)sizeofC;     // a 16-bit field: truncated as in the original for countC >= 1024
    coll->countB = (short)countB;
    coll->collBoxes = (COLLBOX_tag *)data;
    data += numCollBoxes * sizeof(COLLBOX_tag);
    coll->numCollBoxes = numCollBoxes;
    coll->dataStartB = (collDataB *)data;
    coll->dataStartD = data + countB * sizeof(collDataB);

    for(uint i = numCollBoxes; i != 0; i--) {
        COLLBOX_tag *box = coll->collBoxes; // never advanced - see above
        box->boundMax.x = box->boundMax.x + kCollBoxInflate;
        box->boundMax.y = box->boundMax.y + kCollBoxInflate;
        box->boundMax.z = box->boundMax.z + kCollBoxInflate;
        box->boundMin.x = box->boundMin.x - kCollBoxInflate;
        box->boundMin.y = box->boundMin.y - kCollBoxInflate;
        box->boundMin.z = box->boundMin.z - kCollBoxInflate;
    }

    // Count the block, less its header word, as collision memory
    MemStats[0] = MemStats[0] + CurrentBH.size - 4;
}

#pragma pack(push, 1)

typedef struct TARGET_PLACEMENT {
    ushort entityNum;
    char undef[2];
    HASHCODE glistHashcode;
    ObjectPlacementType placementType;
    _VECTOR pos;
    _VECTOR rot;
    quaternion_tag quat;
    _VECTOR unknown;
    ushort splinePoints;            // 0x40 - with a spline: the level_tag's
    ushort splineLength;            // 0x42
    ushort spline;                  // 0x44 - the map's path
    char _unknown46[2];
    uint keyCount;                  // 0x48 - the key pairs (PLACEMENT_KEY) after it
} TARGET_PLACEMENT;

typedef struct PLACEMENT_KEY {
    uint key;
    uint value;
} PLACEMENT_KEY;

static_assert(sizeof(TARGET_PLACEMENT) == 0x4c, "TARGET_PLACEMENT is 0x4c bytes");

static_assert(offsetof(TARGET_PLACEMENT, placementType) == 0x8, "Bad offset of placementType");
static_assert(offsetof(TARGET_PLACEMENT, pos) == 0xc, "Bad offset of pos");

typedef struct {
    ObjectCreationData_Basic basicCreation;
    ushort teamId;
    char _likely_pad_1[2];
} Create_MPSpawnPoint_Params;

typedef struct {
    ObjectCreationData_Basic basicCreation;
    int range;// 0x2c
    uchar r;
    char _pad_1[3];
    uchar g;
    char _pad_2[3];
    uchar b;
    char _pad_3[3];
    undefined2 unknown1;
    char _pad_4[2];
    undefined2 unknown2;
    char _pad_5[2];
    undefined2 unknown3;
    char _pad_6[2];
    undefined2 unknown4;
    char _pad_7[2];
    int unknown5;
} Create_Light_Params;

typedef struct {
    ObjectCreationData_Basic basicCreation;
    ushort unknown_2c;
    char pad_1[2];
    ushort unknown_30;
    char pad_2[2];
    uint unknown_34;
    uint unknown_38;
    uint unknown_3c;
    ushort unknown_40;
    char pad_3[2];
    uint unknown_44;
} Create_Pickup_Params;

typedef struct {
    ObjectCreationData_Basic basicCreation;
    ushort unknown_2c;
    char pad_1[2];
    char unknown_30;
    char pad_2[3];
    ushort unknown_34;
    char pad_3[2];
    ushort unknown_38;
    char pad_4[2];
    char unknown_3c;
} Create_SkyObject_Params;

typedef struct {
    ObjectCreationData_Basic basicCreation;
    uchar unknown_2c; // Unused? Switch polarity?
    uchar _pad_1[3];
    uchar switchChannel;
} Create_Rotor_Params;

#pragma pack(pop)

// AUTOINJECT
void parsemap_create_dynamic_objects(TARGET_PLACEMENT* placement, level_tag* lvl, ObjectPlacementType type, celglist_tag* celglist, void* param_5, void* param_6, char doCreation) {

    // Create local copies that can be passed by reference, for some reason?
    _VECTOR pos;
    _VECTOR rot;
    quaternion_tag quat;
    _VECTOR unknown; // Only used in Drone_AIVolume creation - scope down?
    _MATRIX mtx; // Only used in Emitter creation - scope down?
    Vec_Copy(&placement->pos, &pos);
    Vec_Copy(&placement->rot, &rot);
    Quat_Copy(&quat, &placement->quat);
    Vec_Copy(&placement->unknown, &unknown);

    // Almost all are just dependent on doCreation
    // Exceptions:
    // - A few which are additionally dependent on singleplayer vs multiplayer
    // - Searchlight
    //
    // I've implemented as per the disassembly, rather than breaking out the doCreation check.
    //
    // A lookup table of creation functions doesn't work because a few of the cases do quirky things,
    // like swapping order of args or requiring additional constants. A refactor could fix this.
    switch(type) {

        case Place_Drone:
            if(doCreation)
                Drone_Create(&pos, &rot, lvl);
            return;

        case Place_Breakable:
            if(doCreation)
                Break_Create(&pos, &rot, celglist, lvl);
            return;

        case Place_Ripples:
            if(doCreation)
                Ripples_Create(&pos, &rot, lvl, celglist);
            return;

        case Place_Ladder:
            if(doCreation)
                Ladder_Create(&pos, &rot, lvl, celglist);
            return;

        case Place_PlayerNewStartPos:
            if(doCreation && !MPSettings.isMultiplayer)
                Player_AddNewStartPos(&pos, &rot, 1, lvl);
            return;

        case Place_MPSpawnPoint:
            if(doCreation && MPSettings.isMultiplayer)
                MP_RegisterSpawnPoint(&pos, &rot, ((Create_MPSpawnPoint_Params*)(lvl))->teamId);
            return;

        case Place_MPObject:
            if(doCreation && MPSettings.isMultiplayer)
                MP_RegisterMPObject(&pos, &rot, lvl, celglist);
            return;

        case Place_GunTurret:
            if(doCreation)
                GT_Create(&pos, &rot, &quat, lvl, celglist, 0);
            return;

        case Place_Sensor:
            if(doCreation)
                Sensor_Create(&pos, &rot, lvl, celglist);
            return;

        case Place_Switch:
            if(doCreation)
                Switch_Create(&pos, &rot, lvl, celglist);
            return;

        case Place_SkyObject:
            if(doCreation) {
                Create_SkyObject_Params *skyObj = (Create_SkyObject_Params*)lvl;
                View_AddSkyObj(skyObj->unknown_2c, celglist, &pos, &rot, 0, skyObj->unknown_30, skyObj->unknown_34, skyObj->unknown_38, skyObj->unknown_3c);
            }
            return;

        case Place_LeafGen:
            if(doCreation)
                LeafGen_Create(&pos, &rot, lvl, celglist);
            return;

        case Place_Lightning:
            if(doCreation)
                Lightning_Create(&pos, &rot, lvl);
            return;

        case Place_AddNewStartPos2:
            if(doCreation && !MPSettings.isMultiplayer)
                Player_AddNewStartPos(&pos, &rot, 0, lvl);
            return;

        case Place_Lock:
            if(doCreation)
                Lock_Create(&pos, &rot, lvl, celglist);
            return;

        case Place_Copter:
            if(doCreation)
                Copter_Create(&pos, &rot, lvl);
            return;

        case Place_Monitor:
            if(doCreation)
                Monitor_Create(&pos, &rot, lvl, celglist);
            return;

        case Place_FuseBox:
            if(doCreation)
                FuseBox_Create(&pos, &rot, lvl, celglist);
            return;

        case Place_RainBox:
            if(doCreation)
                RainBox_Create(&pos, &rot, lvl, celglist);
            return;

        case Place_Hint:
            if(doCreation)
                Hint_Create(&pos, &rot, lvl, celglist);
            return;

        case Place_GunImp:
            if(doCreation)
                GunImp_Create(&pos, &quat, celglist);
            return;

        case Place_PCQWorm:
            if(doCreation)
                PCQWorm_Create(&pos, &rot, lvl, celglist);
            return;

        case Place_Sub:
            if(doCreation)
                Sub_Create(&pos, &rot, lvl);
            return;

        case Place_MiniSub:
            if(doCreation)
                MiniSub_Create(&pos, &rot, celglist, lvl);
            return;

        case Place_SpaceLaser:
            if(doCreation)
                Create_SpaceLaser(&pos, &rot, lvl);
            return;

        case Place_SpaceMissile:
            if(doCreation)
                Create_SpaceMissile(&pos, &rot, lvl);
            return;

        case Place_Grapple:
            if(doCreation)
                Grapple_Create(&pos, &rot, lvl, celglist);
            return;

        case Place_DynamicObject:
            if(doCreation)
                DynamicObject_Create(&pos, &rot, lvl, celglist);
            return;

        case Place_Wire:
            if(doCreation)
                Wire_Create(&pos, &rot, lvl, celglist);
            return;

        case Place_CreepWall:
            if(doCreation)
                CreepWall_Create(&pos, &rot, lvl, celglist);
            return;

        case Place_InverterTrigger:
            if(doCreation)
                InverterTrigger_Create(lvl);
            return;

        case Place_Apocalypse:
            if(doCreation)
                Apocalypse_Create(&pos, &rot, lvl, celglist);
            return;

        case Place_Light:
            if(doCreation) {
                Create_Light_Params * lightParams = (Create_Light_Params*)(lvl);
                Light_Create(&pos, lightParams->r, lightParams->g, lightParams->b,
                            (float)lightParams->range,
                            lightParams->unknown1, -1, 0, 1.0f,
                            lightParams->unknown2, lightParams->unknown3, lightParams->unknown4, lightParams->unknown5);
            }
            return;

        case Place_Rotor:
            if(doCreation) {
                Create_Rotor_Params * rotorInfo = (Create_Rotor_Params*) lvl;
                rotor_init(&pos, &rot, celglist, 0,rotorInfo->unknown_2c, rotorInfo->switchChannel);
            }
            return;

        case Place_Shooter:
            if(doCreation)
                Shooter_Create(&pos, &rot, lvl, celglist);
            return;

        case Place_Cloud:
            if(doCreation)
                Cloud_Create(&pos, &rot, lvl, celglist);
            return;

        case Place_OneSided:
            if(doCreation)
                OneSided_Create(&pos, &rot, celglist);
            return;

        case Place_RigidBody:
            if(doCreation)
                RB_Create(&pos, &rot, celglist, lvl);
            return;

        case Place_ScriptPlayer:
            if(doCreation)
                SP_CreateScriptPlayer(&pos, &rot, lvl, NULL, NULL, NULL);
            return;

        case Place_Car:
            if(doCreation)
                Car_Create(&pos, &rot, celglist, lvl);
            return;

        case Place_Door:
            if(doCreation)
                Door_Create(&pos, &rot, lvl, celglist);
            return;

        case Place_Trigger:
            if(doCreation)
                Trigger_Create(&pos, &rot, lvl, celglist);
            return;

        case Place_Searchlight:
            // IMPORTANT: This does NOT check for doCreation - unclear why this is the exception to the rule
            Searchlight_Create(&pos, &rot, lvl, celglist);
            return;

        case Place_Env:
            if(doCreation)
                Env_Create(&pos, &rot, celglist, lvl);
            return;

        case Place_Creature:
            if(doCreation)
                Creature_Create(&pos, &rot, celglist, lvl);
            return;

        case Place_Destroy:
            if (doCreation)
                Destroy_Create(&pos, &rot, lvl, celglist, (celglist_tag*)param_6);
            return;

        case Place_SS:
            if(doCreation)
                SS_Create(&pos, &rot, lvl, celglist);
            return;

        case Place_Flicker:
            if(doCreation)
                Flicker_Create(&pos, &rot, lvl, celglist);
            return;

        case Place_Hurt:
            if(doCreation)
                Hurt_Create(&pos, &rot, lvl, celglist);
            return;

        case Place_DroneCoverCorner:
            if(doCreation)
                Drone_CoverCornerNode(&pos, &rot, lvl);
            return;

        case Place_DroneCoverLow:
            if(doCreation)
                Drone_CoverLowNode(&pos, &rot, lvl);
            return;

        case Place_ThirdCam:
            if(doCreation)
                ThirdCam_Create(&pos, &rot, lvl, celglist);
            return;

        case Place_TriggerLoadLevel:
            if(doCreation)
                Trigger_LoadLevelCreate(&pos, &rot, lvl, celglist);
            return;

        case Place_DroneAIPoint:
            if(doCreation)
                Drone_AIPoint(&pos, &rot, lvl);
            return;

        case Place_TriggerTouchOnce:
            if(doCreation)
                Trigger_TouchOnce(&pos, &rot, lvl, celglist);
            return;

        case Place_TriggerTouch:
            if(doCreation)
                Trigger_Touch(&pos, &rot, lvl, celglist);
            return;

        case Place_TriggerMultiplexIn:
            if(doCreation)
                Trigger_MultiplexIn(&pos, &rot, lvl, celglist);
            return;

        case Place_TriggerMultiplexSIn:
            if(doCreation)
                Trigger_MultiplexSIn(&pos, &rot, lvl, celglist);
            return;

        case Place_TriggerMultiplexOut:
            if(doCreation)
                Trigger_MultiplexOut(&pos, &rot, lvl, celglist);
            return;

        case Place_TriggerMultiplexOrIn:
            if(doCreation)
                Trigger_MultiplexOrIn(&pos, &rot, lvl, celglist);
            return;

        case Place_Pickup:
            if(doCreation) {
                Create_Pickup_Params *pickupParams = (Create_Pickup_Params*)(lvl);
                Pickup_Create(&pos, &rot, NULL, celglist, pickupParams->unknown_2c, pickupParams->unknown_30, pickupParams->unknown_34, pickupParams->unknown_38, pickupParams->unknown_3c, 1,  pickupParams->unknown_40, 0,  pickupParams->unknown_44);
            }
            return;

        case Place_DroneSpawner:
            if(doCreation)
                DroneSpawner_Create(&pos, &rot, lvl);
            return;

        case Place_Emitter:
            if(doCreation) {
                Quat_QuatToMat(&quat, &mtx);
                Vec_Copy(&pos, Mat_Position(mtx));
                Emitter_CreatePlist(&mtx, lvl);
            }
            return;

        case Place_EnvTree:
            if(doCreation)
                Env_Tree_Create(&pos, &rot, lvl);
            return;

        case Place_TriggerMoviePlayer:
            if(doCreation)
                Trigger_MoviePlayer(&pos, &rot, lvl, celglist);
            return;

        case Place_DroneAIVolume1:
        case Place_DroneAIVolume2:
            if(doCreation)
                Drone_AIVolume_Create(&pos, &rot, &unknown, lvl, celglist);
            return;

        case Place_SoundTrigger:
            if(doCreation)
                SoundTrigger_Create(&pos, &rot, lvl, celglist);
            return;

        case Place_ThirdIcon:
            if(doCreation)
                ThirdIcon_Create(&pos, &rot, lvl, celglist);
            return;

        case Place_MusicTrigger:
            if(doCreation)
                MusicTrigger_Create(&pos, &rot, lvl, celglist);
            return;

        case Place_Corona:
            if(doCreation)
                Corona_Create(&pos, &rot, lvl, celglist);
            return;

        case Place_CamSubject:
            if(doCreation)
                CamSubject_Create(&pos, &rot, lvl, celglist);
            return;

        case Place_Mine:
            if(doCreation)
                Mine_Create(&pos, &rot, lvl, celglist);
            return;

        default:
            return;
    }

}

// ---- Reading a map file's blocks. Each starts with a word: its identifier in the top byte, its size (the word
// ---- included) in the rest. A version 1 file has a table of the blocks' offsets after its first word.

// XBE_GLOBAL(0x00274c94, 0x4)
uint *pFileDiscardHeader;
// XBE_GLOBAL(0x00274c9c, 0x4)
uint *prev_block;
// XBE_GLOBAL(0x00274c84, 0x4)
struct MAPHEADER_tag *pBMH;
// XBE_GLOBAL(0x00274c8c, 0x4)
uint32_t MapEntityCount;
// XBE_GLOBAL(0x00274c7c, 0x4)
celglist_tag *pCelList;
// The map's paths, a word each as on the Xbox (the dynamic placements read entries before they are filled, and
// the placements that read them are not made)
// XBE_GLOBAL(0x00274b4c, 0x4)
Ptr32<void> *m_paths;
// XBE_GLOBAL(0x00274c60, 0x4)
Ptr32<void> *m_ppaths;
// The texture tables walked together, an entry a texture: the palette headers, psiCreateMapTextures's results and the
// texture headers
// XBE_GLOBAL(0x00274b30, 0x4)
struct PALHEADER_tag *pPallHeader;
// XBE_GLOBAL(0x00274b3c, 0x4)
texDataEntry *pTexData;
// XBE_GLOBAL(0x00274b48, 0x4)
bmpHeaderEntry *pBmpHeader;

// The map header block's data
typedef struct MAPHEADER_tag {
    uint unknown0;
    uint entityCount;           // 0x04
    uint pathCount;             // 0x08
} MAPHEADER_tag;

// A palette header (8 bytes)
typedef struct PALHEADER_tag {
    uint unknown0;
    uint data;                  // 0x04 - the palette's hashcode (-1 for none), then the address of its data
} PALHEADER_tag;

// The block ids parsemap_handle_block_id acts on
enum {
    BLOCK_ENTITY_PARAMS = 0x04,
    BLOCK_AI_PATHS = 0x05,
    BLOCK_ENTITY_GEOMETRY = 0x0d,
    BLOCK_MAP_HEADER = 0x0e,
    BLOCK_PALETTE_HEADER = 0x0f,
    BLOCK_TEXTURE_HEADER = 0x10,
    BLOCK_PALETTE_DATA_PC = 0x11,
    BLOCK_PALETTE_DATA_PSX = 0x12,
    BLOCK_TEXTURE_DATA_PC = 0x15,
    BLOCK_TEXTURE_DATA_PC2 = 0x16,
    BLOCK_TEXTURE_DATA_GAMECUBE = 0x17,
    BLOCK_TEXTURE_DATA_XBOX = 0x18,
    BLOCK_PATH = 0x19,
    BLOCK_MAP_DATA = 0x1a,
    BLOCK_MEMORY_DISCARD = 0x1c,
    BLOCK_END = 0x1d,
    BLOCK_ENTITY_PARAMS2 = 0x1f,
    BLOCK_PORTALS = 0x21,
    BLOCK_AMBIENT_RADIATORS = 0x26,
    BLOCK_LOD = 0x27,
    BLOCK_SOUNDS = 0x28,
    BLOCK_TEXTURE_DATA_PCDX = 0x29,
    BLOCK_HASHLIST = 0x2c,
    BLOCK_COLLISION = 0x2e,
    BLOCK_COLLISION2 = 0x2f,
    BLOCK_PARTICLES = 0x30,
};

// The current block's data, after its header word
static void *BlockData(void) {
    return FileNextBlock + 1;
}

// The map's entities (a celglist_tag each), its paths and its texture table, allocated from the header's counts
// AUTOINJECT
void parsemap_block_map_header(void) {
    pBMH = (MAPHEADER_tag *)BlockData();
    MapEntityCount = pBMH->entityCount;
    if (MapEntityCount == 0)
        return;
    pCelList = (celglist_tag *)Mem_Malloc(MapEntityCount * sizeof(celglist_tag), (MallocFlags)0x904, 0);
    memset(pCelList, 0, pBMH->entityCount * sizeof(celglist_tag));
    pCelList->colldata = NULL;
    pCurrCelList = pCelList;
    if (pBMH->pathCount == 0) {
        m_paths = NULL;
    } else {
        m_paths = (Ptr32<void> *)Mem_Malloc(pBMH->pathCount * sizeof(Ptr32<void>), (MallocFlags)0x1804, 0);
        if (m_paths != NULL)
            m_ppaths = m_paths;
    }
    m_pmap = (map_tag *)Mem_Malloc(sizeof(map_tag), (MallocFlags)0xa04, 0);
    m_pmap->paletteHeaderData = NULL;
    m_pmap->numPaletteHeaderEntries = 0;
    m_pmap->texHeaderData = NULL;
    m_pmap->numTexHeaderEntries = 0;
    m_pmap->texData = NULL;
}

// The hashcodes the map needs: each not already loaded is asked for (isLoadable). 0x06000000 is passed by.
// AUTOINJECT
void parsemap_block_hashlist(void) {
    uchar *data = (uchar *)BlockData();
    for (uint count = (CurrentBH.size - 4) / 4; count != 0; count--) {
        HASHCODE hashcode = (HASHCODE)BIN_GetDWord(&data);
        if (hashcode != HT_Script_HASHCODE_BASE && hashtable_getitem(hashcode) == NULL) {
            uint loadable = isLoadable(hashcode);
            // GC check (0x80058e5c): one neither loaded nor loadable (the Xbox does not keep the answer)
            NF_WARN_IF(loadable == 0 && (hashcode & 0xff000000) != HT_Texture_HASHCODE_BASE
                       && hashcode != HT_Skin_HASHCODE_BASE,
                       "HT_Script / HT_Skin 0x%x not loaded in HT_Level %x\n", hashcode,
                       GameState.CurrentLevelHashcode);
        }
    }
}

// Each entity's next model down (celglist_tag::lod), by entity number; -1 for none. The entries are 8 bytes.
// AUTOINJECT
void parsemap_block_LOD(void) {
    uint *entry = (uint *)BlockData();
    for (ushort i = 0; i < pBMH->entityCount; i++, entry += 2) {
        if (entry[0] != 0xffffffff)
            pCelList[i].lod = &pCelList[entry[0]];
    }
}

// The palette headers, and psiCreateMapTextures's table of results, an entry each; the texture tables start
// AUTOINJECT
void parsemap_block_palette_header(void) {
    PALHEADER_tag *headers = (PALHEADER_tag *)BlockData();
    m_pmap->paletteHeaderData = headers;
    m_pmap->numPaletteHeaderEntries = (CurrentBH.size - 4) / sizeof(PALHEADER_tag);
    m_pmap->texData = NULL;
    if (m_pmap->numPaletteHeaderEntries != 0)
        m_pmap->texData = (texDataEntry *)Mem_Malloc(m_pmap->numPaletteHeaderEntries * sizeof(texDataEntry),
                                                    (MallocFlags)0xb08, 0);
    pPallHeader = headers;
    pTexData = m_pmap->texData;
}

// A palette's data (blocks 0x11, 0x12 and 0x29, which no Xbox map has): the palette is listed under its hashcode
// with the size of its texture, and its header points at the data. The three tables move on.
// AUTOINJECT
void parsemap_block_palette_data_psx(void) {
    if (pPallHeader->data != 0xffffffff)
        hashtable_additem((HASHCODE)pPallHeader->data, pTexData);
    pTexData->width = pBmpHeader->widthMinusOne + 1;
    pTexData->height = pBmpHeader->heightMinusOne + 1;
    pPallHeader->data = PtrTo32(BlockData());
    pBmpHeader++;
    pPallHeader++;
    pTexData++;
}

// A texture's data: the texture is listed under the hashcode its header holds, with its size, and its header points
// at the data. The three tables move on. (Blocks 0x17 and 0x18 both come here.)
// AUTOINJECT
void parsemap_block_texture_data_Xbox(void) {
    HASHCODE hashcode = (HASHCODE)PtrTo32(pBmpHeader->textureInfo);   // still the file's word
    if (hashcode != 0xffffffff)
        hashtable_additem(hashcode, pTexData);
    pTexData->width = pBmpHeader->widthMinusOne + 1;
    pTexData->height = pBmpHeader->heightMinusOne + 1;
    pBmpHeader->textureInfo = (TextureInfo *)BlockData();
    pBmpHeader++;
    pPallHeader++;
    pTexData++;
}

// The portals: four corners (the second side's reversed), the indices of the two entities whose cels they join,
// and a flag each that the side is shut. A portal is made in each cel, the two partners. The cels found are kept
// from one portal to the next when the next's are not found.
// AUTOINJECT
void parsemap_block_portal_data(void) {
    uint *data = (uint *)BlockData();
    uint count = *data++;
    cel_tag *cel = NULL;            // the cel of the second entity
    cel_tag *other = NULL;          // of the first
    for (ushort i = 0; i < count; i++) {
        portal_tag front, back;
        _VECTOR *corner = (_VECTOR *)data;
        for (int k = 0; k < 4; k++) {
            front.corners[k] = corner[k];
            back.corners[3 - k] = corner[k];
        }
        data += 12;
        Plane_PlaneEq(&front.plane, &front.corners[0], &front.corners[1], &front.corners[2]);
        Plane_PlaneEq(&back.plane, &back.corners[0], &back.corners[1], &back.corners[2]);
        ushort *link = (ushort *)data;
        ushort first = link[0];
        ushort second = link[1];
        ushort flags = link[2];
        data += 2;
        for (cel_tag *c = glb_world->firstCel; c != NULL; c = c->nextCel) {
            if (c->glist == &pCelList[second])
                cel = c;
            if (c->glist == &pCelList[first])
                other = c;
        }
        // GC check (0x80059370): a cel not found (the GameCube then goes on with it)
        if (cel == NULL)
            NF_WARN(">>>FAILED<<< Portal Link unknown<->%s\n", other != NULL ? other->glist->name : "");
        else if (other == NULL)
            NF_WARN(">>>FAILED<<< Portal Link unknown<->%s\n", cel->glist->name);
        char openFront = (flags & 2) != 2;
        char openBack = (flags & 1) != 1;
        portal_tag *a = build_alloc_portal(&front, cel->portals, other, openFront);
        cel->portals = a;
        portal_tag *b = build_alloc_portal(&back, other->portals, cel, openBack);
        other->portals = b;
        a->partner = b;
        b->partner = a;
    }
}

// A placement cel whose place and turn are both zero has its geometry in world space
static bool parseentity_AtOrigin(cel_tag *cel) {
    return Vec_IsEqual(&cel->position, &CONST_ZERO_VECTOR, 0.0f) && Vec_IsEqual(&cel->rotation, &CONST_ZERO_VECTOR, 0.0f);
}

// A room's kind: in world space, and without the sky when key 3 says so. Its keys 0 to 2 are its ambient light (a
// grey of 60 when all are zero), key 4 its music event.
static void parseentity_RoomKeys(cel_tag *cel, level_tag *keys, uint kind) {
    cel->bitsFromPlacementTag = CEL_WORLD_SPACE | CEL_ROOM | kind;
    if (keys->keys[3] != 0)
        cel->bitsFromPlacementTag = CEL_NO_SKY | CEL_WORLD_SPACE | CEL_ROOM | kind;
}

// A cel's kind, from its placement's, and what goes with it: the model's flags out through flags
// AUTOINJECT
void parseentityfixup_entity(cel_tag *cel, uint *flags, level_tag *keys) {
    *flags = cel->glist->applyFlagsToObject;
    switch (cel->bitsFromPlacementTag & 0xffff) {
        case Place_Drone:
            cel->bitsFromPlacementTag = Place_Drone;
            return;
        case Place_Breakable:
            cel->bitsFromPlacementTag = Place_Breakable;
            return;
        case Place_Light:
            cel->bitsFromPlacementTag = Place_Light;
            return;
        case 0x00dd:
            cel->bitsFromPlacementTag = 0x00dd;
            return;
        case Place_GunImp:
        case Place_RigidBody:
        case Place_ScriptPlayer:
        case Place_Car:
        case Place_Env:
        case Place_Creature:
        case Place_Destroy:
        case Place_SS:
        case Place_Flicker:
        case Place_Hurt:
        case Place_Emitter:
        case Place_EnvTree:
            cel->bitsFromPlacementTag = 0x8019;
            return;
        case 0x8017:
            cel->bitsFromPlacementTag = 0x8017;
            if (parseentity_AtOrigin(cel))
                cel->bitsFromPlacementTag |= CEL_WORLD_SPACE;
            return;
        case 0x8018:
            cel->bitsFromPlacementTag = 0x8018;
            if (parseentity_AtOrigin(cel))
                cel->bitsFromPlacementTag |= CEL_WORLD_SPACE;
            return;
        case 0x8019:
            // Its alpha: the speed in key 1, the mode in key 0
            cel->bitsFromPlacementTag = 0x8019;
            if (parseentity_AtOrigin(cel))
                cel->bitsFromPlacementTag |= CEL_WORLD_SPACE;
            cel->blink = (uchar)((uchar)keys->keys[1] << 4 | (uchar)keys->keys[0]);
            return;
        case 0x803d:
            cel->bitsFromPlacementTag = 0x28019;
            return;
        case 0xc023:
            parseentity_RoomKeys(cel, keys, 0xc023);
            cel->shearHeight = cel->someMax.y;
            cel->position.x = 0.0f;
            cel->position.y = 0.0f;
            cel->position.z = 0.0f;
            cel->musicEvent = (uchar)keys->keys[4];
            cel->soundEnvironment = (short)0x9898;
            cel->ambientR[0] = (uchar)keys->keys[0];
            cel->ambientG[0] = (uchar)keys->keys[1];
            cel->ambientB[0] = (uchar)keys->keys[2];
            break;
        case 0xc047:
            // Two ambient lights, chosen by a switch channel (key 5): the second in keys 6 to 8; its sound
            // environment in key 9
            parseentity_RoomKeys(cel, keys, 0xc047);
            cel->position.z = 0.0f;
            cel->position.y = 0.0f;
            cel->position.x = 0.0f;
            cel->musicEvent = (uchar)keys->keys[4];
            cel->someSwitchChannel = (uchar)keys->keys[5];
            cel->ambientR[0] = (uchar)keys->keys[0];
            cel->ambientG[0] = (uchar)keys->keys[1];
            cel->ambientB[0] = (uchar)keys->keys[2];
            cel->ambientR[1] = (uchar)keys->keys[6];
            cel->ambientG[1] = (uchar)keys->keys[7];
            cel->ambientB[1] = (uchar)keys->keys[8];
            cel->shearHeight = -500.0f;
            cel->soundEnvironment = (short)keys->keys[9];
            break;
        default:
            return;
    }
    if (cel->ambientR[0] == 0 && cel->ambientG[0] == 0 && cel->ambientB[0] == 0) {
        cel->ambientB[0] = 60;
        cel->ambientG[0] = 60;
        cel->ambientR[0] = 60;
    }
}

// The cel of a placement: its box and model's flags from the model, its place, then the fixed-up kind and the model's
// sphere in the world
static void parsemap_PlaceCel(TARGET_PLACEMENT *placement, celglist_tag *glist, uint kind, level_tag *keys) {
    glb_world->firstCel = build_alloc_cel(glb_world->firstCel);
    cel_tag *cel = glb_world->firstCel;
    cel->glist = glist;
    if (glist != NULL) {
        cel->someMin = glist->extentMin;
        cel->someMax = glist->extentMax;
    }
    cel->bitsFromPlacementTag = kind;
    cel->flags84 = glist->applyFlagsToObject;
    cel->position = placement->pos;
    cel->rotation = placement->rot;
    cel->bitsFromPlacementTag = kind;
    uint flags = 0;
    parseentityfixup_entity(cel, &flags, keys);
    memcpy(&cel->sphere, &cel->glist->boundSphere, sizeof(sphere_equ_tag));
    parseentity_transform_bounding_box(cel);
}

// A placement's keys, by key number (unbounded, as in the original)
static void parsemap_ReadKeys(TARGET_PLACEMENT *placement, level_tag *keys) {
    memset(keys, 0, sizeof(level_tag));
    PLACEMENT_KEY *pair = (PLACEMENT_KEY *)(placement + 1);
    for (uint i = 0; i < placement->keyCount; i++)
        keys->keys[pair[i].key] = pair[i].value;
}

// The next placement, after this one's keys
static TARGET_PLACEMENT *parsemap_NextPlacement(TARGET_PLACEMENT *placement, uint *left) {
    uint size = sizeof(TARGET_PLACEMENT) + placement->keyCount * sizeof(PLACEMENT_KEY);
    *left -= size;
    return (TARGET_PLACEMENT *)((uchar *)placement + size);
}

// The map's static placements: those of the cel kinds (bit 0x8000) become cels of the world. An entity number one
// past the last is let through (the dynamic placements' test is strict).
// AUTOINJECT
void parsemap_block_map_data_static(void) {
    TARGET_PLACEMENT *placement = (TARGET_PLACEMENT *)BlockData();
    for (uint left = CurrentBH.size - 4; left != 0; placement = parsemap_NextPlacement(placement, &left)) {
        level_tag keys;
        parsemap_ReadKeys(placement, &keys);
        celglist_tag *glist;
        if (placement->glistHashcode != 0xffffffff) {
            glist = (celglist_tag *)hashtable_getitem(placement->glistHashcode);
            // GC check (0x800598ac): a placement's model not loaded
            NF_WARN_IF(glist == NULL, "Entity not loaded (or broken) Hashcode=0x%x [%f,%f,%f]!\n",
                       placement->glistHashcode, placement->pos.x, placement->pos.y, placement->pos.z);
        } else {
            if (placement->entityNum > MapEntityCount)
                continue;
            glist = &pCelList[placement->entityNum];
        }
        uint kind = placement->placementType & 0xffff;
        if (kind & 0x8000)
            parsemap_PlaceCel(placement, glist, kind, &keys);
    }
}

// The map's dynamic placements, the objects: each not of the cel kinds (bits 0x8000 and 0x2000) and with a model is
// handed to parsemap_create_dynamic_objects with its spline and the next entity's model
// AUTOINJECT
void parsemap_block_map_data_dynamic(block_header_tag *bh, uchar *block, uchar doCreation) {
    TARGET_PLACEMENT *placement = (TARGET_PLACEMENT *)(block + 4);
    for (uint left = bh->size - 4; left != 0; placement = parsemap_NextPlacement(placement, &left)) {
        celglist_tag *next = &pCelList[placement->entityNum + 1];
        level_tag keys;
        parsemap_ReadKeys(placement, &keys);
        celglist_tag *glist;
        if (placement->glistHashcode != 0xffffffff) {
            glist = (celglist_tag *)hashtable_getitem(placement->glistHashcode);
        } else if (placement->entityNum < MapEntityCount) {
            glist = &pCelList[placement->entityNum];
        } else {
            // GC check (0x80059a5c): an entity number past the map's, skipped
            NF_WARN("Dynamic HT_Entity_Type 0x%.4x With uninit glist [%f,%f,%f]\n",
                    placement->placementType & 0xffff, placement->pos.x, placement->pos.y, placement->pos.z);
            glist = NULL;
        }
        uint type = placement->placementType;
        if (placement->splinePoints != 0) {
            keys.spline = m_paths[placement->spline];
            keys.splineLength = placement->splineLength;
            keys.splinePoints = placement->splinePoints;
        }
        if (!(type & 0xa000) && glist != NULL)
            parsemap_create_dynamic_objects(placement, &keys, (ObjectPlacementType)type, glist, keys.spline, next,
                                            doCreation);
    }
}

// Acts on a block by its identifier; true at the end block. The paths, ambient light and sounds are the main map's
// alone.
// AUTOINJECT
bool parsemap_handle_block_id(char mainMap) {
    switch (CurrentBH.identifier) {
    case BLOCK_ENTITY_PARAMS:
    case BLOCK_ENTITY_PARAMS2:
        parsemap_block_entity_params();
        return false;
    case BLOCK_AI_PATHS:
        AIPath_Parse((AIPathBlock *)FileNextBlock);
        return false;
    case BLOCK_ENTITY_GEOMETRY:
        pCurrCelList->geom_idx = (int)PtrTo32(BlockData());
        return false;
    case BLOCK_MAP_HEADER:
        parsemap_block_map_header();
        return false;
    case BLOCK_PALETTE_HEADER:
        parsemap_block_palette_header();
        return false;
    case BLOCK_TEXTURE_HEADER:
        pBmpHeader = (bmpHeaderEntry *)BlockData();
        m_pmap->texHeaderData = pBmpHeader;
        m_pmap->numTexHeaderEntries = (CurrentBH.size - 4) / sizeof(bmpHeaderEntry);
        return false;
    case BLOCK_PALETTE_DATA_PSX:
        parsemap_block_palette_data_psx();
        return false;
    case BLOCK_TEXTURE_DATA_PC:
    case BLOCK_TEXTURE_DATA_PC2:
        pBmpHeader->textureInfo = (TextureInfo *)BlockData();
        return false;
    case BLOCK_TEXTURE_DATA_GAMECUBE:
    case BLOCK_TEXTURE_DATA_XBOX:
        parsemap_block_texture_data_Xbox();
        return false;
    case BLOCK_PATH:
        if (mainMap)
            *m_ppaths++ = BlockData();
        return false;
    case BLOCK_MAP_DATA:
        DynamicBH = CurrentBH;
        DynamicPtr = FileNextBlock;
        parsemap_block_map_data_static();
        return false;
    case BLOCK_MEMORY_DISCARD:
        FileDiscard = (uint8_t *)FileNextBlock;
        pFileDiscardHeader = pBinFileHeader;
        return false;
    case BLOCK_END:
        return true;
    case BLOCK_PORTALS:
        parsemap_block_portal_data();
        return false;
    case BLOCK_AMBIENT_RADIATORS:
        if (mainMap)
            Light_SetAmbientRadiators((AMBIENT_RADIATORS *)BlockData());
        return false;
    case BLOCK_LOD:
        parsemap_block_LOD();
        return false;
    case BLOCK_SOUNDS:
        if (mainMap)
            Sound_LoadMapSounds((int *)BlockData());
        return false;
    case BLOCK_TEXTURE_DATA_PCDX:
        pBmpHeader->textureInfo = (TextureInfo *)BlockData();
        // fall through
    case BLOCK_PALETTE_DATA_PC:
        parsemap_block_palette_data_psx();
        return false;
    case BLOCK_HASHLIST:
        parsemap_block_hashlist();
        return false;
    case BLOCK_COLLISION:
    case BLOCK_COLLISION2:
        parsemap_block_Coll_Data_New();
        return false;
    case BLOCK_PARTICLES:
        parsemap_block_particles((uchar *)BlockData());
        return false;
    default:
        return false;
    }
}

// Reads the next block and moves on to the one after; true at the end block. A version 1 file finds its blocks by its
// table of offsets. The GameCube's check of a block of no size (it would read the same block for ever) is not in the
// Xbox build.
// AUTOINJECT
bool parsemap_parsenextblock(char mainMap) {
    if (BinFileVersion == 1)
        FileNextBlock = (uint *)(FileBase + *pBinFileHeader + 4);
    uint header = *FileNextBlock;
    CurrentBH.size = header & 0xffffff;
    CurrentBH.identifier = header >> 24;
    if (parsemap_handle_block_id(mainMap))
        return true;
    prev_block = FileNextBlock;
    if (BinFileVersion == 1) {
        pBinFileHeader++;
        FileNextBlock = (uint *)(FileBase + *pBinFileHeader + 4);
    } else {
        FileNextBlock = (uint *)((uchar *)FileNextBlock + CurrentBH.size);
    }
    return false;
}

// Back to the memory discard block, from which the rest of the file is read again
// AUTOINJECT
void parsemap_reset_to_block_memory_discard(void) {
    if (BinFileVersion == 1) {
        pBinFileHeader = pFileDiscardHeader;
        FileNextBlock = (uint *)(FileBase + *pBinFileHeader + 4);
    } else {
        FileNextBlock = (uint *)FileDiscard;
    }
}

// A cel's sphere in the world: its model's, through the cel's rotation and position
// AUTOINJECT
void parseentity_transform_bounding_box(cel_tag *cel) {
    _MATRIX mtx;
    RotMatrix(&cel->rotation, &mtx);
    Matrix_SetTrans(&cel->position, &mtx);
    if (cel->glist != NULL) {
        MatrixMultiplyVector(&mtx, (_VECTOR *)&cel->glist->boundSphere, &cel->sphere);
        cel->sphereRadius = cel->glist->boundSphere.r;
    }
}
