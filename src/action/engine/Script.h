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

// How a stream's keys are interpolated (Script_GetInterp's mode)
typedef enum {
    ScriptInterp_Linear = 2,
    ScriptInterp_Spline = 4,
} ScriptInterpMode;

// Bits of SSTREAM::flags on an entity stream
enum SStreamFlags {
    SStreamFlag_Event14 = 0x1, // set by script event 14; nothing reimplemented reads it
    SStreamFlag_Spline = 0x2,  // the keys are interpolated by spline (ScriptInterp_Spline), not linearly
};

#pragma pack(push, 1)

// One key of a script's motion (0x30 bytes): where its streams' objects are at a frame.
typedef struct KEYED_POSROT {
    _VECTOR pos;            // 0x00
    char _unknown_0c[4];
    quaternion_tag rot;     // 0x10
    float scale;            // 0x20
    union {
        float alpha;        // 0x24 - entities: 0 to 1
        uchar colour[4];    // 0x24 - lights: the colour, bytes 0-2
    };
    char _unknown_28[4];
    float frame;            // 0x2c
} KEYED_POSROT;

static_assert(sizeof(KEYED_POSROT) == 0x30, "Bad size for KEYED_POSROT");
static_assert(offsetof(KEYED_POSROT, rot) == 0x10, "Bad offset of rot");
static_assert(offsetof(KEYED_POSROT, alpha) == 0x24, "Bad offset of alpha");
static_assert(offsetof(KEYED_POSROT, frame) == 0x2c, "Bad offset of frame");

// One stream of a loaded script's file: its keys and its commands.
typedef struct {
    ushort firstKey;
    ushort lastKey;
    uchar *commands;
} SCRIPTDEF_STREAM;

// A script file as Script_Load registers it in the hashtable (the first time it sees the file); each instance
// (SCRIPTINFO) is made from it.
typedef struct SCRIPTDEF {
    HASHCODE hashcode;              // 0x000
    SCRIPTDEF_STREAM streams[64];   // 0x004
    KEYED_POSROT *keys;             // 0x204
    ushort numStreams;              // 0x208
    ushort numKeys;                 // 0x20a
    ushort loopStart;               // 0x20c
    ushort endFrame;                // 0x20e
    ushort hasCamera;               // 0x210
    char _pad_212[2];
} SCRIPTDEF;

static_assert(sizeof(SCRIPTDEF) == 0x214, "Bad size for SCRIPTDEF");
static_assert(offsetof(SCRIPTDEF, keys) == 0x204, "Bad offset of keys");
static_assert(offsetof(SCRIPTDEF, hasCamera) == 0x210, "Bad offset of hasCamera");

// One stream of a script instance - an entity's motion, a camera, a sound and so on, each reading its own commands.
typedef struct {
    float animStartTime;   // 0x0 - anim streams: elapsedFrames when the animation started
    float animStartFrame;  // 0x4 - anim streams: the animation's frame then
    float elapsedFrames;   // 0x8 - advanced by FRAME_RATE_MUL every Script_Update, cleared by Script_Set2Start
    float waitFrame;       // 0xc - the commands wait until elapsedFrames reaches this (command 5, Script_StreamEnd)
    void* linkedGameOrSoundObject; // 0x10 - what the stream drives: obj_tag* for entity and anim streams, sprite*,
                                   //        SCRIPTCAMERA*, DYNAMICSOUNDS*, light_tag* or the sub-script's SCRIPTINFO*
    uchar* streamBuffer;           // 0x14 - read head into this stream's raw command bytes
    uchar* streamBufferStart;      // 0x18 - saved starting position; Script_Set2Start rewinds streamBuffer to this
    ushort firstKey;               // 0x1c - the stream's keys in SCRIPTINFO::keys
    ushort lastKey;                // 0x1e
    ushort startFrame;             // 0x20 - entity and sub-script streams: elapsedFrames + 1 when the object was made
                                   //        (1 keeps it past the end of a looping script). Sound streams: the stream
                                   //        whose object the sound follows
    ushort flags;                  // 0x22 - entity streams: SStreamFlags. Sound streams: elapsedFrames + 1 when the
                                   //        sound started (1 keeps a looping sound past the end of a looping script)
    uchar whatKindOfInterpolation; // 0x24 - actually SStreamType, but can't tell the compiler to make the enum
                                    // just one byte (same issue as obj_tag::objectType/ObjectType). 0 means this
                                    // slot is inactive/unused
    char parent;                   // 0x25 - the stream whose transform this one's is relative to, -1 for none
    char _pad_26[2];
} SSTREAM;

