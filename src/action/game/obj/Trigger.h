#ifndef TRIGGER_H_
#define TRIGGER_H_

#include "../../actionhelpers.h"

// What a trigger does (TRIGGER::mode). The placements' create functions each set one.
enum TriggerMode {
    TriggerMode_TouchOnce = 1,      // touched: sets its channel, and goes if hidden
    TriggerMode_Touch = 2,          // its channel follows the touching (Trigger_Calc)
    TriggerMode_MultiplexIn = 3,    // its channel is all of the inputs
    TriggerMode_MultiplexSIn = 4,   // ... all of them, set in order
    TriggerMode_MultiplexOut = 5,   // the inputs are set to its channel
    TriggerMode_LoadLevel = 10,     // the player using it loads the level its channel names
    TriggerMode_MultiplexOrIn = 12, // its channel is any of the inputs
    TriggerMode_MoviePlayer = 13,   // plays the movie its channel names
    TriggerMode_Done = 99,          // a level load it has asked for
};

// What sets a trigger off (TRIGGER::touch), and the hits Trigger_Hit finds
enum TriggerTouch {
    TriggerTouch_NeedsChannel = 0x1, // a player or drone counts only while inputs[4] is set
    TriggerTouch_Bullet = 0x2,
    TriggerTouch_Activate = 0x4,     // the player using it (Trigger_Activate)
    TriggerTouch_Player = 0x8,
    TriggerTouch_Drone = 0x10,
};

#define TRIGGER_INPUTS 8

#pragma pack(push, 1)

typedef struct {
    void *path;                     // 0x00 - the placement's keys, when it moves
    float pathPos;                  // 0x04 - where on the path, in keys
    float speed;                    // 0x08 - keys a frame
    ushort lastKey;                 // 0x0c
    uchar padding0e[2];
    _VECTOR up;                     // 0x10
    uint channel;                   // 0x1c
    ushort lock;                    // 0x20 - a channel that stops it; for TriggerMode_LoadLevel inputs[0] instead
    ushort inputs[TRIGGER_INPUTS];  // 0x22 - channels; a list ends at the first 0
    ushort touch;                   // 0x32 - TriggerTouch
    ushort mode;                    // 0x34 - TriggerMode
    ushort hideOnLock;              // 0x36
    ushort cooldown;                // 0x38 - frames until Trigger_Activate repeats its message
    uchar padding3a[2];
} TRIGGER;

#pragma pack(pop)

static_assert(sizeof(TRIGGER) == 0x3c, "Bad size for TRIGGER");
static_assert(offsetof(TRIGGER, channel) == 0x1c, "Bad offset of channel");
static_assert(offsetof(TRIGGER, inputs) == 0x22, "Bad offset of inputs");
static_assert(offsetof(TRIGGER, mode) == 0x34, "Bad offset of mode");
static_assert(offsetof(TRIGGER, cooldown) == 0x38, "Bad offset of cooldown");

obj_tag * Trigger_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
obj_tag * MusicTrigger_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
obj_tag * SoundTrigger_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);

obj_tag * Trigger_LoadLevelCreate(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
obj_tag * Trigger_TouchOnce(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
obj_tag * Trigger_Touch(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
obj_tag * Trigger_MultiplexIn(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
obj_tag * Trigger_MultiplexSIn(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
obj_tag * Trigger_MultiplexOut(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
obj_tag * Trigger_MultiplexOrIn(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
obj_tag * Trigger_MoviePlayer(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);

ushort Trigger_Type(obj_tag *obj);
bool Trigger_Locked(obj_tag *obj);
uchar Trigger_Hit(obj_tag *obj, TRIGGER *trigger);
void Trigger_Update(obj_tag *obj);
void Trigger_Activate(obj_tag *obj, obj_tag *user);
void Trigger_Calc(void);

bool InverterTrigger_Create(level_tag *lvl);
void InverterTrigger_Update(obj_tag *obj);

#endif // TRIGGER_H_
