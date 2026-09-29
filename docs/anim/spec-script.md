# Animation scripts, sets and lists - spec (Xbox `default.xbe`, PS2 names)

Scope: the playback layer. A **script** (`sAnimScript_tag`, file type 0x06) is a timeline in 60 Hz "script
frames" that starts sequences (0x04 files) on its bone/morph channels, fires sounds and events, and is advanced
per tick by `AnimScriptTick`. Several scripts may be alive on one object (a doubly linked list off
`sAnimObject_tag.firstScript`); `AnimFrameResolve` cross-fades them. An **AnimSet** (INVENTED tag name
`sAnimSet_tag`, PS2 decompiler already uses it) is a locomotion controller that picks walk/run/strafe scripts from a
hashcode table by speed. A **list** is a global scratch array of up to 32 script pointers filled by
`AnimListBuild` with a filter mask.

Tools used: Ghidra decompile + disassembly (Xbox and PS2), all read-only. Parser: `parse_script.py` in this folder.
Everything below is Xbox offsets unless marked PS2. "confirmed" = read from Xbox instructions.

---------------------------------------------------------------------------------------------------------------

## 1. The 0x06 script file format (byte level)

All little-endian. Files are zero-padded to a multiple of 32 bytes (32/64/96/128/160 in the dump).

| off | size | field | notes / evidence |
|---|---|---|---|
| +0 | u32 | hashcode (0x06xxxxxx) | = filename; AnimScriptPeekSeq 0x125b0 checks top byte == 6 |
| +4 | s16 | numFrames | AnimScriptInit 0x12695 `MOV DX,[EAX+4]; MOV [ESI+0xac],DX`. Script frames run 1..numFrames |
| +6 | u16 | info | bits 0-8 = numCmds (AnimProcessScriptData `&0x1ff`; but Xbox AnimScriptInit 0x126b9 copies only the LOW BYTE to script+0xae); bits 9-14 = rate (`>>9 & 0x3f` -> script+0xaf; always 60 in the dump; never read by anim code); bit 15 = loop (0x126a4 `TEST AH,AH; JNS` -> flags `|= 0x80000000`) |
| +8 | ... | command stream | numCmds commands, packed, each u16-aligned |
| ... | | zero padding | to 32-byte multiple |

Every command starts with a u16 `op | (len << 8)`; `len` = number of *extra* operand words after the fixed ones.
Stride in words (AnimProcessScriptData 0x110c0, AnimProcessScriptCmds 0x17c50 jump table 0x18414):

| op | name (INVENTED) | layout (u16 words after opcode word) | stride (words, incl. opcode) | used in dump |
|---|---|---|---|---|
| 0 | SEQ | `start, end, seqId` [+len extras, ignored] | 4+len | 592 (all len 0) |
| 1 | SFX | `frame, sfxId` [+len] | 3+len | 112 (len 0) |
| 2 | NOP | `w1, w2` [+len] | 3+len | 0 |
| 4 | EVENT | `frame, type` [, param (only read by type 2 and 3; present when len>=1)] | 3+len | 974 (529 len 0, 445 len 1) |
| 3, >=5 | - | - | Xbox interpreter: `len` words (!); AnimProcessScriptData: len+3 (op 3) / len+1 (op>=5) | 0 |

Example `06000092.bin`: `92000006 | 0817 | 0178 | 0000 0100 0817 0900 | 00...`:
numFrames = 0x1708 = 5896, info 0x7801 -> 1 cmd, rate 60, no loop; cmd0 = SEQ frames 1..5896 seq 0x04000009.