static_assert(sizeof(SSTREAM) == 0x28, "Bad size for SSTREAM");
static_assert(offsetof(SSTREAM, elapsedFrames) == 0x8, "Bad offset of elapsedFrames");
static_assert(offsetof(SSTREAM, linkedGameOrSoundObject) == 0x10, "Bad offset of linkedGameOrSoundObject");
static_assert(offsetof(SSTREAM, streamBuffer) == 0x14, "Bad offset of streamBuffer");
static_assert(offsetof(SSTREAM, firstKey) == 0x1c, "Bad offset of firstKey");
static_assert(offsetof(SSTREAM, startFrame) == 0x20, "Bad offset of startFrame");
static_assert(offsetof(SSTREAM, whatKindOfInterpolation) == 0x24, "Bad offset of whatKindOfInterpolation");
static_assert(offsetof(SSTREAM, parent) == 0x25, "Bad offset of parent");

// A camera stream's own block (Mem_Malloc'd by Script_CameraStart, kept in the stream's linkedGameOrSoundObject)
typedef struct SCRIPTCAMERA {
    _MATRIX view;       // 0x00 - copied to camera 4 by Script_UpdateCamera
    float fov;          // 0x3c - multiplied by the camera's aspect ratio
    ushort firstKey;    // 0x40
    ushort lastKey;     // 0x42 - one past the camera's last key
    ushort interpMode;  // 0x44 - ScriptInterpMode
    char _pad_46[2];
} SCRIPTCAMERA;

static_assert(sizeof(SCRIPTCAMERA) == 0x48, "Bad size for SCRIPTCAMERA");
static_assert(offsetof(SCRIPTCAMERA, firstKey) == 0x40, "Bad offset of firstKey");

// Bits of SCRIPTINFO::scriptFlags
enum ScriptFlags {
    ScriptFlag_RestoreOnKillStream = 0x10,  // Script_KillStream restores (rather than deletes) entity/anim
                                             // streams' linked objects when their stream ends (copies the
                                             // transform matrix's position back into position/lastPosition and
                                             // sets TRANSFORM_MOVED, instead of marking them for deletion).
                                             // Set by script event 12
    ScriptFlag_FFwdKillsCamera = 0x20,      // Script_FFwd skips straight to the end by killing the active Camera
                                             // stream outright, instead of fading out over the remaining frames;
                                             // Script_CameraStart then only reads its header. Set by script event 16
    ScriptFlag_IsCameraNIS = 0x40,          // Script_Play sets ScriptCam from scriptHashcode when set (Script_Load:
                                             // the script file has a camera)
    ScriptFlag_ReEnableDronesOnFree = 0x80, // Script_Free calls Drone_EnableAll when set (script event 7)
    ScriptFlag_EnablePlayerOnFree = 0x100,  // Script_Free calls Player_Enable(glb_players[0], &otherMatrix,
                                             // playerEnableFlags) and PlarStat_LogTimerUnpause (script event 6)
};

// Bits of SCRIPTINFO::playbackFlags
enum ScriptPlaybackFlags {
    ScriptPlayback_Playing = 0x1,    // Set by Script_Play; cleared by Script_Stop, and by Script_StreamEnd at the
                                      // end of a script that does not loop
    ScriptPlayback_AtEnd = 0x2,      // Set by Script_StreamEnd (and command 6) once a stream reaches the end;
                                      // cleared by Script_Set2Start. Without Loop, Script_IsPlaying then reports
                                      // "not playing"
    ScriptPlayback_Loop = 0x8,       // Set by Script_Play(loop=true): Script_StreamEnd rewinds each stream to
                                      // loopStart rather than stopping
    ScriptPlayback_FFwdFading = 0x10, // Set by Script_FFwd when it kicks off a fade-out; cleared by Script_Update
                                       // once that fade finishes (see fadeElapsedFrames/fadeFrames/fadeFlags)
};

// Bits of SCRIPTINFO::fadeFlags
enum ScriptFadeFlags {
    ScriptFade_FFwdRequested = 0x1,  // Set by Script_FFwd; cleared by Script_Update once the fade-out finishes
    ScriptFade_FFwdAllowed = 0x10,   // Gates Script_FFwd entirely - set by Script_CameraStart, cleared by a
                                     // camera stream's end command
    ScriptFade_ThresholdHit = 0x20,  // Set by Script_Update every tick once fadeElapsedFrames has passed fadeFrames
};

