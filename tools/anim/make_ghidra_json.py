"""Writes driving-symbol-matching/results/structs/action-anim.json for ghidra/NightfireStructs.py (run on default.xbe).

Layouts are the Xbox ones from the anim research specs (docs/anim/spec-*.md), all read from the Xbox binary.
Field tuples: (offset, type, name, comment).
"""
import json
import os

T = []

def struct(name, size, comment, fields):
    T.append({"name": name, "size": size, "comment": comment,
              "fields": [{"offset": o, "type": t, "name": n, "comment": c} for (o, t, n, c) in fields]})

struct("SkeletonInfo", 0x1c,
       "INVENTED NAME. A skeleton: a SKELnnnn entry of a level bundle, registered in SkeletonTable (0x1d7578) by "
       "AnimSkeletonProcess. Variable length: 16 + 12*numBones, padded to 32. No hierarchy - that is in each skin.",
       [(0x00, "short", "skeletonIdx", "index into SkeletonTable; equals nnnn of SKELnnnn"),
        (0x02, "uchar", "numBones", ""),
        (0x03, "uchar", "numTransBones", "popcount(transMask) in every skeleton; no reader found"),
        (0x04, "uint[3]", "transMask", "bit b: bone b has animated translation tracks (AnimFrameCopy 0x14013)"),
        (0x10, "_VECTOR[1]", "boneOffset", "[numBones] default local translation, unscaled (AnimFrameCopy 0x13f7c)")])

struct("AnimDatum", 0x24,
       "INVENTED NAME. An attachment point on a skin (weapons, gadgets, effects); stride 0x24 in AnimDatumGetIndex.",
       [(0x00, "int", "id", "matched by AnimDatumGetIndex / AnimGetBoneWorldTrans; weapon_data.whichBone holds such ids"),
        (0x04, "int", "bone", "-1 = none, else a bone index"),
        (0x08, "_VECTOR", "t", "bone-local position"),
        (0x14, "quaternion_tag", "q", "bone-local rotation, x y z w")])

struct("sAnimSkin_tag", 0x30,
       "A skin (hashcode type 0x05), built by AnimProcessSkinData 0x144c0 from the file (Mem_Malloc(0x30), not "
       "zeroed). The hashtable slot is re-pointed here. PS2 is 0x40 (16-byte vector).",
       [(0x00, "HASHCODE", "hashcode", "written by AnimObjectNew, not at load"),
        (0x04, "celglist_tag * *", "riggedBody", "[numRigged] skinned meshes (file hashcodes fixed up in place)"),
        (0x08, "celglist_tag * *", "discreteObj", "[numDiscrete] rigid parts"),
        (0x0c, "uchar *", "discreteBone", "[numDiscrete] bone each rigid part hangs on"),
        (0x10, "_VECTOR", "skinScale", "multiplies decoded translations (Glb_SkinScale 0x1d7794)"),
        (0x1c, "AnimDatum *", "datums", "[numDatums]; AnimPostLoadInit shares 0x05000007's table with skeletons 0/1"),
        (0x20, "uchar *", "bones", "[numBones] bit7 = used by this skin, bits0-6 = parent (0x7f root)"),
        (0x24, "_MATRIX *", "invBind", "[numBones] inverse bind: identity rotation, -bind position"),
        (0x28, "uchar", "numRigged", ""),
        (0x29, "uchar", "numDiscrete", ""),
        (0x2a, "uchar", "numMorphs", "22 on faces"),
        (0x2b, "uchar", "numDatums", ""),
        (0x2c, "uchar", "skeletonNum", "index into SkeletonTable")])

