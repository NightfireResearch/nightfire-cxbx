#include "Script.h"
#include "../game/obj/Emitter.h"
#include "../util/bin.h"
#include "Anim.h"
#include "Collide.h"
#include "Loader.h"
#include "Text.h"
#include "Camera.h"
#include "viewer.h"
#include "../memory.h"
#include "../math/math.h"
#include "../sound/music.h"
#include "../sound/Sound.h"
#include "../game/drone/Drone.h"
#include "../game/obj/Player.h"
#include "../game/obj/Light.h"
#include "../game/obj/control.h"
#include "../game/obj/build.h"
#include "../game/obj/Break.h"
#include "../game/obj/Destroy.h"
#include "../game/obj/Drone_AIHints.h"
#include "../game/obj/Ladder.h"
#include "../game/obj/Pickup.h"
#include "../game/obj/Switch.h"
#include "../game/obj/Trigger.h"
#include "../game/sp/PlayerStats.h"
#include "../game/sp/SwitchChannels.h"
#include "../game/view.h"
#include "../game/Spline.h"
#include "../game.h"
#include "../gfx/Sprite.h"
#include "../input.h"
#include "../engine/parsemap.h"

#include <math.h>
#include <string.h>

#include "../game/obj/Effect.h"
#pragma fp_contract(off)

// Two persistent letterbox-bar sprites: created and shown by Script_CameraStart, disabled (0xff) by Script_KillStream
// when a camera stream ends
// XBE_GLOBAL(0x00279350, 0x8)
static sprite* Borders[2];

// The command Script_Run is running
// XBE_GLOBAL(0x002793d0, 0x1)
static uchar Cmd;

// Script_GetInterp's working state: the keys either side of the frame and their weights (two for linear, four for
// spline), and what they interpolate to
typedef struct {
    _VECTOR pos;
    float _pad_0c;
    quaternion_tag rot;
    float scale;
    float alpha;
} InterpResult;
// XBE_GLOBAL(0x002793b0, 0x10)
static int InterpKeys[4];
// XBE_GLOBAL(0x002793c0, 0x10)
static float InterpWeights[4];
// XBE_GLOBAL(0x00279380, 0x28)
static InterpResult InterpRes;
// The two weighted positions Script_InterpLinear adds
// XBE_GLOBAL(0x00279358, 0x20)
static _VECTOR WeightedPos[2];
// The colours of the two keys a light stream is between
// XBE_GLOBAL(0x0027937c, 0x4)
static uchar *LightColourFrom;
// XBE_GLOBAL(0x00279378, 0x4)
static uchar *LightColourTo;
// Where Script_Interp builds an entity's or a light's matrix
// XBE_GLOBAL(0x002793d4, 0x3c)
static _MATRIX InterpMatrix;

// The placement Script_CreateEntity hands each object's creator: zeroed but for the parameters Script_EntityStart
// reads from the stream. The game's, at its address: Script_EntityStart writes as many parameters as the stream
// gives, without a limit.
typedef struct {
    char header[0x2c];
    uint params[33];    // 0x2c
} ScriptPlacement;
#define tptr (*(ScriptPlacement *)0x002792a0)

// The list Script_Load hands a script file back to when it has too many streams
#define ScriptList (*(DLISTINFO_tag *)0x00279288)

// The letterbox bars' sprite info (the game's data)
#define LetterboxSprInfo ((SpriteInfo *)0x00181be4)
#define LETTERBOX_BOTTOM_Y 0x1a0

// The originals, called by address where Ghidra's prototypes do not fit
// The originals: ours in Break.cpp are not patched in yet
#define moveTowardsValue ((float (__cdecl *)(float current, float target, float speed))0x00024000)

// AUTOGEN
void Vec_Mul_F3(_VECTOR *vecOut, _VECTOR *vecIn, float scaleX, float scaleY, float scaleZ);
// AUTOGEN
void NDrone2_CreateNinjaEyes(obj_tag *obj);
// Generated for Player.cpp
void Player_RamSave(BLData *blData);
// Generated for Player.cpp

typedef enum ScriptCmd {
    ScriptCmd_EndScript=4,
    ScriptCmd_Wait=5,       // wait until the stream's elapsed frames reach the word that follows
    ScriptCmd_MarkEnd=6,    // sets ScriptPlayback_AtEnd once the script is past its end
    ScriptCmd_EntityStart=7,
    ScriptCmd_EntityEnd=9,
    ScriptCmd_AnimStart=10,
    ScriptCmd_AnimEnd=12,
    ScriptCmd_CameraStart=13,
    ScriptCmd_CameraEnd=15,
    ScriptCmd_EventHandler=18,
    ScriptCmd_FadeStart=19,
    ScriptCmd_SpriteStart=20,
    ScriptCmd_SpriteEnd=21,
    ScriptCmd_SoundStart=22,
    ScriptCmd_SoundEnd=23,
    ScriptCmd_LightStart=24,
    ScriptCmd_LightEnd=26,
    ScriptCmd_SubScriptStart=27,
    ScriptCmd_SubScriptEnd=29,
    ScriptCmd_TextStart=30
} ScriptCmd;

// The script events (Script_EventHandler)
typedef enum ScriptEvent {
    ScriptEvent_Wait = 3,
    ScriptEvent_DisablePlayer = 4,
    ScriptEvent_DisableDrones = 5,
    ScriptEvent_EnablePlayerOnFree = 6,
    ScriptEvent_EnableDronesOnFree = 7,
    ScriptEvent_CreateDrone = 8,
    ScriptEvent_PlayerCamMode = 9,
    ScriptEvent_Break = 10,
    ScriptEvent_SetWeapon = 11,
    ScriptEvent_RestoreOnKill = 12,
    ScriptEvent_SetSwitch = 13,
    ScriptEvent_FlagStream = 14,
    ScriptEvent_Callback = 15,
    ScriptEvent_NoCamera = 16,
    ScriptEvent_SetSwitchIf = 17,
    ScriptEvent_LoadLevel = 18,
    ScriptEvent_TakeCamera = 19,
} ScriptEvent;

// Cleans up whatever a single active stream slot is currently linked to, depending on its type, then clears the
// slot. "Restoring" (ScriptFlag_RestoreOnKillStream) leaves persistent things (the object itself, a light) alone
// and just detaches the script's control over them; otherwise they're deleted/freed outright.
// AUTOINJECT
void Script_KillStream(SCRIPTINFO *scriptInfo, SSTREAM *stream) {
    switch (stream->whatKindOfInterpolation) {
    case SStream_Entity: {
        obj_tag* linkedObj = (obj_tag*)stream->linkedGameOrSoundObject;
        if (linkedObj != NULL) {
            linkedObj->scriptPlayer = NULL;
            if (scriptInfo->scriptFlags & ScriptFlag_RestoreOnKillStream) {
                Vec_Copy2(Mat_Position(linkedObj->transformMatrix), &linkedObj->position, &linkedObj->lastPosition);
                linkedObj->transformFlags |= TRANSFORM_MOVED;
            } else {
                linkedObj->flags |= 1; // mark for deletion
            }
        }
        break;
    }

    // Anim/Sprite/Camera/Light all deliberately leave the slot untouched (return rather than falling through to
    // the clear below) when there's nothing linked - matching the original exactly, unlike Entity above
    case SStream_Anim: {
        obj_tag* linkedObj = (obj_tag*)stream->linkedGameOrSoundObject;
        if (linkedObj == NULL)
            return;

        if (scriptInfo->scriptFlags & ScriptFlag_RestoreOnKillStream) {
            linkedObj->extraObjectData = NULL;
        } else {
            linkedObj->flags |= 1; // mark for deletion
        }
        break;
    }

    case SStream_Sprite:
        if (stream->linkedGameOrSoundObject == NULL)
            return;
        Sprite_Delete((sprite*)stream->linkedGameOrSoundObject);
        break;

    case SStream_Camera: {
        void* cameraState = stream->linkedGameOrSoundObject;
        if (cameraState == NULL)
            return;

        Camera_PopStates();
        Camera_Enable(4, 0, 0, NULL);
        Mem_Free(&cameraState);
        Music_Event(0xc, scriptInfo->scriptHashcode);

        if (Borders[0] != NULL) {
            Borders[0]->maybeEnabled = 0xff;
        }
        if (Borders[1] != NULL) {
            Borders[1]->maybeEnabled = 0xff;
        }
        break;
    }

    case SStream_Sound:
        Sound_Stop((DYNAMICSOUNDS*)stream->linkedGameOrSoundObject);
        break;

    case SStream_Light:
        if (stream->linkedGameOrSoundObject == NULL)
            return;
        if (!(scriptInfo->scriptFlags & ScriptFlag_RestoreOnKillStream)) {
            Light_Delete((light_tag*)stream->linkedGameOrSoundObject);
        }
        break;

    case SStream_SubScript:
        if (stream->linkedGameOrSoundObject != NULL) {
            Script_Free((SCRIPTINFO*)stream->linkedGameOrSoundObject);
        }
        break;
    }

    // Every case above either returned early (nothing linked, for the types where that means "leave it alone")
    // or falls through to here to detach the (possibly just freed/deleted) link
    stream->linkedGameOrSoundObject = NULL;
    stream->whatKindOfInterpolation = 0;
}

// ---------------------------------------------------------------------------------------------------------------
// Interpolating a stream's keys
// ---------------------------------------------------------------------------------------------------------------