typedef struct SCRIPTINFO {
    _MATRIX maybeMatrix;
    _MATRIX otherMatrix;      // 0x3c - where script event 6 asks Script_Free to put the player back
    void *scriptPlayer;       // 0x78 - the SCRIPTPLAYER given to Script_Load; its entity streams' objects point
                              //        back at it (obj_tag::scriptPlayer), its anim streams' at extraObjectData
    ScriptPlayerCallback *callback; // 0x7c - script event 15 calls it
    void *callbackContext;    // 0x80
    KEYED_POSROT *keys;       // 0x84 - the script file's keys
    char unknown_0x88[4];
    float scale; // 0x8c - eg. set by SP_CreateScriptPlayer
    HASHCODE scriptHashcode; // 0x90 - the script's own identifying hashcode (first word of the loaded binary
                              // asset, per Script_Load) - NOT a stream's own hashcode. Read back by
                              // Script_Free/Script_Play/SP_Update
    int waitFrames; // 0x94 - Script_Update waits while non-zero, counting it down when positive; script event 3
                    // sets it to its argument - 1, so 0 gives -1: wait until SP_UnPause clears it
    float maybeProgressFrames; // 0x98 - elapsed playback frames for the whole script (vs SSTREAM::elapsedFrames,
                                // which is per-stream); advanced by FRAME_RATE_MUL in Script_Update, compared
                                // against maybeNumFrames to detect "near the end"
    float fadeElapsedFrames;   // 0x9c - 0.0 means no fade-out running. Script_FFwd kicks this off (sets it to
                                // FRAME_RATE_MUL); Script_Update advances it by FRAME_RATE_MUL each tick until it
                                // passes fadeFrames, then leaves it capped (see ScriptFade_ThresholdHit) until
                                // Script_Update's finalize step resets it to 0.0
    SSTREAM streamDataBuffer[64]; // 0xa0
    ushort scriptFlags;  // 0xaa0 - see ScriptFlags
    ushort numElements; // 0xaa2 - number of live entries in streamDataBuffer (see Script_GetObj/Script_HideObj)
    ushort numKeys;     // 0xaa4
    ushort loopStart;   // 0xaa6 - the frame a looping script goes back to
    ushort maybeNumFrames; // at 0xaa8 - the script's end frame
    char playbackFlags; // 0xaaa - see ScriptPlaybackFlags
    uchar streamNum; // at 0xaab - the stream Script_Run is running
    uchar fadeFrames;  // 0xaac - target frame count for the Script_FFwd-triggered fade-out (compared against
                       // fadeElapsedFrames); unrelated to maybeNumFrames
    char fadeFlags;    // 0xaad - see ScriptFadeFlags
    char playerEnableFlags; // 0xaae - passed as Player_Enable's int param by Script_Free (ScriptFlag_EnablePlayerOnFree)
    char unknown_0xaaf;
} SCRIPTINFO;

static_assert(sizeof(SCRIPTINFO) == 0xab0, "SCRIPTINFO is what Script_Load allocates");
static_assert(offsetof(SCRIPTINFO, scriptPlayer) == 0x78, "Offset of scriptPlayer is bad");
static_assert(offsetof(SCRIPTINFO, keys) == 0x84, "Offset of keys is bad");
static_assert(offsetof(SCRIPTINFO, scale) == 0x8c, "Offset of scale is bad");
static_assert(offsetof(SCRIPTINFO, scriptHashcode) == 0x90, "Offset of scriptHashcode is bad");
static_assert(offsetof(SCRIPTINFO, waitFrames) == 0x94, "Offset of waitFrames is bad");
static_assert(offsetof(SCRIPTINFO, maybeProgressFrames) == 0x98, "Offset of maybeProgressFrames is bad");
static_assert(offsetof(SCRIPTINFO, fadeElapsedFrames) == 0x9c, "Offset of fadeElapsedFrames is bad");
static_assert(offsetof(SCRIPTINFO, streamDataBuffer[0]) == 0xa0, "Offset of streamDataBuffer is bad");
static_assert(offsetof(SCRIPTINFO, scriptFlags) == 0xaa0, "Offset of scriptFlags is bad");
static_assert(offsetof(SCRIPTINFO, numElements) == 0xaa2, "Offset of numElements is bad");
static_assert(offsetof(SCRIPTINFO, loopStart) == 0xaa6, "Offset of loopStart is bad");
static_assert(offsetof(SCRIPTINFO, maybeNumFrames) == 0xaa8, "Offset of maybeNumFrames is bad");
static_assert(offsetof(SCRIPTINFO, playbackFlags) == 0xaaa, "Offset of playbackFlags is bad");
static_assert(offsetof(SCRIPTINFO, streamNum) == 0xaab, "Offset of streamNum is bad");
static_assert(offsetof(SCRIPTINFO, fadeFrames) == 0xaac, "Offset of fadeFrames is bad");
static_assert(offsetof(SCRIPTINFO, fadeFlags) == 0xaad, "Offset of fadeFlags is bad");
static_assert(offsetof(SCRIPTINFO, playerEnableFlags) == 0xaae, "Offset of playerEnableFlags is bad");

