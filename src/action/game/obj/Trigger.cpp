#include "Trigger.h"
#include "../../../common/xbeClass.h"
#include "build.h"
#include "bullet.h"
#include "control.h"
#include "../drone/NDrone2.h"
#include "Player.h"
#include "../Spline.h"
#include "../view.h"
#include "../sp/PlayerStats.h"
#include "../sp/SwitchChannels.h"
#include "../../engine/Collide.h"
#include "../../engine/Text.h"
#include "../../game.h"
#include "../../sound/music.h"

#include <string.h>

#pragma fp_contract(off)

// A trigger: a volume (its model, normally hidden) that switch channels follow. Touched by a player, a drone or a
// bullet, or used by the player, it sets its channel; or it combines other channels into its own, or copies its own
// out to them; or it loads a level or plays a movie.

// The placement's parameters as Trigger_Create reads them. The other create functions rearrange theirs into these
// places first.
#define TRIGGER_PARAMS 33

enum TriggerParam {
    TriggerParam_Mode = 0,
    TriggerParam_Channel = 1,
    TriggerParam_Inputs = 2,        // 7 of them
    TriggerParam_Touch = 9,         // 0 is taken as TriggerTouch_Bullet
    TriggerParam_Lock = 10,
    TriggerParam_HideOnLock = 11,
};

#pragma pack(push, 1)

typedef struct {
    ushort unknown00;               // 0x00
    ushort pathPointCount;          // 0x02
    ushort pathDuration;            // 0x04 - frames to cover the path
    uchar padding06[2];
    Ptr32<void> path;               // 0x08
    uchar padding0c[0x20];
    uint param[TRIGGER_PARAMS];     // 0x2c
} Create_Trigger_Params;

#pragma pack(pop)

static_assert(offsetof(Create_Trigger_Params, param) == 0x2c, "Bad offset of param");
static_assert(sizeof(Create_Trigger_Params) == 0xb0, "Bad size for Create_Trigger_Params");

// Channels with fixed meanings here
#define CHANNEL_BOND_MOMENT_FIRST 0x65  // ten channels, one per bond moment
#define CHANNEL_BOND_MOMENT_LAST 0x6e
#define CHANNEL_TOUCH_ONLY_7001500 0x7d // a touch trigger sets it only in level 0x07001500

// The channels that stop a level-load trigger (docs/switch-channels.md)
#define CHANNEL_DRONE_DIED 0x60
#define CHANNEL_ALARM_RAISED 0x61
#define CHANNEL_PLAYER_DEAD 0x62
#define CHANNEL_STEALTH_BROKEN 0x63

#define LEVEL_7001500 ((HASHCODE)0x07001500)

// The GameFlow states pushed here
#define GAMEFLOW_MOVIE 6
#define GAMEFLOW_FADE 7

// The text shown while a level-load trigger's lock channel is clear
#define TXT_LEVEL_LOCKED ((Action_TranslatedText)0x02000021)

// Set while Trigger_Calc updates the touch triggers; at other times Trigger_Update leaves them alone.
// XBE_GLOBAL(0x0029b2cc, 0x1)
uint8_t ProcessTouch;

// The placement's parameters as they were, while a create function rearranges them
// XBE_GLOBAL(0x0029b248, 0x84)
static uint TmpParam[TRIGGER_PARAMS];


