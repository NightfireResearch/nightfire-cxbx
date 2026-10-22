// The drone spawners. DroneSpawner_Create keeps the placement and registers the spawner in NPCGlobals.spawners; after
// the load NDrone2_SetupGroups calls DroneSpawner_Init, which takes the group's drones out of the level as templates.
// Each frame DroneSpawner_Control follows the channels, counts the drones it made that are still alive, begins the next
// wave once a wave has made maxAlive, and spawns into a free slot (DroneSpawner_SpawnDrone).

#include "DroneSpawner.h"
#include "build.h"
#include "control.h"
#include "../drone/NDrone2.h"
#include "../sp/SwitchChannels.h"
#include "../../game.h"             // GameState
#include "../../memory.h"
#include "../../math/math.h"
#include "../../util/Random.h"
#include "../drone/DroneInit.h"

#include <string.h>

#pragma fp_contract(off)


#define MALLOC_SPAWNER_SLOTS_ALIGN4 ((MallocFlags)0x3404)
#define MALLOC_SPAWNER_ENTRIES_ALIGN4 ((MallocFlags)0x3504)

// The first of the two speech sounds DroneSpawner_WaveTalk picks from
#define SFX_WAVE_TALK 0xd6

constexpr float kDefaultDistance = 10.0f;
constexpr ushort kDefaultWaves = 32000;
constexpr float kNoPlayerDistanceSq = 1000.0f;  // what DroneSpawner_SpawnDrone takes the distance to be without a player

static DroneSpawner_tag *Spawner(obj_tag *obj) {
    return (DroneSpawner_tag *)obj->extraObjectData;
}

// The spawner of placement 241. Kinds below 2 make nothing.
// AUTOINJECT
void DroneSpawner_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl) {
    if (Drone_bDisableSystem)
        return;
    DroneSpawnerPlacement *keys = (DroneSpawnerPlacement *)lvl;
    if (keys->kind < SPAWNER_KEEP_UP)
        return;
    obj_tag *obj = control_create_object_xbox(sizeof(DroneSpawner_tag), XBOX_SIZEOF(DroneSpawner_tag),
                                              NULL, NULL, NULL);
    if (obj == NULL)
        return;
    DroneSpawner_tag *spawner = Spawner(obj);
    if (spawner == NULL)
        return;
    control_init_object(obj);
    Vec_Copy2(pos, &obj->position, &obj->lastPosition);
    Vec_Copy2(rot, &obj->rotation, &obj->lastRotation);
    obj->objectType = OBJECTTYPE_DRONE_SPAWNER;
    obj->inCel = build_FindCel(&obj->position, glb_world);

    spawner->kind = keys->kind;
    spawner->group = keys->group;
    spawner->maxAlive = keys->maxAlive;
    spawner->waves = keys->waves;
    spawner->startChannel = keys->startChannel;
    spawner->stopChannel = keys->stopChannel;
    spawner->doneChannel = keys->doneChannel;
    spawner->killChannel = keys->killChannel;
    spawner->distance = (float)keys->distance;
    if (spawner->distance == 0.0f)
        spawner->distance = kDefaultDistance;
    if (spawner->waves == 0)
        spawner->waves = kDefaultWaves;
    spawner->spawn = 0;
    spawner->entryCount = 0;
    spawner->waveSpawned = 0;
    spawner->alive = 0;
    spawner->wave = 0;
    spawner->entryCount2 = 0;
    spawner->unknown30 = 0;
    spawner->entries = NULL;
    spawner->spawned = NULL;
    spawner->ready = 0;
    spawner->running = 0;
    spawner->killing = 0;
    NPCGlobals.spawners[spawner->group] = obj;
}