#pragma pack(pop)

// Set by Script_Play when the playing script has ScriptFlag_IsCameraNIS set; read by whatever drives the
// camera stack to know which script currently owns it.
extern HASHCODE ScriptCam;

void Script_Free(SCRIPTINFO *param_1);
// The register-convention entries (Script.cpp says where each takes its arguments) and their C bodies
void Script_Run(void);
void _Script_Run(SCRIPTINFO *scriptInfo);
void Script_CameraStart(void);
void _Script_CameraStart(SCRIPTINFO *scriptInfo, SSTREAM *stream);
void Script_CreateEntity(void);
obj_tag* _Script_CreateEntity(SSTREAM *stream, HASHCODE hashcode, uchar placementType);
void Script_LightStart(void);
void _Script_LightStart(SCRIPTINFO *scriptInfo, SSTREAM *stream);
void Script_SpriteStart(void);
void _Script_SpriteStart(SSTREAM *stream);
void Script_StreamEnd(void);
void _Script_StreamEnd(SCRIPTINFO *scriptInfo, SSTREAM *stream);
void Script_GetInterp(void);
void _Script_GetInterp(SCRIPTINFO *scriptInfo, SSTREAM *stream, _MATRIX *mtx, float *scaleOut, uint *colourOut,
                       char concat, float posScale, ushort mode, ushort firstKey, ushort lastKey);
void Script_UpdateCamera(void);
void _Script_UpdateCamera(SCRIPTCAMERA *camera);
void Script_SoundStart(void);
void _Script_SoundStart(SCRIPTINFO *scriptInfo, SSTREAM *stream);
void Script_SubScriptStart(void);
void _Script_SubScriptStart(SCRIPTINFO *scriptInfo, SSTREAM *stream);
void Script_EventHandler(void);
void _Script_EventHandler(SCRIPTINFO *scriptInfo, SSTREAM *stream);
void Script_AnimStart(void);
void _Script_AnimStart(SCRIPTINFO *scriptInfo, SSTREAM *stream);
void Script_FindKey(void);
int _Script_FindKey(KEYED_POSROT *keys, int count, float frame);
void Script_InterpLinear(void);
void Script_InterpSpline(void);
void Script_ConcatParents(void);

void GetSplineWeights(int *keys, float *weights, float frame, KEYED_POSROT *base, int count);
void Script_GetLinearWeights(int *keys, float *weights, float frame, KEYED_POSROT *base, int count);
void Script_EntityStart(SCRIPTINFO *scriptInfo, SSTREAM *stream);
void Script_Interp(SCRIPTINFO *scriptInfo, SSTREAM *stream);
void Script_RemoveObj(obj_tag *obj, SCRIPTINFO *scriptInfo);
void Script_SetPosRot(SCRIPTINFO *param_1, _MATRIX *param_2);
void Script_SetColour(SCRIPTINFO *param_1, undefined1 clr_r, undefined1 clr_g, undefined1 clr_b);
SCRIPTINFO* Script_Load(HASHCODE hashcode, _VECTOR *transform, _VECTOR *rotation, uint *fileBuf, void *scriptPlayer,
                        ScriptPlayerCallback *callback, void *callbackContext);
void Script_FreeFromHashTable(HASHCODE hashcode);
void Script_Set2Start(SCRIPTINFO *scriptInfo);
void Script_Stop(SCRIPTINFO *scriptInfo);
void Script_Update(SCRIPTINFO *param_1);
bool Script_Play(SCRIPTINFO *param_1, char loop);
bool Script_IsPlaying(SCRIPTINFO* param_1);
obj_tag* Script_GetObj(SCRIPTINFO *scriptInfo, ushort streamIdx);
void Script_FFwd(SCRIPTINFO *scriptInfo, char doFFwd);
void Script_HideObj(SCRIPTINFO *scriptInfo, char hide);
bool Script_IsDeathNIS(HASHCODE hashcode);
void Script_KillStream(SCRIPTINFO *scriptInfo, SSTREAM *stream);
void Script_Init(void);

#endif // SCRIPT_H_