struct("AnimObj", 0x98,
       "Real tag: sAnimObject_tag. The animated part of an object, embedded at AnimState+0x58. Pose buffer is SoA: "
       "_VECTOR trans[numBones] then quaternion_tag quat[numBones] (xyzw), local, translations pre-scaled.",
       [(0x00, "void *", "headTrackTarget", "obj_tag* if flags&0x40 else _VECTOR*"),
        (0x04, "void *", "aimTarget", "obj_tag* if flags&0x200 else _VECTOR*"),
        (0x08, "float[4]", "boundSphere", "from the base sequence (AnimSeqSet)"),
        (0x18, "void *", "rootMotionCallback", "(obj, animObj, _VECTOR *delta), AnimFrameResolve"),
        (0x1c, "sAnimSkin_tag *", "skin", ""),
        (0x20, "sAnimSet_tag *", "animSets", "locomotion sets (AnimSetAppend), up to 4"),
        (0x24, "sAnimScript_tag *", "firstScript", "list via script+0x44"),
        (0x28, "void *", "postPoseCallback", "(obj, animObj, numBones, trans*, quats*), end of AnimFrameResolve"),
        (0x2c, "celglist_tag * *", "datumGlist", "[numDatums] entity drawn at each datum"),
        (0x30, "celglist_tag *", "sleeveGlist", "replaces rigged body 0 when set (AnimObjectSetSleeveType)"),
        (0x34, "float *", "pose", "numBones*28 bytes"),
        (0x38, "float *", "morphWeights", "[numMorphs], AnimFacialBlend"),
        (0x3c, "uchar *", "datumTimer", "[numDatums] 0 hidden, 0xff always, else frames left"),
        (0x40, "_MATRIX *", "boneMtxs", "[numBones] world matrices, psiBuildMatrixPalette"),
        (0x44, "_MATRIX *", "palette", "[numBones] skinning matrices, bind space -> world"),
        (0x48, "obj_tag * *", "discreteObj", "[numDiscrete] child objects"),
        (0x4c, "uint", "lastUpdateFrame", ""),
        (0x50, "uint", "flags", "1 pose valid, 2 full character, 4 bones valid, 8 palette valid, 0x10/0x20/0x40 head "
                                "track req/on/obj, 0x80/0x100/0x200 aim req/on/obj, 0x400 alt height, 0x1000-0x4000 "
                                "aim state, 0x8000 inverse skin scale, 0x40000 mute script sfx/events"),
        (0x54, "float", "rootHeight", ""),
        (0x58, "float", "heightOffset", ""),
        (0x5c, "float", "distanceThisTick", "AnimSetUpdate (speed * set.b)"),
        (0x60, "float", "rootTravel", "footstep accumulator input"),
        (0x64, "float", "distancePhase", "AnimSet distance phase"),
        (0x68, "float", "headTrackRate", ""),
        (0x6c, "float", "aimRate", ""),
        (0x70, "float", "headTrackRange2", "squared"),
        (0x74, "float", "aimRange2", "squared"),
        (0x78, "float", "headYaw", ""),
        (0x7c, "float", "aimYaw", ""),
        (0x80, "float", "headPitch", ""),
        (0x84, "float", "aimPitch", ""),
        (0x88, "ushort", "poseBufSize", "numBones*28"),
        (0x8a, "ushort", "facialTimer", "AnimFacialUpdate"),
        (0x8c, "uchar", "headTrackTargetBone", ""),
        (0x8d, "uchar", "aimTargetBone", ""),
        (0x8e, "uchar", "headTrackHoldTimer", ""),
        (0x8f, "uchar", "lookMorphX", "eye-look hint for morph channels 11-14"),
        (0x90, "uchar", "lookMorphY", ""),
        (0x91, "uchar", "facialState", "AnimFacialUpdate"),
        (0x94, "uchar[4]", "lod", "per viewport: distance / zoom / 4, clamped (View_AddObjects)")])

struct("AnimState", 0xf0,
       "obj_tag+0xB8. Mem_Malloc(0xf0) + zeroed in AnimObjectNew (PS2 0x120).",
       [(0x50, "ushort", "animFlags", "bit0 weapon zoomed in, bit2 footstep this frame"),
        (0x52, "char", "currentWeaponId", ""),
        (0x53, "char", "switchingToWeaponId", ""),
        (0x54, "char", "prevHeldWeaponId", ""),
        (0x58, "AnimObj", "animObj", "sAnimObject_tag")])

