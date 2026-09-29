# spec-pose: the animated object, pose -> bone matrices -> skin palette -> GPU skinning

Area: sAnimObject_tag / AnimState, AnimObjectNew/Delete/Update/Draw/InitialPose, psiBuildMatrixPalette,
AnimGetBoneWorldTrans(2), head tracking and aiming, the maths helpers, and how the Xbox vertex shaders consume the
palette. All offsets are **Xbox** (`/Xbox_EU/default.xbe`) unless marked PS2. Research was read-only.

Supporting files in this folder:
- `pose_ref.py` - a Python reference model of the palette build and the conventions below, with a self-test:
  **23/23 rigged skins in the dump give an identity palette in bind pose** (worst deviation 0.0105, the same
  ~0.01 rounding the load agent's inverse-bind check shows). A second check (inline, 1000 random quaternions)
  confirmed `QuatToMat(a (x) b) == QuatToMat(b) * QuatToMat(a)` and `MatToQuat(QuatToMat(q)) == +-q` to 1e-15.
- `pose/` - raw decompiles/disassembly (`x_0x....` Xbox, `p_0x....` PS2), `pose/vshdis.py` (NV2A vertex-shader
  microcode disassembler that reads the shaders straight out of the XBE via the Ghidra HTTP API), `pose/vs_*.txt`
  (all 128 disassembled vertex shaders), `pose/xdis.py` (capstone disassembly of raw memory, for code Ghidra has no
  function for).

---------------------------------------------------------------------------------------------------------------

## 0. Conventions (confirmed from the x87 code)

| Thing | Layout | Evidence |
|---|---|---|
| `quaternion_tag` | `float q[4]` = **x, y, z, w** (w LAST, at +0xc) | Quat_QuatToMat 0xd5800: m[0] = 1-2(q1²+q2²), m[1] = 2(q0q1 + q3q2)...; Quat_AxisAngleToQuat 0xd59b0 writes cos(a/2) to +0xc. **The comment `// wxyz order` in src/action/math/math.h is wrong**; the code in math.cpp already treats q[3] as w. |
| `_MATRIX` | 15 floats / 60 bytes. Rows: r0 = m[0..2], r1 = m[4..6], r2 = m[8..10]; **m[3], m[7], m[11] are padding, never written by QuatToMat and never read by the vector maths**; translation t = m[12..14] (+0x30). So "m[9..11]" = r2.y, r2.z, pad. | QuatToMat stores +0x00..0x08, +0x10..0x18, +0x20..0x28; QuatTransToMat 0xd59e0 adds +0x30..0x38; Mat_CopyRot 0xd5010 copies 12 dwords (0x30); Mat_Copy 0xd4ff0 copies 15 dwords. |
| Vector convention | **row vectors, D3D style**: `p' = p*R + t`, row i of R = where axis i goes | MatrixMultiplyVector 0xd7500: out.x = v.x*m[0] + v.y*m[4] + v.z*m[8] + m[12]. |
| Composition | a child's world rotation `R_w(child) = R_local(child) * R_w(parent)`; for quaternions `q_w(child) = q_w(parent) (x) q_local(child)` (Hamilton product) - the two agree (tested). | MultiplyQuaternionAltOrder 0xd7470 = Hamilton product `a (x) b`; Concat 0xd6d20 `(A, B): B = B*A`. |

Maths helpers used by this area (Xbox, all plain cdecl unless noted; PS2 name identical):

| Xbox | Name | Semantics |
|---|---|---|
| 0xd5800 | Quat_QuatToMat(q, m) | rotation rows only (does not touch m[3,7,11,12..14]). **Register-sensitive: only uses EAX + x87; callers (KeyFrame_Interp, Spline_Interp) keep the matrix pointer in ECX across the call** (already handled in math.cpp). |
| 0xd59e0 | Quat_QuatTransToMat(q, t, m) | QuatToMat + m[12..14] = t. Uses EAX/ECX. |
| 0xd5ad0 | Quat_MatToQuat(qOut, m) | standard Shoemake; trace>=0 branch: w = sqrt(tr+1)/2, x = (m[6]-m[9])*s, y = (m[8]-m[2])*s, z = (m[1]-m[4])*s. |
| 0xd7470 | MultiplyQuaternionAltOrder(a, b, out) | out = a (x) b (Hamilton, xyzw). |
| 0xd7500 | MatrixMultiplyVector(M, v, out) | out = v*R + t. |
| 0xd7560 | ApplyMatrixLV(M, v, out) | out = v*R (rotation only). |
| 0xd6b80 | RotTransMat(A, B) | **B.R = B.R * A.R; B.t = B.t + A.t** - the translation is ADDED, not rotated by A (PS2 0x1e31a8 identical: `add.s` only). |
| 0xd6d20 | Concat(A, B) | B = B * A (full affine: B.t = B.t*A.R + A.t; B.R = B.R*A.R). |
| 0xd5010 / 0xd4ff0 | Mat_CopyRot(src, dst) / Mat_Copy(src, dst) | 0x30 / 0x3c bytes. |
| 0xd59b0 | Quat_AxisAngleToQuat(in{ax,ay,az,angle}, out) | (axis*sin(a/2), cos(a/2)); axis assumed unit. |
| 0xd8a90 | Vec_UnitVector(out, a, b) | normalize(b - a). |
| 0xd7730 | Vec_CrossNormalise(a, b, out) | normalize(a x b). |
| 0xd6380 | Ang_RotateToBearing(cur, target, rate, maxStep) | d = clamp(AngleDifference(cur,target), +-maxStep); r = cur + d*rate (no overshoot of cur+d); return target if abs(r-target) < 1e-4 else r. |
| 0xd8590 | Vec_LineSphereIntersection (PS2 name; Ghidra FUN_000d8590 on Xbox) | used by AimAt. |

---------------------------------------------------------------------------------------------------------------

## 1. Struct layouts

### 1.1 AnimState (obj_tag+0xB8) - **0xF0 bytes on Xbox** (PS2 0x120)

Size: `Mem_Malloc(0xf0, 0x2604, 0)` then `REP STOSD` with ECX=0x3c in AnimObjectNew (0x11487..0x114b2).
Only the parts this area touches:

| Off | Size | Type | Name | Evidence | Conf |
|---|---|---|---|---|---|
| 0x00..0x4f | | | (not used by this area) | | |
| 0x50 | 2 | u16 | stateFlags; bit 2 (0x4) = footstep this frame | AnimObjectUpdate 0x187be `AND [EAX+0x50],0xfb`, 0x18865 `OR [EAX+0x50],4` | confirmed |
| 0x52 | 1 | s8 | currentWeaponId (<= 0x72, indexes weapon_data stride 0x10c) | AnimObjectDraw 0x16ead, 0x16f03 (==0x18 watch laser) | confirmed |
| 0x53/0x54 | 1 | | switchingToWeaponId / prevHeldWeaponId | brief | known |
| 0x58 | 0x98 | sAnimObject_tag | animObj (embedded, to 0xF0) | every function does `ADD reg,0x58` | confirmed |

### 1.2 sAnimObject_tag (AnimState+0x58) - 0x98 bytes

"AS+" = the same field as an AnimState offset (what code often uses).

| Off | AS+ | Size | Type | Name | Evidence (Xbox) | Conf |
|---|---|---|---|---|---|---|
| 0x00 | 0x58 | 4 | void* | headTrackTarget: `obj_tag*` if flags&0x40, else `_VECTOR*` (world point) | SetHeadTracking 0x11a1f; HeadTrack | confirmed |
| 0x04 | 0x5c | 4 | void* | aimTarget: `obj_tag*` if flags&0x200, else `_VECTOR*` | SetAiming 0x12270; AimAt | confirmed |
| 0x08..0x17 | | 16 | | not used by this area | | ? |
| 0x18 | 0x70 | 4 | fn* | rootMotionCallback(obj, animObj, _VECTOR* delta) | AnimFrameResolve (0x160a0) `(*(animObj+0x18))(obj,animObj,&d)` | confirmed |
| 0x1c | 0x74 | 4 | sAnimSkin_tag* | skin | New 0x114ba | confirmed |
| 0x20 | | 4 | | not used by this area | | ? |
| 0x24 | 0x7c | 4 | sAnimScript_tag* | firstScript (list via script+0x44 next) | Update 0x18771 | confirmed |
| 0x28 | 0x80 | 4 | fn* | postPoseCallback(obj, animObj, numBones, trans*, quats*) - called at end of AnimFrameResolve | 0x160a0 tail | confirmed |
| 0x2c | 0x84 | 4 | celglist_tag** | datumGlist[numDatums] - glist drawn at each datum (weapons etc.) | New 0x11590 (alloc skin.numDatums*4); Draw 0x17089 | confirmed |
| 0x30 | 0x88 | 4 | celglist_tag* | sleeveGlist - replaces rigged body #0 when non-NULL | SetSleeveType 0x11914; Draw 0x16e41..0x16e62 | confirmed |
| 0x34 | 0x8c | 4 | float* | **pose buffer** (numBones*28 bytes, SoA - see 1.4) | New 0x11500..0x1151a | confirmed |
| 0x38 | 0x90 | 4 | float* | morphWeights[numMorphs] | New 0x11559; Draw 0x16ce0 | confirmed |
| 0x3c | 0x94 | 4 | u8* | datumTimer[numDatums]: 0 = hidden, 0xff = permanent, else frames left (decremented per draw) | New 0x115af; Draw 0x17071..0x17087 | confirmed |
| 0x40 | 0x98 | 4 | _MATRIX* | **boneMtxs[numBones]** - world matrices (stride 0x3c) | New 0x115f4; BuildPalette | confirmed |
| 0x44 | 0x9c | 4 | _MATRIX* | **palette[numBones]** - skinning matrices (stride 0x3c) | New 0x1160f; BuildPalette; Draw 0x16e64 | confirmed |
| 0x48 | 0xa0 | 4 | obj_tag** | discreteObj[numDiscrete] | New 0x116b2; AnimGetNDiscreteObj 0x11941 | confirmed |
| 0x4c | 0xa4 | 4 | u32 | lastUpdateFrame (= GameState.numFrames 0x1f65bc) | Update 0x186e8; Draw 0x16c5c..0x16c70 | confirmed |
| 0x50 | 0xa8 | 4 | u32 | **flags** (table 1.3) | everywhere | confirmed |
| 0x54 | 0xac | 4 | float | rootHeight = rootTrans.y + heightOffset (x0.8627321 in multiplayer when !(flags&0x400)) | AnimFrameResolve tail | confirmed |
| 0x58 | 0xb0 | 4 | float | heightOffset = (flags&0x400 ? -1.160398 : -0.995208) - bboxMin.y/skinScale.y + 0.02 (only if flags&2) | New 0x1177c..0x117c2 | confirmed |
| 0x5c | | 4 | | not used by this area | | ? |
| 0x60 | 0xb8 | 4 | float | per-frame root travel (player footstep accumulator adds abs of it) | Update 0x187d2 | likely |
| 0x64 | | 4 | | not used by this area | | ? |
| 0x68 | 0xc0 | 4 | float | headTrackRate (Ang_RotateToBearing rate) | SetHeadTracking 0x119d6 | confirmed |
| 0x6c | 0xc4 | 4 | float | aimRate | SetAiming 0x12248 | confirmed |
| 0x70 | 0xc8 | 4 | float | headTrackRange² | SetHeadTracking 0x119d0 | confirmed |
| 0x74 | 0xcc | 4 | float | aimRange² | SetAiming 0x12254 | confirmed |
| 0x78 | 0xd0 | 4 | float | headYaw (current) | HeadTrack | confirmed |
| 0x7c | 0xd4 | 4 | float | aimYaw (current) | AimAt | confirmed |
| 0x80 | 0xd8 | 4 | float | headPitch (current) | HeadTrack | confirmed |
| 0x84 | 0xdc | 4 | float | aimPitch (current) | AimAt | confirmed |
| 0x88 | 0xe0 | 2 | u16 | poseBufSize = numBones*28 | New 0x11500 | confirmed |
| 0x8a | 0xe2 | 2 | u16 | facial timer (AnimFacialUpdate) | 0x1762d | see spec-facial |
| 0x8c | 0xe4 | 1 | u8 | headTrackTargetBone (target obj's boneMtxs index, when flags&0x40) | SetHeadTracking 0x11a2c | confirmed |
| 0x8d | 0xe5 | 1 | u8 | aimTargetBone | SetAiming 0x1227f | confirmed |
| 0x8e | 0xe6 | 1 | u8 | headTrackHoldTimer (random FRAME_RATE/10 + rand(FRAME_RATE/4) when started with "randomDelay") | SetHeadTracking 0x11a38 | confirmed |
| 0x8f | 0xe7 | 1 | u8 | lookMorphX (0..255, eye/face look hint, only if skin has morphs) | HeadTrack | confirmed |
| 0x90 | 0xe8 | 1 | u8 | lookMorphY | HeadTrack | confirmed |
| 0x91 | 0xe9 | 1 | u8 | facial state (AnimFacialUpdate) | 0x1754b | see spec-facial |
| 0x92..0x93 | | 2 | | unknown | | ? |
| 0x94..0x97 | 0xec..0xef | 4 | u8[4] | **lod[viewport]** = clamp(dist / viewer.zoom(+0xf4) * 0.25, 0, 255), written by View_AddObjects 0xda64c for viewport min(viewer+0x28, 3) | 0xda600..0xda64c | confirmed |

Size 0x98: AnimState is 0xF0 and animObj starts at 0x58; the last field used is +0x97.
PS2 offsets are larger (PS2 _MATRIX* fields etc. are the same size, but e.g. PS2 flags=+0x50 too while PS2
headTrackRate=+0x70, range²=+0x78; PS2 LOD bytes at AnimState+0x110..0x113) - never use PS2 offsets.

### 1.3 animObj.flags (+0x50)

| Bit | Meaning | Set / cleared by |
|---|---|---|
| 0x1 | pose valid (at least one anim resolved) - Draw and GetBoneWorldTrans do nothing without it | AnimFrameResolve sets |
| 0x2 | "full character" skeleton (numBones > 70): facial update, datum weapons, height offset, obj.flags|=8 | AnimObjectNew |
| 0x4 | boneMtxs valid this frame | BuildPalette sets; Update clears at start (`&0xfffffff3`) |
| 0x8 | palette valid this frame | BuildPalette(…,1) sets; Update clears |
| 0x10 | head tracking requested | SetHeadTracking; HeadTrack clears it while aiming (flags&0x180) |
| 0x20 | head tracking active (still returning to neutral) | HeadTrack sets; cleared (with 0x10) when angles < 0.01 after release |
| 0x40 | headTrackTarget is an obj (+bone 0x8c), not a point | SetHeadTracking |
| 0x80 | aiming requested | SetAiming |
| 0x100 | aiming active | AimAt sets; cleared (with 0x80) when angles < 0.01 after release |
| 0x200 | aimTarget is an obj (+bone 0x8d) | SetAiming |
| 0x400 | full character whose skeletonNum != 1 (alternative height constant / MP scale) | AnimObjectNew |
| 0x1000 | aim target in range this frame | AimAt |
| 0x2000 | aim yaw clamped | AimAt |
| 0x4000 | aim pitch clamped | AimAt (0x1000/0x2000/0x4000 cleared at AimAt entry) |
| 0x8000 | use inverse skin scale (Glb_InvSkinScale = 1/skinScale) during decode | read by AnimObjectUpdate 0x186e6 |

### 1.4 Pose buffer (animObj+0x34): numBones*28 bytes, **structure of arrays**

```
+0                    _VECTOR trans[numBones]   (12 bytes each; local, already multiplied by skin scale)
+numBones*12          quaternion quat[numBones] (16 bytes each; local, xyzw)
```
Evidence: BuildPalette uses `pose + numBones*12 + b*16` for the quaternion and `pose + b*12` for the translation
(0xdd2fc..0xdd356); AnimFrameResolve passes `(pose, pose + numBones*12)` to the post-pose callback;
AnimObjectUpdate finds the root quat of a sequence at `seq->rawData(+0x68) + numBones*12`. The same layout is used
by every decoded sequence frame (seq+0x68). (PS2 is 32 bytes/bone: 16-byte vectors.) The skin scale is applied
while decoding (AnimFrameCopy / FUN_00013c70 multiply x,y,z by Glb_SkinScale 0x1d7794), not in the hierarchy.

### 1.5 sAnimSkin_tag (runtime, 0x30 bytes; built by AnimProcessSkinData 0x144c0 - see spec-load, which agrees)

Fields this area reads: +0x00 hashcode, +0x04 celglist* rigged[numRigged], +0x08 celglist* discrete[numDiscrete],
+0x0c u8 discreteBone[numDiscrete], +0x10 _VECTOR skinScale (12), +0x1c datum array (36 bytes each:
`{s32 id; s32 bone; vec3 t; quat q}`), **+0x20 u8 hierarchy[numBones]** (bit7 = bone used by this skin,
bits0-6 = parent, 0x7f = root), **+0x24 _MATRIX bind[numBones]** (from the file's `{vec3 t; quat q}` per bone),
+0x28 numRigged, +0x29 numDiscrete, +0x2a numMorphs, +0x2b numDatums, +0x2c skeletonNum.

- **Parent indices come from the skin, not the skeleton.** The skeleton (SkeletonTable 0x1d7578,
  `{s16 idx; u8 numBones @+2; u8; u32 transMask[3]; vec3 defaultOffset[numBones] @+0x10}`) only supplies numBones
  and default local translations.
- **bind[] is an inverse bind with identity rotation**: across all 1194 records in the dump the quaternion is
  exactly (0,0,0,1) and `t = -worldBindPosition` (to 0.01). psiBuildMatrixPalette reads only `bind[b].m[12..14]`.
  So the rest pose has every bone axis-aligned with the model, and mesh vertices are stored in model (bind) space.

### 1.6 Other runtime structures touched

- Skin-geometry draw record (INVENTED NAME `GlistGeom`, table at 0x2a0e68 indexed by `celglist+4`): +0x10 numBatches,
  +0x18 total index count, +0x4c stream (vertex buffer) handle, +0x50 index buffer handle, +0x54 batch[] (12 bytes:
  +0 u16 tex0, +2 u16 tex1, +4 u32 numIndices, +8 u16 renderFlags, +0xa u8 stateChangeMask, +0xb u8 boneSet
  (1-based, 0 = keep previous)), +0x58 boneSet[] (0x36 = 54 bytes each: up to 53 bone indices, 0xff-terminated),
  +0x5c morph data (non-NULL = morph streams present). Format of the file side belongs to spec-load; see 5.3 for
  how it is drawn.

---------------------------------------------------------------------------------------------------------------

## 2. Functions

Calling conventions below are the real Xbox ones (checked in the disassembly of each function and its callers).

| Function | Xbox | PS2 | Convention / signature |
|---|---|---|---|
| AnimObjectNew | 0x11440 | 0x1bf038 | cdecl `int (obj_tag*, HASHCODE skin)`; returns 1/0 (PS2 returns a garbage non-zero pointer) |
| AnimObjectDelete | 0x17a60 | 0x1bf5a8 | cdecl `void (obj_tag*)` |
| AnimObjectInitialPose | 0x190f0 | 0x1c6c50 | **obj in ESI**, no stack args, returns float in ST0 |
| AnimObjectUpdate | 0x18690 | 0x1bf700 | cdecl `bool (obj_tag*)` (MOVSX EAX,AL) |
| control_AnimUpdate ("the second AnimObjectUpdate") | 0x2cf00 | 0x134878 | cdecl `void (obj_tag*)`; body is `JMP 0x18690` |
| AnimObjectDraw | 0x16c20 | 0x1c4bd0 | cdecl `void (obj_tag*, viewer_tag*)` |
| psiBuildMatrixPalette | 0xdd290 | 0x111690 | cdecl `void (obj_tag*, sAnimObject_tag*, char buildPalette)` |
| psiDrawSkinObjectMatrix | 0xe0960 | 0x111a20 | cdecl `void (celglist_tag*, _MATRIX* unused, obj_tag*, _MATRIX* palette)` |
| (skinned/rigid glist geometry draw, INVENTED NAME psiDrawGlistGeom) | 0xdd4c0 (no Ghidra function) | - | cdecl `void (int geomIndex)`; called by psiDrawObjectMatrix 0xddef0 |
| d3dSetSkinMatrix | 0xe4e40 | - | cdecl `void (const float m[12] (3 transposed rows), uint slot)` |
| SkinIt | - | 0x10c128 | PS2 only (see 5.4) |
| AnimGetBoneWorldTrans | 0x13070 | 0x1c3858 | cdecl `void (obj_tag*, uint boneOrDatum, int datumOffset, _VECTOR* posOut, _MATRIX* mtxOut /*may be NULL*/)` |
| AnimGetBoneWorldTrans2 | 0x131c0 | 0x1c3a18 | same signature; 0x1730-byte stack frame (_alloca_probe) |
| AnimObjectIsClose | 0x11950 | 0x1c6e68 | cdecl `bool (obj_tag*, uint lodThreshold)` |
| AnimGetNDiscreteObj | 0x11920 | 0x1c6e28 | cdecl `obj_tag* (obj_tag*, uint n)` |
| AnimObjectSetSleeveType | 0x118f0 | 0x1c6d80 | cdecl `(obj_tag*, uint n)`; AnimSleeveGetEntity 0x118b0 inlined-call with n in EAX |
| AnimObjectSetHeadTracking | 0x119a0 | 0x1c6eb8 | cdecl `(obj, void* target, float range, float rate, bool randomDelay, bool targetIsObj, u8 bone)` |
| AnimObjectIsHeadTracking | 0x11a80 | 0x1c6fc0 | cdecl `bool (obj)` = flags & 0x30 |
| AnimObjectSetAiming | 0x12220 | 0x1c6fe8 | cdecl `(obj, void* target, float range, float rate, bool targetIsObj, u8 bone)` |
| AnimObjectIsAiming | 0x122b0 | 0x1c7070 | cdecl `bool (obj)` = flags & 0x180 |
| AnimObjectHeadTrack | 0x11aa0 | 0x1bfd40 | cdecl `void (obj, animObj, quaternion_tag* worldQuats, _MATRIX* boneMtxs, int bone /*=5*/)` |
| AnimObjectAimAt | 0x15060 | 0x1c0580 | same, bone = 2 |
| Quat_QuatTransToMat / Quat_QuatToMat | 0xd59e0 / 0xd5800 | 0x1e4778 / 0x1e4650 | see section 0 |

### 2.1 AnimObjectNew (KEY) - 0x11440

```c
int AnimObjectNew(obj_tag *obj, HASHCODE hash)
{
    if ((hash & 0xff000000) != 0x05000000 || !obj) return 0;
    hash = maybeFixupSkinHashcode(hash);            // 0x11350: 0x05000090 -> 0x05000087 on PowerStationA1,
                                                   //          -> 0x05000093 on EvilBase
    sAnimSkin_tag *skin = hashtable_getitem(hash);  // 0x6b500 (already processed into the 0x30-byte struct)
    if (!skin) return 0;
    AnimState *as = Mem_Malloc(0xF0, 0x2604, 0);    if (!as) goto fail_state;
    obj->animState /*+0xB8*/ = as;  memset(as, 0, 0xF0);
    AnimObj *ao = (AnimObj*)((char*)as + 0x58);
    ao->skin /*+0x1c*/ = skin;
    uint nb = SkeletonTable[skin->skeletonNum /*+0x2c*/]->numBones /*+2*/;
    *(u8*)((char*)obj + 0xD6) |= 8;                 // rendererType |= 8
    ao->poseSize /*+0x88 u16*/ = nb * 28;
    ao->pose /*+0x34*/ = Mem_Malloc(nb*28, 0x2404, 0);  if (!ao->pose) goto fail;
    memset(ao->pose, 0, nb*28);
    if (skin->numMorphs /*+0x2a*/) { ao->morphW /*+0x38*/ = Mem_Malloc(numMorphs*4, 0x3b04, 0);
                                     memset(ao->morphW, 0, numMorphs*4); }          // (no NULL check)
    if (skin->numDatums /*+0x2b*/) {
        ao->datumGlist /*+0x2c*/ = Mem_Malloc(numDatums*4, 0x2604, 0); if (!..) goto fail;
        ao->datumTimer /*+0x3c*/ = Mem_Malloc(numDatums,   0x2604, 0); if (!..) goto fail;
        for (i..numDatums) { datumGlist[i] = 0; datumTimer[i] = 0; }
    }
    ao->boneMtxs /*+0x40*/ = Mem_Malloc(nb*0x3c, 0x2604, 0); if (!..) goto fail;
    ao->palette  /*+0x44*/ = Mem_Malloc(nb*0x3c, 0x2604, 0); if (!..) goto fail;
    if (nb > 70) { ao->flags |= 2; if (skin->skeletonNum != 1) ao->flags |= 0x400; }
    if (skin->numRigged /*+0x28*/)
        hashtable_set_object_to_entity_gfx(obj, hashtable_celglist_to_hashcode(skin->rigged[0]));  // 0x6b5e0/0x6b630
    if (ao->flags & 2)      *(u8*)((char*)obj+0xDA) |= 8;   // obj->flags
    if (skin->numRigged)    *(u8*)((char*)obj+0xDA) |= 4;
    if (skin->numDiscrete /*+0x29*/) {
        ao->discrete /*+0x48*/ = Mem_Malloc(numDiscrete*4, 0x2604, 0); if (!..) goto fail; memset 0;
        obj_tag *parent = (numRigged * numDiscrete) ? obj : NULL;
        for (i..numDiscrete) {
            obj_tag *c = Control_CreateObjEx(0, &obj->position /*+0x24*/, NULL, NULL, skin->discrete[i] /*+0x08*/,
                                             parent, 0, 0xc4, 1.0f, 0, 0xff, 0xff, 0xff);      // 0x2e320
            if (!c) goto fail;
            c->extraObjectData /*+0xbc*/ = obj;  c->objectType /*+0xdb*/ = 0x3d;   // OBJECTTYPE_ANIMOBJECT
            c->effectFlags /*+0xcc*/ = (c->effectFlags & ~0x20) | 0x8000;
            if (numRigged) c->flags /*+0xda*/ |= 4;
            ao->discrete[i] = c;
        }
    }
    skin->hashcode /*+0x00*/ = hash;
    if (ao->flags & 2) {
        obj->effectFlags &= ~0x20;
        float c = (ao->flags & 0x400) ? -1.160398f : -0.995208f;
        ao->heightOffset /*+0x58*/ = c - obj->objGraphics /*+0xb4*/->bboxMin.y /*+0x20*/ / skin->skinScale.y /*+0x14*/ + 0.02f;
    }
    return 1;
fail:   // frees in order: discrete objs (control_delete_object 0x2d080) + array, palette, boneMtxs, datumTimer,
        // datumGlist, morphW, pose; then obj->animState. Returns 0.
}
```

### 2.2 AnimObjectDelete - 0x17a60

`AnimSetDeleteAll(obj)` (0x17820); `AnimListBuild(obj, 1, 0, 0, 1)` (0x122e0); `AnimListDelete(obj)` (0x171e0);
then frees (Mem_Free 0x705f0 takes the address of the pointer and NULLs it): discrete objects
(control_delete_object each non-NULL, then the array, AS+0xa0), palette (AS+0x9c), boneMtxs (AS+0x98),
datumTimer (AS+0x94), datumGlist (AS+0x84), morphW (AS+0x90), pose (AS+0x8c), then obj->animState.

### 2.3 AnimObjectInitialPose (KEY) - 0x190f0, obj in ESI

Returns the world-space Y of bone 55 in the character's idle pose (only caller on Xbox: AnimDebugCreate 0x192fa;
used to find how far the feet are below the object origin).
```c
float AnimObjectInitialPose(/*ESI*/ obj_tag *obj)
{
    _VECTOR p; Vec_Zero(&p);
    uint f = obj->animState->animObj.flags;
    if (f & 2) {
        sAnimScript_tag *s = AnimScriptAdd(obj, (f & 0x400) ? 0x06000259 : 0x0600035e);   // 0x17220
        s->speed /*+0x94*/ = 0.0f;
        s->flags /*+0x74*/ &= 0x7fffffff;
        AnimObjectUpdate(obj);
        AnimGetBoneWorldTrans(obj, 0x80000037 /*bone 55, raw bone*/, 0, &p, NULL);
        AnimScriptDelete(obj, &s);                                                     // 0x15b40
    }
    return p.y;
}
```
(PS2 chooses the same two scripts with the same polarity.)

### 2.4 AnimObjectUpdate (KEY) - 0x18690

Script/sequence ticking and blending belong to spec-script / spec-seq; the pose-relevant skeleton is:
```c
bool AnimObjectUpdate(obj_tag *obj)
{
    AnimState *as = obj->animState; if (!obj || !as) return false;
    AnimObj *ao = &as->animObj;  sAnimSkin_tag *skin = ao->skin;
    ao->flags &= ~0xC;                                  // bone matrices and palette are stale
    Vec_Copy(&skin->skinScale, &Glb_SkinScale /*0x1d7794*/);
    ao->lastUpdateFrame = GameState.numFrames /*0x1f65bc*/;
    Glb_InvSkinScale /*0x1d6a28*/ = (ao->flags & 0x8000) ? 1/skinScale : (1,1,1);
    ScriptFootstepType /*0x1d6a24*/ = 0;
    if (skin->numMorphs && (ao->flags & 2) && AnimObjectIsClose(obj, 12)) AnimFacialUpdate(obj) /*0x17510, ao in ESI*/;
    for (s = ao->firstScript; s; s = s->next /*+0x44*/)
        if (s->active /*+0x58*/ && !(s->flags /*+0x74*/ & 0x10000000)) {
            Glb_bScriptDeleted /*0x1d6a64*/ = 0; Glb_pCurrentScript /*0x1d7778*/ = s;
            AnimScriptTick(obj, s) /*0x18450, ao in ECX*/;  s->prevSpeed /*+0x90*/ = s->speed2 /*+0x8c*/;
        }
    as->stateFlags(+0x50) &= ~4;
    // footsteps: player (obj+0xdb == 3) accumulates |ao+0x60| into BLData(+0xbc)+0x8b4, stride 1.65
    // (0.4125 when climbing, obj+0xd2 == 1); Effect_Create / SFX_FOOTSTEP_LADDER - not pose related.
    bool rootYawMoved = false;
    for (s = ao->firstScript; s; s = s->next) {
        for (k = 0; k < 2; k++) {                       // s->seq[0] (+0x4c bones), s->seq[1] (+0x50 morphs)
            seq = s->seq[k];
            if (seq && s->active && seq->rawHash /*+0x78*/) AnimSeqTick(/*EAX*/s, /*EBX*/ao, /*EDX*/obj, seq?) /*0x172b0*/;
        }
        if (s->flags & 0x20000) {                       // "root yaw drives the object"
            seq = s->seq[0];
            quaternion_tag *rq = (quaternion_tag*)(seq->raw /*+0x68*/ + SkeletonTable[seq->skel /*+0x8d*/]->numBones*12);
            _VECTOR f; Quat_Rot_Vec(rq, &(_VECTOR){0,0,1}, &f);   // 0xd56a0
            float yaw = atan2(f.x, f.z);
            if (s->yaw0 /*+0x9c*/ >= 8.0f) {           // first frame (8.0 = "unset" sentinel)
                s->yaw0 = yaw; Control_GetObjMatrix(obj, &obj->mtx /*+0x70*/); Mat_CopyRot(&obj->mtx, (MATRIX*)s /*+0*/);
            } else {
                obj->rotation.y /*+0x40*/ += Vec_AngleDifference(s->yawPrev /*+0xa0*/, yaw);
                quaternion_tag q; Quat_EulerToQuat(&(_VECTOR){0, Vec_AngleDifference(yaw, s->yaw0), 0}, &q);
                quaternion_tag r; MultiplyQuaternionAltOrder(&q, rq, &r); Quat_Copy(rq, &r);   // root = q (x) root
                rootYawMoved = true;
            }
            s->yawPrev = yaw;
        }
    }
    AnimFrameResolve(obj, ao);                          // 0x160a0: blends all sequences into ao->pose, sets flags|=1
    Glb_SkinScale = Glb_InvSkinScale = (1,1,1);
    if (rootYawMoved) {
        obj->rendererType /*+0xd6 u16*/ |= 0x20;
        Control_GetObjMatrix(obj, &obj->mtx);  ao->flags &= ~4;
        psiBuildMatrixPalette(obj, ao, 0);
    }
    if (obj->objectType == 3 /*PLAYER*/) psiBuildMatrixPalette(obj, ao, 0);
    if (skin->numDiscrete) {
        psiBuildMatrixPalette(obj, ao, 0);
        for (i = 0; i < numDiscrete; i++) {             // rigid per-bone meshes follow their bone
            obj_tag *c = ao->discrete[i];
            c->rendererType(+0xd6) |= 0x20;
            c->effectFlags(+0xcc) |= obj->effectFlags & 0x1a030;
            c->displayMask(+0xd4 u16) = obj->displayMask;
            Mat_Copy(&ao->boneMtxs[skin->discreteBone /*+0x0c*/[i]], &c->mtx /*+0x70*/);
        }
    }
    return true;
}
```
**The second "AnimObjectUpdate" at 0x2cf00 is `control_AnimUpdate`** (PS2 0x134878: `void control_AnimUpdate(obj_tag*)
{ AnimObjectUpdate(obj); }`). On Xbox the compiler turned it into a single `JMP 0x18690`; it is only referenced as
DATA from the per-object-type function table at 0x163c94 / 0x163d0c (stride 0x78 between the two entries), i.e. it
is the "update" callback of object types that are plain animated objects. It should be renamed control_AnimUpdate.

### 2.5 psiBuildMatrixPalette (KEY) - 0xdd290

The heart of the pose pipeline. Stack: tmpObjMtx (60 bytes) + `quaternion_tag worldQ[96]` (so max 96 bones, the
same limit as the skeleton's 3x32-bit translation mask).
```c
void psiBuildMatrixPalette(obj_tag *obj, AnimObj *ao, char buildPalette)
{
    sAnimSkin_tag *skin = ao->skin;                              // +0x1c
    _MATRIX *bone = ao->boneMtxs;                                // +0x40
    int nb = SkeletonTable[skin->skeletonNum]->numBones;
    uint f = ao->flags;                                          // +0x50 (snapshot used for the 0x30/0x180 tests)
    if (!(f & 4)) {
        ao->flags = f | 4;
        bool doHead = (f & 0x30) != 0, doAim = (f & 0x180) != 0;
        _MATRIX objM = *(_MATRIX*)((char*)obj + 0x70);           // obj->mtx, 15 dwords (REP MOVSD)
        _VECTOR           *T = (_VECTOR*)ao->pose;               // +0x34
        quaternion_tag    *Q = (quaternion_tag*)((char*)ao->pose + nb*12);
        quaternion_tag worldQ[96];
        for (int b = 0; b < nb; b++) {
            u8 h = skin->hierarchy[b];                           // skin+0x20
            if (!(h & 0x80)) continue;                           // bone not used by this skin: matrix left STALE
            int p = h & 0x7f;
            _MATRIX *M = &bone[b];
            if (p == 0x7f) {                                     // root
                Quat_QuatTransToMat(&Q[b], &T[b], M);
                RotTransMat(&objM, M);                           // M.R = M.R*objM.R ; M.t = T[b] + objM.t  (!)
                Quat_MatToQuat(&worldQ[b], M);
            } else {
                MultiplyQuaternionAltOrder(&worldQ[p], &Q[b], &worldQ[b]);   // worldQ[b] = worldQ[p] (x) Q[b]
                Quat_QuatToMat(&worldQ[b], M);
                MatrixMultiplyVector(&bone[p], &T[b], (_VECTOR*)&M->m[12]);  // M.t = T[b]*bone[p].R + bone[p].t
                if (doHead && b == 5) AnimObjectHeadTrack(obj, ao, worldQ, bone, 5);
                if (doAim  && b == 2) AnimObjectAimAt   (obj, ao, worldQ, bone, 2);
            }
        }
    }
    if (buildPalette && skin->numRigged /*+0x28*/ && !(ao->flags & 8)) {
        ao->flags |= 8;
        _MATRIX *P = ao->palette;                                // +0x44
        for (int b = 0; b < nb; b++) {
            if (!(skin->hierarchy[b] & 0x80)) continue;
            Mat_CopyRot(&bone[b], &P[b]);                        // P.R = bone.R
            MatrixMultiplyVector(&bone[b], (_VECTOR*)&skin->bind[b].m[12], (_VECTOR*)&P[b].m[12]);
                                                                 // P.t = bind.t*bone.R + bone.t
        }
    }
}
```
Notes:
- Bones are processed in index order; the data guarantees parents precede children (load agent's check), so a
  single pass suffices. Head tracking modifies bone 5 and aiming bone 2 *inside* the loop, so their children
  inherit the change (children read worldQ[5]/bone[5] afterwards).
- `boneMtxs` are **world** matrices (object matrix included). The palette therefore maps a bind-space vertex
  straight to world space: `world = (v + bind.t) * bone.R + bone.t = (v - bindPos) * R_world + P_world`. The GPU then
  applies view-projection only (the skinned glist is drawn with the identity object matrix 0x2ade8c).
- There is **no "+2"** anywhere in the palette: it has exactly numBones entries. The "+2" in the old Anim.cpp comment
  (`SkeletonTable[skin->skeletonNum] + 2`) is the byte offset of numBones inside the skeleton header.
- Scale: the hierarchy has no scale step; per-bone translations are pre-scaled by skinScale at decode time. If the
  object matrix carried scale, only the root's rotation rows would carry it, and MatToQuat would drop it for all
  children - so object scale does not propagate (quirk, probably unused).
- **Quirk (in both builds): the root translation is not rotated by the object matrix** (RotTransMat adds it). Root
  motion is normally stripped into the object, so the root offset is small; an exporter should use the same maths to
  match the game.
- Caching: bit 4 / bit 8 make repeated calls in one frame free; AnimObjectUpdate clears both.
- Callers: AnimObjectUpdate (x3), AnimObjectDraw (buildPalette=1), AnimGetBoneWorldTrans, AnimGetCollData 0x136ec,
  BodyGlow_Update 0xce89e.

### 2.6 AnimGetBoneWorldTrans (KEY) - 0x13070

```c
void AnimGetBoneWorldTrans(obj_tag *obj, uint which, int datumOfs, _VECTOR *pos, _MATRIX *mtxOut)
{
    AnimState *as;
    if (obj && (as = obj->animState) && (as->animObj.flags & 1)) {
        AnimObj *ao = &as->animObj;
        psiBuildMatrixPalette(obj, ao, 0);                 // make sure boneMtxs are current
        sAnimSkin_tag *skin = ao->skin;
        uint idx = which & 0x7fffffff;
        if ((int)which < 0) {                              // bit 31: raw bone index
            *pos = *(_VECTOR*)&ao->boneMtxs[idx].m[12];
            if (mtxOut) *mtxOut = ao->boneMtxs[idx];       // 15 dwords
            return;
        }
        uint id = idx + datumOfs;                          // otherwise: a datum id (e.g. weaponBone + 3)
        int d;
        for (d = 0; d < skin->numDatums; d++) if (skin->datum[d].id /*+0*/ == id) break;   // stride 0x24
        if (d < skin->numDatums) {
            _MATRIX m;
            Quat_QuatTransToMat(&skin->datum[d].q /*+0x14*/, &skin->datum[d].t /*+0x08*/, &m);
            Concat(&ao->boneMtxs[skin->datum[d].bone /*+0x04*/], &m);   // m = m * bone  (datum local -> world)
            *pos = *(_VECTOR*)&m.m[12];
            if (mtxOut) *mtxOut = m;
            return;
        }
    }
    Vec_Zero(pos); if (mtxOut) Mat_Identity(mtxOut);       // 0xd4dc0 / 0xd5130
}
```
(The datum bone is not range-checked; a datum with bone -1 would read before the array. Draw checks `bone >= 0`,
this function does not.) Example uses: AnimObjectDraw `(obj, 8, 3, ...)` = datum id 11 for the watch laser;
AnimObjectInitialPose `(obj, 0x80000037, 0, ...)` = raw bone 55.

### 2.7 AnimGetBoneWorldTrans2 - 0x131c0

Same interface and result, but **recomputes the chain from the pose buffer** instead of using boneMtxs, so it is
valid before/without psiBuildMatrixPalette and is unaffected by head tracking / aiming (AimAt uses it to find the
weapon without recursion):
```c
    // resolve 'which' exactly as above -> start bone B (a datum's bone when bit31 clear) ; fail -> zero/identity
    u8 chain[..]; int n = 0; for (b = B; b != 0x7f; b = skin->hierarchy[b] & 0x7f) chain[n++] = b;   // no 0x80 test
    _MATRIX objM = obj->mtx; _MATRIX M[95];            // stack array indexed by bone
    for (i = n-1; i >= 0; i--) {                       // root first
        b = chain[i];
        Quat_QuatTransToMat(&Q[b], &T[b], &M[b]);      // pose buffer at AS+0x8c
        if (i == n-1) RotTransMat(&objM, &M[b]);       // same root quirk
        else          Concat(&M[chain[i+1]], &M[b]);   // M[b] = M[b] * M[parent]
    }
    // raw bone: pos = M[B].t, mtxOut = M[B];  datum: m = QuatTransToMat(datum) ; Concat(&M[datum.bone], &m) ; out m
```
Note the local array has 95 matrices, so a bone index of 95 would overflow it (skeletons have <= 73 in the data).

### 2.8 AnimObjectDraw (KEY) - 0x16c20

```c
void AnimObjectDraw(obj_tag *obj, viewer_tag *view)
{
    AnimState *as; if (!obj || !(as = obj->animState)) return;
    AnimObj *ao = &as->animObj;
    if (!(ao->flags & 1)) return;                                 // never resolved
    if ((int)(GameState.numFrames - ao->lastUpdateFrame) > 1) return;   // not updated last/this frame: skip
    ao->lastUpdateFrame = GameState.numFrames;
    psiBuildMatrixPalette(obj, ao, 1);
    sAnimSkin_tag *skin = ao->skin;
    if (skin->numRigged && skin->numDiscrete) View_SetupRenderModes2(view, obj) /*0xd9b40*/;
    else                                     View_SetupRenderModes (view, obj) /*0xd9af0*/;
    Morph_Count /*0x1d7826*/ = 0;
    if (AnimObjectIsClose(obj, 12) && skin->numMorphs) {
        // copy morphW[] to Morph_Weight[] 0x1d6a68 with Morph_Index[] 0x1d777c = 0..n-1;
        // partial selection sort: the min(n,8) largest weights first (descending);
        // Morph_Count = that count minus trailing entries with |w| < 1e-6;
        // then re-sort those Morph_Count entries by morph index ascending.   (details: spec-facial)
    }
    celglist_tag *sleeve = ao->sleeveGlist /*+0x30*/;
    for (i = 0; i < skin->numRigged; i++) {
        celglist_tag *g = skin->rigged[i]; if (!g) continue;
        if (sleeve && i == 0) g = sleeve;
        psiDrawSkinObjectMatrix(g, &scratch /*uninitialised, unused*/, obj, ao->palette /*+0x44*/);
    }
    Morph_Count = 0;
    if ((ao->flags & 2) && skin->numDatums) {                     // show the held weapon at its datum
        int w = (s8)as->currentWeaponId /*AS+0x52*/;  HASHCODE gfx; int bone;
        if (w <= 0x72) { bone = weapon_data[w].whichBone /*0x18cff0 + w*0x10c*/; gfx = *(u32*)(0x18d020 + w*0x10c);
                         if (bone == 0xff) bone = -1; } else { bone = -1; gfx = <stale local>; }
        if (bone < 0) bone = 0;
        if (gfx) AnimDatumSetEntity(obj, bone, gfx, 1) /*0x135d0*/;
    }
    if (as->currentWeaponId == 0x18) { /* watch laser: AnimGetBoneWorldTrans(obj,8,3,&p,&m); ray to p+(-100,0,0)*m;
                                          Collide_RayIntersect; draw glist 0x2000131 (NV) / 0x2000133 stretched */ }
    for (d = 0; d < skin->numDatums; d++) {                        // datum-attached glists
        u8 *t = &ao->datumTimer[d]; if (!*t) continue; if (*t != 0xff) (*t)--;
        celglist_tag *g = ao->datumGlist[d]; if (!g) continue;
        _MATRIX m; Quat_QuatTransToMat(&skin->datum[d].q, &skin->datum[d].t, &m);
        if (skin->datum[d].bone >= 0) Concat(&ao->boneMtxs[skin->datum[d].bone], &m);
        if (!(g->flags /*+0x38*/ & 0x800)) psiSetTweakARGB(obj+0xe2, obj+0xdf, obj+0xe0, obj+0xe1) /*0xddf00*/;
        psiDrawObjectMatrix(g, &m, obj->creationTimeFrames /*+0xc8*/) /*0xdde50*/;
    }
    bCharacterLight /*0x1d7824*/ = 0;  bShadowCharacter /*0x1d7825*/ = 0;
}
```
Differences from PS2: PS2 computes "close" inline from its LOD bytes (<0xd); PS2 datum layout reads the same file.

### 2.9 psiDrawSkinObjectMatrix - 0xe0960 / d3dSetSkinMatrix - 0xe4e40

```c
void psiDrawSkinObjectMatrix(celglist_tag *g, _MATRIX *unused, obj_tag *obj, _MATRIX *palette)
{
    g_skinPalette /*0x2adf28*/ = palette;
    psiDrawObjectMatrix(g, &g_identityMtx /*0x2ade8c, set up by Graphics_Init_LowLevel*/, 0);
    if (bShadowCharacter) maybe_psiDrawShadow(<bone data of obj via AS+0x98>, obj, g) /*0xe04e0*/;
}
void d3dSetSkinMatrix(const float *rows12, uint slot)          // slot < 53
{
    if (slot < 0x35) {
        if (Gfx.D3DDevice) D3DDevice_SetVertexShaderConstantNotInline(SkinRegTable[slot] /*0x1b5208*/ + 96, rows12, 12 /*dwords = 3 registers*/);
        Gfx.D3DLastError = 0;
    }
}
```
SkinRegTable (53 ints at 0x1b5208, D3D register numbers -96..95):
slots 0..18 -> c-96, c-93, ... c-42 (3*slot - 96); slots 19..30 -> c-36 ... c-3 (3*slot - 93);
slots 31..52 -> c30 ... c93 (3*slot - 63). Gaps: c-39..c-37 (used by the viewport transform, c-38/c-37) and
c0..c29 (transform c0-c3, fog c6, colour c7, lights c9-c17, morph weights c19/c20, c21, skin constants c22).
d3dSetup fills all 53 slots with identity at start-up.

---------------------------------------------------------------------------------------------------------------

## 3. Head tracking and aiming

Both run inside psiBuildMatrixPalette, right after the target bone's world matrix is built, and pre-multiply that
bone's **world** rotation by two world-space axis-angle rotations. Both are skipped entirely by LOD
(`min(lod[0..3]) > 0x20` for head tracking, `> 0xfe` for aiming; ScriptCam 0x1f6678 != 0 overrides to "close").

### 3.1 AnimObjectSetHeadTracking / SetAiming

```c
void AnimObjectSetHeadTracking(obj, void *target, float range, float rate, bool randomDelay, bool isObj, u8 bone) {
    ao->headRange2(+0x70) = range*range;  ao->headRate(+0x68) = rate;  u8 hold = 0;
    if (!target) { ao->flags &= ~0x10; ao->headTarget(+0x00) = NULL; }
    else { if (randomDelay) hold = Rand_Rand(FRAME_RATE_INT(0x17c0f4) >> 2) + FRAME_RATE_INT/10;
           ao->flags |= 0x10; ao->headTarget = target;
           if (isObj) { ao->flags |= 0x40; ao->headBone(+0x8c) = bone; } else ao->flags &= ~0x40; }
    ao->headHold(+0x8e) = hold;
}
void AnimObjectSetAiming(obj, void *target, float range, float rate, bool isObj, u8 bone) {
    ao->aimRate(+0x6c) = rate;  ao->aimRange2(+0x74) = range*range;
    if (!target) { ao->aimTarget(+0x04) = NULL; ao->flags &= ~0x80; return; }
    ao->flags |= 0x80; ao->aimTarget = target;
    if (isObj) { ao->flags |= 0x200; ao->aimBone(+0x8d) = bone; } else ao->flags &= ~0x200;
}
```
A target given as an object is resolved each frame to `target->animState->animObj.boneMtxs[bone].t`
(`*(target+0xb8)+0x98` + bone*0x3c + 0x30); if that object has no animState, a point straight ahead of the bone
is used (i.e. no rotation).

### 3.2 AnimObjectHeadTrack - 0x11aa0 (bone = 5)

```c
state = ((f & 0x10) ? 1 : 0) | ((f & 0x20) ? 2 : 0);   // after: if (f & 0x180) f &= ~0x10 (aiming wins)
ao->flags |= 0x20;
_VECTOR *P  = &bone[5].t;                               // head/neck pivot
_VECTOR fwd = bone[5].r2;                               // m[8..10], the bone's Z row = facing direction
_VECTOR up  = Vec_UnitVector(bone[2].t -> bone[5].t);   // spine direction
_VECTOR side= Vec_CrossNormalise(up, fwd);
_VECTOR *tgt = (f & 0x40) ? <target obj bone pos, or P+fwd> : (_VECTOR*)ao->headTarget;
yaw = pitch = 0; mask = 0;
if (state == 0) ao->headYaw(+0x78) = ao->headPitch(+0x80) = 0;
if (state != 2 && Vec_SqDist3D(P, tgt) <= ao->headRange2) {
    // pitch: project target onto the plane through P spanned by (up, fwd)
    plane = Plane_PlaneEq(P, P+up, P+fwd); t1 = PutPntOnPlane(tgt, plane);
    if (SqDist(P, t1) >= 0.25) { mask = 2; a = unit(P -> PutPntOnPlane(P+fwd)); b = unit(P -> t1);
        d = dot(a, b); if (d >= 0) { pitch = acos(clamp(d,-1,1)); if (TripleProduct(a, b, side) < 0) pitch = -pitch; } }
    // yaw: plane spanned by (side, fwd)
    plane = Plane_PlaneEq(P, P+side, P+fwd); t2 = PutPntOnPlane(tgt, plane);
    if (SqDist(P, t2) >= 0.25) { mask |= 1; a = unit(P -> PutPntOnPlane(P+fwd)); b = unit(P -> t2);
        yaw = acos(clamp(dot(a,b),-1,1)); if (TripleProduct(a, b, up) < 0) yaw = -yaw; }
    if (mask == 1) pitch = 0; else if (mask == 2) yaw = 0;
}
yaw   = (fabs(yaw) > 2.6) ? 0 : clamp(yaw, -1.1, 1.1);      // target straight behind: look ahead
pitch = clamp(pitch, -1.0, 0.6);
if (ao->headHold) { yaw = ao->headYaw; pitch = ao->headPitch; ao->headHold--; }   // hold current
ao->headYaw   = yawN   = Ang_RotateToBearing(ao->headYaw,   yaw,   ao->headRate, PI);
ao->headPitch = pitchN = Ang_RotateToBearing(ao->headPitch, pitch, ao->headRate, PI);
qa = AxisAngle(up, yawN); qb = AxisAngle(side, pitchN);
worldQ[5] = (qa (x) qb) (x) worldQ[5];   Quat_QuatToMat(&worldQ[5], &bone[5]);   // translation unchanged
if (skin->numMorphs) {                                      // eye/look morph hints (spec-facial)
    ao->lookMorphX(+0x8f) = (u8)(clamp01(2*(yawRaw - yawN) + 0.5) * 255);
    ao->lookMorphY(+0x90) = (u8)((1 - clamp01(2*(pitchRaw - pitchN) + 0.5)) * 255);
}   // yawRaw/pitchRaw = the clamped targets before the hold substitution
if (state == 2 && fabs(yawN) < 0.01 && fabs(pitchN) < 0.01) { ao->flags &= ~0x30; if (numMorphs) +0x8f = +0x90 = 0; }
```

### 3.3 AnimObjectAimAt - 0x15060 (bone = 2, the spine)

```c
ao->flags &= ~0x7000;  (LOD gate > 0xfe)
state = ((f & 0x80) ? 1 : 0) | ((f & 0x100) ? 2 : 0);  ao->flags |= 0x100;
w = (s8)as->currentWeaponId; wb = (w <= 0x72 && weapon_data[w].whichBone != 0xff) ? whichBone : -1;   // inlined AnimDatumGetWeaponInfo
if (wb < 0) return;  if (multiplayer && !weapon_data[w].weaponAnimationSet) return;
// weapon frame: datum id wb+3 (muzzle?) else wb+1 else give up (asks for id 0x41 -> zero/identity)
AnimGetBoneWorldTrans2(obj, wb /*or 0x40*/, 3 /*or 1*/, &gunPos, &gunM);
_VECTOR *P  = &bone[parent(2)].t;                  // pivot = the spine's parent (pelvis) position
_VECTOR dir = ApplyMatrixLV(gunM, (-1,0,0));       // barrel direction = gun's -X axis
_VECTOR up  = unit(P -> bone[2].t);   _VECTOR side = CrossNormalise(up, dir);
tgt = (f & 0x200) ? <target obj bone pos, or P+dir> : ao->aimTarget;
yaw = pitch = 0; mask = 0; isPlayer = obj->objectType == 3;
if (state == 0) ao->aimYaw(+0x7c) = ao->aimPitch(+0x84) = 0;
if (state != 2 && SqDist(P, tgt) <= ao->aimRange2) {
    ao->flags |= 0x1000;  muzzleFar = gunPos + dir*100;
    // pitch in the (up, dir) plane: rotate so the barrel line (gunPos->muzzleFar, projected) passes through the
    // projected target: Vec_LineSphereIntersection(gunP, farP, P, |P - tgtP|, &hit) then angle(P->tgtP, P->hit)
    ... if (SqDist(P,tgtP) >= 0.25) mask = 2;
    if (Dist(P,gunP) <= Dist(P,tgtP) && intersects && (isPlayer || dot >= 0)) {
        pitch = acos(clamp(dot)); if (TripleProduct(a, b, side) >= 0) pitch = -pitch; }
    limits: isPlayer ? [-1.5707964, 1.2707963] : [-0.9, 0.9]   (players use the limits even with no intersection)
    if clamped: ao->flags |= 0x4000;
    if (!isPlayer) {   // yaw in the (side, dir) plane, same construction, sign from TripleProduct(.., up)
        ... if (SqDist >= 0.25) mask |= 1;
        yaw clamped to [-0.9, 0.9], ao->flags |= 0x2000 when clamped   -- see BUG below
    }
    if (mask == 1) pitch = 0; else if (mask == 2) yaw = 0;
}
ao->aimYaw   = yawN   = Ang_RotateToBearing(ao->aimYaw,   yaw,   ao->aimRate, PI);
ao->aimPitch = pitchN = Ang_RotateToBearing(ao->aimPitch, pitch, ao->aimRate, PI);
worldQ[2] = (AxisAngle(up, yawN) (x) AxisAngle(side, pitchN)) (x) worldQ[2];  Quat_QuatToMat(&worldQ[2], &bone[2]);
if (state == 2 && fabs(yawN) < 0.01 && fabs(pitchN) < 0.01) ao->flags &= ~0x180;
```
**BUG (original):** the upper yaw clamp tests the *pitch* variable:
`if (yaw < -0.9) yaw = -0.9; else if (pitch > 0.9) yaw = 0.9;` (Xbox 0x15776..0x157aa: the lower test reads
[ESP+0x1c] = yaw, the upper reads [ESP+0x18] = pitch). The PS2 decompile (AimAt 0x1c0580, `if (fVar14 <= 0.9)`
after testing fVar15) looks the same, so it is probably in the shared C source. So an NPC's aim yaw is not
limited on the positive side unless its pitch exceeds 0.9, and a large pitch forces yaw to +0.9. A faithful
reimplementation must keep it.

Because the pivot is the spine's parent and the rotation is applied to bone 2's world rotation (translation kept),
the whole upper body (children of bone 2: arms, head, weapon) swings to aim.

---------------------------------------------------------------------------------------------------------------

## 4. Other small functions

- **AnimObjectIsClose(obj, n)** 0x11950: `if (ScriptCam) return 1; return min(lod[0..3]) <= n;` (n = 12 for facial
  and morphs; lod units are distance/4 corrected by the viewer's zoom factor).
- **AnimGetNDiscreteObj(obj, n)** 0x11920: `obj && as && n < skin->numDiscrete ? ao->discrete[n] : NULL`.
- **AnimObjectSetSleeveType(obj, n)** 0x118f0: `h = AnimSleeveGetEntity(n)` (first loaded of SleeveEnts_SleeveTable[8]
  starting at n, wrapping); `if (h && (g = hashtable_hashcode_to_celglist(h))) ao->sleeveGlist(+0x30) = g;`
- **IsHeadTracking / IsAiming**: `flags & 0x30` / `flags & 0x180`.

---------------------------------------------------------------------------------------------------------------

## 5. From palette to GPU (enough to rebuild skinned meshes for Blender)

### 5.1 Glist draw with bone sets (0xdd4c0, called by psiDrawObjectMatrix 0xdde50)

psiDrawObjectMatrix transposes the object matrix into a 4x4 (adds the 0,0,0,1 column) and sets it as the world
transform (0xe4c50), then draws geometry record `glist+4`. For the skinned glist the object matrix is identity, so
c0..c3 end up as view-projection only. Per batch (12-byte records at geom+0x54):
```c
for (batch in geom->batches) {
    if (batch.boneSet /*+0xb*/) {
        const u8 *set = geom->boneSets(+0x58) + (batch.boneSet - 1) * 0x36;
        for (slot = 0; set[slot] != 0xff; slot++) {                         // up to 53 bones per batch
            const _MATRIX *m = &g_skinPalette[set[slot]];                   // palette index = skin bone index
            float rows[12] = { m[0],m[4],m[8],m[12],  m[1],m[5],m[9],m[13],  m[2],m[6],m[10],m[14] };
            d3dSetSkinMatrix(rows, slot);                                   // 3 constant registers
        }
    }
    ... render-state changes by stateChangeMask, then DrawIndexedVertices(TRIANGLESTRIP, count+2, start)
    start += count + 2;
}
```
Morph weights: when geom+0x5c is non-NULL the (up to 8) Morph_Index/Morph_Weight pairs go to 0xe5700 (-> c19/c20).

### 5.2 Vertex layout and the skinning shader

Vertex shader handles: 128 variants (vtxShaderHandles, created by xboxInitGraphics 0xe7000). Index bit 1 = skinned
(the stream is the 32-byte format), bit 0 = morph streams present. Declarations (Xbox D3DVSD tokens):

| Decl (addr) | Used when | Stream 0 | extra streams |
|---|---|---|---|
| 0x1b50e4 | rigid | v0 FLOAT3 pos, v1 NORMPACKED3 normal, v2 D3DCOLOR colour, v3 FLOAT2 uv = **28 bytes** | - |
| 0x1b5100 | rigid + morph | same | streams 1..8: v7..v14 SHORT3 morph deltas |
| 0x1b5158 | **skinned** | + v4 D3DCOLOR **bone/weight** = **32 bytes** (d3dBindBuffers stride 0x1c/0x20) | - |
| 0x1b5178 | skinned + morph | 32 bytes | v7..v14 SHORT3 |

Skinned shader #2 (disassembled by pose/vshdis.py; c22 = (-96, 1, 256, 1/256) set by d3dSetup):
```
MAD r4.xy, v4.zy, c22.z, c22.x        ; regA = 256*v4.z - 96, regB = 256*v4.y - 96
MAD r5.x,  v4.x, c22.w, v4.w          ; wA = v4.x/256 + v4.w
ADD r6.y,  v3.w, -r5.x                ; wB = 1 - wA   (v3 is FLOAT2 so v3.w = 1)
ARL a0.x, r4.x ; DP4 r7.xyz, v0, c[a0+0..2]        ; pA = v0 * palette[boneA]
ARL a0.x, r4.y ; DP4 r8.xyz, v0, c[a0+0..2]        ; pB = v0 * palette[boneB]
MUL/MAD r10 = pA*wA + pB*wB ; DPH oPos, r10, c0..c3
```
Lit variants (e.g. #6) also do `DP3 normal, v1, c[a0(A)+0..2]` - **normals use bone A only**. The morph variant
(#3) adds `v0.xyz += sum_i v(7+i).xyz * weight_i` (c19.xyzw, c20.xyzw) before skinning.

So each skinned vertex has **two bones and one 16-bit weight**. Decoding the v4 D3DCOLOR dword (little-endian
bytes b0 b1 b2 b3 in memory = B, G, R, A; the shader sees v4 = (R, G, B, A)/255 = (b2, b1, b0, b3)/255):
```
regA  = b0 - 96   (256*b0/255 - 96, truncated by ARL)  -> NV2A constant index b0 (0..191)
regB  = b1 - 96
wA    = (b3*256 + b2) / 65280.0        (≈ u16/65280; b3 is the high byte)
wB    = 1 - wA
slot  = inverse of SkinRegTable:  s = b/3 for b < 57 ; (b-3)/3 for 60 <= b <= 93 ; (b-33)/3 for b >= 126
bone  = boneSet[batch.boneSet-1][slot]      (skin bone index; palette/boneMtxs index)
```
For Blender: vertex positions are in model bind space; bone b's rest head = `-skin.bind[b].t` (world bind
position), rest rotation identity; weights as above; the animated world matrix of bone b is boneMtxs[b] from 2.5.
Posed vertex = `wA*((v - bindPosA)*R_A + P_A) + wB*((v - bindPosB)*R_B + P_B)`.
(Exact batch/boneSet/vertex-buffer file layout is spec-load's; I only confirmed the runtime record offsets above.)

### 5.3 PS2 SkinIt (0x10c128) and its Xbox equivalent

PS2 psiDrawSkinObjectMatrix clips the object, looks it up in a 128-entry SkinCache (per object, status 3 = done
this frame), otherwise CacheSkin()s a private copy of the glist's float vertex list, uploads the palette rows for the
glist's `skinInfo->boneIndexList` (0xff-terminated) into VU0 data memory (0x11004000, row k of matrix i at
+0x400*k + 16*i), and calls **SkinIt(glistinfo, floatListOriginal, floatListAllocated, numVerts, morphData)**.
SkinIt is a CPU/VU0 software skinner: it double-buffers the vertex list through the scratchpad with DMA
(D_CTRL 0x1000d4xx), applies up to Morph_Count morph deltas on the CPU (12-byte deltas scaled by Morph_Weight),
then runs VU0 micro-programs (`vcallms 0x0 / 0x17 / 0x2e`) four vertices at a time to transform them by their
bone matrices, writing into the cached copy, which psiDrawClonedObject then sends to the GS.
**Xbox has no SkinIt/SkinCache**: the same job is done per vertex on the GPU by the skinned vertex shaders (5.2),
with the palette uploaded per batch by d3dSetSkinMatrix. InitCacheSkin/AgeSkinCache/CacheSkin have no Xbox
counterparts.

---------------------------------------------------------------------------------------------------------------

## 6. Reimplementation notes (C++)

- psiBuildMatrixPalette and AnimGetBoneWorldTrans are self-contained given the helpers in math.cpp and are good
  first candidates. Keep the exact operation order (MatToQuat on the root, quaternion products for children, then
  QuatToMat) - it is not bit-identical to matrix concatenation. Keep stale matrices for unused (bit7 clear) bones.
- Keep RotTransMat's unrotated root translation and the AimAt yaw-clamp bug for fidelity.
- Quat_QuatToMat must preserve ECX/EDX (already done). Nothing else in this area uses register arguments except
  AnimObjectInitialPose (obj in ESI, float returned in ST0), AnimFacialUpdate (ao in ESI), AnimScriptTick (ao in
  ECX) and AnimSeqTick (EAX = script, EBX = animObj, EDX = obj) - check callers before replacing those.
- `worldQ[96]` on the stack bounds numBones at 96.

## 7. Open questions

1. animObj +0x08..+0x17, +0x20, +0x5c, +0x64, +0x92..+0x93 are not touched by this area (script/facial?).
2. Which object types use control_AnimUpdate (the object-type table at 0x163c94/0x163d0c) - map the table.
3. What the per-object-type table stride/layout is (entries 0x78 apart hold the same function).
4. The file layout of the glist batches / bone sets / 32-byte vertices is inferred from the draw loop only; spec-load
   should confirm the slot->register byte encoding against real vertex data (b0/b1 values should all be in the
   SkinRegTable+96 set: 0,3,...,54, 60,...,93, 126,...,189).
5. The exact semantics of weapon_data.whichBone and datum ids +1/+3 (grip vs muzzle?) - AnimDatum* functions.
6. maybe_psiDrawShadow's use of `boneMtxs+0xd18` (bone 56 region) - not needed for animation.