// The key at frame, or the last before it: a binary search over count + 1 keys by their frames, which nudges the
// frame up by 0.001 first. Below the first key it answers -1.
int _Script_FindKey(KEYED_POSROT *keys, int count, float frame) {
    float target = frame + 0.001f;
    int low = 0;
    int high = count;
    do {
        int middle = (low + high) >> 1;
        if (target < keys[middle].frame) {
            high = middle - 1;
        } else if (target > keys[middle].frame) {
            low = middle + 1;
        } else {
            return middle;
        }
    } while (low <= high);
    return high;
}

// The original takes the count in EAX and the keys in EBX, the frame on the stack.
// AUTOLTCG
void __declspec(naked) Script_FindKey(void) {
    _asm {
        push dword ptr [esp + 4]
        push eax
        push ebx
        call _Script_FindKey
        add esp, 12
        ret
    }
}

// The two keys around the whole frame of time (keys[0], keys[1]) and their weights for linear interpolation.
// Outside the keys, the end key alone.
// AUTOINJECT
void Script_GetLinearWeights(int *keys, float *weights, float time, KEYED_POSROT *base, int count) {
    float frame = (float)(int)time;
    int key = _Script_FindKey(base, count, frame);
    keys[0] = key;
    if (key >= count || key < 0) {
        keys[0] = key >= count ? count : 0;
        weights[0] = 1.0f;
        keys[1] = keys[0];
        weights[1] = 0.0f;
        return;
    }

    keys[1] = key + 1;
    double t = ((double)frame - base[key].frame) / ((double)base[key + 1].frame - base[key].frame);
    weights[1] = (float)t;
    weights[0] = (float)(1.0 - t);
}

// Catmull-Rom weights of the four keys around the whole frame of time (keys[0..3]). Outside the keys, the end key
// alone.
// AUTOINJECT
void GetSplineWeights(int *keys, float *weights, float time, KEYED_POSROT *base, int count) {
    float frame = (float)(int)time;
    int key = _Script_FindKey(base, count, frame);
    keys[1] = key;
    if (key >= count || key < 0) {
        keys[1] = key >= count ? count : 0;
        weights[1] = 1.0f;
        keys[3] = keys[1];
        keys[2] = keys[1];
        keys[0] = keys[1];
        weights[3] = 0.0f;
        weights[2] = 0.0f;
        weights[0] = 0.0f;
        return;
    }

    int next = key + 1;
    if (count < next)
        next = count;
    keys[0] = key - 1 < 0 ? 0 : key - 1;
    keys[2] = next;
    keys[3] = count < key + 2 ? count : key + 2;

    // In the x87's precision until each weight is stored
    double t = ((double)frame - base[key].frame) / ((double)base[next].frame - base[key].frame);
    double t2 = t * t;
    double t3 = t2 * t;
    weights[0] = (float)(t2 - (t3 + t) * 0.5);
    weights[1] = (float)((1.0 - t2 * 2.5) + t3 * 1.5);
    weights[2] = (float)((t2 + t2 + t * 0.5) - t3 * 1.5);
    weights[3] = (float)((t3 - t2) * 0.5);
}

// Interpolates two keys linearly (the position by weights, the rotation by slerp).
static void _Script_InterpLinear(int *keys, float *weights, KEYED_POSROT *base, InterpResult *res, char isCamera) {
    KEYED_POSROT *from = &base[keys[0]];
    KEYED_POSROT *to = &base[keys[1]];
    Vec_MulR32(&WeightedPos[0], &from->pos, weights[0]);
    Vec_MulR32(&WeightedPos[1], &to->pos, weights[1]);
    Vec_Add2(&WeightedPos[0], &WeightedPos[1], &res->pos);
    Quat_Slerp_Acc(weights[1], from->rot.q, to->rot.q, res->rot.q);
    res->scale = (float)((double)to->scale * weights[1] + (double)from->scale * weights[0]);
    res->alpha = (float)((double)to->alpha * weights[1] + (double)from->alpha * weights[0]);
}

// The original takes the keys in ECX, the weights in EBX and the base key in EDX; the result and a flag on the stack.
// AUTOLTCG
void __declspec(naked) Script_InterpLinear(void) {
    _asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push edx
        push ebx
        push ecx
        call _Script_InterpLinear
        add esp, 20
        ret
    }
}

// Interpolates four keys by spline (the position by Catmull-Rom, the rotation by slerp between the middle two).
static void _Script_InterpSpline(int *keys, float *weights, KEYED_POSROT *base, InterpResult *res, char isCamera) {
    KEYED_POSROT *k0 = &base[keys[0]];
    KEYED_POSROT *k1 = &base[keys[1]];
    KEYED_POSROT *k2 = &base[keys[2]];
    KEYED_POSROT *k3 = &base[keys[3]];
    Spline_Eval3D(&k0->pos, &k1->pos, &k2->pos, &k3->pos, weights[1], &res->pos);
    Quat_Slerp_Acc(weights[1], k1->rot.q, k2->rot.q, res->rot.q);
    res->scale = (float)((((double)k1->scale * weights[1] + (double)k0->scale * weights[0])
                          + (double)k3->scale * weights[3]) + (double)k2->scale * weights[2]);
    res->alpha = (float)((((double)k1->alpha * weights[1] + (double)k0->alpha * weights[0])
                          + (double)k3->alpha * weights[3]) + (double)k2->alpha * weights[2]);
}

// The original takes the keys in EAX, the weights in ESI and the base key in ECX; the result and a flag on the stack.
// AUTOLTCG
void __declspec(naked) Script_InterpSpline(void) {
    _asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push ecx
        push esi
        push eax
        call _Script_InterpSpline
        add esp, 20
        ret
    }
}

// The matrix (and scale, and alpha or colour) of a stream at its current frame, from keys firstKey to lastKey,
// interpolated by mode. The position is scaled by posScale, and with concat put in the script's frame.
void _Script_GetInterp(SCRIPTINFO *scriptInfo, SSTREAM *stream, _MATRIX *mtx, float *scaleOut, uint *colourOut,
                       char concat, float posScale, ushort mode, ushort firstKey, ushort lastKey) {
    KEYED_POSROT *keys = &scriptInfo->keys[firstKey];
    if (mode == ScriptInterp_Linear) {
        Script_GetLinearWeights(InterpKeys, InterpWeights, stream->elapsedFrames, keys, lastKey - firstKey);
        _Script_InterpLinear(InterpKeys, InterpWeights, keys, &InterpRes,
                             stream->whatKindOfInterpolation == SStream_Camera);
    } else {
        GetSplineWeights(InterpKeys, InterpWeights, stream->elapsedFrames, keys, lastKey - firstKey);
        _Script_InterpSpline(InterpKeys, InterpWeights, keys, &InterpRes,
                             stream->whatKindOfInterpolation == SStream_Camera);
    }

    bool hasAlpha = false;
    switch (stream->whatKindOfInterpolation) {
    case 0:
    case SStream_Entity:
    case SStream_Camera:
    case SStream_SubScript:
        Quat_QuatToMat(&InterpRes.rot, mtx);
        Vec_Copy(&InterpRes.pos, Mat_Position(*mtx));
        if (scaleOut != NULL)
            *scaleOut = InterpRes.scale * scriptInfo->scale;
        hasAlpha = true;
        break;

    case SStream_Anim:
        if (!concat) {
            obj_tag *part = AnimGetNDiscreteObj((obj_tag *)stream->linkedGameOrSoundObject, 0);
            if (part != NULL)
                Mat_Copy(&part->transformMatrix, mtx);
        }
        if (scaleOut != NULL)
            *scaleOut = InterpRes.scale * scriptInfo->scale;
        hasAlpha = true;
        break;

    case SStream_Light: {
        // Each channel moves from the first key's colour towards the second's by the first key's weight
        LightColourFrom = keys[InterpKeys[0]].colour;
        LightColourTo = keys[InterpKeys[1]].colour;
        uint red = (int)moveTowardsValue(LightColourFrom[2], LightColourTo[2], InterpWeights[0]);
        uint green = (int)moveTowardsValue(LightColourFrom[1], LightColourTo[1], InterpWeights[0]);
        uint blue = (int)moveTowardsValue(LightColourFrom[0], LightColourTo[0], InterpWeights[0]);
        *colourOut = (((red << 8 | green) << 8 | blue) << 8) | 0xff;
        *scaleOut = InterpRes.scale * scriptInfo->scale;
        Vec_Copy(&InterpRes.pos, Mat_Position(*mtx));
        break;
    }
    }

    // The float to unsigned conversion of the original's compiler: the low dword of a 64-bit truncation
    if (hasAlpha && colourOut != NULL)
        *colourOut = (uint)(int64_t)((double)InterpRes.alpha * 255.0f + 0.5f);

    if (posScale != 1.0f)
        Vec_Mul_F3(Mat_Position(*mtx), Mat_Position(*mtx), posScale, posScale, posScale);
    if (concat)
        Concat(&scriptInfo->maybeMatrix, mtx);
}

