# Nightfire action engine: facial animation system (Xbox default.xbe)

Research spec. Ghidra was only read, never written. Xbox offsets and sizes come from the Xbox binary. PS2
(ACTION.ELF) symbol names were used to work out intent. Parser: `parse_facial.py` in this folder.

## 0. Summary

* **Facial animation uses vertex morph targets (blend shapes) only.** It uses no bones and no texture swaps.
  Each head mesh has **22 morph targets**. Eye direction is also done with morph targets: channels 11 to 14
  are driven from head tracking. Blinks, moods and lip sync are all morph weights.
* **Where the data lives**
  * **Weights over time:** "morph sequences". These are ordinary `0x04xxxxxx` sequence files whose header
    flag bit 3 (`flags@0x0c & 8`) is set. Each has one scalar track per channel (22). The curve encoding is
    the same piecewise quintic polynomial used by bone tracks.
  * **Playback:** morph sequences are played by ordinary `0x06` animation scripts. A script can play a bone
    sequence and a morph sequence side by side (cutscenes do this). Each `sAnimScript_tag` owns a second
    sequence object, `morphAnimSeq` at +0x50. Its 0x58-byte raw buffer holds 22 floats: the channel weights.
  * **Morph target deltas:** stored in the character's Xbox geometry block (`ModelData`, magic 0x0645584B) in
    the level files (`0x01xxxxxx`). They are 22 targets x `morphVtxCnt` vertices x SHORT3 (int16 x,y,z).
    The skin files (`0x05`) only carry the count (`numMorphs` = 22, file byte 0x12 -> `sAnimSkin_tag+0x2a`) and
    the hashcode of the rigged model.
* **How weights are driven**
  * **Mood and blinking, from AI state:** `AnimFacialUpdate`, per drone. A base "mood" expression is chosen
    by `Drone.alertStatus` (0..3) and held with speed 0. It lives on layer key 0xE0000000. Random blinks go
    on layer 0xE0000001.
  * **Lip sync:** each speech SFX in `Drone_SfxInfo` has an optional facial script hashcode. It is played
    by `NDrone2_SpeechSFX -> NDrone2_PlayFacialAnim -> AnimFacialAppend` on layer 0xE0000002. It is
    pre-authored per line; nothing is derived from the audio.
  * **Cutscenes (`ScriptCam != 0`):** every script appended to a morph-capable character gets layer
    0xE0000002. Only that layer is copied, and mood, blinks and eye tracking are switched off.
  * **Eye direction:** head tracking (`AnimObjectHeadTrack`) writes two bytes that `AnimFacialBlend` turns
    into weights for channels 11 to 14.
* **Rendering:** GPU morphing in the vertex shader. `AnimObjectDraw` picks the **8 largest** weights, cuts
  trailing ~0 weights and sorts them by channel index. `RecurseAndDrawBoxes` hands them to
  `d3dSetStreamSources`, which works as follows:
  * It binds the 8 selected targets' delta buffers as vertex streams 1..8. Each is SHORT3 with a stride of
    6 bytes.
  * It uploads `weight/32767` x 8 to vertex-shader constants 0x73..0x74. This is hardware slot 115, which is
    D3D c19/c20; the count is 8 DWORDs.
  * Only primitives flagged "morph" use them. Those primitives only reference the first `morphVtxCnt`
    vertices (the head).
  * The shader does `pos += sum_i delta_i * weight_i / 32767` (model units).
* **Parser results over all 32 level bundles (`lvl/07*.bin`):** every byte is accounted for.
  * 145/145 distinct morph sequences parse cleanly.
  * 213 facial scripts are found.
  * 179/179 morph-target ModelData copies validate. These are 62 distinct head meshes, all with 22 targets.
  * 61/61 skins with numMorphs = 22 resolve to their mesh.

  The small dump gives 31/31, 39, 5/5 and 5/5.

## 1. Struct layouts (Xbox)

### sAnimObject_tag (embedded at AnimState+0x58), facial fields only