// A spawner object with nothing to spawn, at the origin: the drones' dummy player (NPCGlobals.dummyPlayer), left
// switched off. Made after every load (Drone_PostLoad_Init).
// AUTOINJECT
void DroneSpawner_CreateBlank(void) {
    obj_tag *obj = control_create_object_xbox(sizeof(DroneSpawner_tag), XBOX_SIZEOF(DroneSpawner_tag),
                                              NULL, NULL, NULL);
    if (obj == NULL || obj->extraObjectData == NULL)
        return;
    control_init_object(obj);
    _VECTOR pos = {0.0f, 0.0f, 0.0f};
    _VECTOR rot = {0.0f, 0.0f, 0.0f};
    Vec_Copy2(&pos, &obj->position, &obj->lastPosition);
    Vec_Copy2(&rot, &obj->rotation, &obj->lastRotation);
    obj->objectType = OBJECTTYPE_DRONE_SPAWNER;
    obj->inCel = build_FindCel(&obj->position, glb_world);
    NPCGlobals.dummyPlayerActive = 0;
    NPCGlobals.dummyPlayer = obj;
}

// After the load (NDrone2_SetupGroups): the group's drones become the templates and leave the level. With no drones in
// the group the spawner deletes itself.
// AUTOINJECT
void DroneSpawner_Init(obj_tag *obj) {
    DroneSpawner_tag *spawner = Spawner(obj);
    if (spawner == NULL)
        return;
    ushort count = NPCGlobals.groupCounts[spawner->group];
    NF_WARN_IF(count == 0xffff, "ERROR: More than one DroneSpawner has the same group");   // GC check (0x80085400)
    if (count == 0) {
        obj->flags |= 1;
        NPCGlobals.spawners[spawner->group] = NULL;
        return;
    }
    if (spawner->maxAlive == 0 || spawner->maxAlive > count)
        spawner->maxAlive = count;
    Drone_tag *drone = NPCGlobals.groups[spawner->group];
    if (drone != NULL) {
        spawner->entryCount2 = count;
        spawner->entryCount = count;
        if ((spawner->startChannel == 0 || switch_channels[spawner->startChannel])
            && (spawner->stopChannel == 0 || !switch_channels[spawner->stopChannel]))
            spawner->anywhere = 1;
        spawner->spawned = (obj_tag **)Mem_MallocXbox(spawner->maxAlive * sizeof(obj_tag *),
                                                      spawner->maxAlive * XBOX_SIZEOF(obj_tag *),
                                                      MALLOC_SPAWNER_SLOTS_ALIGN4, 0);
        spawner->entries = (DroneSpawnEntry_tag *)Mem_MallocXbox(spawner->entryCount * sizeof(DroneSpawnEntry_tag),
                                                                 spawner->entryCount * XBOX_SIZEOF(DroneSpawnEntry_tag),
                                                                 MALLOC_SPAWNER_ENTRIES_ALIGN4, 0);
        NF_WARN_IF(spawner->entries == NULL || spawner->spawned == NULL,
                   "Couldn't allocate memory for DroneSpawner (group %d)\n", spawner->group);  // GC check (0x80085400)
        memset(spawner->spawned, 0, spawner->maxAlive * sizeof(obj_tag *));
        memset(spawner->entries, 0, spawner->entryCount * sizeof(DroneSpawnEntry_tag));
        DroneSpawnEntry_tag *entry = spawner->entries;
        do {
            entry->spawned = 0;
            entry->vars = drone->diVars;
            entry->cel = drone->gameObj->inCel;
            NDrone2_Enable(0, drone);
            drone->gameObj->flags |= 1;
            Drone_tag *next = drone->groupNext;
            control_delete_object(drone->gameObj);
            entry++;
            drone = next;
        } while (drone != NULL);
    }
    spawner->ready = 1;
    spawner->initFrame = GameState.NumFramesUnpaused;
    NPCGlobals.groupCounts[spawner->group] = 0xffff;
    if (spawner->startChannel == 0)
        spawner->running = 1;
}

// AUTOINJECT
void DroneSpawner_Delete(obj_tag *obj) {
    if (obj == NULL)
        return;
    DroneSpawner_tag *spawner = Spawner(obj);
    if (spawner == NULL)
        return;
    if (spawner->spawned != NULL)
        Mem_Free((void **)&spawner->spawned);
    if (spawner->entries != NULL)
        Mem_Free((void **)&spawner->entries);
    spawner->entries = NULL;
    spawner->spawned = NULL;
    NPCGlobals.spawners[spawner->group] = NULL;
}