struct("sAnimScript_tag", 0xb4,
       "One playing animation script (a 0x06 file). Mem_Malloc(180) + zeroed in AnimScriptNew. PS2 0xC0, offsets "
       "+4 (64-byte matrix).",
       [(0x00, "_MATRIX", "rotMtx", "object rotation captured for anim-rotation mode (flag 0x20000)"),
        (0x3c, "uint", "magic", "0x159f5ab2 alive, 0x983c4837 deleted"),
        (0x40, "sAnimScript_tag *", "prev", ""),
        (0x44, "sAnimScript_tag *", "next", ""),
        (0x48, "sAnimScript_tag *", "syncMaster", "script this one follows (speed mode 2) / blends into"),
        (0x4c, "sAnimSeq_tag *", "boneAnimSeq", "pose buffer 28*numBones"),
        (0x50, "sAnimSeq_tag *", "morphAnimSeq", "0x58 bytes = 22 weights; only if the skin has morphs"),
        (0x54, "void *", "boneMask", "AnimFrameBlend 4th argument; non-zero = partial/overlay"),
        (0x58, "uchar *", "scriptData", "the 0x06 file"),
        (0x5c, "uchar *", "doneFlag", ""),
        (0x60, "sAnimScript_tag * *", "ownerSlot", "cleared on delete"),
        (0x64, "void *", "onDelete", "(obj, animObj, script)"),
        (0x68, "void *", "onStop", "(obj, animObj, script)"),
        (0x6c, "void *", "onEvent", "(obj, animObj, script, ushort param)"),
        (0x70, "HASHCODE", "hashcode", ""),
        (0x74, "uint", "flags", "see spec-script: 0x10000 step, 0x20000 anim rotation, 0x40000 facial-only, "
                                "0x100000/0x400000 blend out, 0x800000 ticked, 0x10000000 stopped, 0x20000000 just "
                                "started, 0x40000000 wrapped, 0x80000000 loop"),
        (0x78, "uint", "firedMask", "one bit per command"),
        (0x7c, "uint", "groupTag", "AnimSet tag; 0xe0000000 mood, 0xe0000001 blink, 0xe0000002 speech/cutscene"),
        (0x80, "uint", "timestamp", "creation order (AnimScriptTimeStamp)"),
        (0x84, "float", "syncBlend", ""),
        (0x88, "float", "targetPos", "speed mode 3"),
        (0x8c, "float", "curFrame", "1..numFrames, 60 Hz"),
        (0x90, "float", "prevFrame", ""),
        (0x94, "float", "speed", "script frames per 60 Hz frame; sign = direction"),
        (0x98, "float", "weight", "fadeTime/fadeLen"),
        (0x9c, "float", "rotBaseYaw", "8.0 = capture next tick"),
        (0xa0, "float", "rotLastYaw", ""),
        (0xa4, "float", "fadeTime", ""),
        (0xa8, "float", "fadeLen", "8.0 for AnimScriptAppendBlend"),
        (0xac, "short", "numFrames", ""),
        (0xae, "uchar", "numCmds", "low byte only"),
        (0xaf, "uchar", "rate", "60, unused"),
        (0xb0, "uchar", "speedMode", "0 normal, 1 distance, 2 sync to master, 3 seek"),
        (0xb1, "uchar", "playMode", "0 once, 1 loop, 2 ping-pong, 3 once then delete"),
        (0xb2, "char", "fadeDir", "+1 in, -1 out, 0 hold")])