| off | size | type | name | evidence | conf |
|---|---|---|---|---|---|
| +0x1c | 4 | sAnimSkin_tag* | skin | AnimFrameResolve 0x160a0 `skin = *(animObj+0x1c)` | confirmed |
| +0x24 | 4 | sAnimScript_tag* | firstScript | AnimListBuild 0x122e0 | confirmed |
| +0x38 | 4 | float* | morphWeights (numMorphs floats) | AnimObjectNew 0x11440 `Mem_Malloc(numMorphs*4, malloc_anim_morph_data_buffers)`, zeroed; freed in AnimObjectDelete 0x17a60; written by AnimFacialBlend 0x14c10 `[ESI+0x38]`; read by AnimObjectDraw 0x16ce0 `MOV ECX,[EBP+0x38]` | confirmed |
| +0x50 | 4 | u32 | flags. Bit 0x2 = facial update enabled (AnimObjectUpdate 0x18751 `TEST [EBP+0x50],2`). Bit 0x400 = male (selects the male tables; AnimFacialUpdate 0x17533; also the MP height scale in AnimFrameResolve). Bit 0x10000 = facial update suppressed (0x17514). Bit 0x8000 = apply inverse skin scale | confirmed |
| +0x8a | 2 | s16 | facialTimer: frames until the next blink (AnimFacialUpdate `DEC word [ESI+0x8a]`) | confirmed |
| +0x8f | 1 | u8 | eyeYaw 0..255, 128 = centre (AnimObjectHeadTrack 0x12150) | confirmed |
| +0x90 | 1 | u8 | eyePitch 0..255 (AnimObjectHeadTrack 0x121a3) | confirmed |
| +0x91 | 1 | u8 | facialState. Low 7 bits: 0 = uninitialised, 1 = mood/blink, 2 = speaking. Bit 0x80 = "double blink pending" (females only) | confirmed |
| +0x94..0x97 | 4 x u8 | per-viewport closeness/LOD (AnimState+0xec..0xef); `AnimObjectIsClose(o,n)` = min(...) <= n or ScriptCam | confirmed |

PS2 uses the same fields at different offsets (morphList inline, `field95_0x99` = state, `0x92` = timer,
`0x97`/`0x98` = eye bytes).

### sAnimScript_tag, facial-relevant fields (0xB4 = 180 bytes)

The size comes from `AnimScriptNew 0x159d0: Mem_Malloc(180)` plus a memset of 45 dwords. The brief's
0xC0 is wrong.

| off | size | type | name | evidence | conf |
|---|---|---|---|---|---|
| +0x3c | 4 | u32 | magic 0x159f5ab2 (0x983c4837 after delete) | AnimScriptNew/Delete | confirmed |
| +0x40/+0x44 | 4+4 | prev/next | list links | | confirmed |
| +0x4c | 4 | sAnimSeq_tag* | seq[0] = boneAnimSeq, raw buffer = 28*numBones | AnimScriptNew | confirmed |
| +0x50 | 4 | sAnimSeq_tag* | seq[1] = morphAnimSeq, raw buffer **0x58 = 22 floats**. Allocated only if skin->numMorphs != 0 (`AnimSeqNew(0x58, malloc_anim_morph_data_buffers)`) | AnimScriptNew 0x15ade | confirmed |
| +0x58 | 4 | void* | script file data | AnimScriptInit | confirmed |
| +0x5c | 4 | u8* | "stopped" flag pointer (set to &drone+0x35 by PlayFacialAnim). INVENTED NAME: pStopFlag | 0x3750f | likely |
| +0x60 | 4 | void** | owner back-pointer, zeroed by AnimScriptDelete (drone->facialScript) | AnimScriptDelete `*(script+0x60)=0` | confirmed |
| +0x74 | 4 | u32 | script flags. 0x10000000 = stopped. 0x40000 = first sequence started was a morph seq (the "facial script" marker; AnimListBuild bit 0x800 selects on it). 0x80000 = ignore morph sequences in this script. 0x80000000 = cleared by PlayFacialAnim | AnimProcessScriptCmds case 0, AnimListBuild | confirmed |
| +0x7c | 4 | u32 | **layer key**. 0xE0000000 = mood, 0xE0000001 = blink, 0xE0000002 = speech/cutscene, 0 = normal. AnimListBuild bit 0x200 tests `==`, bit 0x400 tests `&`. Ghidra calls it `maybeMorphPlaybackFlags` | AnimFacialUpdate `MOV [EAX+0x7c],0xe0000000` | confirmed |
| +0x80 | 4 | u32 | timestamp (AnimFrameResolve sorts on it) | AnimScriptInit | confirmed |
| +0x8c | 4 | float | current frame | AnimScriptInit (1.0) | confirmed |
| +0x94 | 4 | float | speed (0.0 = hold the mood; 0.2..0.4 = slow blink) | AnimFacialUpdate `[EAX+0x94]` | confirmed |
| +0xae | 1 | u8 | command count (low byte of file+6) | AnimScriptInit | confirmed |
| +0xb1 | 1 | u8 | end mode; 3 = delete the script when it stops (AnimScriptTick 0x18450) | confirmed |

### sAnimSeq_tag used as the morph sequence (0x90 bytes, AnimSeqNew 0x12ac0)

Fields are set by FUN_00014420 (INVENTED NAME: AnimSeqSetFromFile; PS2 AnimSeqSet 0x1c7bd0) and by
FUN_00017960 (INVENTED NAME: AnimScriptStartSeq):

| off | name | value for a morph seq |
|---|---|---|
| +0x54..+0x60 | header floats (file 0x10..0x1c) | bounding sphere, unused for morph |
| +0x68 | rawData | 22 float weights (**the output**) |
| +0x6c | unpakOpt | decode cache; unused for morph |
| +0x70 | custom tick callback | 0 |
| +0x74 | file pointer | |
| +0x78 | hashcode | 0x04xxxxxx |
| +0x7c | flags | low nibble = file flags (0x8 = morph). 0x20000000 = restart |
| +0x80 | float current frame | = script frame (+0x8c) - (s16)+0x84 + 1 |
| +0x84 | s16 start frame | script frame of the play command |
| +0x86 | s16 min frame | 1 |
| +0x88/+0x8a | s16 max frame / numFrames | |
| +0x8d | skeleton | set to skin+0x2c |
| +0x8e | u8 element count | file byte 0x3a = 22 channels |

