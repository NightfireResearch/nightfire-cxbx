#include "parsemap.h"

#include "psiFile.h"
#include "psiGraphics.h"
#include "../memory.h"
#include "../game/mp/multiplayer.h"
#include "../game/obj/car.h"
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

#define pCurrCelList (*(celglist_tag**)(0x00274c80))
#define FileNextBlock (*(uint**)(0x00274c70))
#define StartingNewMap BOOL8_AT(0x00274c68)
#define m_pmap (*(map_tag**)0x00274b50)

// Also used in Loader.cpp
#define MemType U32_AT(0x00274c98)

typedef struct block_header_tag {
    uint size;
    uint identifier;
} block_header_tag;

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
} block_entity_data;

// AUTOGEN
bool parsemap_parsenextblock(char param_1);

// AUTOGEN
void parsemap_block_map_data_dynamic(block_header_tag *bh, uchar* param_2, uchar doCreation);

#define DynamicBH (*(block_header_tag*)0x00274b34)
#define DynamicPtr PTR_AT(0x00274c58)
#define MapHashCode U32_AT(0x00274c88)
#define ParseMap_State U32_AT(0x00274ca4)
#define filename (*(char*)0x00274b58)
#define FileLastBlock U32_AT(0x00274c74)
#define FileDiscard U32_AT(0x00274c78)

// AUTOINJECT
bool parsemap_parsemap(uint hashcode, bool secondPass) {

    switch(ParseMap_State) {
        case 0: {
            // Load the file into RAM?
            const char* filePath = "";
            sprintf(&filename, "%s%8.8X.bin", filePath, hashcode);
            psiFileLoadForParse(&filename);
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
                Mem_Shrink((void**)&FileDiscard, FileLastBlock - FileDiscard);
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
    //...
} TARGET_PLACEMENT;

static_assert(offsetof(TARGET_PLACEMENT, placementType) == 0x8, "Bad offset of placementType");
static_assert(offsetof(TARGET_PLACEMENT, pos) == 0xc, "Bad offset of pos");

typedef struct {
    ObjectCreationData_Basic basicCreation;
    ushort teamId;
    char _likely_pad_1[2];
} Create_MPSpawnPoint_Params;

typedef struct {
    ObjectCreationData_Basic basicCreation;
    int maybeBrightness;// 0x2c
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
            // Some logging / blank function also if the type is 2?
            //
            // Deliberately calling the ORIGINAL, untouched compiled Break_Create here instead of our own
            // (Break.cpp's) - that reimplementation is known-incomplete (Break_Kill is an unfinished no-op
            // stub) and was causing real breakable-object resource leaks (never releasing vertex/index buffer
            // slots) that exhausted their shared 2048-slot table on revisiting an already-played segment,
            // leading to a crash. Break_Create is called directly here (not via the AUTOINJECT hook), so it
            // doesn't route through the original bytes on its own - this raw-address call bypasses our C++
            // implementation entirely until Break_Kill/the rest of that system is completed.
            if(doCreation)
                ((obj_tag*(__cdecl*)(_VECTOR*, _VECTOR*, celglist_tag*, void*))0x0001ffb0)(&pos, &rot, celglist, lvl);
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
                            (float)lightParams->maybeBrightness,
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