// A drone of the spawner's group is deleted (Drone_Delete): its slot is freed
// AUTOINJECT
void DroneSpawner_DroneDelete(obj_tag *obj, obj_tag *drone) {
    if (obj == NULL || drone == NULL || (obj->flags & 1))
        return;
    DroneSpawner_tag *spawner = Spawner(obj);
    if (spawner == NULL || spawner->spawned == NULL)
        return;
    for (uint i = 0; i < spawner->maxAlive; i++) {
        if (spawner->spawned[i] == drone)
            spawner->spawned[i] = NULL;
    }
}

// A new wave: every template may spawn again. Our name.
// FUNC_AT(0x00030860)
void DroneSpawner_ResetEntries(obj_tag *obj) {
    DroneSpawner_tag *spawner = Spawner(obj);
    if (spawner == NULL || spawner->spawned == NULL || spawner->kind < SPAWNER_KEEP_UP)
        return;
    for (uint i = 0; i < spawner->entryCount; i++)
        spawner->entries[i].spawned = 0;
}

// In PowerStationA1 and A2 a wave after the first begins with one of two speech sounds, played without a drone. Our
// name.
// FUNC_AT(0x00030910)
void DroneSpawner_WaveTalk(ushort wave) {
    HASHCODE level = GameState.CurrentLevelHashcode;
    if (level >= HT_Level_PowerStationA1 && level <= HT_Level_PowerStationA2 && wave != 0)
        NDrone2_PlaySFX(NULL, Rand_Rand(2) + SFX_WAVE_TALK, NULL, 1, 1.0f);
}

// Draws a template that has not spawned this wave and, unless the spawner spawns anywhere, is at least the spawner's
// distance from the player and out of the player's sight, and makes its drone. The draw counts only the templates
// that pass; NULL when fewer pass than the draw, or the one drawn is in sight.
// AUTOINJECT
obj_tag* DroneSpawner_SpawnDrone(obj_tag *obj) {
    DroneSpawner_tag *spawner = Spawner(obj);
    if (spawner == NULL || spawner->spawned == NULL)
        return NULL;
    uint pick = Rand_Rand((short)spawner->entryCount2);     // sign-extended, as the original loads it
    DroneSpawnEntry_tag *entry = spawner->entries;
    obj_tag *player = NDrone2_Player();
    double distanceSq = kNoPlayerDistanceSq;
    for (uint i = 0; i < spawner->entryCount; i++, entry++) {
        if (player != NULL)
            distanceSq = Vec_SqDist3D(&player->position, &entry->vars.position);
        // A NaN distance does not pass
        if (!spawner->anywhere && !(distanceSq >= (double)spawner->distance * spawner->distance))
            continue;
        if (entry->spawned != 0)
            continue;
        if (pick-- != 0)
            continue;
        if (!spawner->anywhere && Drone_PositionVisible(&entry->vars.position, entry->cel))
            return NULL;
        obj_tag *drone = NDrone2_CreateFromDIVars(&entry->vars);
        if (drone != NULL) {
            NDrone2_PostLoad_Init(drone);
            entry->spawned++;
            spawner->waveSpawned++;
        }
        return drone;
    }
    return NULL;
}