### sAnimSkin_tag (0x30 bytes, runtime)

`+0x2a u8 numMorphs`. It comes from skin file byte 0x12. AnimProcessSkinData 0x144c0 reads 3 floats, then
the bytes +0x10 -> +0x28, +0x11 -> +0x29, **+0x12 -> +0x2a**, +0x13 -> +0x2b and +0x14 -> +0x2c (skeleton).
In the dump it is 22 for 05000007, 0d, 2e, 30 and 35, and 0 for every other skin.

### Globals

| addr | name | notes |
|---|---|---|
| 0x163454 | Male_Facial_State_Anims[5] | 060000f8, 060000f9, 060000f5, 060000f6, **060000f3 (blink)** |
| 0x163468 | Female_Facial_State_Anims[5] | 0600036d, 0600036f, 0600036a, 0600036b, **06000370 (blink)** |
| 0x1d6a68 | Morph_Weight[22] float | draw-time scratch (PS2 symbol name) |
| 0x1d777c | Morph_Index[22] u8 | |
| 0x1d7826 | Morph_Count u8 | 0..8 |
| 0x2ae29c | morphStreamIdx[8] u32 | copied by RecurseAndDrawBoxes |
| 0x2ae2bc | morphStreamWeight[8] float | |
| 0x2ff358 (Gfx+0x39c08) | VS constants: 8 weights x 1/32767 | d3dSetStreamSources |
| 0x174b28 | Drone_SfxInfo[0x15a], 32 bytes each | +0 sound id, **+4 facial script hash**, +8 last start frame, +0xc repeat interval |
| 0x1f6678 | ScriptCam | != 0 during in-engine cutscenes |
| 0x17c0f4 | FRAME_RATE_INT | |

## 2. Data formats

### 2.1 Morph sequence (0x04 file, flags bit 3), 31 in the dump

```
0x00 u32 hashcode (runtime: -> track data)   0x04 u32 0 (runtime: size)   0x08 u32 0 (runtime: cursor)
0x0c u32 flags = 0x00000008
0x10 f32[4] bounding sphere of the head (centre, radius) - copied to seq+0x54, unused for morphs
0x20 f32[6] 0
0x38 s16 numFrames          0x3a s16 numTracks = 22 (u8 copied to seq+0x8e = number of weights written)
0x3c u8 skeleton the face was authored on (0 = male heads, 1 = female heads)  0x3d 0  0x3e copy of 0x3c  0x3f 0
0x40 22 tracks, then zero padding to a multiple of 32 bytes
track: u32 w0 (low16 = track byte size incl. w0; high16 = first key word), then keys:
  key word u16 = len:10 (bits 6..15) | mask:6; one f32 coefficient per set mask bit (c0..c5) follows the u32
  that held the key word; key words are packed two per u32 (low half first); keys are walked until
  sum(len) >= numFrames-1.
value(frame) = c0 + c1 t + c2 t^2 + c3 t^3 + c4 t^4 + c5 t^5, with t = clamp((int)frame, 1, numFrames) - 1
               ABSOLUTE (not segment-relative), using the first key whose running sum(len) >= t.
```

This is the same scheme as bone tracks (see spec-seq.md). In AnimFrameCopy 0x13e50 the morph path is
`(seq+0x7c>>3)&1`. It sets element count = seq+0x8e, uses type 6 per element (one track) and writes the
values sequentially into the output via FUN_00013c70 case 6. Quirks:

* The frame is truncated to an integer, so morphs are **not** interpolated between frames. AnimSeqTick
  does not go through AnimFrameSet for flag-8 sequences.
* After decoding, AnimFrameCopy still does `Vec_Mul(out, Glb_InvSkinScale)`. That scales weights 0..2 by
  1/skinScale when animObj flag 0x8000 is set. It is harmless because every skin scale is 1.0.
* AnimFrameCopy also writes the 3-vector bookkeeping at seq+0x18/+0x24.

The dump holds 31 morph sequences, all with 22 channels. Weight range is about -0.4..1.23; the cutscene
curves overshoot 1.

### 2.2 Facial scripts (0x06)

These are ordinary scripts (see spec-script.md). Command type 0 is
`{u16 type|extra<<8, u16 startFrame, u16 endFrame, u16 seqHash&0xffff}`. If the referenced sequence has
header flag 8, `FUN_00017960` puts it in seq slot 1 (morph), otherwise in slot 0 (bone). The dump holds 39
facial scripts:

| script | sequence | frames | meaning |
|---|---|---|---|
| 060000f8 / f9 / f5 / f6 | 0400007c / 7d / 79 / 7a | 2-3 | male mood 0..3. Held at frame 1 with speed 0. 7c is neutral (all 0). 7a has ch10 = 1 (eyes shut) + ch0, used for alertStatus 3 |
| 060000f3 | 04000076 | 10 | male blink: ch10 0->1->0, ch12 0.3 |
| 0600036d / 6f / 6a / 6b | 040002d4 / d6 / d1 / d2 | 2-3 | female mood 0..3 |
| 06000370 | 040002d7 | 12 | female blink (ch10) |
| 060000ef, f0, f1, f2, f4, f7; 0600036c, 36e, 371 | 04000073..78, 7b; 040002d3, d5, d8 | 3-175 | other male/female expressions, not referenced from code (probably level scripts) |
| 06000127..12a | 040000c0, af, b0, aa | 40-90 | short face clips. 06000128 is used by AnimDebugCreate |
| 0600011b, 11d, 11f, 124, 126, 1fb, 996, 997 | body + face (04000020 + 04000021, etc.) | 500-1500 | cutscene performances |
| 0600011c, 11e, 120, 125, 12b, 76b, 99a, 99b | face only (same morph seqs) | | cutscene face-only variants |

**Lip sync.** `Drone_SfxInfo` references 62 distinct facial hashcodes: 060006c4/c7/ca/cc,
060000fa..0600011a, 06000372..0600038e and 06000976..979.

* The level bundles contain all of them except 060006c4/c7/ca/cc and 06000977/978/979, which appear in no
  bundle.
* Each lip-sync script is a 36-225 frame morph sequence plus a **type-1 play-sound command at frame 1**.
  The sound id equals the table's sound id; for example, 060000fa plays sound 0x1ae, which is
  Drone_SfxInfo[80].sound.
* So the facial script starts its own voice line. That explains why NDrone2_SpeechSFX only plays the sound
  directly when there is no facial script.

Over all bundles:

* 213 facial scripts: 68 with a body sequence as well (cutscenes), and 145 morph-only.
* 145 morph sequences:
  * 94 authored on skeleton 0 (male faces) and 51 on skeleton 1 (female faces).
  * 125,504 frames in total.
  * Weight range -1.14..1.23.

### 2.3 Morph targets: Xbox ModelData in level files (0x01), from psiCreateEntityGfx 0xde220

```
+0x00 u32 magic 0x0645584B ('KXE\6')     +0x04 u32 gfx hashcode 0x02xxxxxx (referenced by the skin)
+0x08 u32 vtxCnt   +0x0c u32 idxCnt   +0x10 u32 primCnt   +0x14,+0x18 u32 ?   +0x1c u32 bonePaletteCnt
+0x20 u32 ? (0x2031)   +0x24 u32 morphTargetCnt (22)   +0x28 u32 morphVtxCnt (304..324)
+0x2c char[32] mesh name ("generic_head_grunt", "bond_generic_head_grunt", "polySurface1"...)
+0x4c runtime: vtxBuffers  +0x50 idxBuffer  +0x54 prims*  +0x58 palettes*  +0x5c morph stream base index
+0x60 vertices  vtxCnt * (0x20 skinned | 0x1c) + 3 slack
      indices   idxCnt * u16 + 3 slack      (each section: data first, 3 slack bytes last;
      prims     primCnt * 12                 the loader memmoves the data up to 4-byte alignment)
      palettes  bonePaletteCnt * 0x36
      morph     morphTargetCnt * morphVtxCnt * {s16 dx, dy, dz} + 3 slack   (target-major)
then 0xFF padding to u32, then the next level block header.
prim (12 bytes): u16 tex0, u16 tex1, u32 stripLen-2, u16 stateFlags (bit0 = morph on),
                 u8 changeMask (bit0 = re-evaluate the morph stream binding), u8 bone palette (1-based)
```

The level bundles hold 62 distinct morph-capable meshes (179 copies, since graphics packs are repeated,
and slightly different, per level). All have 22 targets, and morphVtxCnt ranges from about 300 to 330. Names
include generic_head_grunt, bond_generic_head_grunt, Mayhew_tux_generic_head_grunt, castleheavy_b and
polySurfaceN. The 61 skins with numMorphs = 22 each reference one of them as their single rigged body.
In the dump there are 5 heads. In every copy the walk ends exactly at the 0xFF padding, and every
morph-enabled strip only indexes vertices < morphVtxCnt, so the head vertices come first. Deltas are
int16, and the displacement is `delta * weight / 32767` model units. The maximum |delta| is 840, about
0.026 units, or 13% of head height (the jaw).

Per-target analysis of model 020004c2 (head is y-up and z-forward; head bbox y 0.50..0.70):

