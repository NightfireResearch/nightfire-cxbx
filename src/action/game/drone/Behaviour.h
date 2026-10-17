#ifndef BEHAVIOUR_H_
#define BEHAVIOUR_H_

#include "../../actionhelpers.h"

#define NUM_BEHAVIOUR_CLASSES 0x11  // Drone_tag.behaviourClass: 0-9 soldier ... 16 bot
#define NUM_BEHAVIOUR_MODES 6       // attack modes per behaviour
#define DRONE_STATS_FIELDS 8

#pragma pack(push, 1)

// What behaviour_util_get unpacks a placement's behaviour block into (PS2 type name _BehaviourStruct; a local of
// NDrone2_DoModeSettingsNEW, which zeroes it, sets both headers to 3 and points words1/words2 at the drone's
// behaviour1/behaviour2). docs/drone/behaviours/README.md 2.1.
typedef struct BehaviourStruct {
    uint behaviourClass;    // 0x00 - key 15, < 0x11
    uint mode1;             // 0x04 - attack mode of behaviour 1, < 6
    ushort count1;          // 0x08 - behaviour 1's word count, 1..3
    ushort numProps1;       // 0x0a - its property count, 1..0x5b
    uint *words1;           // 0x0c - where behaviour 1's words are copied (skipped if NULL)
    uint mode2;             // 0x10
    ushort count2;          // 0x14
    ushort numProps2;       // 0x16
    uint *words2;           // 0x18
} BehaviourStruct;

// The fields of a stats record, in their packed order (widths 8,3,8,8,5,8,8,1). DoModeSettingsNEW copies the low
// byte of each into Drone_tag+0x98..+0x9e, except health, which becomes Drone_tag.health as a float.
enum {
    DRONE_STAT_ACCURACY,    // -> Drone_tag.accuracy (really inaccuracy)
    DRONE_STAT_AGGRESSION,  // -> .aggression
    DRONE_STAT_HEALTH,      // -> .health (0: 10 hp and accuracy 5)
    DRONE_STAT_SPEED,       // -> .speed (never read)
    DRONE_STAT_9B,          // -> .stat9b (never read)
    DRONE_STAT_REACTION,    // -> .reaction (DroneFunc_ReactionTime)
    DRONE_STAT_RECOVER,     // -> .recover (DroneFunc_RecoverTime)
    DRONE_STAT_BADSIDE,     // -> .badSide
};

// One unpacked stats record (behaviour_util_getStats)
typedef struct DroneStatsRecord {
    uint field[DRONE_STATS_FIELDS]; // indexed by DRONE_STAT_*
} DroneStatsRecord;

#pragma pack(pop)

static_assert(sizeof(BehaviourStruct) == 0x1c, "BehaviourStruct is 0x1c bytes");
static_assert(offsetof(BehaviourStruct, words1) == 0x0c, "Wrong offset for words1");
static_assert(offsetof(BehaviourStruct, mode2) == 0x10, "Wrong offset for mode2");
static_assert(offsetof(BehaviourStruct, words2) == 0x18, "Wrong offset for words2");
static_assert(sizeof(DroneStatsRecord) == 0x20, "DroneStatsRecord is 0x20 bytes");

// drone_stats (BSS): per behaviour class, three stats records (only record 1 is ever used), filled by
// behaviour_util_get from each placement - last writer wins, zero at boot. Only behaviour_util_get and
// behaviour_util_getStats reference it (scanned the XBE for every address in the range), so once both are ours it
// could become our own variable; it stays the game's for now so the shadow test's original and ours write the same
// array (see notes.md).
// XBE_GLOBAL(0x001d7830, 0x660)
#define drone_stats (*(DroneStatsRecord(*)[NUM_BEHAVIOUR_CLASSES][3])0x001d7830)

uint behaviour_util_getProperty(int id, uint *words);
void behaviour_util_setProperty(int id, uint *words, int value);
bool behaviour_util_get(BehaviourStruct *bs, uint *data);
DroneStatsRecord *behaviour_util_getStats(int behaviourClass, int j);

#endif // BEHAVIOUR_H_
