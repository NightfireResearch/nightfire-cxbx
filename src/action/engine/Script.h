#ifndef SCRIPT_H_
#define SCRIPT_H_

#include "../actionhelpers.h"

typedef enum {
    SStream_Entity = 1,
    SStream_Anim = 2,
    SStream_Sprite = 3,
    SStream_Camera = 4,
    SStream_Sound = 5,
    SStream_Light = 6,
    SStream_SubScript = 7,
} SStreamType;

#pragma pack(push, 1)

// One "active command" slot within a SCRIPTINFO - eg. an entity move, a camera cut, a sound cue. Which union of
// meaning applies to field0-field3 depends on whatKindOfInterpolation; only the common tail (used generically by
// Script_GetObj/Script_HideObj/Script_RemoveObj/Script_SetColour/Script_Set2Start/Script_Stop) is understood so far
// - the per-type interpretation of field0-field3 belongs to Script_Interp, which isn't reimplemented yet.
typedef struct {
    float field0_0x0;
    float field1_0x4;
    float elapsedFrames; // 0x8 - how long this stream has been running; advanced by FRAME_RATE_MUL every
                          // Script_Update call regardless of stream type, cleared by Script_Set2Start (rewind)
    float field3_0xc;
    void* linkedGameOrSoundObject; // 0x10 - obj_tag* for entity/anim/camera/light/subscript streams
    uchar* streamBuffer;           // 0x14 - read head into this stream's raw command bytes
    uchar* streamBufferStart;      // 0x18 - saved starting position; Script_Set2Start rewinds streamBuffer to this
    char unknown_0x1c[4];
    char elementAt;                // 0x20
    char unknown_0x21[3];
    uchar whatKindOfInterpolation; // 0x24 - actually SStreamType, but can't tell the compiler to make the enum
                                    // just one byte (same issue as obj_tag::objectType/ObjectType). 0 means this
                                    // slot is inactive/unused
    char unknown_0x25[3];
} SSTREAM;

static_assert(sizeof(SSTREAM) == 0x28, "Bad size for SSTREAM");
static_assert(offsetof(SSTREAM, linkedGameOrSoundObject) == 0x10, "Bad offset of linkedGameOrSoundObject");
static_assert(offsetof(SSTREAM, streamBuffer) == 0x14, "Bad offset of streamBuffer");
static_assert(offsetof(SSTREAM, elementAt) == 0x20, "Bad offset of elementAt");
static_assert(offsetof(SSTREAM, whatKindOfInterpolation) == 0x24, "Bad offset of whatKindOfInterpolation");

// Bits of SCRIPTINFO::scriptFlags
enum ScriptFlags {
    ScriptFlag_RestoreOnKillStream = 0x10,  // Script_KillStream restores (rather than deletes) entity/anim
                                             // streams' linked objects when their stream ends (copies the
                                             // transform matrix's position back into position/lastPosition and
                                             // sets renderType bit 0x20, instead of marking them for deletion)
    ScriptFlag_FFwdKillsCamera = 0x20,      // Script_FFwd skips straight to the end by killing the active Camera
                                             // stream outright, instead of fading out over the remaining frames
    ScriptFlag_IsCameraNIS = 0x40,          // Script_Play sets ScriptCam from scriptHashcode when set
    ScriptFlag_ReEnableDronesOnFree = 0x80, // Script_Free calls Drone_EnableAll when set
};

// Bits of SCRIPTINFO::playbackFlags
enum ScriptPlaybackFlags {
    ScriptPlayback_Playing = 0x1,    // Set by Script_Play; cleared by Script_Stop
    ScriptPlayback_AtEnd = 0x2,      // Cleared by Script_Set2Start (rewind); exact meaning otherwise unconfirmed -
                                      // when set (and Reverse isn't), Script_IsPlaying reports "not playing"
    ScriptPlayback_Reverse = 0x8,    // Set by Script_Play(reverse=true)
    ScriptPlayback_FFwdFading = 0x10, // Set by Script_FFwd when it kicks off a fade-out; cleared by Script_Update
                                       // once that fade finishes (see fadeElapsedFrames/fadeFrames/fadeFlags)
};

// Bits of SCRIPTINFO::fadeFlags
enum ScriptFadeFlags {
    ScriptFade_FFwdRequested = 0x1,  // Set by Script_FFwd; cleared by Script_Update once the fade-out finishes
    ScriptFade_FFwdAllowed = 0x10,   // Gates Script_FFwd entirely - not observed being set by anything
                                     // reimplemented so far, presumably comes from the script asset via Script_Load
    ScriptFade_ThresholdHit = 0x20,  // Set by Script_Update every tick once fadeElapsedFrames has passed fadeFrames
};