| ch | region / motion | proposed name (GUESS unless noted) |
|---|---|---|
| 0-9 | mouth area (y~0.55), small mixed motions | 10 mouth shapes / visemes (the cutscene speech curves use exactly these) |
| 10 | eyelids (y 0.625), move down | **blink / eyes closed** (both blink sequences drive it; mood 3 = 1.0) |
| 11 | eye area, +y | **eyes look up** (driven by eyePitch/255; CODE) |
| 12 | eye area, -y | **eyes look down** ((255-eyePitch)/255; CODE) |
| 13 | eye area, -x | **eyes look to one side** ((255-eyeYaw)/255; CODE) |
| 14 | eye area, +x | **eyes look to the other side** (eyeYaw/255; CODE) |
| 15 | brows (y 0.636, front), +y | brows raise |
| 16, 17, 19, 20 | cheeks / mid face | smile, sneer and similar |
| 18 | one brow (x = -0.016), +y | single-brow raise |
| 21 | lower face, -y, largest | jaw open |

`python parse_facial.py --obj outdir` writes the base mesh plus one OBJ per target at weight 1, so the
channels can be named by looking at them.

### 2.4 PS2 difference

PS2 keeps the deltas in a separate level block, id 0x2B (`parsemap_block_morph_data` ->
`celglist->ps2gfx->morphData`). They are applied in `SkinIt(..., pMorphData)` from
`psiDrawSkinObjectMatrix`, which uses the same Morph_Count/Weight/Index globals. Xbox has no 0x2B block;
its deltas are inside the ModelData block.

## 3. Functions

Calling conventions below are Xbox. "stk" means cdecl stack arguments.

| Xbox | PS2 | conv | signature |
|---|---|---|---|
| AnimFacialBlend 0x14c10 | 0x1bed70 | EAX = obj, ESI = animObj, stk: skin; caller cleans | `void AnimFacialBlend(obj_tag*, sAnimObject_tag*, sAnimSkin_tag*)` |
| AnimFacialUpdate 0x17510 | 0x1bea38 | stk: obj, ESI = animObj | `void AnimFacialUpdate(obj_tag*, sAnimObject_tag*)` |
| AnimFacialAppend 0x17160 | 0x1c6c00 | cdecl (obj, hash); **returns the script in EAX** (Ghidra says void) | `sAnimScript_tag* AnimFacialAppend(obj_tag*, HASHCODE)` |
| NDrone2_PlayFacialAnim 0x374e0 | inlined in NDrone2_SpeechSFX 0x142190 | cdecl (DCVars*, hash) | |
| NDrone2_SpeechSFX 0x37530 | 0x142190 | cdecl | calls PlayFacialAnim |
| AnimObjectDraw 0x16c20 | 0x1c4bd0 | cdecl | morph selection |
| RecurseAndDrawBoxes 0xdd4c0 (body runs to 0xdd8d2; Ghidra stops at 0xdd56c) | - | cdecl (geomIdx), EBP = geomIdx | morph stream binding per primitive |
| d3dSetStreamSources 0xe5700 | - | cdecl, 17 arguments | `(int base, int idx0, float w0, ... idx7, w7)` |
| psiCreateEntityGfx 0xde220 | - | cdecl | builds the morph vertex buffers |
| psiSetUpColourBlend 0xdd930 | 0x121b58 | cdecl (int) | **unrelated**: just `d3dSetupRenderStatesAndFog(p == 0)` (sky/sprites/Game_Draw) |
| psiBuildMatrixPalette 0xdd290 / psiDrawSkinObjectMatrix 0xe0960 | | | bone skinning only; no morph handling on Xbox |

### 3.1 AnimFacialBlend (KEY), called from AnimFrameResolve 0x16915

Call site: `if (skin->numMorphs && (ScriptCam || AnimObjectIsClose(obj, 0xc)))`, inlined as
min(AnimState+0xec..0xef) <= 12.

```c
void AnimFacialBlend(obj_tag *obj /*EAX*/, u8 *animObj /*ESI*/, u8 *skin /*stack*/)
{
    sAnimScript *mood  = AnimListBuild(obj, 0x200, 0xE0000000, NULL, 1);   // first script with +0x7c == key
    sAnimScript *blink = AnimListBuild(obj, 0x200, 0xE0000001, NULL, 1);
    sAnimScript *talk  = AnimListBuild(obj, 0x200, 0xE0000002, NULL, 1);
    u32 n = skin[0x2a];                                   // numMorphs (22)
    float *w = *(float**)(animObj + 0x38);
    #define SRC(s) (*(float**)(*(u8**)((u8*)(s) + 0x50) + 0x68))   // script->morphAnimSeq->rawData
    if (ScriptCam == 0) {
        if (mood)  for (i = 0; i < n; i++) w[i] = SRC(mood)[i];                    // plain copy
        if (blink) for (i = 0; i < n; i++) if (fabsf(SRC(blink)[i]) > 0.01f) w[i] = SRC(blink)[i];
        if (talk)  for (i = 0; i < n; i++) if (fabsf(SRC(talk)[i])  > 0.01f) w[i] = SRC(talk)[i];
        float f;
        f = animObj[0x8f] * (1/255.f);          if (f > 0.01f) w[14] = f;   // +0x38
        f = (255 - animObj[0x8f]) * (1/255.f);  if (f > 0.01f) w[13] = f;   // +0x34
        f = animObj[0x90] * (1/255.f);          if (f > 0.01f) w[11] = f;   // +0x2c
        f = (255 - animObj[0x90]) * (1/255.f);  if (f > 0.01f) w[12] = f;   // +0x30
    } else if (talk) {
        for (i = 0; i < n; i++) w[i] = SRC(talk)[i];                       // cutscene: plain copy
    }
}
```