### Opcode semantics (AnimProcessScriptCmds 0x17c50)
* **SEQ**: when the frame span traversed this tick overlaps `[start,end]` and the command's fired bit is clear:
  `raw = hashtable_getitem(0x04000000|seqId)` (missing -> return, abort remaining commands);
  `isMorph = (raw+0xc) >> 3 & 1` (bit 3 of the sequence file's u32 at +0xc = facial/morph sequence);
  if isMorph and script flag 0x80000 -> skip; else start it on channel isMorph (0 = bone seq script+0x4c,
  1 = morph seq script+0x50) at sequence frame `(forward ? to : from) - start + 1` via AnimSeqSet, and record
  `start` in seq+0x84 (u16). A sequence therefore plays script frames start..end mapped to its own frames
  1..(end-start+1); seq time is recomputed every tick from the script frame (see AnimSeqTick, section 4).
* **SFX**: at `frame`, unless muted (animObj+0x50 bit 18) -> weapon-dependent substitution on the parent's
  `animState->currentWeaponId` (sfx 1 -> 2 for weapon 7/9; 0x403 -> 2 for 3/5; 0xfa -> 0x1fb for 0x14;
  0x106 -> 0x284 for 0x1e..0x23; 0x1e1 -> 0x602 for 0x10; 4 -> 0x604 for 0x16/0x17), position = obj->position
  (transformed by parent matrix + parent position if parented; first-person weapon on the player uses bone 0 or
  +1.5 z), `Sound_Play3D(sfx, &pos, 100.0, 100.0, 0.0, Rand_Rand(500)-250, 0, 0)`. (PS2 also SFXPreCache's sounds
  <=0x32 frames ahead in flag-0x10000 mode; Xbox dropped that.)
* **EVENT** `type` (also muted by animObj+0x50 bit 18):

| type | action | count in dump |
|---|---|---|
| 0 | `ScriptFootstepFoot ^= 1`, then footstep type (below) | 0 |
| 1 | `parent->extraObjectData[+0x8f4] ^= 1` (PS2 +0x964) if parented | 2 |
| 2 | `script->onEvent(obj, animObj, script, param)` (script+0x6c) | 443 |
| 3 | `Sound_StopAllWithId(param)` | 2 |
| 4 | `ScriptFootstepFoot = 0` + footstep type | 246 |
| 5 | `ScriptFootstepFoot = 1` + footstep type | 240 |
| 6 | `Effect_Create(obj->animState, obj, 0xc0, 1.0)` | 41 |
| 7 | fire weapon: AnimDatumGetWeaponInfo, AnimGetBoneWorldTrans(datum,1), dir = mtx*(0,0,1), Bullet_init, muzzle AnimDatumSetEntity + Light_Create (+ Gas_Init smoke unless weapon flag 0x2000) | 0 |
| other | nothing | 0 |

  Footstep type: `ScriptFootstepType = (script->hashcode == 0x6000099 || == 0x60000ae) ? 1 : 2`
  (AnimObjectUpdate turns it into an Effect_Create footstep). Event-2 params seen: 0,1,2,6,7,10,11,12,14,15,16,18.
  For drones the callback is `DroneAnim_EventFunc` 0x33590: 1/9 throw grenade, 2 change weapon, 3/4 swap datum
  0x28/100 entity 0x2000312, 5/10 melee impact 1, 11 melee impact 2, 6 SM msg 0x20, 7 clear flag, 14 SM msg 0xb,
  15 drone+0x20=1, 16 drone+0x20=0,+0x21=0, 18 drone+0x20=+0x21=1.
* **NOP**: advances, does nothing (but still rewrites its fired bit from the previous command's result - harmless).

### Fired mask
Commands are walked in file order with a counter `k = numCmds-1 .. 0`; command i uses bit `1 << (numCmds-1-i)` of
script+0x78. After each command the bit is set if its span test was true, else cleared. A command acts only when
its test is true AND its bit was clear -> each command fires once on entering its window and re-arms after the
window has been left. More than 32 commands would alias bits (max in dump: 20).

### Parser results (`parse_script.py`, run on all files in `build\windows\Release\dump`)
* **582/582 files parse cleanly** - header, every command, zero padding all accounted for; hashcode == filename.
* ops seen: SEQ 592, SFX 112, EVENT 974; no NOP/op3/op>=5; rate field always 60; loop bit set in 59 files.
* 582 distinct sequences referenced, **all present** as 04*.bin; every 04*.bin in the dump is referenced.
* 513 scripts satisfy AnimScriptPeekSeq's shortcut (byte 7 == 0x78 and cmd0 is SEQ).
* 3 data warnings (060000e4: SEQ 1..664 vs numFrames 663; 06000996/06000997: morph SEQ 1..1500 vs 1401): a SEQ
  range ends past the script's numFrames - harmless, the script clamps at numFrames.
* Scripts commonly pair a bone SEQ and a morph/facial SEQ over the same range (cutscene lines).
* `python parse_script.py -v` prints the full disassembly; `script_work/all_scripts.txt` holds a copy.

---------------------------------------------------------------------------------------------------------------

## 2. `sAnimScript_tag` layout (Xbox 0xB4 = 180 bytes; PS2 0xC0)

Size: AnimScriptNew 0x159d0 `Mem_Malloc(180, malloc_anim_script_structs)` + 45-dword memset (confirmed). The
old "0xC0" is the PS2 size (PS2 `Mem_Malloc(0xc0)`); PS2 offsets = Xbox + 4 (PS2 matrix is 64 bytes) - e.g. magic
Xbox +0x3c / PS2 +0x40, numFrames +0xac / +0xb0, speedMode +0xb0 / +0xb4.

| off | size | type | name (INVENTED unless noted) | conf. | evidence |
|---|---|---|---|---|---|
| 0x00 | 0x3c | _MATRIX | rotMtx - object rotation captured for anim-rotation mode | confirmed | AnimObjectUpdate `Mat_CopyRot(&obj->mtx,(_MATRIX*)script)`; AnimFrameResolve uses script as matrix for ApplyMatrixLV when flag 0x20000 |
| 0x3c | 4 | u32 | magic: 0x159f5ab2 alive, 0x983c4837 deleted (magicStateIndicator) | confirmed | AnimScriptNew, AnimScriptDelete |
| 0x40 | 4 | sAnimScript_tag* | prev | confirmed | AnimScriptNew/Delete, ListBuild bit 4 |
| 0x44 | 4 | sAnimScript_tag* | next | confirmed | same, AnimObjectUpdate loop |
| 0x48 | 4 | sAnimScript_tag* | syncMaster - script this one follows (speed mode 2) / blends into | confirmed | AnimSetUpdate sets B+0x48=A; Tick case 2 reads it; ListBuild 0x40/0x80/0x100; AnimFrameResolve blends +0x48's data |
| 0x4c | 4 | sAnimSeq_tag* | boneAnimSeq (channel 0) raw buffer 28*numBones | confirmed | AnimScriptNew |
| 0x50 | 4 | sAnimSeq_tag* | morphAnimSeq (channel 1), 0x58 bytes, only if skin has morphs | confirmed | AnimScriptNew |
| 0x54 | 4 | void* | boneMask (passed as 4th arg of AnimFrameBlend; nonzero -> list bit 0x2000 "partial/overlay") | likely | AnimFrameResolve 0x16120, ListBuild 0x124a0; Init zeroes |
| 0x58 | 4 | u8* | scriptData (the 0x06 file) | confirmed | Init |
| 0x5c | 4 | u8* | doneFlag - `*doneFlag = 1` then cleared when the script stops | confirmed | AnimScriptEnd 0x12913 |
| 0x60 | 4 | sAnimScript_tag** | ownerSlot - owner's pointer to this script; `*ownerSlot = 0` on delete (unless it is the pointer passed to Delete) | confirmed | Delete, AnimSetUpdate, DroneAnim_SetHTAnim |
| 0x64 | 4 | fn(obj,animObj,script) | onDelete | confirmed | Delete |
| 0x68 | 4 | fn(obj,animObj,script) | onStop (called once) | confirmed | End, AnimScriptCallOnStop 0x12870 |
| 0x6c | 4 | fn(obj,animObj,script,u16 param) | onEvent | confirmed | ProcessScriptCmds event 2, AnimScriptCallOnEvent 0x12860 |
| 0x70 | 4 | HASHCODE | hashcode | confirmed | Init |
| 0x74 | 4 | u32 | flags (table below) | confirmed | |
| 0x78 | 4 | u32 | firedMask (per command) | confirmed | ProcessScriptCmds |
| 0x7c | 4 | u32 | groupTag - AnimSet tag, or 0xe0000002 = cutscene-camera script (Init, when ScriptCam && skin has morphs) | confirmed | ListBuild 0x200/0x400; AnimSetUpdate |
| 0x80 | 4 | u32 | timestamp (AnimScriptTimeStamp 0x1d7570, never 0) - creation order for blending | confirmed | Init; AnimFrameResolve sort |
| 0x84 | 4 | float | syncBlend - weight for blending this (syncMaster'ed) script into its master (AnimSet: walk->run fraction) | confirmed | AnimSetUpdate 0x84 write, AnimFrameResolve 0x16124 |
| 0x88 | 4 | float | targetPos - normalised position for speed mode 3 | confirmed | Tick case 3 |
| 0x8c | 4 | float | curFrame (1..numFrames) | confirmed | End writes; AnimSeqTick reads |
| 0x90 | 4 | float | prevFrame (= curFrame after each tick) | confirmed | AnimObjectUpdate `+0x90 = +0x8c` after Tick |
| 0x94 | 4 | float | speed (script frames per 60 Hz frame; sign = direction) | confirmed | Init, End, SetPlayDirection, AddSpeed |
| 0x98 | 4 | float | weight = fadeTime/fadeLen (0..1) | confirmed | AnimFrameResolve |
| 0x9c | 4 | float | rotBaseYaw (8.0 = "capture next tick" sentinel) | confirmed | SetAnimRotation 0x12aa5, AnimObjectUpdate |
| 0xa0 | 4 | float | rotLastYaw | confirmed | AnimObjectUpdate |
| 0xa4 | 4 | float | fadeTime (blend counter) / overlay weight for flag-0x4000 scripts | confirmed | Init=1.0, AppendBlend=8.0, AnimFrameResolve |
| 0xa8 | 4 | float | fadeLen (blend duration in 60 Hz frames) | confirmed | Init=1.0, AppendBlend=8.0 (drones override) |
| 0xac | 2 | s16 | numFrames | confirmed | Init |
| 0xae | 1 | u8 | numCmds (low byte only) | confirmed | Init, ProcessScriptCmds 0x17c93 |
| 0xaf | 1 | u8 | rate (60) - unused by anim code | confirmed | Init |
| 0xb0 | 1 | u8 | speedMode 0 normal, 1 distance-driven, 2 phase-sync to syncMaster, 3 seek to targetPos | confirmed | Tick jump table 0x1867c |
| 0xb1 | 1 | u8 | playMode 0 play once and hold, 1 loop, 2 ping-pong, 3 once then auto-delete | confirmed | End jump table 0x12a10, Tick delete |
| 0xb2 | 1 | s8 | fadeDir +1 fade in, -1 fade out, 0 hold | confirmed | Init=1, AppendBlend=0xff, AnimFrameResolve |
| 0xb3 | 1 | - | pad | - | |

Init writes (0x12620): +0x58,+0x70,+0x78=0,+0x74,+0xac,+0xae,+0xaf,+0x8c,+0x94,+0x90,+0xb0=0,+0xb1=0,+0x64/60/68/5c/6c
=0,+0x80,+0x7c,+0xa8=+0xa4=1.0,+0xb2=1,+0x84=+0x88=0,+0x54=0. It does **not** touch 0x00-0x3b,+0x48,+0x98-+0xa0.

### Script flags (+0x74)
| bit | meaning | set / used by |
|---|---|---|
| 0x00008000 | share root rotation with neighbour in blend (copy first quaternion instead of blending) | AnimFrameResolve 0x16430ff; drones set it |
| 0x00004000 | overlay script (AnimSet strafe) - blended on top with weight +0xa4, excluded from fading | ListBuild 0x4000; AnimSetUpdate |
| 0x00010000 | step mode: no inter-frame interpolation (frac = 0), long seqs (>0x100 frames) get an unpack cache (seq flag 0x200000) | AnimSeqTick, ProcessScriptCmds, AnimFacialUpdate |
| 0x00020000 | anim rotation: root yaw of the anim rotates the object (AnimScriptSetAnimRotation) | AnimObjectUpdate |
| 0x00040000 | first SEQ started this pass was a morph seq ("facial-only script"); excluded from blending lists (0x800) | ProcessScriptCmds |
| 0x00080000 | do not start morph SEQs | ProcessScriptCmds |
| 0x00100000 | blend-out requested | AppendBlend / AnimSetUpdate |
| 0x00200000 | fade finished (fadeTime reached 0 while fading out) | AnimFrameResolve |
| 0x00400000 | fading out (guard so blend-out is not restarted) | AppendBlend (0x500000 = 0x400000/0x100000) |
| 0x00800000 | ticked at least once | Tick; AnimGetFirstTrans |
| 0x01000000 / 0x02000000 / 0x04000000 | zero root-motion delta X / Y / Z | AnimSeqTick -> seq 0x2000000/0x4000000/0x8000000 |
| 0x08000000 | preserved across Init; set by AnimSet/drones; not read in the anim module (game-side meaning) | |
| 0x10000000 | stopped | End; Tick auto-delete; ListBuild 0x10; IsStopped |
| 0x20000000 | just started (first tick: no advance) - set by Init, cleared by AnimSeqTick | |
| 0x40000000 | wrapped this tick (loop) | End; cleared at Tick start; copied to seq |
| 0x80000000 | loop (from file bit 15 or set by code); End forces playMode 1 when set | |

---------------------------------------------------------------------------------------------------------------

## 3. Globals

| addr | name | use |
|---|---|---|
| 0x1d77a0 | GlbAnimList (count) | AnimListBuild |
| 0x1d77a4 | list[32] of sAnimScript_tag* (PS2 DAT_002c7fac) | AnimListBuild; first entry returned |
| 0x1d7570 | AnimScriptTimeStamp | Init (skips 0) |
| 0x1d6a60 | AnimTagTimeStamp | AnimTagAllocAndGetUnique, AnimSetAppend |
| 0x1d7828 | ReverseFlag (u8) | AnimScriptAddReverse sets, Init consumes |
| 0x1d6a64 | Glb_bScriptDeleted (u8) | Delete sets when deleting Glb_pCurrentScript; Tick/End abort |
| Glb_pCurrentScript | script being ticked | AnimObjectUpdate |
| 0x17c100 | FRAME_RATE_MUL = 60/gameFrameRate | GS_SetRefreshRate 0x6b080; Tick mode 0, fades |
| 0x1f6678 | ScriptCam | Init (cutscene tag) |
| ScriptFootstepFoot/Type | footstep side / type | events 0,4,5 |
| 0x1d6ac4 / 0x1d6af0 | distance-table cache count / {hash, table}[32] | AnimDistanceTableGet (FUN_000174a0) |

---------------------------------------------------------------------------------------------------------------

## 4. Time and playback

* **Units**: script frames are 60 Hz frames. Speed mode 0 advances `speed * FRAME_RATE_MUL` per game tick, so a
  30 Hz game advances 2 frames per tick at speed 1. Frames are floats in [1, numFrames]; commands compare
  truncated ints (`__ftol2`).
* **Per tick** (AnimObjectUpdate 0x18690): for each script with data and not stopped: `Glb_bScriptDeleted=0;
  Glb_pCurrentScript=s; AnimScriptTick(animObj, obj, s); s->prevFrame = s->curFrame;` Then for each script, each
  channel seq with data and seq+0x78 != 0: `AnimSeqTick` (below), then anim-rotation, then `AnimFrameResolve`.
* **AnimSeqTick** (0x172b0, EAX=script, ECX=seq, EDX=obj, EBX=animObj; PS2 0x1c3008): copies script flags to seq
  flags (0x10000000 stopped, 0x80000000 loop, 0x20000000 just started [and clears it on the script],
  0x40000000 wrapped, 0x1000000/0x2000000/0x4000000 -> seq 0x2000000/0x4000000/0x8000000 axis locks);
  **seq time `seq+0x80 = script.curFrame - seq.startFrame(u16 +0x84) + 1.0`**; then if seq callback (+0x70) ->
  call it (AnimSet_WalkUpdater = AnimFrameSet with frac(seq+0x80)); else if morph seq (flag 8) -> AnimFrameCopy if
  AnimObjectIsClose(obj,0xc); else `AnimFrameSet(obj, frac)` with frac = fractional part of seq+0x80, or 0 when
  script flag 0x10000. Root motion: delta = rawData.rootTrans - seq.lastRoot (seq+0x0), or previous delta
  (seq+0xc) if seq flags 0x60000000 (just started / wrapped); axis locks zero components; seq+0xc = delta,
  seq+0x0 = root, root in rawData zeroed. (Sampling itself: spec-seq.md.)
* **Blending over time** (in AnimFrameResolve 0x160a0, see 5.6): each non-overlay, non-morph script has
  `fadeTime += FRAME_RATE_MUL * sign(fadeDir)`, clamped to [0, fadeLen]; `weight = fadeTime/fadeLen`.
  A fresh script has fadeTime=fadeLen=1, fadeDir=+1 => weight 1 at once. AnimScriptAppendBlend puts every older
  script into fade-out: fadeTime=fadeLen=8 (8 frames at 60 Hz = 133 ms), fadeDir=-1, flags|=0x500000.
  Scripts are sorted oldest-first by timestamp and folded: `acc = oldest; for i: acc = lerp(acc, s[i+1],
  1 - s[i].weight)`. When an old one reaches weight 0 it gets 0x200000 and is deleted in the same resolve.

---------------------------------------------------------------------------------------------------------------

## 5. KEY functions - reimplementation specs

Pseudocode types: `S` = sAnimScript_tag* (Xbox offsets), `F32(p,o)` = *(float*)(p+o) etc.

### 5.1 AnimScriptInit - Xbox 0x12620, PS2 0x1c1d38
Convention: **EAX = obj, ESI = script, EDI = hashcode**, no stack args, caller cleans nothing.
```c
void AnimScriptInit(obj_tag *obj /*EAX*/, S s /*ESI*/, u32 hash /*EDI*/) {
    u8 rev = ReverseFlag; ReverseFlag = 0;                       // 0x1d7828
    if (!obj) return;
    AnimState *as = obj->animState;                              // obj+0xb8
    if (!as || !s || (hash & 0xff000000) != 0x06000000) return;
    u8 *d = hashtable_getitem(hash);  U32(s,0x58) = d;  if (!d) return;
    u32 f = (U32(s,0x74) & 0x080fffff) | 0x20000000;             // keep bit27 + low 20 bits, set "just started"
    U32(s,0x70) = hash;  U32(s,0x78) = 0;  U32(s,0x74) = f;
    S16(s,0xac) = S16(d,4);                                      // numFrames
    u16 info = U16(d,6);
    if (info & 0x8000) U32(s,0x74) = f | 0x80000000;             // loop
    U8(s,0xae) = (u8)info;  U8(s,0xaf) = (info >> 9) & 0x3f;
    if (!rev) { F32(s,0x8c) = 1.0f; F32(s,0x94) = 1.0f; }
    else      { F32(s,0x94) = -1.0f; F32(s,0x8c) = (float)S16(s,0xac); }
    F32(s,0x90) = F32(s,0x8c);
    U8(s,0xb0) = 0; U8(s,0xb1) = 0;
    U32(s,0x64) = U32(s,0x60) = U32(s,0x68) = U32(s,0x5c) = U32(s,0x6c) = 0;
    if (AnimScriptTimeStamp == 0) AnimScriptTimeStamp = 1;
    U32(s,0x80) = AnimScriptTimeStamp++;
    U32(s,0x7c) = 0;
    if (ScriptCam && as->animObj.skinPtr->numMorphs /*skin+0x2a*/) U32(s,0x7c) = 0xe0000002;
    F32(s,0xa8) = F32(s,0xa4) = 1.0f;  U8(s,0xb2) = 1;
    U32(s,0x84) = 0; U32(s,0x88) = 0; U32(s,0x54) = 0;
}
```
(PS2 masks with 0x280fffff; the extra bit 29 is ORed in anyway.)

### 5.2 AnimScriptTick - Xbox 0x18450, PS2 0x1c2d68
Convention: **ECX = animObj**, stack `(obj, script)` cdecl. Returns nothing.
```c
void AnimScriptTick(sAnimObject_tag *ao /*ECX*/, obj_tag *obj, S s) {
    float spd = F32(s,0x94);
    U32(s,0x74) = (U32(s,0x74) & ~0x40000000) | 0x00800000;
    int starting = U32(s,0x74) & 0x20000000;
    if (starting)
        AnimProcessScriptCmds(obj, ao, s, ftol(F32(s,0x90)), ftol(F32(s,0x8c)));
    switch (U8(s,0xb0)) {                                        // jump table 0x1867c
    case 0: spd = F32(s,0x94) * FRAME_RATE_MUL; break;           // advance = speed*60/fps
    case 1: U32(s,0x74) |= 0x80000000;                           // forces loop
            F32(s,0x94) = AnimDistanceTableDistanceToFrame2(obj /*EAX*/, ao /*EDI*/, s /*EBX*/);
            spd = F32(s,0x94); break;                            // 'starting' NOT cleared
    case 2: { S m = PTR(s,0x48);                                  // phase-sync to master
            float mp = (F32(m,0x8c) - 1) / (float)(S16(m,0xac) - 1);
            float n1 = (float)(S16(s,0xac) - 1);
            float mine = (F32(s,0x8c) - 1) / n1;
            float d = mp - mine;
            if (F32(m,0x94) >= 0) { if (d < 0) d = (1 - mine) + mp; }
            else                  { if (d >= 1) d = (mine + 1) - mp; }
            F32(s,0x94) = n1 * d + 1.0f; spd = F32(s,0x94); starting = 0; break; }
    case 3: F32(s,0x94) = (float)(S16(s,0xac) + 1) * F32(s,0x88) - F32(s,0x8c);
            spd = F32(s,0x94); starting = 0; break;
    default: break;                                              // spd = raw speed
    }
    int from = ftol(F32(s,0x90));
    float nf = F32(s,0x8c);
    if (!starting) nf += spd;                                    // first tick holds the start frame
    float to = AnimScriptEnd(obj, ao, nf, s /*ESI*/);
    if (Glb_bScriptDeleted) return;
    AnimProcessScriptCmds(obj, ao, s, from, ftol(to));
    if (U8(s,0xb1) == 3 && (U32(s,0x74) & 0x10000000))
        AnimScriptDelete(obj, &s);
}
```
Note: in case 2 the result is `curFrame + d*(n-1) + 1`, i.e. the slave lands one frame ahead of the master's
phase each tick (faithful to both builds).

### 5.3 AnimScriptEnd - Xbox 0x12880, PS2 0x1c2068
Convention: **ESI = script**, stack `(obj, animObj, float frame)`; returns float in ST0.
```c
float AnimScriptEnd(obj_tag *obj, sAnimObject_tag *ao, float fr, S s /*ESI*/) {
    u32 f = U32(s,0x74);
    if (f & 0x80000000) U8(s,0xb1) = 1;                          // loop flag forces loop mode
    int n = S16(s,0xac);
    if (fr >= 1.0f && fr <= (float)n) { F32(s,0x8c) = fr; return fr; }
    int rev = F32(s,0x94) < 0.0f;
    switch (U8(s,0xb1)) {                                        // jump table 0x12a10
    case 0: case 3:                                              // stop
        U32(s,0x74) = f | 0x10000000;
        fr = rev ? 1.0f : (float)n;
        if (PTR(s,0x5c)) { *(u8*)PTR(s,0x5c) = 1; PTR(s,0x5c) = 0; }
        if (PTR(s,0x68)) {
            ((void(*)(obj_tag*,sAnimObject_tag*,S))PTR(s,0x68))(obj, ao, s);
            if (Glb_bScriptDeleted) return 1.0f;
            PTR(s,0x68) = 0;
        }
        break;
    case 1:                                                      // loop
        U32(s,0x74) = f | 0x40000000;
        fr = rev ? fr + n : fr - n; break;
    case 2:                                                      // ping-pong
        fr = (rev ? 1.0f : (float)n) - F32(s,0x94);
        F32(s,0x94) = -F32(s,0x94); break;
    default: break;
    }
    if (fr < 1.0f) fr = 1.0f; else if (fr > (float)n) fr = (float)n;
    F32(s,0x8c) = fr; return fr;
}
```

### 5.4 AnimProcessScriptCmds - Xbox 0x17c50, PS2 0x1c2270
Convention: cdecl `(obj, animObj, script, int from, int to)`, 0xb4-byte frame.
```c
static bool Crossed(int a /*from,EAX*/, int d /*to,EDX*/, int s /*ECX*/, int e /*ESI*/, int fwd /*EDI*/)
{   // Xbox FUN_00012a20 (INVENTED name AnimScriptSpanHit; inlined on PS2)
    if (s <= a && a <= e) return true;
    if (s <= d && d <= e) return true;
    if (fwd) {
        if (a <= d) return (a <= s && s <= d) || (a <= e && e <= d);
        return s >= a || s <= d || e >= a || e <= d;             // wrapped forward: [a,n] U [1,d]
    } else {
        if (a >= d) return (d <= s && s <= a) || (d <= e && e <= a);
        return s <= a || s >= d || e <= a || e >= d;             // wrapped backward
    }
}
void AnimProcessScriptCmds(obj_tag *obj, sAnimObject_tag *ao, S s, int from, int to) {
    int fwd = F32(s,0x94) >= 0.0f;
    bool hit = false; int nSeq = 0;
    int mute = (U32(ao,0x50) >> 18) & 1;
    u16 *c = (u16*)(PTR(s,0x58) + 8);
    for (int k = U8(s,0xae) - 1; k >= 0; --k) {
        u32 bit = 1u << k;  int op = c[0] & 0xff, len = c[0] >> 8;
        u32 fired = U32(s,0x78) & bit;  int adv;
        switch (op) {
        case 0:
            hit = Crossed(from, to, c[1], c[2], fwd);
            if (hit && !fired) {
                u32 h = 0x04000000 | c[3];
                u8 *raw = hashtable_getitem(h);  if (!raw) return;   // aborts, mask not updated
                int morph = (U32(raw,0xc) >> 3) & 1;
                if (!(morph && (U32(s,0x74) & 0x80000))) {
                    int sf = (fwd ? to : from) - c[1] + 1;
                    sAnimSeq_tag *q = AnimSeqSet(obj, ao, raw, ftol((float)sf), morph, /*ECX*/s, /*EBX*/h);
                    U16(q,0x84) = c[1];
                    U32(s,0x74) &= ~0x40000;
                    if (nSeq == 0 && morph) U32(s,0x74) |= 0x40000;
                    U32(q,0x7c) &= ~0x200000;
                    if ((U32(s,0x74) & 0x10000) && !morph && S16(q,0x8a) > 0x100) {
                        U32(q,0x7c) |= 0x200000;
                        if (!PTR(q,0x6c)) { u32 n = U8(q,0x8e) * 8;
                            PTR(q,0x6c) = Mem_Malloc(n, 0x3d04, 0); if (PTR(q,0x6c)) memset(PTR(q,0x6c), 0, n); }
                    }
                    nSeq++;
                }
            }
            adv = len + 4; break;
        case 1:
            hit = Crossed(from, to, c[1], c[1], fwd);
            if (hit && !mute && !fired) PlayScriptSfx(obj, c[2]);   // substitution + Sound_Play3D, see section 1
            adv = len + 3; break;
        case 2:
            adv = len + 3; break;                                    // 'hit' keeps previous value
        case 4:
            hit = Crossed(from, to, c[1], c[1], fwd);
            if (hit && !mute && !fired) DoScriptEvent(obj, ao, s, c[2], c[3]);  // table in section 1
            adv = len + 3; break;
        default:
            adv = len; break;                                        // op 3 / >4 (never in data)
        }
        c += adv;
        if (hit) U32(s,0x78) |= bit; else U32(s,0x78) &= ~bit;
    }
}
```
`AnimSeqSet` (Xbox FUN_00017960, PS2 0x1c7bd0; ECX=script, EBX=seq hash, stack obj, animObj, raw, int frame,
int channel): `q = script->seq[channel]` (+0x4c/+0x50); requires animObj->skin; attaches raw (FUN_00014420, EAX=raw,
ESI=q, ECX=hash); `F32(q,0x80)=frame; U16(q,0x86)=1; U16(q,0x88)=U16(q,0x8a); q+0xc=0; q+0x30=obj->position (obj+0x24);
q+0x18=q+0x24=0; U32(q,0x7c) = (U32(q,0x7c)&0x01efffff)|0x20000000; if (U32(q,0x7c)&7) PTR(q,0x64)=
AnimDistanceTableGet(hash); if (channel==0 && U32(obj,0xb4)) copy 16 bytes q+0x54 -> animObj+0x08;
U8(q,0x8d)=skin->skeletonNum(+0x2c); return q`. (Sequence fields: spec-seq.md.)

### 5.5 AnimListBuild - Xbox 0x122e0, PS2 0x1c16f8
Convention: cdecl `(obj_tag *obj, u32 mask, u32 value, void (*cb)(obj_tag*, S), u32 mode)` -> returns list[0]
(after callbacks) or 0 if the list is empty / obj invalid.
```c
S AnimListBuild(obj_tag *obj, u32 mask, u32 value, void (*cb)(obj_tag*, S), u32 mode) {
    if (!obj || !obj->animState) return 0;
    AnimState *as = obj->animState;
    if (mode & 1) GlbAnimList = 0;                               // 1 = reset, 2 = remove instead of add
    for (S s = as->animObj.firstScript /*as+0x7c*/; s; s = PTR(s,0x44)) {
        for (u32 m = mask, b = 1; m; b <<= 1) {
            while (!(m & b)) b <<= 1;
            m ^= b;
            bool t;
            switch (b) {
            case 0x0001: t = true; break;
            case 0x0002: t = s != (S)value; break;
            case 0x0004: t = PTR(s,0x40) == 0; break;             // first in chain
            case 0x0008: t = PTR(s,0x44) == 0; break;             // last
            case 0x0010: t = U32(s,0x74) & 0x10000000; break;     // stopped
            case 0x0020: t = U32(s,0x70) == value; break;         // hashcode
            case 0x0040: t = s == (S)value || PTR(s,0x48) == (void*)value; break;
            case 0x0080: t = PTR(s,0x48) == (void*)value; break;  // slaves of value
            case 0x0100: t = PTR(s,0x48) != 0; break;             // any slave
            case 0x0200: t = U32(s,0x7c) == value; break;         // group tag
            case 0x0400: t = U32(s,0x7c) & value; break;
            case 0x0800: t = U32(s,0x74) & 0x40000; break;        // morph-only script
            case 0x1000: t = as->animObj.skinPtr->numMorphs && (U32(PTR(s,0x50),0x7c) & 8); break;
            case 0x2000: t = PTR(s,0x54) != 0; break;             // masked overlay
            case 0x4000: t = U32(s,0x74) & 0x4000; break;         // overlay (strafe)
            default:     t = false;
            }
            if (!t) continue;
            int i = 0; while (i < GlbAnimList && list[i] != s) i++;
            if (!(mode & 2)) { if (i == GlbAnimList && i < 32) list[GlbAnimList++] = s; }
            else if (i != GlbAnimList) { GlbAnimList--; for (; i < GlbAnimList; i++) list[i] = list[i+1]; }
        }
    }
    if (GlbAnimList == 0) return 0;
    if (cb) for (int i = 0; i < GlbAnimList; i++) cb(obj, list[i]);
    return list[0];
}
```
Mask bits are OR-ed (any true test adds/removes). Callers build a set then subtract with mode 2, e.g.
`ListBuild(obj,1,0,0,1); ListBuild(obj,0x800,0,0,2)` = "all scripts except morph-only ones". No caller passes a
callback in the Xbox anim module (PS2 comment "callback is always null" agrees). Callback signature
`void cb(obj_tag*, sAnimScript_tag*)` (PS2 type `AnimListBuilt`).
PS2 difference: bit 0x40 compares only `s == value || s+0x4c(PS2) == value` - same semantics.

### 5.6 AnimSetUpdate - Xbox 0x18bb0, PS2 0x1c1060
Convention: cdecl `(obj_tag *obj, sAnimSet_tag *set, float moving, float fwdSpeed, float maxSpeed,
float sideSpeed, float maxSide)` (INVENTED param names; callers Player_SSWalk/SSCrouch/ZeroG pass
`FRAME_RATE_MUL*const` etc.).
```c
void AnimSetUpdate(obj_tag *obj, sAnimSet_tag *set, float p3, float p4, float p5, float p6, float p7) {
    float frac = 0; AnimState *as;
    if (!obj || !(as = obj->animState) || !set) return;
    if (p3 <= 0.0002f && p3 >= -0.0002f) p4 = 0;                 // not moving
    F32(as,0xb4) /*animObj+0x5c: distance this tick*/ = p4 * F32(set,0x28);
    while (U8(set,0x33)) {                                       // one-shot state
        if (U8(set,0x33) != 1) goto mp;
        S t = AnimListBuild(obj, 0x200, U32(set,0x20), 0, 1);
        if (!(U32(t,0x74) & 0x10000000) && p3 == 0) goto mp;     // keep playing
        AnimScriptDelete(obj, &t); U8(set,0x33) = 0;
    }
    float t = fabsf(p4); if (t > p5) t = p5; t /= p5;            // 0..1
    int idx;
    if (t > 0.0002f /* or < -0.0002 */) {
        int n2 = U8(set,0x31) - 2;  float x = t * n2;  int i = ftol(x);
        if (i == n2) i--;
        idx = i + 1;  frac = x - i;
    } else idx = 0;
    if (U8(set,0x30)) U8(set,0x30)--;                            // switch cool-down
    int cur = (s8)U8(set,0x2e), eff = cur;
    if (idx != cur && U8(set,0x30) == 0) {
        U8(set,0x30) = 4;
        bool edge = (cur == 0 || idx == 0);
        U8(set,0x2e) = idx;
        if (edge) {                                              // idle <-> moving: cross-fade everything
            AnimListBuild(obj, 0x200, U32(set,0x20), 0, 1);
            for (i in list) if (!(U32(list[i],0x74) & 0x400000)) {
                U32(list[i],0x74) |= 0x500000; F32(list[i],0xa8) = F32(list[i],0xa4) = 8.0f; U8(list[i],0xb2) = 0xff; }
        }
        HASHCODE *hc = PTR(set,0x18);
        if (idx == 0) { S a = AnimScriptAppend(obj, hc[0]); U32(a,0x7c) = U32(set,0x20); }
        else {
            if (!edge) {                                         // moving -> moving: drop the old pair outright
                AnimListBuild(obj, 0x200, U32(set,0x20), 0, 1);
                for (i in list) if (U32(list[i],0x80) == U32(set,8) || U32(list[i],0x80) == U32(set,0xc))
                    AnimScriptDelete(obj, &list[i]);
            }
            S a = AnimScriptAppend(obj, hc[idx]);
            U32(a,0x7c) = U32(set,0x20); U32(a,0x74) |= 0x8d000000; U8(a,0xb0) = 1;   // distance-driven
            PTR(PTR(a,0x4c),0x70) = AnimSet_WalkUpdater;          // 0x171a0
            U32(set,8) = U32(a,0x80);
            S b = AnimScriptAppend(obj, hc[idx+1]);
            U32(b,0x7c) = U32(set,0x20); U32(b,0x74) |= 0x8d000000; U8(b,0xb0) = 2;   // phase-locked to a
            PTR(b,0x48) = a; U32(set,0xc) = U32(b,0x80); PTR(set,0x10) = b; PTR(b,0x60) = set+0x10;
        }
        AnimObjectUpdate(obj);
        eff = idx;
    }
    if (eff != 0 && PTR(set,0x10)) F32(PTR(set,0x10),0x84) = frac;  // walk->run blend
    AnimListBuild(obj, 0x200, U32(set,0x20), 0, 1);
    if (GlbAnimList == 1) F32(as,0xbc) /*animObj+0x64 distance phase*/ = F32(set,0x24);
    else U16(set,0x2c) = (Rand_Random() & 0x3ff) + 0xff;
mp:
    if (!MPSettings.isMultiplayer) return;
    float r = p6 / p7;
    float ang = fabsf(atan2f(r, fabsf(p4 / p5)) * 0.63661975f);  // 2/pi
    int dir = (fabsf(p6) <= 0.0002f || ang < 0.5f) ? 0 : (p6 > 0 ? 1 : 2);
    if (dir != U8(set,0x2f)) {
        U8(set,0x2f) = dir; U8(set,0x2e) = 0xff;                 // force re-pick next update
        if (PTR(set,0x14)) AnimScriptDelete(obj, (S*)(set+0x14));
        if (dir == 0) return;
        HASHCODE h = PTR(set,0x18)[U8(set,0x31) + (dir == 1 ? 1 : 3)];  // 2nd list [0] or [2]
        S o = AnimScriptAppendBlend(obj, h);
        PTR(set,0x14) = o; PTR(o,0x60) = set+0x14; U32(o,0x74) |= 0x8d004000;   // overlay
    } else if (dir == 0) return;
    if (PTR(set,0x14)) { F32(PTR(set,0x14),0x94) = fabsf(r); F32(PTR(set,0x14),0xa4) = ang; }
}
```
(The Xbox code compares `t` against 0.0002 with the x87 "<=/==" idiom; the PS2 body is the same logic.)

### 5.7 AnimFrameResolve blend section (Xbox 0x160a0 - owned by spec-pose.md; the time/weight part)
```c
// 1. slaves into masters
if (AnimListBuild(obj,0x100,0,0,1)) for each s in list:
    AnimFrameBlend(EAX=s->bone.raw, EDX=ECX=s->master->bone.raw, obj, numBones, F32(s,0x84), PTR(s,0x54));
// 2. fade update for all scripts except morph-only/overlay/masked (0x800|0x2000|0x4000)
ListBuild(obj,1,0,0,1); ListBuild(obj,0x6800,0,0,2);
for s in list: U32(s,0x74) &= ~0x200000;
    if ((s8)U8(s,0xb2) > 0) F32(s,0xa4) += FRAME_RATE_MUL;
    else if ((s8)U8(s,0xb2) < 0) { F32(s,0xa4) -= FRAME_RATE_MUL; if (F32(s,0xa4) <= 0) U32(s,0x74) |= 0x200000; }
    clamp F32(s,0xa4) to [0, F32(s,0xa8)];  F32(s,0x98) = F32(s,0xa4) / F32(s,0xa8);
// 3. remove slaves, sort remaining by timestamp (+0x80) ascending, fold:
//    1 script -> copy its raw pose (animObj+0x88 bytes) to animObj+0x34;
//    else acc = list[0]; for i in 0..n-2: f = 1 - F32(list[i],0x98); (flag 0x8000 root-quat sharing);
//         AnimFrameBlend(acc, list[i+1], f); root translation/rotation vectors lerped by f, clamped.
// 4. masked scripts (0x2000): blend with weight 1.0 and mask +0x54; overlays (0x4000): blend with weight +0xa4.
// 5. after root motion: among non-morph scripts, if any has 0x200000 & 0x100000, delete every script whose
//    fadeDir==0 or which has 0x200000 & 0x100000.
```

---------------------------------------------------------------------------------------------------------------

## 6. Other functions

| function | Xbox | PS2 | convention (Xbox) | behaviour |
|---|---|---|---|---|
| AnimScriptNew | 0x159d0 | 0x1c1ad0 | EAX=obj, stack prevScript | alloc 180 (PS2 0xc0) zeroed; magic; boneSeq = AnimSeqNew(numBones*28 [PS2 *32]); morph seq AnimSeqNew(0x58) if skin->numMorphs; insert after prevScript (or standalone). Failure: frees seqs + script, returns 0. Does NOT init. |
| AnimScriptDelete | 0x15b40 | 0x1c1c28 | cdecl (obj, S *pScript) | if s == Glb_pCurrentScript -> Glb_bScriptDeleted=1; onDelete(obj,animObj,s); if ownerSlot && ownerSlot != pScript -> *ownerSlot=0; free morph then bone seq (inlined AnimSeqDelete: frees seq+0x6c unpak, seq+0x68 raw, seq); unlink (prev==0 -> animObj.firstScript=next); magic=0x983c4837; Mem_Free(pScript) (clears *pScript). |
| AnimScriptAppend | 0x15c30 | 0x1c7770 | cdecl (obj, hash) | if no scripts: New(0) as firstScript; else walk to last, count; >=32 -> return 0; New(last). Then Init. Returns script. |
| AnimScriptAppendBlend | 0x15ca0 | 0x1c1ef8 | cdecl (obj, hash) | list = all minus morph-only; each not already fading (0x400000): flags\|=0x500000, fadeLen=fadeTime=8.0, fadeDir=-1; then AnimScriptAppend. |
| AnimScriptAdd | 0x17220 | 0x1c74b0 | cdecl (obj, hash) | delete ALL scripts (ListBuild 1 + AnimListDelete), New(0) as firstScript, Init. Hard cut. |
| AnimScriptAddSpeed | 0x17270 | 0x1c7590 | cdecl (obj, hash, float speed) | Add, then +0x94 = speed. |
| AnimScriptAddReverse | 0x172a0 | 0x1c7688 | tail-jumps to Add | ReverseFlag=1 -> Init starts at last frame with speed -1. |
| AnimScriptPeekSeq | 0x125b0 | 0x1c7380 | cdecl (hash) -> raw seq | only if file byte 7 == 0x78 (60 fps, no loop, <256 cmds): hashtable_getitem(0x04000000 \| U16(file,0xe)) = first command's seqId (assumes cmd0 is SEQ). Used by Sub_Create/MiniSub_Create. |
| AnimScriptPeekSfx | inlined/absent | 0x1c7418 | (hash) -> u16 | first SFX command's sfxId; stops (0) at an op>4. |
| AnimScriptIsStopped | 0x12780 | 0x1c7848 | cdecl (obj) -> bool | builds list of all scripts; returns `countNotStopped != count`, i.e. TRUE if ANY script is stopped (name is misleading with several scripts). |
| AnimScriptSetPlayDirection | 0x127d0 | 0x1c78d8 | cdecl (obj, dir) | on firstScript (animState+0x7c) if not stopped: 0 -> speed=\|speed\|, 1 -> -\|speed\|, 2 -> -speed. |
| AnimScriptCallOnEvent | 0x12860 | 0x1c7968 | cdecl (S, fn) | +0x6c = fn; fn(obj, animObj, script, u16 param). |
| AnimScriptCallOnStop | 0x12870 | 0x1c7970 | cdecl (S, fn) | +0x68 = fn; fn(obj, animObj, script). |
| AnimScriptSetAnimRotation | 0x12a90 | 0x1c7978 | cdecl (S, s8 on) | on: flags\|=0x20000, +0x9c = 8.0 (capture sentinel); off: clear 0x20000. AnimObjectUpdate then extracts the root quaternion's yaw each tick (rawData + numBones*12 = first quat), adds the yaw change to obj->rotation.y and counter-rotates the root so the anim turns the object. |
| AnimTagAllocAndGetUnique | 0x15970 | (AnimTagGetUnique 0x1c7f30 + inline alloc) | ESI = previous set | alloc 0x34 zeroed (tag 0x3c04), insert after ESI, +0x20 = AnimTagTimeStamp++ (skips 0). Returns set. |
| AnimListDelete | 0x171e0 | 0x1c7308 | cdecl (obj) | AnimScriptDelete every list entry, GlbAnimList = 0. |
| AnimSetInit | 0x17860 | 0x1c71b8 | EAX=hashArray, ECX=set, stack (obj, a, b) | +0x18=array; +0x31 = count of first 0-terminated list; +0x1c = &array[+0x31 + 1]; +0x32 = count of second list; +0x33=0,+0x2f=+0x2e=0xff,+0x14=0,+0x30=0,+0x8=+0xc=+0x10=0; +0x2c = (Rand&0x3ff)+0xff; +0x24=a; animObj+0x64=a; +0x28=b; deletes all scripts. |
| AnimSetAppend | 0x17b90 | 0x1c0ee0 | cdecl (obj, hashArray, float a, float b) | first set -> alloc 0x34 into animState+0x78 (animObj+0x20); else append (max 4 sets) via AnimTagAllocAndGetUnique; AnimSetInit. Players: a=0.37, b=0.5. |
| AnimSetDelete | 0x177a0 | inlined in DeleteAll | cdecl (obj, set **) | delete overlay (+0x14), delete all scripts with tag +0x20, unlink, free. |
| AnimSetDeleteAll | 0x17820 | 0x1c7098 | cdecl (obj) | AnimSetDelete each. |
| AnimSet_WalkUpdater | 0x171a0 (FUN_) | 0x1c7178 | cdecl (obj, skin, seq) | AnimFrameSet(obj, frac(seq+0x80)) - always interpolates (ignores 0x10000). |
| AnimDistanceTableDistanceToFrame2 | 0x13420 | 0x1c49a8 | EAX=obj, EDI=animObj, EBX=script -> ST0 | uses seq+0x64 table {float cycleDist; int nFrames; ?; float cum[...]} and animObj+0x64 (phase) + animObj+0x5c (distance this tick) to return the frame delta that keeps feet in sync; wraps phase into [0,cycleDist). |
| AnimDistanceTableGet | 0x174a0 (FUN_) | 0x1c7e60 | ESI=seq hash | cache of 32 {hash, table}; builds with AnimDistanceTableCreate (0x16950 / PS2 0x1c4798). |
| AnimDebugCreate | 0x19180 | 0x1bd6d0 | void | level AllCharacters: ring of every skin with skin+0x2c in {0,1}, bind pose; RefRoom: skin 0x500002e with Add(0x600011b) (flags \|= 0x80080000) + Append(0x6000128) playMode 3, tag 0xe0000002. |
| AnimDebugHandler | (absent) | 0x1bce10 | PS2 only | debug states cycling test scripts on skin 0x5000007 (0x6000243 / 0x6000259 with flags 0x87000000 = loop + all axis locks, speed 0), 0x5000038 + 0x600041a (speedMode 1, flags 0x8c000000) etc. |

### sAnimSet_tag (INVENTED tag name) - 0x34 bytes (Mem_Malloc(0x34) in AnimSetAppend/AnimTagAllocAndGetUnique)
| off | size | type | name | evidence |
|---|---|---|---|---|
| 0x00 | 4 | set* | prev | AnimTagAlloc, SetDelete |
| 0x04 | 4 | set* | next | same |
| 0x08 | 4 | u32 | timestamp of walk script A | SetUpdate |
| 0x0c | 4 | u32 | timestamp of blend script B | SetUpdate |
| 0x10 | 4 | S | script B (its ownerSlot points here) | SetUpdate |
| 0x14 | 4 | S | strafe overlay script (ownerSlot points here) | SetUpdate MP part, SetDelete |
| 0x18 | 4 | HASHCODE* | speed list: [idle, walk.., run.., 0] | SetInit |
| 0x1c | 4 | HASHCODE* | second list (strafe: [0]=side 1, [2]=side 2) | SetInit |
| 0x20 | 4 | u32 | tag (unique; copied to scripts' +0x7c) | AnimTagAlloc |
| 0x24 | 4 | float | a - distance phase reset value (animObj+0x64) | SetInit, SetUpdate |
| 0x28 | 4 | float | b - speed -> distance scale (animObj+0x5c = p4*b) | SetUpdate |
| 0x2c | 2 | u16 | random idle timer (Rand&0x3ff)+0xff (not consumed in anim module) | SetInit/Update |
| 0x2e | 1 | s8 | current speed index (0xff = none) | SetUpdate |
| 0x2f | 1 | u8 | strafe dir (0 none, 1, 2; 0xff init) | SetUpdate |
| 0x30 | 1 | u8 | switch cool-down (4 updates) | SetUpdate |
| 0x31 | 1 | u8 | speed-list count | SetInit |
| 0x32 | 1 | u8 | second-list count | SetInit |
| 0x33 | 1 | u8 | one-shot state (1 = one-shot playing; game sets it) | SetUpdate |
Example Handgun set 0x17e764: `{0x60009dc, 0x60009de, 0x60009db, 0, 0x60009d7, 0x60009d7, 0x60009d8, 0x60009d8, 0,
...}` - with count 3, any movement picks idx 1 (walk 0x60009de, distance-driven) + run 0x60009db phase-locked,
blended by `frac = |speed|/max`.

Related sAnimObject_tag fields (animObj = AnimState+0x58): +0x1c skin, +0x20 set list, +0x24 firstScript,
+0x50 flags (bit 18 = mute script SFX/events, bit 0 = pose written), +0x5c distance this tick, +0x64 distance phase.

---------------------------------------------------------------------------------------------------------------

## 7. Open questions / suspected bugs

1. **Op 3 / op >= 5 stride mismatch**: the Xbox interpreter advances `len` words (0 when len=0 -> the same
   command is re-read), AnimProcessScriptData advances len+3 / len+1. Harmless (no such data), but a
   reimplementation should reject such files.
2. **numCmds bit 8** is dropped by the Xbox runtime (byte copy) but honoured by AnimProcessScriptData and PS2
   PeekSfx. All data has < 256 commands, and > 32 commands would alias fired bits.
3. **AnimScriptIsStopped** returns true if *any* script is stopped - probably meant "all".
4. Speed mode 2 phase-sync gives `frame = masterPhase*(n-1) + 2` (one frame ahead) - faithful, maybe intended.
5. Ping-pong reflects by one step (`n - speed`), not by the overshoot.
6. Rate field (60) and script flag 0x08000000 are not consumed in the anim module; the u16 at AnimSet+0x2c
   (idle timer) likewise - check game code if needed.
7. `boneMask` +0x54: set outside the anim module; exact mask format belongs with AnimFrameBlend (spec-pose.md).
8. AnimFrameBlend argument roles (EAX/EDX/ECX data pointers, weight 0/1 shortcuts) - see spec-pose.md; the
   fold direction here (weight of the newer script = 1 - older.weight) is established from the root-vector lerp
   next to it in AnimFrameResolve.