typedef struct SCRIPTINFO {
    _MATRIX maybeMatrix;
    _MATRIX otherMatrix;      // 0x3c - also reinterpreted as a _MATRIX* by Script_Free when restoring player control
    char unknown_0x78[0x8c-0x78]; // 0x78=maybeScriptPlayer (SCRIPTPLAYER* set by Script_Load), 0x7c=callback,
                                   // 0x80=callback context - all mirrored from SP_LoadScript's arguments, but
                                   // unused by anything reimplemented so far
    float scale; // 0x8c - eg. set by SP_CreateScriptPlayer
    HASHCODE scriptHashcode; // 0x90 - the script's own identifying hashcode (first word of the loaded binary
                              // asset, per Script_Load) - NOT a stream's own hashcode. Read back by
                              // Script_Free/Script_Play/SP_Update
    int maybeSomeSleepFrames; // 0x94
    float maybeProgressFrames; // 0x98 - elapsed playback frames for the whole script (vs SSTREAM::elapsedFrames,
                                // which is per-stream); advanced by FRAME_RATE_MUL in Script_Update, compared
                                // against maybeNumFrames to detect "near the end"
    float fadeElapsedFrames;   // 0x9c - 0.0 means no fade-out running. Script_FFwd kicks this off (sets it to
                                // FRAME_RATE_MUL); Script_Update advances it by FRAME_RATE_MUL each tick until it
                                // passes fadeFrames, then leaves it capped (see ScriptFade_ThresholdHit) until
                                // Script_Update's finalize step resets it to 0.0
    SSTREAM streamDataBuffer[64]; // start at 0xa0, guessed at the size. Nicely fills almost all the space
    char scriptFlags;  // 0xaa0 - see ScriptFlags
    char pausesPlayer; // 0xaa1 - bit0: Script_Free calls Player_Enable(glb_players[0], &otherMatrix, playerEnableFlags)
                        // and PlarStat_LogTimerUnpause when set
    ushort numElements; // 0xaa2 - number of live entries in streamDataBuffer (see Script_GetObj/Script_HideObj)
    char unknown_0xaa4[4];
    ushort maybeNumFrames; // at 0xaa8
    char playbackFlags; // 0xaaa - see ScriptPlaybackFlags
    char streamNum; // at 0xaab
    char fadeFrames;  // 0xaac - target frame count for the Script_FFwd-triggered fade-out (compared against
                       // fadeElapsedFrames); unrelated to maybeNumFrames
    char fadeFlags;    // 0xaad - see ScriptFadeFlags
    char playerEnableFlags; // 0xaae - passed as Player_Enable's int param by Script_Free when pausesPlayer is set
    char unknown_0xaaf; // trailing byte - Ghidra's own struct size (2752/0xac0) exceeds what Script_Load allocates
                         // (0xab0/2736), so there's likely a little more space here that's simply never touched
} SCRIPTINFO;


static_assert(offsetof(SCRIPTINFO, scale) == 0x8c, "Offset of scale is bad");
static_assert(offsetof(SCRIPTINFO, scriptHashcode) == 0x90, "Offset of scriptHashcode is bad");
static_assert(offsetof(SCRIPTINFO, maybeSomeSleepFrames) == 0x94, "Offset of maybeSomeSleepFrames is bad");
static_assert(offsetof(SCRIPTINFO, maybeProgressFrames) == 0x98, "Offset of maybeProgressFrames is bad");
static_assert(offsetof(SCRIPTINFO, fadeElapsedFrames) == 0x9c, "Offset of fadeElapsedFrames is bad");
static_assert(offsetof(SCRIPTINFO, streamDataBuffer[0]) == 0xa0, "Offset of streamDataBuffer is bad");
static_assert(offsetof(SCRIPTINFO, scriptFlags) == 0xaa0, "Offset of scriptFlags is bad");
static_assert(offsetof(SCRIPTINFO, numElements) == 0xaa2, "Offset of numElements is bad");
static_assert(offsetof(SCRIPTINFO, maybeNumFrames) == 0xaa8, "Offset of maybeNumFrames is bad");
static_assert(offsetof(SCRIPTINFO, playbackFlags) == 0xaaa, "Offset of playbackFlags is bad");
static_assert(offsetof(SCRIPTINFO, streamNum) == 0xaab, "Offset of streamNum is bad");
static_assert(offsetof(SCRIPTINFO, fadeFrames) == 0xaac, "Offset of fadeFrames is bad");
static_assert(offsetof(SCRIPTINFO, fadeFlags) == 0xaad, "Offset of fadeFlags is bad");
static_assert(offsetof(SCRIPTINFO, playerEnableFlags) == 0xaae, "Offset of playerEnableFlags is bad");

#pragma pack(pop)

// Set by Script_Play when the playing script has ScriptFlag_IsCameraNIS set; read by whatever drives the
// camera stack to know which script currently owns it.
#define ScriptCam (*(HASHCODE*)0x001f6678)

void Script_Free(SCRIPTINFO *param_1);
void Script_Run(SCRIPTINFO *);
void Script_RemoveObj(obj_tag *obj, SCRIPTINFO *scriptInfo);
void Script_SetPosRot(SCRIPTINFO *param_1, _MATRIX *param_2);
void Script_SetColour(SCRIPTINFO *param_1, undefined1 clr_r, undefined1 clr_g, undefined1 clr_b);
SCRIPTINFO* Script_Load(HASHCODE param_1,_VECTOR *transform,_VECTOR *rotation,uint *fileBuf,void*, void* callback, void* maybeCallbackContext);
void Script_Update(SCRIPTINFO *param_1);
bool Script_Play(SCRIPTINFO *param_1, char param_2);
bool Script_IsPlaying(SCRIPTINFO* param_1);
obj_tag* Script_GetObj(SCRIPTINFO *scriptInfo, ushort streamIdx);
void Script_FFwd(SCRIPTINFO *scriptInfo, char doFFwd);
void Script_HideObj(SCRIPTINFO *scriptInfo, char hide);
bool Script_IsDeathNIS(HASHCODE hashcode);
void Script_KillStream(SCRIPTINFO *scriptInfo, SSTREAM *stream);
#endif // SCRIPT_H_