Notes:

* The constant is 0.003921569 = 1/255.
* The compiler unrolled the loops by 4; the behaviour is identical.
* Overlay semantics are "override if |w| > 0.01", not additive.
* Weights are never cleared. If no mood script exists, the previous frame's values persist.
* The eye channels are overwritten every frame, even when the bytes are 0. Byte 0 gives w13 = 1.0 and
  w14 is left alone (0 is not > 0.01).
* **Possible bugs:**
  * The fixed indices 11..14 are written even if numMorphs < 15; that would overflow the buffer, though it
    never happens with this data.
  * If numMorphs > 22, the rawData reads run out of bounds.

### 3.2 AnimFacialUpdate (KEY), called from AnimObjectUpdate 0x18769

Call site: `if (skin->numMorphs && (animObj->flags(+0x50) & 2) && AnimObjectIsClose(obj, 0xc))`.

```c
void AnimFacialUpdate(obj_tag *obj /*stack*/, u8 *ao /*ESI*/)
{
    u32 fl = *(u32*)(ao + 0x50);
    if ((fl & 0x10000) || ScriptCam) return;
    bool male = (fl >> 10) & 1;
    HASHCODE *tab = male ? (HASHCODE*)0x163454 : (HASHCODE*)0x163468;   // [0..3] moods, [4] blink
    sAnimScript *s;
    switch (ao[0x91] & 0x7f) {
    case 0:                                                   // first time: base mood 0
        s = AnimScriptAppend(obj, tab[0]);                    // (no NULL check - see bugs)
        s[0xb1] = 3;  *(float*)(s + 0x94) = 0.0f;  *(u32*)(s + 0x7c) = 0xE0000000;
        ao[0x91] = 1;
        *(s16*)(ao + 0x8a) = Rand_Rand(FRAME_RATE_INT*3) + (FRAME_RATE_INT*3 >> 1);   // 1.5..4.5 s
        break;
    case 1: {
        bool alert = true;
        u8 *as = Drone_AlertStatus(obj);                      // &drone[0x418] if a drone, else NULL
        if (as) {                                             // as[0] alertStatus 0..3, as[2] mood-changed flag
            if (as[2]) {                                      // mood changed: replace all facial scripts
                as[2] = 0;
                AnimListBuild(obj, 0x800, 0, NULL, 1);        // every script with +0x74 & 0x40000
                AnimListDelete(obj);
                s = AnimScriptAppend(obj, tab[as[0]]);
                s[0xb1] = 3; *(float*)(s + 0x94) = 0.0f; *(u32*)(s + 0x7c) = 0xE0000000;
                return;
            }
            alert = as[0] != 0;
            if (as[0] == 3) return;                           // status 3 (eyes-shut mood): no blinking
        }
        if (--*(s16*)(ao + 0x8a) >= 0) return;
        *(s16*)(ao + 0x8a) = alert ? Rand_Rand(FRAME_RATE_INT*2) + FRAME_RATE_INT       // 1..3 s
                                   : Rand_Rand(FRAME_RATE_INT*2) + FRAME_RATE_INT*2;    // 2..4 s
        s = AnimScriptAppend(obj, tab[4]);                    // blink
        s[0xb1] = 3; *(u32*)(s + 0x7c) = 0xE0000001;          // speed stays 1.0 from AnimScriptInit
        if (male) return;
        if (ao[0x91] & 0x80) { ao[0x91] &= 0x7f; return; }    // second blink of a double blink done
        u32 r = Rand_Rand(100);
        if (r <= 50) return;
        if (r <= 75) {                                        // schedule a quick second blink
            ao[0x91] |= 0x80;
            *(s16*)(ao + 0x8a) = Rand_Rand(FRAME_RATE_INT/10) + FRAME_RATE_INT/10;
        } else
            *(float*)(s + 0x94) = Float_FRand(0.2f) + 0.2f;  // slow blink (speed 0.2..0.4)
        break; }
    case 2:                                                   // speaking
        if (AnimListBuild(obj, 0x200, 0xE0000002, NULL, 1) == NULL) {   // speech script finished
            ao[0x91] = 1;
            u8 *as = Drone_AlertStatus(obj);
            if (as) as[2] = 1;                                // re-apply the mood next update
        }
        break;
    }
}
```

Supporting details:

* Rand_Rand is 0xd5310 and returns [0,n). Float_FRand is 0xd5360. Drone_AlertStatus is 0x31d70:
  `obj->objectType == DRONE ? extra + 0x418 : 0`.