// AUTOINJECT
obj_tag * Trigger_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {
    Create_Trigger_Params *params = (Create_Trigger_Params *)lvl;

    obj_tag *obj = control_create_object_xbox(sizeof(TRIGGER), XBOX_SIZEOF(TRIGGER), pos, rot, NULL);
    if (obj == NULL)
        return NULL;

    TRIGGER *t = (TRIGGER *)obj->extraObjectData;
    memset(t, 0, sizeof(TRIGGER));
    t->pathPos = 0.0f;
    t->path = params->path;
    t->lastKey = params->pathPointCount != 0 ? params->pathPointCount - 1 : 0;
    t->mode = params->param[TriggerParam_Mode];
    t->channel = params->param[TriggerParam_Channel];
    for (int i = 0; i < TRIGGER_INPUTS - 1; i++)
        t->inputs[i] = params->param[TriggerParam_Inputs + i];
    t->inputs[TRIGGER_INPUTS - 1] = 0;
    t->touch = params->param[TriggerParam_Touch] != 0 ? params->param[TriggerParam_Touch] : TriggerTouch_Bullet;
    double keys = (double)t->lastKey * REC_FRAME_RATE;
    t->speed = (float)((double)_FRAME_RATE / params->pathDuration * keys);
    t->lock = params->param[TriggerParam_Lock];
    t->hideOnLock = params->param[TriggerParam_HideOnLock];
    if (t->lastKey != 0)
        obj->transformFlags |= TRANSFORM_MATRIX_PLACED;
    obj->objectType = OBJECTTYPE_TRIGGER;
    obj->objGraphics = celgl;
    obj->effectFlags = celgl->applyFlagsToObject;
    build_LinkToRoom(obj, 0, glb_world);

    _MATRIX mtx;
    View_RotTransMatrix(rot, pos, &mtx);
    Mat_GetUp(&t->up, &mtx);

    // A level-load trigger is seen
    if (t->mode == TriggerMode_LoadLevel)
        obj->effectFlags &= ~FLAG_HIDDEN;
    return obj;
}

// The multiplexers take the channel first and the inputs after it
static void Trigger_MultiplexParams(Create_Trigger_Params *params, TriggerMode mode) {
    memcpy(TmpParam, params->param, sizeof(TmpParam));
    params->param[TriggerParam_Mode] = mode;
    params->param[TriggerParam_Channel] = TmpParam[0];
    for (int i = 0; i < TRIGGER_INPUTS - 2; i++)
        params->param[TriggerParam_Inputs + i] = TmpParam[1 + i];
}

