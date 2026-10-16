#ifndef DRONESPAWNER_H_
#define DRONESPAWNER_H_

#include "../../actionhelpers.h"
#include "../drone/Drone.h"

// A drone spawner (OBJECTTYPE_DRONE_SPAWNER, placement 241): it takes the drones of its group out of the level as
// templates, and makes drones from them, in waves, while it runs.

#pragma pack(push, 1)

// The placement's keys (level_tag + 0x2c)
typedef struct DroneSpawnerPlacement {
    char base[0x2c];
    uint kind;                  // 0x2c - DroneSpawnerKind; below 2 no spawner is made
    uint group;                 // 0x30 - the drone group it takes its templates from
    uint maxAlive;              // 0x34
    uint waves;                 // 0x38 - 0 for 32000
    uint startChannel;          // 0x3c
    uint stopChannel;           // 0x40
    uint doneChannel;           // 0x44
    uint killChannel;           // 0x48
    uint distance;              // 0x4c - 0 for 10
    char _unknown50[0xb0 - 0x50];
} DroneSpawnerPlacement;

// DroneSpawner_tag.kind
typedef enum {
    SPAWNER_KEEP_UP = 2,        // spawns while fewer than maxAlive are alive
    SPAWNER_WAVES = 3,          // spawns again once all of a wave are dead
} DroneSpawnerKind;

// A template: one drone of the group, as DroneSpawner_Init took it (0xac bytes)
typedef struct DroneSpawnEntry_tag {
    uint spawned;               // 0x00 - drones made from it this wave
    cel_tag *cel;               // 0x04 - the drone's cel
    DIVars_tag vars;            // 0x08 - the drone's creation vars
} DroneSpawnEntry_tag;

// The spawner object's extraObjectData (0x3c bytes)
typedef struct DroneSpawner_tag {
    uint kind;                  // 0x00 - DroneSpawnerKind
    uint group;                 // 0x04 - its slot in NPCGlobals.spawners
    ushort entryCount;          // 0x08 - templates
    ushort maxAlive;            // 0x0a - slots in spawned; no more than the group's drones
    ushort waves;               // 0x0c
    ushort startChannel;        // 0x0e - switches it on (then cleared); 0 for running from the start
    ushort stopChannel;         // 0x10 - switches it off, and deletes it unless it is killing
    ushort doneChannel;         // 0x12 - set when the last wave is all dead
    ushort killChannel;         // 0x14 - deletes the drones it made, then itself
    ushort _pad16;
    float distance;             // 0x18 - spawns no nearer the player than this
    uchar spawn;                // 0x1c - DroneSpawner_Control may spawn
    uchar ready;                // 0x1d - DroneSpawner_Init has run
    uchar running;              // 0x1e
    uchar anywhere;             // 0x1f - spawns without the distance and sight tests: the first wave, when the channels
                                //        let it run at DroneSpawner_Init
    uchar killing;              // 0x20 - the kill channel has been seen on
    uchar _pad21[3];
    uint initFrame;             // 0x24 - GameState.NumFramesUnpaused at DroneSpawner_Init
    ushort waveSpawned;         // 0x28 - drones made this wave
    ushort alive;               // 0x2a - of those in spawned, counted each frame
    ushort wave;                // 0x2c - waves begun
    ushort entryCount2;         // 0x2e - what DroneSpawner_SpawnDrone draws a template from
    ushort unknown30;           // 0x30 - cleared by DroneSpawner_Create
    ushort _pad32;
    obj_tag **spawned;          // 0x34 - the drones it made (maxAlive slots)
    DroneSpawnEntry_tag *entries;   // 0x38
} DroneSpawner_tag;

#pragma pack(pop)

static_assert(sizeof(DroneSpawnerPlacement) == 0xb0, "DroneSpawnerPlacement is 0xb0 bytes");
static_assert(offsetof(DroneSpawnerPlacement, distance) == 0x4c, "Wrong offset for DroneSpawnerPlacement.distance");
static_assert(sizeof(DroneSpawnEntry_tag) == 0xac, "DroneSpawnEntry_tag is 0xac bytes");
static_assert(sizeof(DroneSpawner_tag) == 0x3c, "DroneSpawner_tag is 0x3c bytes");
static_assert(offsetof(DroneSpawner_tag, distance) == 0x18, "Wrong offset for DroneSpawner_tag.distance");
static_assert(offsetof(DroneSpawner_tag, initFrame) == 0x24, "Wrong offset for DroneSpawner_tag.initFrame");
static_assert(offsetof(DroneSpawner_tag, spawned) == 0x34, "Wrong offset for DroneSpawner_tag.spawned");

void DroneSpawner_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl);
void DroneSpawner_Init(obj_tag *spawner);
void DroneSpawner_Delete(obj_tag *spawner);
void DroneSpawner_DroneDelete(obj_tag *spawner, obj_tag *drone);
void DroneSpawner_ResetEntries(obj_tag *spawner);
void DroneSpawner_WaveTalk(ushort wave);
obj_tag* DroneSpawner_SpawnDrone(obj_tag *spawner);
void DroneSpawner_Control(obj_tag *spawner);

#endif // DRONESPAWNER_H_