struct("sAnimSeq_tag", 0x90,
       "A sequence being sampled for a script. Mem_Malloc(0x90) + zeroed in AnimSeqNew. PS2 0xB0.",
       [(0x00, "_VECTOR", "rootPrev", "raw bone-0 translation at the last tick"),
        (0x0c, "_VECTOR", "rootDelta", "root motion this tick"),
        (0x18, "_VECTOR", "firstTrans", "original bug: holds quat[0].xyz of the first decode"),
        (0x24, "_VECTOR", "transRelFirst", ""),
        (0x30, "_VECTOR", "objPosAtSet", ""),
        (0x3c, "float[6]", "rootBox", "from the file +0x20"),
        (0x54, "float[4]", "boundSphere", "from the file +0x10"),
        (0x64, "void *", "distanceTable", ""),
        (0x68, "float *", "pose", "vec3[nb] + quat[nb], or float[numElems] for a morph sequence"),
        (0x6c, "uint *", "unpakCache", "numElems*8 bytes"),
        (0x70, "void *", "frameCallback", "(obj, skin, seq), replaces AnimFrameSet"),
        (0x74, "sAnimRawSeq_tag *", "rawSeq", ""),
        (0x78, "HASHCODE", "hashcode", ""),
        (0x7c, "uint", "flags", "bits 0-3 from file (0-2 distance axes, 3 morph), 0x100000, 0x200000 cache, "
                                "0x2000000-0x8000000 zero root delta x/y/z"),
        (0x80, "float", "frame", "1-based"),
        (0x84, "short", "startTime", "in script frames"),
        (0x86, "short", "firstFrame", ""),
        (0x88, "short", "lastFrame", ""),
        (0x8a, "short", "numFrames", ""),
        (0x8c, "uchar", "raw3e", "copy of file +0x3e, no reader"),
        (0x8d, "uchar", "skeletonNum", ""),
        (0x8e, "uchar", "numElems", "morph count / cache size")])

struct("sAnimRawSeq_tag", 0x40,
       "A sequence file (hashcode type 0x04) header; tracks follow at +0x40. AnimProcessSeqData replaces the first "
       "three words. Tracks: u32 head (size | key0<<16), key words (len<<6 | mask), float polynomial coefficients in "
       "absolute time t = frame-1.",
       [(0x00, "uint *", "trackData", "on disk: the hashcode"),
        (0x04, "uint", "trackDataSize", ""),
        (0x08, "uint *", "cursor", ""),
        (0x0c, "uint", "flags", "bits 0-2 distance axes, bit 3 morph (facial)"),
        (0x10, "float[4]", "boundSphere", ""),
        (0x20, "float[6]", "rootBox", "bone 0 translation box"),
        (0x38, "short", "numFrames", ""),
        (0x3a, "short", "numTracks", "low byte = numElems"),
        (0x3c, "uchar", "skeletonNum", ""),
        (0x3e, "uchar", "raw3e", "equals skeletonNum in every file")])

struct("sAnimSet_tag", 0x34,
       "INVENTED NAME. A locomotion controller: walk/run scripts kept in step and blended by speed, plus a strafe "
       "overlay. Mem_Malloc(0x34) in AnimSetAppend / AnimTagAllocAndGetUnique.",
       [(0x00, "sAnimSet_tag *", "prev", ""),
        (0x04, "sAnimSet_tag *", "next", ""),
        (0x08, "uint", "walkTimestamp", ""),
        (0x0c, "uint", "blendTimestamp", ""),
        (0x10, "sAnimScript_tag *", "blendScript", ""),
        (0x14, "sAnimScript_tag *", "strafeScript", ""),
        (0x18, "HASHCODE *", "speedList", "[idle, walk.., run.., 0]"),
        (0x1c, "HASHCODE *", "strafeList", ""),
        (0x20, "uint", "tag", "copied to scripts' groupTag"),
        (0x24, "float", "a", "distance phase reset value"),
        (0x28, "float", "b", "speed -> distance scale"),
        (0x2c, "ushort", "idleTimer", ""),
        (0x2e, "char", "speedIndex", ""),
        (0x2f, "uchar", "strafeDir", ""),
        (0x30, "uchar", "switchCooldown", ""),
        (0x31, "uchar", "speedCount", ""),
        (0x32, "uchar", "strafeCount", ""),
        (0x33, "uchar", "oneShotState", "")])

P = []

def proto(addr, ret, params):
    P.append({"address": hex(addr), "calling_convention": "__cdecl", "return": ret,
              "params": [{"name": n, "type": t} for (t, n) in params]})