// AUTOINJECT
obj_tag * Trigger_MultiplexIn(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {
    Trigger_MultiplexParams((Create_Trigger_Params *)lvl, TriggerMode_MultiplexIn);
    return Trigger_Create(pos, rot, lvl, celgl);
}

// AUTOINJECT
obj_tag * Trigger_MultiplexOrIn(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {
    Trigger_MultiplexParams((Create_Trigger_Params *)lvl, TriggerMode_MultiplexOrIn);
    return Trigger_Create(pos, rot, lvl, celgl);
}

// AUTOINJECT
obj_tag * Trigger_MultiplexSIn(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {
    Trigger_MultiplexParams((Create_Trigger_Params *)lvl, TriggerMode_MultiplexSIn);
    return Trigger_Create(pos, rot, lvl, celgl);
}

// AUTOINJECT
obj_tag * Trigger_MultiplexOut(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {
    Trigger_MultiplexParams((Create_Trigger_Params *)lvl, TriggerMode_MultiplexOut);
    return Trigger_Create(pos, rot, lvl, celgl);
}

// The touch triggers take the channel, what touches it, the inputs, then the lock and whether to hide; the
// touch-once one has one more input before the lock.
static void Trigger_TouchParams(Create_Trigger_Params *params, TriggerMode mode, int lockParam) {
    memcpy(TmpParam, params->param, sizeof(TmpParam));
    params->param[TriggerParam_Mode] = mode;
    params->param[TriggerParam_Channel] = TmpParam[0];
    params->param[TriggerParam_Touch] = TmpParam[1];
    params->param[TriggerParam_Lock] = TmpParam[lockParam];
    params->param[TriggerParam_HideOnLock] = TmpParam[lockParam + 1];
    for (int i = 0; i < TRIGGER_INPUTS - 2; i++)
        params->param[TriggerParam_Inputs + i] = TmpParam[2 + i];
}

// AUTOINJECT
obj_tag * Trigger_Touch(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {
    Trigger_TouchParams((Create_Trigger_Params *)lvl, TriggerMode_Touch, 6);
    return Trigger_Create(pos, rot, lvl, celgl);
}

// AUTOINJECT
obj_tag * Trigger_TouchOnce(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {
    Trigger_TouchParams((Create_Trigger_Params *)lvl, TriggerMode_TouchOnce, 7);
    return Trigger_Create(pos, rot, lvl, celgl);
}

// The level to load, the lock and the two channels it may set; only the player using it sets it off.
// AUTOINJECT
obj_tag * Trigger_LoadLevelCreate(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {
    Create_Trigger_Params *params = (Create_Trigger_Params *)lvl;
    memcpy(TmpParam, params->param, sizeof(TmpParam));
    params->param[TriggerParam_Mode] = TriggerMode_LoadLevel;
    for (int i = 0; i < 5; i++)
        params->param[TriggerParam_Channel + i] = TmpParam[i];
    params->param[TriggerParam_Touch] = TriggerTouch_Activate | TriggerTouch_Player | TriggerTouch_Bullet;
    return Trigger_Create(pos, rot, lvl, celgl);
}

// The movie, what sets it off, and the channel that plays it.
// AUTOINJECT
obj_tag * Trigger_MoviePlayer(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {
    Create_Trigger_Params *params = (Create_Trigger_Params *)lvl;
    memcpy(TmpParam, params->param, sizeof(TmpParam));
    params->param[TriggerParam_Mode] = TriggerMode_MoviePlayer;
    params->param[TriggerParam_Channel] = TmpParam[0];
    params->param[TriggerParam_Inputs] = TmpParam[2];
    params->param[TriggerParam_Touch] = TmpParam[1];
    return Trigger_Create(pos, rot, lvl, celgl);
}

// AUTOINJECT
ushort Trigger_Type(obj_tag *obj) {
    if (obj == NULL)
        return 0;
    return ((TRIGGER *)obj->extraObjectData)->mode;
}

// A level-load trigger is locked until its first input is set; the others while their lock is set.
// AUTOINJECT
bool Trigger_Locked(obj_tag *obj) {
    if (obj == NULL)
        return false;

    TRIGGER *t = (TRIGGER *)obj->extraObjectData;
    if (t->mode == TriggerMode_LoadLevel)
        return t->inputs[0] != 0 && switch_channels[t->inputs[0]] == 0;
    return t->lock != 0 && switch_channels[t->lock] != 0;
}

// Whether what touches the trigger sets it off. For a player or a drone touching a touch trigger, each input names
// what must be touching it: 0xb a drone, 0xe the player, anything else a drone of that skin. For a bullet, each
// input is the weapon (its base variant) of a bullet that must be touching it. Otherwise any hit of a kind in touch.
// AUTOINJECT
uchar Trigger_Hit(obj_tag *obj, TRIGGER *trigger) {
    uint touching = 0;
    for (HITDATA_tag *hit = obj->hitList; hit != NULL; hit = hit->next) {
        if (hit->hitObj == NULL)
            continue;
        switch (hit->hitObj->objectType) {
        case OBJECTTYPE_DRONE:
        case OBJECTTYPE_SUB:
            touching |= TriggerTouch_Drone;
            break;
        case OBJECTTYPE_PLAYER:
            touching |= TriggerTouch_Player;
            break;
        case OBJECTTYPE_BULLET:
            touching |= TriggerTouch_Bullet;
            break;
        }
    }

    ushort touch = trigger->touch;
    bool touchMode = trigger->mode == TriggerMode_TouchOnce || trigger->mode == TriggerMode_Touch;

    if ((touch & (TriggerTouch_Player | TriggerTouch_Drone)) && (touching & (TriggerTouch_Player | TriggerTouch_Drone))
        && touchMode) {
        if (touch == TriggerTouch_Player)
            return (touching & TriggerTouch_Player) != 0;
        if (touch == TriggerTouch_Drone && !(touching & TriggerTouch_Drone))
            return 0;

        // The channel's own value, not 0 or 1, when there are no inputs
        uchar hit = (touch & TriggerTouch_NeedsChannel) ? switch_channels[trigger->inputs[4]] : 1;
        for (int i = 0; i < TRIGGER_INPUTS && trigger->inputs[i] != 0; i++) {
            ushort input = trigger->inputs[i];
            if (input == 0xb) {
                hit &= (touching & TriggerTouch_Drone) != 0;
            } else if (input == 0xe) {
                hit &= (touching & TriggerTouch_Player) != 0;
            } else {
                uchar found = 0;
                for (HITDATA_tag *h = obj->hitList; h != NULL; h = h->next) {
                    if (h->hitObj != NULL && h->hitObj->objectType == OBJECTTYPE_DRONE &&
                        input == (ushort)Drone_GetSkin(h->hitObj)) {
                        found = 1;
                        break;
                    }
                }
                hit &= found;
            }
        }
        return hit;
    }

    if ((touch & TriggerTouch_Bullet) && (touching & TriggerTouch_Bullet) && touchMode) {
        uchar hit = 1;
        for (int i = 0; i < TRIGGER_INPUTS && trigger->inputs[i] != 0; i++) {
            uchar found = 0;
            for (HITDATA_tag *h = obj->hitList; h != NULL; h = h->next) {
                if (h->hitObj != NULL && h->hitObj->objectType == OBJECTTYPE_BULLET &&
                    (short)trigger->inputs[i] == ((BU_tag *)h->hitObj->extraObjectData)->wpnDef->weaponBaseNum) {
                    found = 1;
                    break;
                }
            }
            hit &= found;
        }
        return hit;
    }

    return (touch & touching) != 0;
}

// The player has used a trigger.
// AUTOINJECT
void Trigger_Activate(obj_tag *obj, obj_tag *user) {
    if (user == NULL || obj == NULL || user->objectType != OBJECTTYPE_PLAYER)
        return;

    TRIGGER *t = (TRIGGER *)obj->extraObjectData;
    switch (t->mode) {
    case TriggerMode_TouchOnce:
        if (!(t->touch & TriggerTouch_Activate))
            return;
        if (t->channel != 0) {
            switch_channels[t->channel] = 1;
            switch_channels_time[t->channel] = GameState.NumFramesUnpaused;
        }
        if (obj->effectFlags & FLAG_HIDDEN)
            obj->flags |= 1;
        return;

    case TriggerMode_Touch:
        if ((t->touch & TriggerTouch_Activate) && t->channel != 0) {
            switch_channels[t->channel] = 1;
            switch_channels_time[t->channel] = GameState.NumFramesUnpaused;
        }
        return;

    case TriggerMode_MoviePlayer:
        if (!(t->touch & TriggerTouch_Activate))
            return;
        GameState.MovieHashcode = t->channel;
        GameState.field5_0x14 = 0;
        GameFlow_PushState(GAMEFLOW_MOVIE, 0.0f, 0xff);
        GameFlow_PushState(GAMEFLOW_FADE, 300.0f, 0xff);
        return;

    case TriggerMode_LoadLevel:
        break;

    default:
        return;
    }

    if (switch_channels[CHANNEL_STEALTH_BROKEN] || switch_channels[CHANNEL_PLAYER_DEAD] ||
        switch_channels[CHANNEL_ALARM_RAISED] || switch_channels[CHANNEL_DRONE_DIED])
        return;

    // Locked: say so, at most every three seconds
    if (t->inputs[0] != 0 && !switch_channels[t->inputs[0]]) {
        if (t->cooldown == 0) {
            Text_AddMsg(0, 0, 1, (char *)Txt_BindLabel(TXT_LEVEL_LOCKED, 0), 0, 180);
            t->cooldown = FRAME_RATE_INT * 3;
        }
        return;
    }

    // With inputs[1] set it sets inputs[3], otherwise inputs[2]
    ushort set = (t->inputs[1] != 0 && switch_channels[t->inputs[1]]) ? t->inputs[3] : t->inputs[2];
    if (set != 0) {
        switch_channels[set] = 1;
        switch_channels_time[set] = GameState.NumFramesUnpaused;
    }

    Music_Event(3, 1);
    GS_PauseGame(true);
    ResetMap_LevelToLoad((HASHCODE)t->channel, true, false);
    GameFlow_PushState(GAMEFLOW_FADE, 300.0f, 0xff);
    t->mode = TriggerMode_Done;
    if (glb_blokes[0] != NULL)
        Player_RamSave(glb_blokes[0]);
}

// AUTOINJECT
void Trigger_Update(obj_tag *obj) {
    if (obj == NULL)
        return;
    TRIGGER *t = (TRIGGER *)obj->extraObjectData;
    if (t == NULL)
        return;

    if (t->cooldown != 0)
        t->cooldown--;
    if (t->mode == TriggerMode_Touch && !ProcessTouch)
        return;

    Control_SetEffectType(obj, obj->objGraphics != NULL ? obj->objGraphics->applyFlagsToObject : obj->effectFlags);
    if (t->lock != 0 && switch_channels[t->lock]) {
        if (t->hideOnLock)
            obj->effectFlags |= FLAG_HIDDEN | 0x20;
        return;
    }

    if (t->lastKey != 0) {
        Spline_Interp(t->path, 0, t->lastKey, t->pathPos, &obj->transformMatrix, NULL, 2);
        obj->transformFlags |= TRANSFORM_MOVED;
        double pathPos = (double)FRAME_RATE_MUL * t->speed + t->pathPos;
        t->pathPos = (float)pathPos;
        if (pathPos > t->lastKey)
            t->pathPos = 0.0f;
    }

    uchar hit = Trigger_Hit(obj, t);
    uint channel = t->channel;
    uint now = GameState.NumFramesUnpaused;

    switch (t->mode) {
    case TriggerMode_TouchOnce:
        if (switch_channels[channel]) {
            if (obj->effectFlags & FLAG_HIDDEN)
                obj->flags |= 1;
            return;
        }
        // fall through
    case TriggerMode_Touch:
        if (GameState.CurrentLevelHashcode != LEVEL_7001500 && channel == CHANNEL_TOUCH_ONLY_7001500)
            return;
        if (channel == 0)
            return;
        switch_channels[channel] |= hit != 0;
        if (hit)
            switch_channels_time[channel] = now;
        return;

    case TriggerMode_MultiplexIn: {
        uchar all = 1;
        for (int i = 0; i < TRIGGER_INPUTS && t->inputs[i] != 0; i++)
            all &= switch_channels[t->inputs[i]];
        if (channel == 0)
            return;
        switch_channels[channel] = all;
        if (!all)
            return;
        switch_channels_time[channel] = now;
        // A bond moment's hidden trigger goes once it is done
        if (channel >= CHANNEL_BOND_MOMENT_FIRST && channel <= CHANNEL_BOND_MOMENT_LAST &&
            (obj->effectFlags & FLAG_HIDDEN))
            obj->flags |= 1;
        return;
    }

    case TriggerMode_MultiplexSIn: {
        // Each input set no more than four frames before the one ahead of it
        uchar all = switch_channels[t->inputs[0]];
        for (int i = 1; i < TRIGGER_INPUTS && t->inputs[i] != 0 && all; i++) {
            ushort input = t->inputs[i];
            all &= switch_channels[input] != 0 &&
                   (int)switch_channels_time[input] >= (int)(switch_channels_time[t->inputs[i - 1]] - 4);
        }
        if (channel == 0)
            return;
        switch_channels[channel] = all;
        if (all)
            switch_channels_time[channel] = now;
        return;
    }

    case TriggerMode_MultiplexOut:
        for (int i = 0; i < TRIGGER_INPUTS; i++) {
            ushort output = t->inputs[i];
            if (output == 0)
                continue;
            switch_channels[output] = switch_channels[channel];
            if (switch_channels[channel])
                switch_channels_time[output] = now;
        }
        return;

    case TriggerMode_LoadLevel:
        if (t->inputs[1] != 0 && switch_channels[t->inputs[1]])
            Trigger_Activate(obj, glb_players[0]);
        return;

    case TriggerMode_MultiplexOrIn: {
        uchar any = 0;
        for (int i = 0; i < TRIGGER_INPUTS && t->inputs[i] != 0; i++)
            any |= switch_channels[t->inputs[i]];
        if (channel == 0)
            return;
        switch_channels[channel] = any;
        if (any)
            switch_channels_time[channel] = now;
        return;
    }

    case TriggerMode_MoviePlayer:
        if (t->inputs[0] != 0 && switch_channels[t->inputs[0]]) {
            switch_channels[t->inputs[0]] = 0;
            GameState.MovieHashcode = channel;
            GameState.field5_0x14 = 0;
            GameFlow_PushState(GAMEFLOW_MOVIE, 0.0f, 0xff);
            GameFlow_PushState(GAMEFLOW_FADE, 30.0f, 0xff);
        }
        return;
    }
}

// Once a frame: scores the bond moments whose channels are set, sets two channels that follow others in two levels,
// and runs the touch triggers. Each clears its channel first, so it stays set only while something touches it.
// AUTOINJECT
void Trigger_Calc(void) {
    for (ushort moment = CHANNEL_BOND_MOMENT_FIRST; moment <= CHANNEL_BOND_MOMENT_LAST; moment++) {
        if (switch_channels[moment])
            PlrStat_DoneBondMoment(0, moment);
    }

    if (GameState.CurrentLevelHashcode == HT_Level_CastleExterior) {
        if (switch_channels[0xa4])
            switch_channels[0x6f] = 1;
    } else if (GameState.CurrentLevelHashcode == HT_Level_TowerC) {
        if (switch_channels[0x34])
            switch_channels[0x2d] = 1;
    }

    for (obj_tag *obj = control_first_object(); obj != NULL; obj = obj->nextObject) {
        if (obj->objectType != OBJECTTYPE_TRIGGER)
            continue;
        TRIGGER *t = (TRIGGER *)obj->extraObjectData;
        if (t->mode == TriggerMode_Touch && t->channel != 0)
            switch_channels[t->channel] = 0;
    }

    ProcessTouch = 1;
    for (obj_tag *obj = control_first_object(); obj != NULL; obj = obj->nextObject) {
        if (obj->objectType == OBJECTTYPE_TRIGGER && ((TRIGGER *)obj->extraObjectData)->mode == TriggerMode_Touch)
            Trigger_Update(obj);
    }
    ProcessTouch = 0;
}

// The inverter (no model, no room): while its input channel (curState) is set, it clears its output (subState).
// Returns 0 whether or not it was made.
// AUTOINJECT
bool InverterTrigger_Create(level_tag *lvl) {
    Create_Trigger_Params *params = (Create_Trigger_Params *)lvl;
    obj_tag *obj = control_create_object(4, &CONST_ZERO_VECTOR, &CONST_ZERO_VECTOR, NULL);
    if (obj != NULL) {
        obj->objectType = OBJECTTYPE_INVERTER_TRIGGER;
        obj->curState = params->param[0];
        obj->subState = params->param[1];
    }
    return false;
}

// The channels are read as signed shorts.
// AUTOINJECT
void InverterTrigger_Update(obj_tag *obj) {
    if (switch_channels[(short)obj->curState] && obj->subState != 0)
        switch_channels[(short)obj->subState] = 0;
}

#pragma pack(push, 1)

// A sound trigger's data: its channel plays its sound, its stop channel stops it
typedef struct {
    ushort channel;         // 0x00
    ushort stopChannel;     // 0x02
    ushort once;            // 0x04 - 1: deleted once played
    ushort sound;           // 0x06 - an Action_SFX
    ushort is2d;            // 0x08 - Sound_PlayExt, not Sound_Play3D at the trigger
    uchar padding0a[2];
    DYNAMICSOUNDS *handle;  // 0x0c
    uchar exclusive;        // 0x10 - the one such sound playing (GlobalVoiceHandle)
    uchar keep;             // 0x11 - plays on with its channel clear
    uchar padding12[2];
} SOUNDTRIGGER;

// A music trigger's data: while its channel is set, and 30 frames after, it sends its music event
typedef struct {
    uint event;             // 0x00 - Music_Event's id, sent with 2
    ushort channel;         // 0x04
    ushort stopChannel;     // 0x06 - deletes it
    ushort frames;          // 0x08
    uchar once;             // 0x0a - deleted after the first event
    uchar padding0b;
} MUSICTRIGGER;

#pragma pack(pop)

XBOX_SIZE(SOUNDTRIGGER, 0x14);
static_assert(sizeof(MUSICTRIGGER) == 0xc, "MUSICTRIGGER is 0xc bytes");

// The exclusive sound trigger's sound while one plays
// XBE_GLOBAL(0x0029b240, 0x4)
DYNAMICSOUNDS *GlobalVoiceHandle;

#define NO_SOUND 0xffff
#define NO_MUSIC_EVENT 0xff
#define MUSIC_TRIGGER_FRAMES 30

// The placement's difficulty parameter: 1 to 4 the highest it plays at, 5 to 7 only at 1 to 3
enum {
    SoundTriggerDifficulty_Only1 = 5,
    SoundTriggerDifficulty_Only2 = 6,
    SoundTriggerDifficulty_Only3 = 7,
};

// No sound, no channel or another difficulty, and nothing is made
// AUTOINJECT
obj_tag * SoundTrigger_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {
    Create_Trigger_Params *params = (Create_Trigger_Params *)lvl;
    GlobalVoiceHandle = NULL;

    if (params->param[2] == 0 || params->param[2] == NO_SOUND || params->param[0] == 0)
        return NULL;
    uint difficulty = GameState.difficultyModifier;
    uint only = params->param[6];
    if (only != 0 && difficulty > only)
        return NULL;
    if (only == SoundTriggerDifficulty_Only1 && difficulty != 1)
        return NULL;
    if (only == SoundTriggerDifficulty_Only2 && difficulty != 2)
        return NULL;
    if (only == SoundTriggerDifficulty_Only3 && difficulty != 3)
        return NULL;

    obj_tag *obj = control_create_object_xbox(sizeof(SOUNDTRIGGER), XBOX_SIZEOF(SOUNDTRIGGER), pos, rot, NULL);
    if (obj == NULL)
        return NULL;

    SOUNDTRIGGER *trigger = (SOUNDTRIGGER *)obj->extraObjectData;
    trigger->channel = params->param[0];
    trigger->once = params->param[1];
    trigger->sound = params->param[2];
    trigger->is2d = params->param[3];
    trigger->stopChannel = params->param[4];
    trigger->handle = NULL;
    trigger->exclusive = params->param[5];
    trigger->keep = params->param[7];
    obj->objectType = OBJECTTYPE_SOUNDTRIGGER;
    obj->objGraphics = celgl;
    obj->effectFlags = celgl->applyFlagsToObject;
    build_LinkToRoom(obj, 0, glb_world);
    return obj;
}

// The stop channel deletes a trigger not playing (read even when it is 0), and stops one playing as its channel
// clearing does unless it keeps on; an exclusive one that cannot play is deleted.
// AUTOINJECT
void SoundTrigger_Update(obj_tag *obj) {
    SOUNDTRIGGER *trigger = (SOUNDTRIGGER *)obj->extraObjectData;

    if (trigger->handle == NULL) {
        if (switch_channels[trigger->stopChannel]) {
            obj->flags |= 1;
            return;
        }
        if (!switch_channels[trigger->channel])
            return;
        if (trigger->exclusive && GlobalVoiceHandle != NULL) {
            obj->flags |= 1;
            return;
        }
        if (trigger->is2d)
            trigger->handle = Sound_PlayExt((Action_SFX)trigger->sound, 100.0f, 0, 0);
        else
            trigger->handle = Sound_Play3D((Action_SFX)trigger->sound, &obj->position, 100.0f, -1.0f, -1.0f, 0, 0, 0);
        if (trigger->exclusive)
            GlobalVoiceHandle = trigger->handle;
        return;
    }

    if (Sound_IsFinnished(trigger->handle)) {
        trigger->handle = NULL;
        if (trigger->exclusive)
            GlobalVoiceHandle = NULL;
    } else {
        bool stop = trigger->stopChannel != 0 && switch_channels[trigger->stopChannel];
        if (trigger->keep || switch_channels[trigger->channel]) {
            if (!stop)
                return;
        }
        Sound_Stop(trigger->handle);
        trigger->handle = NULL;
        if (trigger->exclusive) {
            obj->flags |= 1;
            GlobalVoiceHandle = NULL;
        }
    }
    if (trigger->once == 1)
        obj->flags |= 1;
}

// No event or no channel, and nothing is made
// AUTOINJECT
obj_tag * MusicTrigger_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {
    Create_Trigger_Params *params = (Create_Trigger_Params *)lvl;
    if (params->param[2] == NO_MUSIC_EVENT || params->param[0] == 0)
        return NULL;

    obj_tag *obj = control_create_object(sizeof(MUSICTRIGGER), pos, rot, NULL);
    if (obj == NULL)
        return NULL;

    MUSICTRIGGER *trigger = (MUSICTRIGGER *)obj->extraObjectData;
    trigger->channel = params->param[0];
    trigger->stopChannel = params->param[1];
    trigger->event = params->param[2];
    trigger->once = params->param[3];
    obj->objectType = OBJECTTYPE_MUSIC_TRIGGER;
    obj->objGraphics = celgl;
    obj->effectFlags = celgl->applyFlagsToObject;
    build_LinkToRoom(obj, 0, glb_world);
    return obj;
}

// AUTOINJECT
void MusicTrigger_Update(obj_tag *obj) {
    MUSICTRIGGER *trigger = (MUSICTRIGGER *)obj->extraObjectData;
    if (trigger->stopChannel != 0 && switch_channels[trigger->stopChannel])
        obj->flags |= 1;
    if (switch_channels[trigger->channel])
        trigger->frames = MUSIC_TRIGGER_FRAMES;
    if (trigger->frames > 0) {
        trigger->frames--;
        Music_Event(trigger->event, 2);
        if (trigger->once)
            obj->flags |= 1;
    }
}