// The original takes the mode in EAX, firstKey in DX and lastKey in CX, the rest on the stack.
// AUTOLTCG
void __declspec(naked) Script_GetInterp(void) {
    _asm {
        movzx ecx, cx
        push ecx
        movzx edx, dx
        push edx
        movzx eax, ax
        push eax
        push dword ptr [esp + 40]
        push dword ptr [esp + 40]
        push dword ptr [esp + 40]
        push dword ptr [esp + 40]
        push dword ptr [esp + 40]
        push dword ptr [esp + 40]
        push dword ptr [esp + 40]
        call _Script_GetInterp
        add esp, 40
        ret
    }
}

// Puts out in the frame of stream's parent, and of its parent in turn up to a stream with none, then of the
// script. A chain that leads out of the script's streams stops there, without the script's frame.
static void _Script_ConcatParents(SCRIPTINFO *scriptInfo, SSTREAM *stream, _MATRIX *out) {
    _MATRIX parentMtx;
    float scale;
    uint colour;

    short parent = stream->parent;
    short depth = 0;
    Mat_IdentityT(&parentMtx);
    for (;;) {
        if (parent >= scriptInfo->numElements || depth >= scriptInfo->numElements)
            return;

        SSTREAM *parentStream = &scriptInfo->streamDataBuffer[parent];
        ushort mode = (parentStream->flags & SStreamFlag_Spline) ? ScriptInterp_Spline : ScriptInterp_Linear;
        _Script_GetInterp(scriptInfo, parentStream, &parentMtx, &scale, &colour, 0, scriptInfo->scale, mode,
                          parentStream->firstKey, parentStream->lastKey);
        Concat(&parentMtx, out);
        parent = parentStream->parent;
        depth++;
        if (parent == -1 || depth > scriptInfo->numElements)
            break;
    }
    Concat(&scriptInfo->maybeMatrix, out);
}

// The original takes the stream in EAX and the script in EDI, the matrix on the stack.
// AUTOLTCG
void __declspec(naked) Script_ConcatParents(void) {
    _asm {
        push dword ptr [esp + 4]
        push eax
        push edi
        call _Script_ConcatParents
        add esp, 12
        ret
    }
}

// Copies a camera stream's view to camera 4 and leaves the camera to find its room again.
void _Script_UpdateCamera(SCRIPTCAMERA *camera) {
    if (camera == NULL || glb_viewer[4] == NULL)
        return;

    glb_viewer[4]->viewMatrix = camera->view;
    Camera_CalcViewAngles(4, glb_viewer[4]->AspectRatio * camera->fov);
    glb_viewer[4]->someCel = NULL;
}

// The original takes the camera block in EAX.
// AUTOLTCG
void __declspec(naked) Script_UpdateCamera(void) {
    _asm {
        push eax
        call _Script_UpdateCamera
        add esp, 4
        ret
    }
}