* Drone_tag+0x418 is alertStatus, +0x419 is the last status, and +0x41a is the facial-refresh flag.
* The PS2 body is identical.

### 3.3 AnimFacialAppend (KEY)

```c
sAnimScript *AnimFacialAppend(obj_tag *obj, HASHCODE h)
{
    u8 *ao = (u8*)obj->animState /* obj+0xb8 */ + 0x58;
    sAnimScript *s = AnimScriptAppend(obj, h);      // NULL if 32 scripts already or no animState -> crash
    s[0xb1] = 3;                                    // delete when stopped
    *(u32*)(s + 0x7c) = 0xE0000002;                 // speech layer
    ao[0x91] = 2;                                   // facialState = speaking (AnimState+0xe9)
    return s;                                       // EAX
}
```

### 3.4 NDrone2_PlayFacialAnim 0x374e0, and its caller

```c
void NDrone2_PlayFacialAnim(DCVars *dc, HASHCODE h)
{
    u8 *drone = dc->drone;                       // dc+4 ; dc+0 = gameObj
    sAnimScript *s = AnimFacialAppend(dc->gameObj, h);
    *(sAnimScript**)(drone + 0x438) = s;         // drone->facialScript
    if (s) {
        drone[0x35] = 0; drone[0x36] = 1;        // 0x35 = "stopped" flag, 0x36 = speaking-face busy
        *(u8**)(s + 0x5c) = drone + 0x35;
        *(void**)(s + 0x60) = drone + 0x438;     // cleared on delete
        *(u32*)(s + 0x74) &= 0x7fffffff;
        s[0xb1] = 3;
    }
    drone[0x37] = 0;
}
```

The caller is NDrone2_SpeechSFX 0x37530 (dc, sfxNum, force, withFace, faceOnly):

* It is rate-limited by the game clock and by `Drone_SfxInfo[sfx]`: +8 is the last start and +0xc is the
  interval.
* If `withFace` and the entry has a facial hash (checked with hashtable_getitem, the inlined
  AnimScriptPeekSfx; the hash is zeroed if the file is missing), and the drone is not busy (+0x36, +0x37),
  it calls PlayFacialAnim. If a face started, it stamps +8.
* The sound itself is played only when the entry has no facial hash, so the facial script is expected to
  trigger its own sound through a script command.
* NDrone2_ControlSTANDARD 0x3f5f0 clears drone+0x36 once the script is stopped, deleted, or +0x35 is set.
* On PS2, ControlSTANDARD also had a delayed facial start (drone+0x37 / +0x4c); on Xbox it is gone,
  because +0x37 is always cleared.

### 3.5 How morph weights are produced each frame

1. **AnimObjectUpdate 0x18690:** for every script and for both seq slots (+0x4c, +0x50), if seq+0x78
   and script data are set, it calls AnimSeqTick 0x172b0 (EAX = script).
2. **AnimSeqTick:** sets seq+0x80 = script+0x8c - (s16)seq+0x84 + 1. If `seq+0x7c & 8`, and only if
   AnimObjectIsClose(obj, 12): `AnimFrameCopy(seq->rawData, seq, NULL, seq+0x80)`, then return. The
   bone-delta code is skipped.
3. **Starting a morph sequence:** AnimProcessScriptCmds 0x17c50, command 0, sees
   `(file+0xc >> 3) & 1` and calls FUN_00017960 with slot = 1. Scripts with +0x74 & 0x80000 skip
   morph sequences. The first sequence started sets +0x74 |= 0x40000 if it is a morph sequence.
4. **Blending:** AnimFrameResolve -> AnimFacialBlend copies layers into animObj+0x38, as in 3.1.

### 3.6 Rendering path

**AnimObjectDraw 0x16c20** (after psiBuildMatrixPalette):

```c
Morph_Count = 0;
if (AnimObjectIsClose(obj, 0xc) && (n = skin[0x2a]) != 0) {
    for (i = 0; i < n; i++) { Morph_Weight[i] = w[i]; Morph_Index[i] = i; }
    k = min(n, 8);
    for (i = 0; i < k; i++)                       // partial selection sort, descending (signed compare)
        for (j = i+1; j < n; j++) if (Morph_Weight[i] < Morph_Weight[j]) swap(i, j);
    Morph_Count = k;
    while (Morph_Count && fabsf(Morph_Weight[Morph_Count-1]) < 1e-6f) Morph_Count--;   // trim zeros
    // then selection-sort the chosen Morph_Count entries by ascending Morph_Index (keeps stream order stable)
}
for each rigged glist: psiDrawSkinObjectMatrix(...)  ->  ... -> RecurseAndDrawBoxes(geomIdx)
Morph_Count = 0;
```

Negative weights are ranked below zero, so a strongly negative weight can be dropped.

**RecurseAndDrawBoxes 0xdd4c0**, for geometry g = d3dGeometryObjs[idx] (0x2a0e68):