// The spawner object's update (control_funcs)
// AUTOINJECT
void DroneSpawner_Control(obj_tag *obj) {
    if (!NPCGlobals.enabled || Drone_bDisableSystem)
        return;
    if (obj == NULL || (obj->flags & 1))
        return;
    DroneSpawner_tag *spawner = Spawner(obj);
    if (spawner == NULL || spawner->group == 0 || spawner->spawned == NULL || spawner->kind < SPAWNER_KEEP_UP
        || !spawner->ready)
        return;

    if (spawner->killChannel != 0 && switch_channels[spawner->killChannel])
        spawner->killing = 1;
    if (!spawner->running && !spawner->killing) {
        if (spawner->startChannel == 0 || !switch_channels[spawner->startChannel])
            return;
        spawner->running = 1;
        spawner->startChannel = 0;
    }
    if (spawner->stopChannel != 0 && switch_channels[spawner->stopChannel]) {
        spawner->running = 0;
        if (!spawner->killing)
            obj->flags |= 1;
        return;
    }

    if (spawner->killing) {
        // These levels spare the drones drawn in the last two ticks, and the spawner waits until there are none
        bool all;
        switch (GameState.CurrentLevelHashcode) {
        case HT_Level_Tower2A:
        case HT_Level_Tower2B:
        case HT_Level_Tower2C:
        case HT_Level_EvilBaseC:
        case HT_Level_Tower2Elevator:
            all = false;
            break;
        default:
            all = true;
            break;
        }
        int kept = 0;
        for (ushort i = 0; i < spawner->maxAlive; i++) {
            obj_tag *drone = spawner->spawned[i];
            if (drone == NULL)
                continue;
            if (!all && drone->drawnCountdown) {
                kept++;
                continue;
            }
            if (GameState.CurrentLevelHashcode == HT_Level_EvilBaseC)
                DroneFunc_SetDeathChannel((Drone_tag *)drone->extraObjectData);
            drone->flags |= 1;
            spawner->spawned[i] = NULL;
        }
        if (!all && kept != 0)
            return;
        obj->flags |= 1;
        spawner->running = 0;
        return;
    }

    // A slot is freed when its drone is no longer one, is being deleted, is dead or dying, or has no health left
    spawner->alive = 0;
    for (ushort i = 0; i < spawner->maxAlive; i++) {
        obj_tag *drone = spawner->spawned[i];
        if (drone == NULL)
            continue;
        if (drone->objectType != OBJECTTYPE_DRONE || (drone->flags & 1)) {
            spawner->spawned[i] = NULL;
            continue;
        }
        Drone_tag *data = (Drone_tag *)drone->extraObjectData;
        if (data == NULL || (data->flags & 0x600) || data->health <= 0.0f) {
            spawner->spawned[i] = NULL;
            continue;
        }
        spawner->alive++;
    }

    if (spawner->wave >= spawner->waves) {
        // After the last wave; with a done or kill channel, once all its drones are dead
        if (spawner->doneChannel != 0 || spawner->killChannel != 0) {
            if (spawner->alive != 0)
                return;
            if (spawner->doneChannel != 0) {
                switch_channels[spawner->doneChannel] = 1;
                switch_channels_time[spawner->doneChannel] = GameState.NumFramesUnpaused;
            }
        }
        obj->flags |= 1;
        spawner->ready = 0;
        spawner->running = 0;
        return;
    }
    if (spawner->waveSpawned >= spawner->maxAlive) {
        spawner->waveSpawned = 0;
        spawner->wave++;
        spawner->anywhere = 0;
        DroneSpawner_ResetEntries(obj);
        spawner->spawn = 0;
        return;
    }
    switch (spawner->kind) {
    case SPAWNER_KEEP_UP:
        spawner->spawn = spawner->alive < spawner->maxAlive;
        break;
    case SPAWNER_WAVES:
        if (spawner->anywhere)
            spawner->spawn = 1;
        if (spawner->alive == 0) {
            DroneSpawner_WaveTalk(spawner->wave);
            spawner->spawn = 1;
        }
        break;
    }
    if (!spawner->spawn)
        return;

    // One drone into the first free slot; a draw that tested a drone's line of sight without spawning waits for the
    // next frame
    for (ushort i = 0; i < spawner->maxAlive; i++) {
        if (spawner->spawned[i] != NULL)
            continue;
        ushort visionTests = NPCGlobals.visionTests;
        spawner->spawned[i] = DroneSpawner_SpawnDrone(obj);
        if (spawner->spawned[i] != NULL)
            return;
        if (visionTests != NPCGlobals.visionTests)
            return;
    }
}