O, A, S = "obj_tag *", "AnimObj *", "sAnimScript_tag *"
proto(0x11050, "void", [("char *", "data")])
proto(0x14a20, "void", [("HASHCODE", "hashcode"), ("char", "noSeqProcess")])
proto(0x14b80, "void", [("char", "keepSkeletons")])
proto(0x138e0, "void", [("HASHCODE", "hashcode")])
proto(0x13e50, "void", [("float *", "out"), ("sAnimSeq_tag *", "seq"), ("sAnimSkin_tag *", "skin"), ("float", "frame")])
proto(0x11440, "int", [(O, "obj"), ("HASHCODE", "skin")])
proto(0x17a60, "void", [(O, "obj")])
proto(0x18690, "bool", [(O, "obj")])
proto(0x16c20, "void", [(O, "obj"), ("viewer_tag *", "viewer")])
proto(0xdd290, "void", [(O, "obj"), (A, "animObj"), ("char", "buildPalette")])
for a in (0x13070, 0x131c0):
    proto(a, "void", [(O, "obj"), ("uint", "which"), ("int", "datumOffset"), ("_VECTOR *", "posOut"), ("_MATRIX *", "mtxOut")])
proto(0x11950, "bool", [(O, "obj"), ("uint", "lodThreshold")])
proto(0x11920, "obj_tag *", [(O, "obj"), ("uint", "n")])
proto(0x118f0, "void", [(O, "obj"), ("int", "sleeveNum")])
proto(0x119a0, "void", [(O, "obj"), ("void *", "target"), ("float", "range"), ("float", "rate"), ("bool", "randomDelay"),
                        ("bool", "targetIsObj"), ("uchar", "bone")])
proto(0x11a80, "bool", [(O, "obj")])
proto(0x12220, "void", [(O, "obj"), ("void *", "target"), ("float", "range"), ("float", "rate"), ("bool", "targetIsObj"),
                        ("uchar", "bone")])
proto(0x122b0, "bool", [(O, "obj")])
for a in (0x11aa0, 0x15060):
    proto(a, "void", [(O, "obj"), (A, "animObj"), ("quaternion_tag *", "worldQuats"), ("_MATRIX *", "boneMtxs"), ("int", "bone")])
proto(0xe0960, "void", [("celglist_tag *", "glist"), ("_MATRIX *", "unused"), (O, "obj"), ("_MATRIX *", "palette")])
proto(0x13590, "int", [(O, "obj"), ("int", "datumId")])
proto(0x135d0, "void", [(O, "obj"), ("int", "datumId"), ("HASHCODE", "entity"), ("uchar", "frames")])
proto(0x15b40, "void", [(O, "obj"), ("sAnimScript_tag * *", "pScript")])
proto(0x15c30, S, [(O, "obj"), ("HASHCODE", "hashcode")])
proto(0x17220, S, [(O, "obj"), ("HASHCODE", "hashcode")])
proto(0x12780, "bool", [(O, "obj")])
proto(0x127d0, "void", [(O, "obj"), ("int", "dir")])
proto(0x12860, "void", [(S, "script"), ("void *", "onEvent")])
proto(0x12870, "void", [(S, "script"), ("void *", "onStop")])
proto(0x12a90, "void", [(S, "script"), ("char", "on")])
proto(0x171e0, "void", [(O, "obj")])
proto(0x17820, "void", [(O, "obj")])
proto(0x17160, S, [(O, "obj"), ("HASHCODE", "hashcode")])
proto(0x122e0, S, [(O, "obj"), ("uint", "mask"), ("uint", "value"), ("void *", "callback"), ("uint", "mode")])

doc = {"program": "default.xbe", "types": T, "prototypes": P}
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "driving-symbol-matching", "results",
                   "structs", "action-anim.json")
with open(out, "w") as f:
    json.dump(doc, f, indent=1)
print("wrote %d types, %d prototypes to %s" % (len(T), len(P), out))