```c
if (g->morphStreamBase /*+0x5c*/) {
    for (i = 0; i < Morph_Count; i++) { streamW[i] = Morph_Weight[i]; streamIdx[i] = Morph_Index[i]; }
    for (; i < 8; i++) streamW[i] = 0, streamIdx[i] = 0;
}
... bind vertex/index buffers ...
for each prim p (12 bytes at g+0x54, count g+0x10):
    bone palette p[0xb] -> d3dSetSkinMatrix per bone ...
    if (p[0xa] & 1)                               // state change: morph binding
        if (p[8] & 1) d3dSetStreamSources(g+0x5c base, streamIdx[0], streamW[0], ..., streamIdx[7], streamW[7]);
        else          d3dSetStreamSources(0, 0...);          // unbind
    draw strip (p[4] + 2 indices)
if (g+0x5c) d3dSetStreamSources(0, ...);                      // unbind at end
```

**d3dSetStreamSources 0xe5700**:

```c
if (base == 0) { flags &= ~1; SetStreamSource(1..8, NULL, 6); return; }
consts[i] = w_i * 3.051851e-05f (1/32767), i = 0..7;              // Gfx+0x39c08
D3DDevice_SetVertexShaderConstantNotInline(0x73, consts, 8);      // ECX, EDX, stack; count in DWORDs -> c115,c116 hw (D3D c19,c20)
flags |= 1;
SetStreamSource(1 + i, d3dstreamDataPtr[(base + idx_i) * 9], 6);  // stride 6 = SHORT3 deltas
```

**psiCreateEntityGfx 0xde220**:
`g+0x5c = d3dCreateVertexBuffers(morphVtxCnt, morphData, 0, morphTargetCnt)`. This registers
morphTargetCnt consecutive stream slots, each `morphVtxCnt*6` bytes. Stream slot `base + t` is target t.

The repo already reimplements d3dSetStreamSources in `src/action/engine/Direct3D/d3dSeam.cpp:1497`, but
its parameter names ("streamNOffset" and "streamNStride") are misleading. The float arguments are **morph
weights** and the int arguments are **morph target indices**. `Gfx_StreamStrideConstants` holds weights.

## 4. Parser

`parse_facial.py [dumpdir | --bundles [lvldir]] [--weights 0400xxxx | --seq h --csv f] [--obj dir]`

With `--bundles` it walks every decompressed level bundle `lvl/07*.bin` using the directory format in
spec-load.md. The full output is in `facial/bundles_report.txt`; the dump output is in
`facial/dump_report.txt`. Results over all 32 level bundles:

* **Morph sequences:** 145/145 parse cleanly.
* **Facial scripts:** 213.
* **Morph-target ModelData copies:** 179/179 validate.
* **Skins with numMorphs = 22:** 61/61 linked.
* **Same-hash content conflicts:** none outside the 01 graphics packs. Every copy of those packs is
  validated.

On the dump in Q:\nightfire-cxbx\build\windows\Release\dump:

* **Morph sequences:** 31/31 parse cleanly. Every track's walked size equals its size word, the padding
  is zero, the size is a multiple of 32 bytes, and the header is as described.
* **Facial scripts:** 39 found (all command lists walk cleanly).
* **Morph-target blocks:** 5/5 validate (section walk ends at 0xFF pad, indices in range, and morph strips
  only use the first morphVtxCnt vertices).
* **Skins with numMorphs = 22:** 5/5, each linked to its model.

## 5. Open questions

1. The vertex shader microcode that consumes c19/c20 and v1..v8 was not located. `pos += sum delta * c`
   is the only interpretation consistent with the 1/32767 scale, and the delta magnitudes look right. Are
   normals morphed? Probably not: only position streams are bound.
2. What ModelData +0x14, +0x18 and +0x20 (0x2031) mean belongs to whoever covers the glist format.
3. The channel names are guesses from geometry, except for 10 to 14. Check them with `--obj`.
4. Who writes Drone+0x418 alertStatus and +0x41a, and does status 3 really mean "unconscious"? The mood-3
   face has its eyes shut and blinking is disabled for it.
5. The unreferenced dumped expressions (060000ef-f2, f4, f7, 0600036c/36e/371) are probably started from
   level/cutscene scripts or AI code through the hashcode. None of them is referenced in code.
   Drone_SfxInfo references 060006c4/c7/ca/cc and 06000977/978/979, but they are in no level bundle. Those
   lines play without a face, or not at all: the inlined AnimScriptPeekSfx zeroes the hash when the file is
   missing, and the plain sound is then played.
7. Quaternion order does not matter for facial animation (the lead notes it is XYZW, not WXYZ).
6. **Bugs in the original:**
   * AnimFacialAppend and AnimFacialUpdate do not NULL-check AnimScriptAppend. It returns NULL once 32
     scripts are attached.
   * AnimFacialBlend has the fixed-index and out-of-bounds risks noted in 3.1.
   * A seq file with > 22 channels would overflow the 0x58 buffer.