// Puts what a stream drives where its keys say for the stream's current frame: an entity (its matrix, scale and
// alpha), an animation (its frame, and the footstep it lands), the camera, a sound (its fade and position), a
// light (position, radius and colour) or a sub-script (its frame, then a tick of it).
// AUTOINJECT
void Script_Interp(SCRIPTINFO *scriptInfo, SSTREAM *stream) {
    if (stream->linkedGameOrSoundObject == NULL)
        return;

    switch (stream->whatKindOfInterpolation) {
    case SStream_Entity: {
        obj_tag *obj = (obj_tag *)stream->linkedGameOrSoundObject;
        uint colour;
        obj->transformFlags |= TRANSFORM_MOVED;
        ushort mode = (stream->flags & SStreamFlag_Spline) ? ScriptInterp_Spline : ScriptInterp_Linear;
        _Script_GetInterp(scriptInfo, stream, &InterpMatrix, &obj->scale, &colour, stream->parent == -1,
                          scriptInfo->scale, mode, stream->firstKey, stream->lastKey);
        obj->tweakA = (uchar)colour;
        if (obj->scale == 1.0f) {
            Mat_Copy(&InterpMatrix, &obj->transformMatrix);
        } else {
            Mat_Scale3f(&obj->transformMatrix, &InterpMatrix, obj->scale, obj->scale, obj->scale);
        }
        if (stream->parent != -1)
            _Script_ConcatParents(scriptInfo, stream, &obj->transformMatrix);
        break;
    }

    case SStream_Anim: {
        obj_tag *obj = (obj_tag *)stream->linkedGameOrSoundObject;
        AnimObj *animObj = &obj->animState->animObj;
        if (scriptInfo->fadeFlags & ScriptFade_ThresholdHit)
            return;

        // Every script-driven animation on the object is held at the stream's frame
        AnimListBuild(obj, 1, 0, NULL, 1);
        for (int i = 0; i < GlbAnimListCount; i++) {
            sAnimScript_tag *animScript = GlbAnimList[i];
            if (!(animScript->flags & ANIMSCRIPT_SCRIPT_DRIVEN))
                continue;

            double frame = ((double)stream->elapsedFrames - stream->animStartTime) + stream->animStartFrame;
            animScript->curFrame = (float)frame;
            if (frame < 1.0) {
                animScript->curFrame = 1.0f;
            } else if ((float)frame > animScript->numFrames) {
                animScript->curFrame = animScript->numFrames;
            }
            animScript->speed = 0.0f;
        }
        AnimObjectUpdate(obj);
        obj->transformFlags |= TRANSFORM_MOVED;
        if (animObj->skin->hashcode == 0x500001e)
            NDrone2_CreateNinjaEyes(obj);

        // A footstep this frame: a ray 4 down from the object finds the ground under it
        if (!(obj->animState->animFlags & 4))
            return;

        _VECTOR from, to;
        Vec_Copy2(Mat_Position(obj->transformMatrix), &from, &to);
        to.y -= 4.0f;
        HITDATA_tag *hitList = NULL;
        if (!Collide_RayIntersect(&from, &to, build_FindCel(&from, glb_world), obj, NULL, &hitList, 0, 0xf0d, 0))
            return;

        if ((double)hitList->dist - animObj->rootHeight <= 0.2f)
            Effect_Create(hitList, obj, (FootstepFoot != 0 ? 8 : 4) | 0x42, 1.0f);
        Collide_FreeHitList(&hitList);
        break;
    }

    case SStream_Camera: {
        SCRIPTCAMERA *camera = (SCRIPTCAMERA *)stream->linkedGameOrSoundObject;
        uint colour;
        if (scriptInfo->fadeFlags & ScriptFade_ThresholdHit)
            return;

        _Script_GetInterp(scriptInfo, stream, &camera->view, &camera->fov, &colour, 1, scriptInfo->scale,
                          camera->interpMode, camera->firstKey, camera->lastKey - 1);
        _Script_UpdateCamera(camera);
        break;
    }

    case SStream_Sound: {
        DYNAMICSOUNDS *sound = (DYNAMICSOUNDS *)stream->linkedGameOrSoundObject;
        if (Sound_IsFinnished(sound)) {
            Script_KillStream(scriptInfo, stream);
            return;
        }

        // Fades out with a fast-forward
        if (scriptInfo->fadeElapsedFrames != 0.0f) {
            double frames = scriptInfo->fadeFrames;
            Sound_SetVolume(sound, (float)((frames - scriptInfo->fadeElapsedFrames) / frames * 100.0f));
        }

        obj_tag *obj = Script_GetObj(scriptInfo, stream->startFrame);
        if (obj != NULL)
            Sound_SetPosition(sound, Mat_Position(obj->transformMatrix));
        break;
    }

    case SStream_Light: {
        light_tag *light = (light_tag *)stream->linkedGameOrSoundObject;
        uint colour;
        _Script_GetInterp(scriptInfo, stream, &InterpMatrix, &light->brightness, &colour, stream->parent == -1,
                          scriptInfo->scale, ScriptInterp_Linear, stream->firstKey, stream->lastKey);
        Vec_Copy(Mat_Position(InterpMatrix), &light->position);
        light->clr_r = colour >> 24;
        light->clr_g = colour >> 16;
        light->clr_b = colour >> 8;
        if (stream->parent != -1) {
            _Script_ConcatParents(scriptInfo, stream, &InterpMatrix);
            Vec_Copy(Mat_Position(InterpMatrix), &light->position);
        }
        break;
    }

    case SStream_SubScript: {
        SCRIPTINFO *subScript = (SCRIPTINFO *)stream->linkedGameOrSoundObject;
        uint colour;
        _Script_GetInterp(scriptInfo, stream, &subScript->maybeMatrix, &subScript->scale, &colour,
                          stream->parent == -1, subScript->scale, ScriptInterp_Linear, stream->firstKey,
                          stream->lastKey);
        if (stream->parent != -1)
            _Script_ConcatParents(scriptInfo, stream, &subScript->maybeMatrix);
        Script_Update(subScript);
        break;
    }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The commands
// ---------------------------------------------------------------------------------------------------------------

// Starts a camera stream of a script: reads its header (a mode byte and two words), takes over the cameras (every
// one disabled, camera 4 enabled for the script), plays the cutscene music event, and shows the letterbox bars -
// creating them the first time. A script flagged ScriptFlag_FFwdKillsCamera only has the header read. The
// original ends by jumping to Light_Update.
void _Script_CameraStart(SCRIPTINFO *scriptInfo, SSTREAM *stream) {
    ScriptCam = scriptInfo->scriptHashcode;
    Mat_IdentityT(&scriptInfo->maybeMatrix);
    uchar spline = BIN_GetByte(&stream->streamBuffer);
    ushort firstKey = BIN_GetWord(&stream->streamBuffer);
    ushort lastKey = BIN_GetWord(&stream->streamBuffer);
    if (scriptInfo->scriptFlags & ScriptFlag_FFwdKillsCamera)
        return;

    Input_ClearAllActions(-1);
    SCRIPTCAMERA *camera = (SCRIPTCAMERA *)stream->linkedGameOrSoundObject;
    if (camera == NULL) {
        camera = (SCRIPTCAMERA *)Mem_Malloc(sizeof(SCRIPTCAMERA), (MallocFlags)0x1404, 0);
        if (camera == NULL)
            return;
    }
    camera->firstKey = firstKey;
    camera->lastKey = lastKey;
    camera->interpMode = spline ? ScriptInterp_Spline : ScriptInterp_Linear;
    stream->whatKindOfInterpolation = SStream_Camera;
    stream->linkedGameOrSoundObject = camera;
    Script_Interp(scriptInfo, stream);

    Camera_PushStates();
    for (ushort i = 0; i <= 6; i++)
        Camera_Enable(i, 0, 0, NULL);
    Camera_Enable(4, 1, 1, NULL);
    scriptInfo->fadeFlags |= ScriptFade_FFwdAllowed;
    Music_Event(0xb, scriptInfo->scriptHashcode);

    if (Borders[0] == NULL)
        Borders[0] = Sprite_Create2(LetterboxSprInfo);
    if (Borders[1] == NULL) {
        Borders[1] = Sprite_Create2(LetterboxSprInfo);
        Borders[1]->positionY = LETTERBOX_BOTTOM_Y;   // not checked for NULL, as in the original
    }
    if (Borders[0] != NULL)
        Borders[0]->maybeEnabled = 1;
    if (Borders[1] != NULL)
        Borders[1]->maybeEnabled = 1;

    Light_Update();
}

// The original takes the stream in EAX and the script in ESI (link-time code generation; pops nothing). Only the
// original script interpreter calls it; C callers call _Script_CameraStart.
// AUTOLTCG
void __declspec(naked) Script_CameraStart(void) {
    _asm {
        push eax
        push esi
        call _Script_CameraStart
        add esp, 8
        ret
    }
}

// The object an entity stream drives, made by the creator its placement type names (with tptr as the placement), or
// a plain object showing hashcode. An object the stream already has is kept if it is of the type wanted - which only
// a breakable, a ladder or an AI volume can be, as the others are compared by their placement type. The object
// leaves its room.
obj_tag* _Script_CreateEntity(SSTREAM *stream, HASHCODE hashcode, uchar placementType) {
    _MATRIX identity;
    _VECTOR scale;

    Mat_IdentityT(&identity);
    uchar type;
    switch (placementType) {
    case Place_Breakable:
        type = OBJECTTYPE_BREAK;
        break;
    case Place_Ladder:
        type = OBJECTTYPE_LADDER;
        break;
    case Place_DroneAIVolume1:
        type = OBJECTTYPE_DRONE_AIVOLUME;
        break;
    case Place_Switch:
    case Place_Destroy:
    case Place_TriggerTouchOnce:
    case Place_TriggerTouch:
    case Place_Pickup:
    case Place_Emitter:
        type = placementType;
        break;
    default:
        type = 0;
        break;
    }

    obj_tag *obj = (obj_tag *)stream->linkedGameOrSoundObject;
    if (obj != NULL) {
        if (obj->objectType == type) {
            hashtable_set_object_to_entity_gfx(obj, hashcode);
            control_unlink_object(obj);
            return obj;
        }
        obj->flags |= 1; // mark for deletion
        stream->linkedGameOrSoundObject = NULL;
    }

    scale.x = 1.0f;
    scale.y = 1.0f;
    scale.z = 1.0f;
    level_tag *placement = (level_tag *)&tptr;
    switch (type) {
    case Place_Switch:
        obj = Switch_Create(&CONST_ZERO_VECTOR, &CONST_ZERO_VECTOR, placement, hashtable_hashcode_to_celglist(hashcode));
        break;
    case Place_Emitter:
        obj = Emitter_CreatePlist(&identity, placement);
        control_unlink_object(obj);
        break;
    case Place_Destroy:
        obj = Destroy_Create(&CONST_ZERO_VECTOR, &CONST_ZERO_VECTOR, placement,
                             hashtable_hashcode_to_celglist(hashcode), NULL);
        break;
    case Place_Pickup:
        obj = Pickup_Create(&CONST_ZERO_VECTOR, &CONST_ZERO_VECTOR, NULL, hashtable_hashcode_to_celglist(hashcode),
                            tptr.params[0], tptr.params[1], tptr.params[2], tptr.params[3], tptr.params[4], 1,
                            tptr.params[5], 0, 0);
        break;
    case OBJECTTYPE_DRONE_AIVOLUME:
        obj = Drone_AIVolume_Create(&CONST_ZERO_VECTOR, &CONST_ZERO_VECTOR, &scale, placement,
                                    hashtable_hashcode_to_celglist(hashcode));
        break;
    case OBJECTTYPE_LADDER:
        obj = Ladder_Create(NULL, NULL, placement, hashtable_hashcode_to_celglist(hashcode));
        break;
    case OBJECTTYPE_BREAK:
        obj = Break_Create(NULL, NULL, hashtable_hashcode_to_celglist(hashcode), placement);
        break;
    case Place_TriggerTouch:
        obj = Trigger_Touch(&CONST_ZERO_VECTOR, &CONST_ZERO_VECTOR, placement, hashtable_hashcode_to_celglist(hashcode));
        break;
    case Place_TriggerTouchOnce:
        obj = Trigger_TouchOnce(&CONST_ZERO_VECTOR, &CONST_ZERO_VECTOR, placement,
                                hashtable_hashcode_to_celglist(hashcode));
        break;
    default:
        obj = control_create_object(0, NULL, NULL, NULL);
        if (hashcode != 0)
            hashtable_set_object_to_entity_gfx(obj, hashcode);
        break;
    }
    control_unlink_object(obj);
    return obj;
}

// The original takes the stream on the stack, the hashcode in EDI and the placement type in BL.
// AUTOLTCG
void __declspec(naked) Script_CreateEntity(void) {
    _asm {
        movzx eax, bl
        push eax
        push edi
        push dword ptr [esp + 12]
        call _Script_CreateEntity
        add esp, 12
        ret
    }
}

// Command 7: an entity follows the stream's keys - hashcode, parent (if any), spline or not, placement type and
// that placement's parameters. The object points back at the script player.
// AUTOINJECT
void Script_EntityStart(SCRIPTINFO *scriptInfo, SSTREAM *stream) {
    HASHCODE hashcode = (HASHCODE)BIN_GetDWord(&stream->streamBuffer);
    uchar hasParent = BIN_GetByte(&stream->streamBuffer);
    uchar parent = BIN_GetByte(&stream->streamBuffer);
    uchar spline = BIN_GetByte(&stream->streamBuffer);
    uchar placementType = BIN_GetByte(&stream->streamBuffer);
    uchar numParams = BIN_GetByte(&stream->streamBuffer);
    for (uint i = 0; i < numParams; i++)
        tptr.params[i] = BIN_GetDWord(&stream->streamBuffer);

    obj_tag *obj = _Script_CreateEntity(stream, hashcode, placementType);
    if (obj == NULL)
        return;

    if (stream->linkedGameOrSoundObject == NULL)
        stream->startFrame = (int)stream->elapsedFrames + 1;
    stream->parent = hasParent ? parent : -1;
    stream->linkedGameOrSoundObject = obj;
    stream->whatKindOfInterpolation = SStream_Entity;
    stream->flags = spline ? SStreamFlag_Spline : 0;
    obj->transformFlags |= TRANSFORM_MATRIX_PLACED | TRANSFORM_MOVED | TRANSFORM_POSITION_FROM_MATRIX | 0x80;
    obj->scriptPlayer = scriptInfo->scriptPlayer;
    Script_Interp(scriptInfo, stream);
    Control_BuildWorldSph(obj);
    if (obj->inCel == NULL)
        build_LinkToRoom(obj, 1, glb_world);

    _VECTOR norm;
    Mat_GetNorm(&norm, &obj->transformMatrix);
    Vec_NormToRot(&norm, &obj->rotation);
    Vec_Copy2(Mat_Position(obj->transformMatrix), &obj->lastPosition, &obj->position);
}

// Command 24: a light (white, radius 255 until the first interpolation) follows the stream's keys - parent (if any)
// and kind (1 for one with light type 1).
void _Script_LightStart(SCRIPTINFO *scriptInfo, SSTREAM *stream) {
    uchar hasParent = BIN_GetByte(&stream->streamBuffer);
    uchar parent = BIN_GetByte(&stream->streamBuffer);
    uchar kind = BIN_GetByte(&stream->streamBuffer);
    stream->parent = hasParent ? parent : -1;
    stream->linkedGameOrSoundObject = Light_Create(&CONST_ZERO_VECTOR, 0xff, 2, 3, 255.0f, 0, -1, kind == 1, 1.0f,
                                                   0, 0, 0, 0xffff);
    stream->whatKindOfInterpolation = SStream_Light;
    Script_Interp(scriptInfo, stream);
}

// The original takes the script on the stack and the stream in EDI.
// AUTOLTCG
void __declspec(naked) Script_LightStart(void) {
    _asm {
        push edi
        push dword ptr [esp + 8]
        call _Script_LightStart
        add esp, 8
        ret
    }
}

// Command 20: a sprite on camera 4 - its rectangle, flags (2 and 4 select two of the sprite's own flags) and
// image.
void _Script_SpriteStart(SSTREAM *stream) {
    ushort x = BIN_GetWord(&stream->streamBuffer);
    ushort y = BIN_GetWord(&stream->streamBuffer);
    ushort width = BIN_GetWord(&stream->streamBuffer);
    ushort height = BIN_GetWord(&stream->streamBuffer);
    ushort flags = BIN_GetWord(&stream->streamBuffer);
    HASHCODE hashcode = (HASHCODE)BIN_GetDWord(&stream->streamBuffer);
    sprite *spr = (sprite *)stream->linkedGameOrSoundObject;
    if (spr == NULL) {
        spr = Sprite_CreateAndLink2Viewer(4);
        if (spr == NULL)
            return;
    }

    hashtable_set_sprite(spr, hashcode);
    spr->spritesheetWidth--;
    spr->spritesheetHeight--;
    spr->positionX = x;
    spr->positionY = y;
    spr->onscreenWidth = width;
    spr->onscreenHeight = height;
    spr->maybeEnabled = 2;
    spr->colourTint = 0xffffffff;
    if (flags & 2)
        spr->maybeFlags |= 0x40;
    if (flags & 4)
        spr->maybeFlags |= 0x80;
    spr->maybeFlags |= 0x100;
    stream->whatKindOfInterpolation = SStream_Sprite;
    stream->linkedGameOrSoundObject = spr;
}

// The original takes the stream in EDI. Script_Run also pushes the script, which it does not read.
// AUTOLTCG
void __declspec(naked) Script_SpriteStart(void) {
    _asm {
        push edi
        call _Script_SpriteStart
        add esp, 4
        ret
    }
}

// Command 4, a stream's end at the frame that follows. Until then the command is read again; at it, a looping
// script goes back to its loop start and any other stops.
void _Script_StreamEnd(SCRIPTINFO *scriptInfo, SSTREAM *stream) {
    stream->waitFrame = BIN_GetWord(&stream->streamBuffer);
    if (stream->elapsedFrames <= stream->waitFrame && stream->elapsedFrames < scriptInfo->maybeNumFrames) {
        stream->streamBuffer -= 3;
        return;
    }

    if (scriptInfo->playbackFlags & ScriptPlayback_Loop) {
        stream->streamBuffer = stream->streamBufferStart;
        stream->waitFrame = 0.0f;
        scriptInfo->maybeProgressFrames = scriptInfo->loopStart;
        stream->elapsedFrames = scriptInfo->loopStart - FRAME_RATE_MUL;
    }
    if (!(scriptInfo->playbackFlags & ScriptPlayback_Loop))
        scriptInfo->playbackFlags &= ~ScriptPlayback_Playing;
    scriptInfo->playbackFlags |= ScriptPlayback_AtEnd;
}

// The original takes the script in EDI and the stream in ESI.
// AUTOLTCG
void __declspec(naked) Script_StreamEnd(void) {
    _asm {
        push esi
        push edi
        call _Script_StreamEnd
        add esp, 8
        ret
    }
}

// Command 22: a sound - its id, whether it is 2D, and the stream whose object a 3D sound follows (if not given, the
// stream before this one). A 3D sound with no object to follow plays in 2D. Nothing plays while a fast-forward's
// fade has run its course.
void _Script_SoundStart(SCRIPTINFO *scriptInfo, SSTREAM *stream) {
    Action_SFX sfx = (Action_SFX)BIN_GetDWord(&stream->streamBuffer);
    uchar is2D = BIN_GetByte(&stream->streamBuffer);
    uchar hasObjectStream = BIN_GetByte(&stream->streamBuffer);
    uchar objectStream = BIN_GetByte(&stream->streamBuffer);
    stream->linkedGameOrSoundObject = NULL;
    if (hasObjectStream)
        stream->startFrame = objectStream;
    else
        stream->startFrame = scriptInfo->streamNum != 0 ? scriptInfo->streamNum - 1 : 0;
    stream->flags = (int)stream->elapsedFrames + 1;
    obj_tag *obj = Script_GetObj(scriptInfo, stream->startFrame);
    if (scriptInfo->fadeFlags & ScriptFade_ThresholdHit)
        return;

    if (obj == NULL || is2D == 1) {
        stream->linkedGameOrSoundObject = Sound_PlayExt(sfx, 100.0f, 0, 0);
    } else if (is2D == 0) {
        stream->linkedGameOrSoundObject = Sound_Play3D(sfx, Mat_Position(obj->transformMatrix), 100.0f, -1.0f, -1.0f,
                                                       0, 0, 0);
    }
    stream->whatKindOfInterpolation = SStream_Sound;
}

// The original takes the script on the stack and the stream in EDI.
// AUTOLTCG
void __declspec(naked) Script_SoundStart(void) {
    _asm {
        push edi
        push dword ptr [esp + 8]
        call _Script_SoundStart
        add esp, 8
        ret
    }
}

// Command 27: a sub-script, playing in a loop, follows the stream's keys - its hashcode and parent (if any).
void _Script_SubScriptStart(SCRIPTINFO *scriptInfo, SSTREAM *stream) {
    HASHCODE hashcode = (HASHCODE)BIN_GetDWord(&stream->streamBuffer);
    uchar hasParent = BIN_GetByte(&stream->streamBuffer);
    uchar parent = BIN_GetByte(&stream->streamBuffer);
    SCRIPTINFO *subScript = (SCRIPTINFO *)stream->linkedGameOrSoundObject;
    if (subScript == NULL)
        stream->startFrame = (int)stream->elapsedFrames + 1;
    stream->parent = hasParent ? parent : -1;
    if (subScript == NULL)
        stream->linkedGameOrSoundObject = Script_Load(hashcode, &CONST_ZERO_VECTOR, &CONST_ZERO_VECTOR, NULL, NULL,
                                                      NULL, NULL);
    stream->whatKindOfInterpolation = SStream_SubScript;
    stream->flags = 0;
    Script_Play((SCRIPTINFO *)stream->linkedGameOrSoundObject, 1);
    Script_Interp(scriptInfo, stream);
}

// The original takes the script on the stack and the stream in ESI.
// AUTOLTCG
void __declspec(naked) Script_SubScriptStart(void) {
    _asm {
        push esi
        push dword ptr [esp + 8]
        call _Script_SubScriptStart
        add esp, 8
        ret
    }
}

// Command 18: an event - its number of arguments, its id and the arguments. Several act on the object of the
// stream before this one (an entity or animation).
void _Script_EventHandler(SCRIPTINFO *scriptInfo, SSTREAM *stream) {
    SSTREAM *prevStream = NULL;
    obj_tag *prevObj = NULL;
    uint args[8];

    if (scriptInfo->streamNum != 0)
        prevStream = &scriptInfo->streamDataBuffer[scriptInfo->streamNum - 1];
    uchar numArgs = BIN_GetByte(&stream->streamBuffer);
    uchar event = BIN_GetByte(&stream->streamBuffer);
    memset(args, 0, sizeof(args));
    for (uint i = 0; i < numArgs; i++)
        args[i] = BIN_GetDWord(&stream->streamBuffer);   // no limit, as in the original
    if (prevStream != NULL && (prevStream->whatKindOfInterpolation == SStream_Anim ||
                               prevStream->whatKindOfInterpolation == SStream_Entity))
        prevObj = (obj_tag *)prevStream->linkedGameOrSoundObject;

    switch (event) {
    case ScriptEvent_Wait:
        // args[0] - 1 frames (0: until SP_UnPause); not during a fast-forward. The objects stop where they are.
        if (!(scriptInfo->fadeFlags & ScriptFade_FFwdRequested))
            scriptInfo->waitFrames = args[0] - 1;
        for (ushort i = 0; i < scriptInfo->numElements; i++) {
            obj_tag *obj = Script_GetObj(scriptInfo, i);
            if (obj != NULL)
                Vec_Copy2(Mat_Position(obj->transformMatrix), &obj->position, &obj->lastPosition);
        }
        break;

    case ScriptEvent_DisablePlayer:
        Player_Disable(glb_players[0], 0);
        PlarStat_LogTimerPause(0);
        break;

    case ScriptEvent_DisableDrones:
        Drone_EnableAll(0, scriptInfo->scriptHashcode);
        break;

    case ScriptEvent_EnablePlayerOnFree:
        // Where Script_Free puts the player back: the previous object, or where he is
        scriptInfo->scriptFlags |= ScriptFlag_EnablePlayerOnFree;
        if (prevObj != NULL)
            Mat_Copy(&prevObj->transformMatrix, &scriptInfo->otherMatrix);
        else
            Mat_Copy(&glb_players[0]->transformMatrix, &scriptInfo->otherMatrix);
        scriptInfo->playerEnableFlags = args[0];
        break;

    case ScriptEvent_EnableDronesOnFree:
        scriptInfo->scriptFlags |= ScriptFlag_ReEnableDronesOnFree;
        break;

    case ScriptEvent_CreateDrone:
        if (prevObj != NULL)
            Drone_CoderCreate(Mat_Position(prevObj->transformMatrix), &prevObj->rotation, (HASHCODE)args[0], (HASHCODE)args[1], args[2],
                              args[3], 4);
        break;

    case ScriptEvent_PlayerCamMode:
        Player_Cam2Mode(0, args[0]);
        break;

    case ScriptEvent_Break:
        if (prevObj != NULL)
            Break_DoBreak(Mat_Position(prevObj->transformMatrix));
        break;

    case ScriptEvent_SetWeapon:
        if (prevObj != NULL && prevObj->animState != NULL)
            prevObj->animState->currentWeaponId = args[0];
        break;

    case ScriptEvent_RestoreOnKill:
        scriptInfo->scriptFlags |= ScriptFlag_RestoreOnKillStream;
        break;

    case ScriptEvent_SetSwitch:
        if (args[0] == 0)
            return;
        switch_channels[args[0]] = args[1];
        if (args[1] != 0)
            switch_channels_time[args[0]] = GameState.NumFramesUnpaused;
        break;

    case ScriptEvent_FlagStream:
        stream->flags |= SStreamFlag_Event14;
        break;

    case ScriptEvent_Callback:
        if (scriptInfo->callback != NULL)
            scriptInfo->callback(prevObj, args[0], scriptInfo->callbackContext);
        break;

    case ScriptEvent_NoCamera:
        scriptInfo->scriptFlags |= ScriptFlag_FFwdKillsCamera;
        break;

    case ScriptEvent_SetSwitchIf: {
        // Channel args[1] if channel args[0] is on, else channel args[2], set to args[3]
        uint channel = switch_channels[args[0]] ? args[1] : args[2];
        if (channel == 0)
            return;
        switch_channels[channel] = args[3];
        if (args[3] != 0)
            switch_channels_time[channel] = GameState.NumFramesUnpaused;
        break;
    }

    case ScriptEvent_LoadLevel:
        if (glb_blokes[0] != NULL)
            Player_RamSave(glb_blokes[0]);
        ResetMap_LevelToLoad((HASHCODE)args[0], 1, 0);
        break;

    case ScriptEvent_TakeCamera:
        ScriptCam = scriptInfo->scriptHashcode;
        break;
    }
}

// The original takes the script in ESI and the stream on the stack.
// AUTOLTCG
void __declspec(naked) Script_EventHandler(void) {
    _asm {
        push dword ptr [esp + 4]
        push esi
        call _Script_EventHandler
        add esp, 8
        ret
    }
}

// Command 10: an animated object on its own - the animation script, the skin and the frame to start at. The
// script is held at the stream's frame (Script_Interp).
void _Script_AnimStart(SCRIPTINFO *scriptInfo, SSTREAM *stream) {
    _MATRIX mtx;
    float scale;
    uint colour;

    HASHCODE scriptHashcode = (HASHCODE)BIN_GetDWord(&stream->streamBuffer);
    HASHCODE skin = (HASHCODE)BIN_GetDWord(&stream->streamBuffer);
    uint startFrame = BIN_GetDWord(&stream->streamBuffer);
    if (stream->linkedGameOrSoundObject != NULL)
        Script_KillStream(scriptInfo, stream);

    obj_tag *obj = control_create_object(0, &CONST_ZERO_VECTOR, &CONST_ZERO_VECTOR, NULL);
    if (obj == NULL)
        return;

    AnimObjectNew(obj, skin);
    obj->animState->animObj.flags |= 0x8000;
    sAnimScript_tag *animScript = AnimScriptAdd(obj, scriptHashcode);
    animScript->flags |= ANIMSCRIPT_SCRIPT_DRIVEN;
    float frame = (float)startFrame;
    animScript->curFrame = frame;
    if (frame < 1.0f) {
        animScript->curFrame = 1.0f;
    } else if (frame > animScript->numFrames) {
        animScript->curFrame = animScript->numFrames;
    }

    stream->linkedGameOrSoundObject = obj;
    stream->whatKindOfInterpolation = SStream_Anim;
    stream->animStartFrame = animScript->curFrame;
    stream->animStartTime = stream->elapsedFrames;
    AnimObjectUpdate(obj);
    AnimGetFirstTrans(animScript, &obj->position);
    obj->transformFlags |= TRANSFORM_KEEP_ROTATION | TRANSFORM_MOVED | TRANSFORM_POSITION_FROM_MATRIX;
    obj->extraObjectData = scriptInfo->scriptPlayer;
    obj->objectType = 0;
    _Script_GetInterp(scriptInfo, stream, &mtx, &scale, &colour, 1, scriptInfo->scale, ScriptInterp_Linear,
                      stream->firstKey, stream->lastKey);
    if (obj->inCel == NULL)
        build_LinkToRoom(obj, 0, glb_world);
}

// The original takes the script on the stack and the stream in EDI.
// AUTOLTCG
void __declspec(naked) Script_AnimStart(void) {
    _asm {
        push edi
        push dword ptr [esp + 8]
        call _Script_AnimStart
        add esp, 8
        ret
    }
}

// Whether an object or sound a stream made at its first frame (startedAt 1) stays past the end of a looping script
static bool KeptPastLoopEnd(SCRIPTINFO *scriptInfo, SSTREAM *stream, ushort startedAt) {
    return scriptInfo->maybeNumFrames <= stream->elapsedFrames && (scriptInfo->playbackFlags & ScriptPlayback_Loop) &&
           startedAt == 1;
}

// Runs the commands of the stream SCRIPTINFO::streamNum up to the first that waits for a later frame. Each command
// with a handler ends in a byte of its own, which is skipped. The end of a looping script starts the stream over.
void _Script_Run(SCRIPTINFO *scriptInfo) {
    SSTREAM *stream = &scriptInfo->streamDataBuffer[scriptInfo->streamNum];
    if (stream->streamBuffer == NULL)
        return;

    bool restart;
    do {
        if (scriptInfo->maybeNumFrames >= stream->elapsedFrames)
            Script_Interp(scriptInfo, stream);

        restart = false;
        while (!restart && stream->streamBuffer != NULL && !(stream->elapsedFrames < stream->waitFrame)) {
            Cmd = *stream->streamBuffer++;
            switch (Cmd) {
            case ScriptCmd_EndScript:
                _Script_StreamEnd(scriptInfo, stream);
                if (!(scriptInfo->playbackFlags & ScriptPlayback_Loop))
                    return;
                restart = true;
                continue;

            case ScriptCmd_Wait:
                stream->waitFrame = BIN_GetWord(&stream->streamBuffer);
                break;

            case ScriptCmd_MarkEnd:
                BIN_GetWord(&stream->streamBuffer);
                if (scriptInfo->maybeNumFrames <= stream->elapsedFrames)
                    scriptInfo->playbackFlags |= ScriptPlayback_AtEnd;
                break;

            case ScriptCmd_EntityStart:
                Script_EntityStart(scriptInfo, stream);
                break;

            case ScriptCmd_EntityEnd:
            case ScriptCmd_SubScriptEnd:
                if (stream->linkedGameOrSoundObject != NULL && !KeptPastLoopEnd(scriptInfo, stream, stream->startFrame))
                    Script_KillStream(scriptInfo, stream);
                break;

            case ScriptCmd_AnimStart:
                _Script_AnimStart(scriptInfo, stream);
                break;

            case ScriptCmd_AnimEnd:
            case ScriptCmd_SpriteEnd:
            case ScriptCmd_LightEnd:
                Script_KillStream(scriptInfo, stream);
                break;

            case ScriptCmd_CameraStart:
                _Script_CameraStart(scriptInfo, stream);
                break;

            case ScriptCmd_CameraEnd:
                if (!(scriptInfo->maybeNumFrames <= stream->elapsedFrames &&
                      (scriptInfo->playbackFlags & ScriptPlayback_Loop))) {
                    Script_KillStream(scriptInfo, stream);
                    scriptInfo->fadeFlags &= ~ScriptFade_FFwdAllowed;
                }
                break;

            case ScriptCmd_EventHandler:
                _Script_EventHandler(scriptInfo, stream);
                break;

            case ScriptCmd_FadeStart: {
                float frames = BIN_GetFloat(&stream->streamBuffer);
                if (!(scriptInfo->fadeFlags & ScriptFade_FFwdRequested))
                    Camera_SetFade(4, -frames, 0xff, 0);
                break;
            }

            case ScriptCmd_SpriteStart:
                _Script_SpriteStart(stream);
                break;

            case ScriptCmd_SoundStart:
                _Script_SoundStart(scriptInfo, stream);
                break;

            case ScriptCmd_SoundEnd:
                // Sounds that do not loop end by themselves
                if (Sound_IsLooping((DYNAMICSOUNDS *)stream->linkedGameOrSoundObject) &&
                    !KeptPastLoopEnd(scriptInfo, stream, stream->flags))
                    Script_KillStream(scriptInfo, stream);
                break;

            case ScriptCmd_LightStart:
                _Script_LightStart(scriptInfo, stream);
                break;

            case ScriptCmd_SubScriptStart:
                _Script_SubScriptStart(scriptInfo, stream);
                break;

            case ScriptCmd_TextStart: {
                Action_TranslatedText label = (Action_TranslatedText)BIN_GetDWord(&stream->streamBuffer);
                ushort frames = BIN_GetWord(&stream->streamBuffer);
                Text_AddMsg(-1, 0, 4, (char *)Txt_BindLabel(label, 0), 0, frames);
                break;
            }

            default:
                continue;
            }
            stream->streamBuffer++;
        }
    } while (restart);
}

// The original takes the script in EBX. Script_Update is its only caller.
// AUTOLTCG
void __declspec(naked) Script_Run(void) {
    _asm {
        push ebx
        call _Script_Run
        add esp, 4
        ret
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Scripts
// ---------------------------------------------------------------------------------------------------------------

// AUTOINJECT
void Script_SetPosRot(SCRIPTINFO *scriptInfo, _MATRIX *mtx) {
    Mat_Copy(mtx, &scriptInfo->maybeMatrix);

    if (scriptInfo->waitFrames != 0) {
        for (ushort i = 0; i < scriptInfo->numElements; i++) {
            Script_Interp(scriptInfo, scriptInfo->streamDataBuffer + i);
        }
    }
}

// Registers a script file the first time it is given (fileBuf), and returns NULL; or, given no file, makes an
// instance of the script registered under hashcode at the transform and rotation given. A file of another version
// is registered as -1, which no instance can be made of.
// AUTOINJECT
SCRIPTINFO* Script_Load(HASHCODE hashcode, _VECTOR *transform, _VECTOR *rotation, uint *fileBuf, void *scriptPlayer,
                        ScriptPlayerCallback *callback, void *callbackContext) {
    if ((hashcode & 0xff000000) != 0x6000000)
        return NULL;

    SCRIPTDEF *def = (SCRIPTDEF *)hashtable_getitem(hashcode);
    if (def == NULL) {
        if (fileBuf == NULL) {
            // GC check (0x80060338)
            NF_WARN("Warning : map contains script 0x%x that is not loaded / not setup correctly xyz[%.1f,%.1f,%.1f]",
                    hashcode, transform->x, transform->y, transform->z);
            return NULL;
        }

        uchar *cursor = (uchar *)fileBuf;
        if (BIN_GetWord(&cursor) != 0x1c) {
            hashtable_additem(hashcode, (void *)-1);
            return NULL;
        }

        def = (SCRIPTDEF *)Mem_Malloc(sizeof(SCRIPTDEF), (MallocFlags)0x1404, 0);
        hashtable_additem(hashcode, def);
        def->hasCamera = BIN_GetWord(&cursor);
        def->numStreams = BIN_GetWord(&cursor);
        def->loopStart = BIN_GetWord(&cursor);
        def->endFrame = BIN_GetWord(&cursor);
        def->hashcode = hashcode;
        if (def->numStreams >= 0x3f) {
            // Handed to a list it was never taken from, and left in the hashtable
            DList_RemoveFromInUse2Free(&ScriptList, (LLNODE_tag *)def);
            return NULL;
        }

        for (uint i = 0; i < def->numStreams; i++) {
            def->streams[i].firstKey = BIN_GetWord(&cursor);
            def->streams[i].lastKey = BIN_GetWord(&cursor);
            uint size = BIN_GetDWord(&cursor);
            def->streams[i].commands = cursor;
            cursor += size - 4;
        }
        def->numKeys = BIN_GetDWord(&cursor);
        uint padding = BIN_GetDWord(&cursor);
        def->keys = (KEYED_POSROT *)(cursor + padding);
        return NULL;
    }

    if (def == (SCRIPTDEF *)-1 || fileBuf != NULL)
        return NULL;

    SCRIPTINFO *scriptInfo = (SCRIPTINFO *)Mem_Malloc(sizeof(SCRIPTINFO), (MallocFlags)0x1404, 0);
    if (scriptInfo == NULL)
        return NULL;

    scriptInfo->playbackFlags = 0;
    scriptInfo->scriptHashcode = def->hashcode;
    scriptInfo->numElements = def->numStreams;
    scriptInfo->numKeys = def->numKeys;
    scriptInfo->maybeNumFrames = def->endFrame;
    scriptInfo->keys = def->keys;
    scriptInfo->scriptPlayer = scriptPlayer;
    scriptInfo->waitFrames = 0;
    scriptInfo->fadeElapsedFrames = 0.0f;
    scriptInfo->fadeFrames = 0;
    scriptInfo->fadeFlags = 0;
    scriptInfo->maybeProgressFrames = 0.0f;
    scriptInfo->loopStart = def->loopStart;
    scriptInfo->callback = callback;
    scriptInfo->scale = 1.0f;
    scriptInfo->callbackContext = callbackContext;
    scriptInfo->scriptFlags = def->hasCamera ? ScriptFlag_IsCameraNIS : 0;
    View_RotTransMatrix(rotation, transform, &scriptInfo->maybeMatrix);
    for (uint i = 0; i < def->numStreams; i++) {
        SSTREAM *stream = &scriptInfo->streamDataBuffer[i];
        memset(stream, 0, sizeof(SSTREAM));
        stream->elapsedFrames = 0.0f;
        stream->firstKey = def->streams[i].firstKey;
        stream->lastKey = def->streams[i].lastKey;
        stream->streamBufferStart = def->streams[i].commands;
        stream->streamBuffer = def->streams[i].commands;
        stream->parent = -1;
        stream->linkedGameOrSoundObject = NULL;
    }
    return scriptInfo;
}

// Removes a script file from the hashtable and frees it.
// AUTOINJECT
void Script_FreeFromHashTable(HASHCODE hashcode) {
    void *def = hashtable_getitem(hashcode);
    if (def == NULL)
        return;

    hashtable_deleteitem(hashcode);
    Mem_Free(&def);
}

// Rewinds every stream to its first command.
// AUTOINJECT
void Script_Set2Start(SCRIPTINFO *scriptInfo) {
    if (scriptInfo == NULL)
        return;

    for (ushort i = 0; i < scriptInfo->numElements; i++) {
        scriptInfo->streamDataBuffer[i].streamBuffer = scriptInfo->streamDataBuffer[i].streamBufferStart;
        scriptInfo->streamDataBuffer[i].elapsedFrames = 0.0f;
    }
    scriptInfo->playbackFlags &= ~ScriptPlayback_AtEnd;
}

// The name is shared with the menus' Script_Stop (ui/MenuScript.cpp).
// FUNC_AT(0x000c26f0)
void Script_Stop(SCRIPTINFO *scriptInfo) {
    if (scriptInfo != NULL)
        scriptInfo->playbackFlags &= ~ScriptPlayback_Playing;
}

// AUTOINJECT
void Script_Update(SCRIPTINFO *scriptInfo) {
    if (scriptInfo == NULL)
        return;

    if (!(scriptInfo->playbackFlags & ScriptPlayback_Playing))
        return;

    if (scriptInfo->waitFrames != 0) {
        if (scriptInfo->waitFrames > 0) {
            scriptInfo->waitFrames--;
        }
        return;
    }

    // This can genuinely loop more than once per call: if Script_Run's own bookkeeping flips the script to
    // "no longer playing" partway through (eg. it just reached its last command), we fall through to finalize
    // below in the same tick rather than waiting for the next Script_Update call.
    for (;;) {
        if (Script_IsPlaying(scriptInfo)) {
            scriptInfo->streamNum = 0;
            while (scriptInfo->streamNum < scriptInfo->numElements) {
                _Script_Run(scriptInfo);
                scriptInfo->streamNum++;
            }

            scriptInfo->maybeProgressFrames += FRAME_RATE_MUL;

            scriptInfo->streamNum = 0;
            while (scriptInfo->streamNum < scriptInfo->numElements) {
                scriptInfo->streamDataBuffer[scriptInfo->streamNum].elapsedFrames += FRAME_RATE_MUL;
                scriptInfo->streamNum++;
            }

            // Pre-emptively start fading out a scripted camera NIS once it's within 20 frames of its end
            if (ScriptCam != 0 && (scriptInfo->scriptFlags & ScriptFlag_IsCameraNIS) &&
                (float)(int)(scriptInfo->maybeNumFrames - 20) <= scriptInfo->maybeProgressFrames &&
                glb_viewer[4]->fadeState != VIEWFADE_FADING) {

                if (Script_IsDeathNIS(scriptInfo->scriptHashcode)) {
                    goto finalize;
                }

                // A handful of specific scripts (hashcode-identified) either skip this automatic fade entirely,
                // or opt out of it - exact reason for each isn't confirmed
                if (scriptInfo->scriptHashcode != 0x60008c8 && scriptInfo->scriptHashcode != 0x6000080) {
                    if (scriptInfo->scriptHashcode != 0x60004fa && scriptInfo->scriptHashcode != 0x6000994) {
                        Camera_SetFade(4, 40.0f, 0xff, 0);
                    }
                }
            }
        }

        // Advance the fade-out timer (started by Script_FFwd) once it's running
        if (scriptInfo->fadeElapsedFrames != 0.0f) {
            if ((float)scriptInfo->fadeFrames < scriptInfo->fadeElapsedFrames) {
                scriptInfo->fadeFlags |= ScriptFade_ThresholdHit;
            } else {
                scriptInfo->fadeElapsedFrames += FRAME_RATE_MUL;
            }
        }

        if (scriptInfo->fadeElapsedFrames <= (float)scriptInfo->fadeFrames || !Script_IsPlaying(scriptInfo)) {
            break;
        }
    }

finalize:
    if (scriptInfo->fadeElapsedFrames <= (float)scriptInfo->fadeFrames)
        return;

    // Still playing (only reachable via the DeathNIS goto above) - not done fading yet
    if (Script_IsPlaying(scriptInfo))
        return;

    Camera_SetFade(0, -22.0f, 0xff, 1);
    scriptInfo->playbackFlags &= ~ScriptPlayback_FFwdFading;
    scriptInfo->fadeFlags &= ~ScriptFade_FFwdRequested;
    scriptInfo->fadeElapsedFrames = 0.0f;
}

// Kicks off (or cancels) the "skip cutscene" fade-out. doFFwd==false always cancels any in-progress fade;
// doFFwd==true only starts one if the script permits it (ScriptFade_FFwdAllowed) and one isn't already running.
// AUTOINJECT
void Script_FFwd(SCRIPTINFO *scriptInfo, char doFFwd) {
    if (scriptInfo == NULL)
        return;

    if (doFFwd == 0 || !(scriptInfo->fadeFlags & ScriptFade_FFwdAllowed) || (scriptInfo->fadeFlags & ScriptFade_FFwdRequested)) {
        scriptInfo->playbackFlags &= ~ScriptPlayback_FFwdFading;
        return;
    }

    if (ScriptCam != 0) {
        Input_ClearAllActions(-1);
        Text_FlushAllSubtitles();
    }

    if (scriptInfo->scriptFlags & ScriptFlag_FFwdKillsCamera) {
        // Skip straight to the end by killing the active camera stream outright, rather than fading out
        for (ushort i = 0; i < scriptInfo->numElements; i++) {
            if (scriptInfo->streamDataBuffer[i].whatKindOfInterpolation == SStream_Camera) {
                Script_KillStream(scriptInfo, scriptInfo->streamDataBuffer + i);
                return;
            }
        }
        return;
    }

    // Fade out over whatever's left of the script, capped at 45 frames
    float framesRemaining = (float)scriptInfo->maybeNumFrames - scriptInfo->maybeProgressFrames;
    if (framesRemaining > 45.0f)
        framesRemaining = 45.0f;
    scriptInfo->fadeFrames = (uchar)(int)framesRemaining;
    scriptInfo->fadeElapsedFrames = FRAME_RATE_MUL;
    scriptInfo->playbackFlags |= ScriptPlayback_FFwdFading;
    Music_Event(5, scriptInfo->scriptHashcode);
    scriptInfo->fadeFlags |= ScriptFade_FFwdRequested;
    Camera_SetFade(4, (float)scriptInfo->fadeFrames, 0xff, 1);

    // Also nudge every currently-linked Anim stream's object: sets bit 0x40000 of its AnimObj's flags for anything
    // with an AnimState. The original checks this via "animState + 0x58 == 0" (ie. animState == -0x58) rather than a
    // plain null check - that's just the compiler reusing the flags from computing &animState->animObj
    // (animState+0x58) instead of emitting a separate TEST; it's a null check.
    for (ushort i = 0; i < scriptInfo->numElements; i++) {
        SSTREAM *stream = scriptInfo->streamDataBuffer + i;
        if (stream->whatKindOfInterpolation != SStream_Anim || stream->linkedGameOrSoundObject == NULL)
            continue;

        obj_tag *linkedObj = (obj_tag*)stream->linkedGameOrSoundObject;
        if (linkedObj->animState != NULL) {
            linkedObj->animState->animObj.flags |= 0x40000;
        }
    }
}

// AUTOINJECT
bool Script_IsDeathNIS(HASHCODE hashcode) {
    return hashcode == 0x6000952 || hashcode == 0x600098a || hashcode == 0x6000991;
}

// AUTOINJECT
bool Script_IsPlaying(SCRIPTINFO *scriptInfo) {
    if (scriptInfo == NULL)
        return false;

    // AtEnd set without Loop means "not playing"
    if (!(scriptInfo->playbackFlags & ScriptPlayback_Loop) && (scriptInfo->playbackFlags & ScriptPlayback_AtEnd))
        return false;

    return scriptInfo->playbackFlags & ScriptPlayback_Playing;
}

// The original always returns false - every path through this function (including the null check below) ends in
// "return false", and every caller we've checked (SP_LoadScript, Explode_Create, C_NIS_Handler, ...) discards the
// result anyway. Kept as bool/false rather than void to match the original faithfully, in case some not-yet-found
// caller does care - but as far as we can tell, this return value is dead.
// AUTOINJECT
bool Script_Play(SCRIPTINFO *scriptInfo, char loop) {
    if (scriptInfo == NULL)
        return false;

    if (loop) {
        scriptInfo->playbackFlags |= ScriptPlayback_Loop;
    } else {
        scriptInfo->playbackFlags &= ~ScriptPlayback_Loop;
    }
    
    scriptInfo->playbackFlags |= ScriptPlayback_Playing;   

    if (scriptInfo->scriptFlags & ScriptFlag_IsCameraNIS) {
        ScriptCam = scriptInfo->scriptHashcode;

        if (Script_IsDeathNIS(scriptInfo->scriptHashcode)) {
            Music_Event(6, 1);
        }

        Text_FlushAllSubtitles();
    }

    return false;
}

// AUTOINJECT
obj_tag* Script_GetObj(SCRIPTINFO *scriptInfo, ushort streamIdx) {
    if (scriptInfo == NULL || streamIdx >= scriptInfo->numElements)
        return NULL;

    uchar kind = scriptInfo->streamDataBuffer[streamIdx].whatKindOfInterpolation;
    if (kind == 0 || kind >= SStream_Sprite)
        return NULL;

    return (obj_tag*)scriptInfo->streamDataBuffer[streamIdx].linkedGameOrSoundObject;
}

// AUTOINJECT
void Script_HideObj(SCRIPTINFO *scriptInfo, char hide) {
    if (scriptInfo == NULL)
        return;

    for (ushort i = 0; i < scriptInfo->numElements; i++) {
        SSTREAM* stream = &scriptInfo->streamDataBuffer[i];
        uchar kind = stream->whatKindOfInterpolation;

        // Only entity/anim streams have a hideable linked object
        if (kind != 0 && kind < SStream_Sprite && stream->linkedGameOrSoundObject != NULL) {
            obj_tag* linkedObj = (obj_tag*)stream->linkedGameOrSoundObject;
            if (hide == 0) {
                linkedObj->effectFlags &= ~FLAG_HIDDEN;
            } else {
                linkedObj->effectFlags |= FLAG_HIDDEN;
            }
        }

        // Hiding also kills any active sound stream outright
        if (kind == SStream_Sound) {
            Script_KillStream(scriptInfo, stream);
        }
    }
}

// AUTOINJECT
void Script_RemoveObj(obj_tag *obj, SCRIPTINFO *scriptInfo) {
    if (obj == NULL || scriptInfo == NULL)
        return;

    for (ushort i = 0; i < scriptInfo->numElements; i++) {
        SSTREAM* stream = &scriptInfo->streamDataBuffer[i];
        if (stream->whatKindOfInterpolation == SStream_Entity && stream->linkedGameOrSoundObject == obj) {
            Script_KillStream(scriptInfo, stream);
            return;
        }
    }
}

// AUTOINJECT
void Script_SetColour(SCRIPTINFO *scriptInfo, undefined1 clr_r, undefined1 clr_g, undefined1 clr_b) {
    if (scriptInfo == NULL)
        return;

    for (ushort i = 0; i < scriptInfo->numElements; i++) {
        uchar kind = scriptInfo->streamDataBuffer[i].whatKindOfInterpolation;
        if (kind == 0 || kind >= SStream_Sprite)
            continue;

        obj_tag* linkedObj = (obj_tag*)scriptInfo->streamDataBuffer[i].linkedGameOrSoundObject;
        if (linkedObj == NULL)
            continue;

        linkedObj->tweakR = clr_r;
        linkedObj->tweakG = clr_g;
        linkedObj->tweakB = clr_b;
    }
}

// AUTOINJECT
void Script_Free(SCRIPTINFO *scriptInfo) {
    if (scriptInfo == NULL)
        return;

    // Script_KillStream mutates streamNum/streamDataBuffer as it goes, so this mirrors the original's use of
    // streamNum (rather than a fresh loop counter) as the iteration index
    scriptInfo->streamNum = 0;
    while (scriptInfo->streamNum < scriptInfo->numElements) {
        Script_KillStream(scriptInfo, scriptInfo->streamDataBuffer + scriptInfo->streamNum);
        scriptInfo->streamNum++;
    }

    HASHCODE hashcode = scriptInfo->scriptHashcode;
    if (isLoadable(hashcode)) {
        SFXSuspendFileAccess();
        LoadableReload(hashcode);
        SFXUnSuspendFileAccess();
    }

    if (scriptInfo->scriptFlags & ScriptFlag_ReEnableDronesOnFree) {
        Drone_EnableAll(1, hashcode);
    }

    if (scriptInfo->scriptFlags & ScriptFlag_EnablePlayerOnFree) {
        Player_Enable(glb_players[0], &scriptInfo->otherMatrix, (int)(byte)scriptInfo->playerEnableFlags);
        PlarStat_LogTimerUnpause(0);
    }

    if (ScriptCam == hashcode) {
        ScriptCam = (HASHCODE)0;
    }

    Mem_Free((void**)&scriptInfo);
}

// No letterbox borders and no script camera yet.
// AUTOINJECT
void Script_Init(void) {
    Borders[0] = NULL;
    Borders[1] = NULL;
    ScriptCam = (HASHCODE)0;
}
