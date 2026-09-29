# Animation sequence data (0x04xxxxxx files) and frame decoding (spec-seq)

Scope: the sequence file format, AnimProcessSeqData, the runtime `sAnimSeq_tag`, how AnimFrameCopy turns a frame
number into per-bone local rotations and translations, fractional frames (AnimFrameSet/AnimFrameBlend), root
motion, the distance table, and timing. Xbox EU `default.xbe` offsets throughout; PS2 addresses for the named
counterparts. Tooling: `decode_seq.py` (this folder) decodes and validates; scratch work in `seqwork/`.

## 0. Headline findings

1. **Keys are quintic polynomials in absolute time.** A track is a run of keys. Each key is a 16-bit word
   `len:10 | mask:6` followed by one float coefficient per set mask bit. The value of the track at frame `f`
   (1-based) is `c0 + c1*t + c2*t^2 + c3*t^3 + c4*t^4 + c5*t^5` with **t = f - 1, not relative to the key start**.
   The key used is the first one whose running sum of `len` is `>= t`. Masks seen: only 1, 3, 7, 0xf, 0x1f,
   0x3f (polynomial degree 0 to 5). There is no quantisation and no delta coding: the coefficients are plain
   IEEE floats. The recurring `0800 8100` in 04000078.bin is a whole track: size 8, key `0x0081` (len 2, mask 1:
   constant), then one float coefficient.
2. **Tracks per bone come from the skeleton, not the file.** For every bone of `SkeletonTable[skel]`, in bone
   order: 6 tracks `tx ty tz qx qy qz` if the bone's bit is set in the skeleton's translation mask
   (`skel+4`, 96 bits), else 3 tracks `qx qy qz` (the translation is the skeleton default at `skel+0x10+12*b`).
   `numTracks == 3*numBones + 3*popcount(mask)` holds for all 551 dumped bone sequences. The file's skeleton byte
   is `+0x3c`, but at play time AnimSeqSet overwrites `seq+0x8d` with the **skin's** `skeletonNum` (skin+0x2c).
3. **The quaternion is XYZW with w derived.** The file stores x, y and z. `w = +sqrt(1-x^2-y^2-z^2)` (0 if that is
   <= 0). The sign of w is carried in z: a stored z >= 2.0 means "z-4, and w is negative". The buffer layout is
   `q[0..3] = x,y,z,w`. Quat_QuatToMat/QuatTransToMat build the D3DX row-vector matrix of (x,y,z,w). So
   `math.h`'s "wxyz" comment is wrong for these quaternions. For Blender: `Quaternion((q[3], q[0], q[1], q[2]))`.
4. **The output is local (parent-relative).** AnimFrameCopy writes `vec3 trans[numBones]` followed by
   `quat rot[numBones]`: 12+16 = 28 bytes per bone. psiBuildMatrixPalette composes the hierarchy from the skin's
   parent bytes (see spec-pose.md). Translations are multiplied by `Glb_SkinScale`. Bone 0's is divided back
   (it stays raw) and becomes root motion: AnimSeqTick turns its frame-to-frame change into object movement
   and **zeroes bone 0's translation** in the pose.
5. **Frames are 1-based at 60 fps.** A seq's frame = script time − seq start + 1. Script time advances by
   `speed * FRAME_RATE_MUL` per game tick, where `FRAME_RATE_MUL = 60 / gameFPS` (GS_SetRefreshRate 0x6b08a).
   AnimFrameCopy only ever samples integer frames. AnimFrameSet blends frame `floor(f)` with
   `min(floor(f)+1, lastFrame)` by the fraction: translations are lerped and rotations slerped (Quat_Slerp_Acc
   near the camera, a 0.5 step function when far).
6. **The +0x38 value the loader sums is numFrames** (`AnimFileSizes.SomeQuantityFromAnimSequences` is the total
   frame count of the loaded sequences).
7. Validation: **all 582 dumped files and all 1,279 distinct sequences in the 32 level bundles parse with every
   byte accounted for** (header + tracks + zero padding to a 32-byte multiple). 1,267 of the 1,279 also match
   their skeleton's track count. The other 12 are stale exports with 30 extra tracks (section 5.1). Bone
   sequences decode to unit quaternions (max | |q|-1 | 1.5e-3 in the dump, only where x²+y²+z² slightly exceeds
   1). The exception is one exporter sentinel (-9.2e13 constant) on two unused bones of 0400078e. Decoded
   curves are continuous frame to frame, except for some long cutscene sequences that contain frame ranges of
   garbage data (section 6).

## 1. File format: sequence (`sAnimRawSeq_tag`, hash top byte 0x04)

All little-endian. Offsets are from the start of the file (= the hashtable data pointer).

| off | size | type | name (INVENTED unless noted) | on disk | at runtime / use | evidence | conf. |
|---|---|---|---|---|---|---|---|
| 0x00 | 4 | u32 | hashcode / `trackData` | file's own hashcode | AnimProcessSeqData: `= file+0x40` (pointer to track data) | 0x13b53 `*puVar5 = puVar6` | confirmed |
| 0x04 | 4 | u32 | `trackDataSize` | 0 | `= end-of-last-track - (file+0x40)` | 0x13b50 `puVar5[1]=` | confirmed |
| 0x08 | 4 | u32 | `cursor` | 0 | set to 0 by ProcessSeqData; AnimFrameCopy copies +0x00 into it and decodes from it | 0x13f45 `mov [eax+8],esi` | confirmed |
| 0x0c | 4 | u32 | `flags` | 0 or 8 | ProcessSeqData keeps the low nibble (`&= 0xf`, and bytes 0xd-0xf zeroed). It then forces `|= 7` for a hard-coded list of hashes (0x40000e9, 0x4000125, 0x4000144/145, 0x4000150/151, 0x4000154/155, 0x400015c/15d, 0x4000167/168, 0x400079c, 0x40007b0, 0x40007b3, 0x40007b6, 0x40007e4/7e5, 0x4000804, 0x4000809, 0x400080c, 0x400080f, 0x4000811, 0x400082c/82d, 0x4000831/832, 0x4000835, 0x4000838, 0x4000847, 0x400084e/84f). Bits 0-2 = the distance-table axis mask (AnimSeqSet builds a table when `flags&7`). Bit 3 = morph (facial) sequence. Copied into `seq+0x7c` bits 0-3. | 0x1394b, 0x13b3c; FrameCopy 0x13f18 | confirmed |
| 0x10 | 16 | f32[4] | `boundSphere` (centre xyz, radius) | e.g. (0.003,-0.187,0.065, 1.004) | -> seq+0x54..0x60; AnimSeqSet copies it to sAnimObject+0x08..+0x14 for the base seq of a drawn object | FUN_00014420; FUN_00017960 | fields confirmed, meaning likely |
| 0x20 | 12 | f32[3] | `rootBoxA` = (min x, min y, **max** z) of bone 0's translation over the sequence | | -> seq+0x3c..0x44 | matches the decoded root track in all checked files (e.g. 04000045: z runs 0.06 -> 1.54, +0x28 = 1.54) | likely (no reader found) |
| 0x2c | 12 | f32[3] | `rootBoxB` = (max x, max y, **min** z) | | -> seq+0x48..0x50 | same | likely |
| 0x38 | 2 | s16 | `numFrames` | | -> seq+0x8a. Frames are 1..numFrames. AnimLoadFile adds it to a global | 0x14a9d | confirmed |
| 0x3a | 2 | s16 | `numTracks` | | ProcessSeqData walks this many tracks. The low byte is also `numElems` (-> seq+0x8e), which is the element count of a morph sequence and the size of the unpak cache | 0x139e8..0x13a0e; FUN_00014420 | confirmed |
| 0x3c | 1 | u8 | `skeletonNum` | | -> seq+0x8d (then overwritten by skin->skeletonNum in AnimSeqSet) | FUN_00014420 | confirmed |
| 0x3d | 1 | u8 | pad | 0 in every file | | | |
| 0x3e | 1 | u8 | unknown | equals +0x3c in every file | -> seq+0x8c; no reader found | FUN_00014420 | guess |
| 0x3f | 1 | u8 | pad | 0 | | | |
| 0x40 | .. | | tracks | numTracks tracks back to back | | | confirmed |
| end | <32 | | zero padding | to a multiple of 32 bytes (the file-system granularity) | | all 1,279 bundle sequences + 582 dump files | confirmed |

AnimProcessSeqData reads the ten floats at 0x10-0x37 through the helper at 0x11000 (see 2.1). It discards them:
it only uses them to advance the cursor.

### 1.1 Track

```
u32  head      : low 16 bits = byte size of this track (including this u32); high 16 bits = key word #0
key words      : u16, consumed in u32 pairs (low half first). Word = (len << 6) | mask
                 len  (bits 6-15) : number of frames the key adds to the running total
                 mask (bits 0-5)  : coefficient c_i is present iff bit i is set (c0 = constant term)
coefficients   : after the u32 that holds a key word come that key's float coefficients (popcount(mask) f32,
                 c0 first). When a u32 holds two key words, the first key's coefficients follow the u32 and
                 the second key's follow those.
```

The key list ends when the running total reaches `numFrames-1`. If the last key word was the low half of a u32,
the high half is padding (ProcessSeqData skips it; it is 0 in every file). The track therefore ends
u32-aligned, and `head & 0xffff` equals the parsed length in all files. AnimFrameCopy uses the size to skip
tracks of bones the skin does not use.

Key selection and evaluation (AnimFrameCopy 0x13e50, with FUN_00013c70 doing the maths):

```
t    = frame - 1            (frame is an integer here: (float)(int)arg, clamped to [seq+0x86, seq+0x88])
acc  = 0
for each key: acc += len;  if (t <= acc) use this key         // key covers t in (accBefore, acc]; key 0 also covers t=0
value = c0 + c1*t + c2*t^2 + c3*t^3 + c4*t^4 + c5*t^5         // missing c_i = 0
```

On Xbox the powers `P = {1, t, t^2, t^3, t^4, t^5}` are computed once per call in extended precision and stored
as floats at 0x1d6acc..0x1d6ae0. The evaluation is five `Vec4_MulAdd` (0xd7300) steps, `r += P[i]*c_i`, each
rounded to float. Up to 4 tracks are evaluated at once in the SoA buffer 0x1d69c0 (6 rows x float[4]).
decode_seq.py reproduces this order and rounding.

The coefficients really are absolute-time polynomials. A key late in a sequence has large c0 and cancelling
higher terms (e.g. 04000185 track 87: `-38.1867 + 0.86878 t`). The `<=` rule is the one the encoder used:
a second-difference roughness test on 5 files is lower with `<=` than with `<` in 4 of 5. The coefficients stay
usable in float at t up to 5895 (04000009) because keys are short.

### 1.2 Track order and channel meaning

```
if (seq.flags & 8)            // morph / facial sequence
    numElems (u8 @0x3a) tracks, one scalar each -> float weights[numElems]      (channel id 6)
else for bone b in 0 .. skeleton.numBones-1:
    if skeleton.transMask bit b:  tracks tx, ty, tz, qx, qy, qz               (channel ids 5,4,3,2,1,0)
    else:                          tracks qx, qy, qz                           (channel ids 2,1,0)
```

Channel ids are what FrameCopy stores in 0x1d6a34[i]. Their effect in FUN_00013c70:

| id | effect |
|---|---|
| 5 | `trans->x = v * Glb_SkinScale.x` |
| 4 | `trans->y = v * Glb_SkinScale.y` |
| 3 | `trans->z = v * Glb_SkinScale.z; trans++` |
| 2 | `quat->x = v` |
| 1 | `quat->y = v` |
| 0 (default) | `if (v >= 2.0) { quat->z = v-4; s = 1-(x²+y²+z²); quat->w = s>0 ? -sqrt(s) : 0 } else { quat->z = v; quat->w = s>0 ? sqrt(s) : 0 }; quat++` |
| 6 | `*weights++ = v` |

Constants: 2.0 @0x15d34c, 4.0 @0x15d348, 1.0 @0x15d334, 0.0 @0x15d2e8.

For bones without translation tracks, FrameCopy writes `trans = skeleton.defTrans[b] * Glb_SkinScale`
(Vec_Copy 0xd50f0 + Vec_Mul).

## 2. Functions

### 2.1 Helpers (unnamed on Xbox)

| Xbox | INVENTED name | convention | behaviour |
|---|---|---|---|
| 0x11000 | `AnimReadF32` | no args; uses/advances global `pAnimData` 0x1d6ac8 | Reads 4 bytes little-endian at pAnimData, advances it by 4, returns the value as a float in ST0. Ghidra shows 0x11000-0x11005 as the string "Hello\0", so the database has these 6 bytes corrupted. Its function list starts a function at 0x11006 (`movzx ecx,[eax]; xor edx,edx; inc eax; mov [pAnimData],eax ... fld [esp]; pop ecx; ret`). The missing prologue must be `push ecx; mov eax,[0x1d6ac8]`. The PS2 build inlines this. |
| 0x13880 | `AnimReadKeyWord` | cdecl `(u32 *pendingHalf, u32 **pAnimData(unused, always &0x1d6ac8), int *parity)` -> u16 in AX | If `*parity` is even: read a u32 at pAnimData (advance 4), `*pendingHalf = u32>>16`, return the low half. Otherwise return `*pendingHalf`. Always `++*parity`. |
| 0x13c70 | `AnimFrameEmitChannels` | cdecl `(uint n, float **morphOut, vec3 **transOut, quat **quatOut)` | For n (1..4) buffered tracks: evaluate the polynomials (5 Vec4_MulAdd per group of 4 lanes), then apply the channel table above in lane order. |
| 0x14420 | `AnimSeqGetInfo` (PS2 0x1c8110, inlined in some Xbox callers) | custom: ECX = hashcode, ESI = seq, EAX = raw ptr or 0 | `seq+0x78 = hash`; if type 04: `raw = EAX ?: hashtable_getitem(hash)`; `seq+0x74 = raw`; copies raw+0x10..0x1c -> seq+0x54..0x60, raw+0x20..0x34 -> seq+0x3c..0x50, s16 raw+0x38 -> seq+0x8a, u8 raw+0x3c -> +0x8d, u8 raw+0x3e -> +0x8c, u8 raw+0x3a -> +0x8e, `seq+0x7c = (seq+0x7c & ~0xf) \| (raw+0xc & 0xf)`; returns raw (0 on failure). |
| 0x16950 | `AnimDistanceTableCreate` (PS2 0x1c4798) | ECX = axis mask, EDX = hashcode, stack: loop flag | see 2.9 |
| 0x174a0 | `AnimDistanceTableGet` (PS2 0x1c7e60) | ESI = hashcode | Looks up a 32-entry cache {hash, table*} at 0x1d6af0 (count 0x1d6ac4). On a miss, creates a table with `(ECX=4, EDX=hash, loop=0)`, i.e. the z axis only. Returns the table or 0 when the cache is full. |
| 0x17960 | `AnimSeqSet` (PS2 0x1c7bd0) | thiscall-like: ECX = script, EBX = hashcode; stack: `(obj, animObj, rawSeq, int frame, int which)` | see 2.7 |

### 2.2 AnimProcessSeqData — Xbox 0x138e0, PS2 0x1c5420 (KEY)

cdecl `void AnimProcessSeqData(HASHCODE hash)`. Called by AnimLoadFile 0x14a20 (unless its 2nd arg is set) right
after the file is added to the hashtable.

```c
void AnimProcessSeqData(u32 hash) {
    if ((hash & 0xff000000) != 0x04000000) return;
    u8 *f = hashtable_getitem(hash);
    pAnimData = f;                              // 0x1d6ac8
    if (!f) return;
    // flags: keep low nibble of the u32 at +0xc (byte-wise rewrite at 0x13918..0x13975)
    *(u32*)(f+0xc) &= 0xf;
    pAnimData = f + 0x10;
    for (i = 0; i < 10; i++) AnimReadF32();      // skip 0x10..0x37 (results discarded)
    s16 numFrames = *(s16*)(f+0x38), numTracks = *(s16*)(f+0x3a);
    pAnimData = f + 0x40;                        // (reads 4 bytes, skips 4)
    u8 *start = pAnimData;
    u32 half = 0; int parity = 0;
    for (tr = 0; tr < numTracks; tr++) {
        AnimReadKeyWord(&half, &pAnimData, &parity);      // track size word (value unused here)
        u32 acc = 0;
        do {
            u32 w;
            if (!(parity & 1)) { u32 d = *(u32*)pAnimData; pAnimData += 4; w = d & 0xffff; half = d >> 16; }
            else w = half;
            parity++;
            acc += (w & 0xffff) >> 6;
            for (m = w & 0x3f; m; m >>= 1) if (m & 1) pAnimData += 4;   // skip coefficients
        } while (acc < (u32)(numFrames - 1));
        if (parity & 1) parity++;                // drop a pending half-word: next track starts u32-aligned
    }
    f = hashtable_getitem(hash);
    if (hash in LIST_WITH_DISTANCE_TABLE) *(u32*)(f+0xc) |= 7;    // list in section 1
    u32 *h = hashtable_getitem(hash);
    h[1] = pAnimData - start;  h[0] = (u32)start;  h[2] = 0;
}
```

Note: the walk never reads the per-track size, and the result (`h[1]`) is not used by the decoder. The pointer
at +0 is what AnimFrameCopy starts from.

AnimLoadFile then does `AnimFileSizes.SomeQuantityFromAnimSequences += *(s16*)(seq+0x38)`, which is **numFrames**
(the field survives the in-place fix-up).

### 2.3 AnimFrameCopy — Xbox 0x13e50, PS2 0x1c5da8 (KEY)

cdecl `void AnimFrameCopy(void *out, sAnimSeq_tag *seq, sAnimSkin_tag *skin /*may be 0*/, float frame)`.
Callers: AnimFrameSet, AnimSeqTick (morph path), AnimGetFirstTrans, AnimGetFrameTransRaw, AnimDistanceTableCreate.

```c
void AnimFrameCopy(void *out, Seq *seq, Skin *skin, float frameArg) {
    float fr = (float)(int)frameArg;                         // __ftol2 truncation
    if (fr == -1.0f) fr = F32(seq,0x80);                     // "current frame" (NOT truncated: fractional t possible)
    if (fr < (float)S16(seq,0x86)) fr = S16(seq,0x86);       // first frame (1)
    if (fr > (float)S16(seq,0x88)) fr = S16(seq,0x88);       // last frame (numFrames)
    float t = fr - 1.0f;
    P[0]=1; P[1]=t; P[2]=t*t; P[3]=t^3; P[4]=t^4; P[5]=t^5;   // globals 0x1d6acc..0x1d6ae0
    u32 ti = (u32)(int)t;                                    // key search uses the integer part
    SkeletonInfo *sk = SkeletonTable[U8(seq,0x8d)];
    bool morph = (U32(seq,0x7c) >> 3) & 1;
    int nElems = morph ? U8(seq,0x8e) : sk->numBones;
    u32 *raw = PTR(seq,0x74);  raw[2] = raw[0];               // cursor = track data
    u32 *p = (u32*)raw[2];
    float *morphOut = out;  vec3 *transOut = out;  quat *quatOut = (quat*)((vec3*)out + nElems);
    bool useCache = (U32(seq,0x7c) >> 21) & 1;               // 0x200000
    u32 *cache = PTR(seq,0x6c) - 8;                          // 8 bytes per track, see 2.3.1
    int pending = 0;
    for (b = 0; b < nElems; b++) {
        bool skip = false; int ch;
        if (morph) ch = 6;
        else {
            if (pending) { AnimFrameEmitChannels(pending,&morphOut,&transOut,&quatOut); pending = 0; }
            if (skin && (s8)skin->boneHierarchy[b] >= 0) {    // skin+0x20; bit 7 clear = bone unused
                transOut++; quatOut++; skip = true;           // output left untouched
            }
            if (sk->transMask[b>>5] & (1u << (b&31))) ch = 5;
            else { if (!skip) { *transOut = sk->defTrans[b]; Vec_Mul(transOut, &Glb_SkinScale); transOut++; } ch = 2; }
        }
        do {                                                  // one track per iteration, ch = 5..0 or 6
            u32 head = *p;  u32 *next = (u32*)((u8*)p + (head & 0xffff));  p++;
            if (!skip) {
                u32 half = head, par = 1, acc = 0;            // key word #0 is head>>16
                if (useCache) {
                    cache += 2;
                    if (cache[0]) { acc = U16(cache,6); p = (u32*)((raw[2] + cache[0]) & ~1);
                                    half = U16(cache,4) << 16; par = cache[0] & 1; }
                }
                for (;;) {
                    if (useCache) { cache[0] = ((u8*)p - raw[2]) | par; U16(cache,4) = half>>16; U16(cache,6) = acc; }
                    u32 w;
                    if (par) w = half >> 16; else { half = *p++; w = half & 0xffff; }
                    par = !par;
                    acc += w >> 6;  m = w & 0x3f;
                    if (ti <= acc) break;
                    p += popcount(m);
                }
                for (i = 0; i < 6; i++) C[i][pending] = (m >> i) & 1 ? *(float*)p++ : 0.0f;   // 0x1d69c0 + 0x10*i
                CH[pending] = ch;                                                          // 0x1d6a34
                if (++pending == 4) { AnimFrameEmitChannels(4,&morphOut,&transOut,&quatOut); pending = 0; }
            }
            if (ch == 6) ch = 0;
            p = next;
        } while (ch-- != 0);
    }
    if (pending) AnimFrameEmitChannels(pending,&morphOut,&transOut,&quatOut);
    Vec_Mul((vec3*)out, &Glb_InvSkinScale);                   // bone 0 translation back to raw units
    if (!(U32(seq,0x7c) & 0x100000)) { U32(seq,0x7c) |= 0x100000; VEC(seq,0x18) = *transOut; }
    VEC(seq,0x24) = *transOut - VEC(seq,0x18);
}
```

Notes and quirks:
- **Integer frames only** in practice: every caller passes a value that is truncated, and the -1 path is never
  used by a caller.
- The last two lines were meant to record the root translation. But `transOut` points one past the last
  translation at that point, i.e. at `quat[0]`. So seq+0x18/+0x24 hold `quat[0].xyz` values. PS2 has the same
  bug (the `local_dc` pointer). No reader of +0x18/+0x24 was found in the anim module.
- The morph path also applies `Vec_Mul(out, InvSkinScale)` to the first three weights. This is harmless only
  when the skin scale is 1: FrameCopy is called from AnimSeqTick with skin 0, but Glb_SkinScale is global.
  Treat it as an original quirk.
- 2.3.1 **Unpak cache** (seq+0x6c, flag 0x200000): 8 bytes per track `{u32 (keyWordPtr-trackBase)|parity;
  u16 pendingHigh; u16 accBefore}`. It holds the state at the start of the key found last time, so the next
  call resumes there. It is only correct when frames never go backwards. AnimProcessScriptCmds 0x17e36 enables
  it only for non-looping playback of sequences longer than 0x100 frames when the script has flag 0x10000.
  It allocates `numElems*8` bytes. **Bug:** numElems is the low byte of numTracks (u8 at 0x3a), so a sequence
  with more than 255 tracks gets too small a cache. Skeleton 36 (62 bones, 258 tracks) would overflow, but no
  shipped sequence uses skeleton 36.
- Output layout is Xbox-specific: vec3 is 12 bytes (PS2 uses 16-byte vectors, so offsets differ there).

### 2.4 AnimFrameSet — Xbox 0x15f80, PS2 0x1c7d10 (KEY)

Custom: **ESI = seq, EDI = skin**; stack `(obj_tag *obj, float frac)`. Callers: AnimSeqTick, FUN_000171a0.

```c
void AnimFrameSet(obj_tag *obj, float frac /*, Seq *seq=ESI, Skin *skin=EDI */) {
    float fr = F32(seq,0x80);
    int nb = SkeletonTable[skin->skeletonNum /*+0x2c*/]->numBones;
    if (frac <= 0.0002f && frac >= -0.0002f) { AnimFrameCopy(PTR(seq,0x68), seq, skin, fr); return; }
    float nxt = fr + 1.0f;  if ((float)S16(seq,0x88) < nxt) nxt = (float)S16(seq,0x88);   // 0.0002 @0x15d384/-0.0002 @0x15d380
    if (frac == 1.0f) { AnimFrameCopy(PTR(seq,0x68), seq, skin, nxt); return; }
    float tmp[28*nb/4];                                        // stack 0xa80 bytes
    AnimFrameCopy(PTR(seq,0x68), seq, skin, fr);
    AnimFrameCopy(tmp, seq, skin, nxt);
    AnimFrameBlend(/*ECX=out*/PTR(seq,0x68), /*EDX=a*/PTR(seq,0x68), /*EAX=b*/tmp, obj, nb, frac, NULL);
}
```

The last frame is not wrapped: at `numFrames` the "next" frame is clamped to numFrames, so looping animations do
not blend the last frame into the first.

### 2.5 AnimFrameBlend — Xbox 0x12b40, PS2 0x1c32b8 (KEY)

Custom: **ECX = out, EDX = a, EAX = b** (each a 28*n pose buffer); stack
`(obj_tag *obj, int numBones, float t, u8 *boneList /* may be 0 */)`.

```c
mode = (t == 0) ? -1 : (t == 1) ? +1 : 0;
bool accurate = ScriptCam || min(AnimState[obj->0xb8].dist[0xec..0xef]) <= 0xf0;    // Quat_Slerp_Acc vs Quat_Slerp
if (!boneList) {
    if (mode < 0) { if (out != a) memcpy(out, a, 28*n); return; }
    if (mode > 0) { if (out != b) memcpy(out, b, 28*n); return; }
    if (out == a && out == b) return;
    for (i < n) { out.t[i].xyz = moveTowardsValue(a.t[i].xyz, b.t[i].xyz, t);    // lerp, clamped at b
                  accurate ? Quat_Slerp_Acc(t, a.q[i], b.q[i], out.q[i]) : Quat_Slerp(t, a.q[i], b.q[i], out.q[i]); }
} else {
    for (i < n) {
        bool listed = false;
        for (u8 *p = boneList; *p != 0xff; p += 2) if (*p == i) { listed = true; break; }   // list of {bone, ?} byte pairs
        if (listed || mode > 0) { out.t[i] = b.t[i]; out.q[i] = b.q[i]; }
        else if (mode < 0)      { out.t[i] = a.t[i]; out.q[i] = a.q[i]; }
        else { lerp-clamped t (same as moveTowardsValue, inlined); slerp as above }
    }
}
```

- `moveTowardsValue` 0x24000: `v = a + t*(b-a)`, clamped so it does not pass b.
- **Quat_Slerp 0xd5e40 (PS2 0x1e48f0) is not a slerp.** It returns `t < 0.5 ? a : b`. That is the LOD path for
  objects more than 0xf0 away in every viewport.
- **Quat_Slerp_Acc 0xd5e90** (cdecl `(float t, float *a, float *b, float *out)`):
  `d = a·b; neg = d<0; d=|d|; if (1-d <= 0.01) {sb=t; sa=1-t} else {th=acos(d); sb=sin(th*t)/sin(th);
  sa=sin(th-th*t)/sin(th)}; if (neg) sa=-sa; out = sb*b + sa*a` (not renormalised).
- The byte-pair bone list is read one byte per pair; the second byte is ignored. Listed bones take `b`
  outright. With `t == 1` the list makes no difference. Callers: AnimFrameResolve (list = script+0x54).

### 2.6 AnimSeqTick — Xbox 0x172b0, PS2 0x1c3008

Custom: **ECX = seq, EDX = obj, EAX = script (may be 0), EBX = animObj**.

```c
if (script) {
    seq.flags(0x7c) &= 0x1ffffff;
    copy script+0x74 bits: 0x10000000->0x10000000, 0x80000000->0x80000000, 0x40000000->0x40000000,
         0x20000000 -> seq 0x20000000 (and cleared on the script), 0x1000000->0x2000000, 0x2000000->0x4000000,
         0x4000000->0x8000000;
    F32(seq,0x80) = F32(script,0x8c) - (float)S16(seq,0x84) + 1.0f;       // frame = scriptTime - start + 1
}
if (PTR(seq,0x70)) PTR(seq,0x70)(obj, animObj->skin, seq);                   // custom frame callback
else if (seq.flags & 8) {                                                    // morph
    if (!AnimObjectIsClose(obj, 0xc)) return;
    AnimFrameCopy(PTR(seq,0x68), seq, NULL, F32(seq,0x80)); return;
} else {
    float frac = (script->0x74 & 0x10000) ? 0 : F32(seq,0x80) - (int)F32(seq,0x80);
    AnimFrameSet(obj, frac);                                                  // ESI=seq, EDI=animObj->skin (from EBX)
}
vec3 *root = PTR(seq,0x68);                                                  // pose[0].trans = raw root translation
vec3 d;
if (seq.flags & 0x60000000) { seq.flags &= 0x9fffffff; d = VEC(seq,0x0c); }  // just set / looped: reuse last delta
else d = *root - VEC(seq,0x00);
VEC(seq,0x00) = *root;
if (flags & 0x2000000) d.x = 0;  if (flags & 0x4000000) d.y = 0;  if (flags & 0x8000000) d.z = 0;
VEC(seq,0x0c) = d;                   // root motion for this tick (consumed by AnimFrameResolve)
*root = (vec3){0,0,0};               // bone 0 is drawn at the object origin
```

FUN_000171a0 (INVENTED `AnimSeqCallbackSmooth`) is used as the +0x70 callback by AnimSetUpdate (locomotion sets):
`AnimFrameSet(obj, frame - (int)frame)`. It always interpolates, even when the script has flag 0x10000.

### 2.7 AnimSeqSet (Xbox FUN_00017960 = PS2 AnimSeqSet 0x1c7bd0), AnimSeqNew, AnimSeqGetInfo, AnimSeqDelete

- **AnimSeqNew** 0x12ac0 (PS2 0x1c79b8) cdecl `(size_t poseBytes, MallocFlags f)`. It mallocs 0x90 bytes
  (malloc_anim_seq_structs), clears them (0x24 dwords), and mallocs `poseBytes` into +0x68, cleared. On failure
  it frees and returns 0. PS2 struct is 0xb0 bytes.
- **AnimSeqSet**: `seq = script[0x4c + 4*which]` (0 = bone seq, 1 = morph seq). It needs `animObj->skin`
  (+0x1c). It calls AnimSeqGetInfo(hash, seq, rawSeq), then sets:
  `F32(0x80) = frame; S16(0x86) = 1; S16(0x88) = S16(0x8a); VEC(0x0c) = 0; VEC(0x30) = obj->pos (obj+0x24);
  VEC(0x18) = VEC(0x24) = 0; flags = (flags & 0x1efffff) | 0x20000000` (this clears 0x100000 and the high
  bits). If `flags & 7`, it sets `PTR(0x64) = AnimDistanceTableGet(hash)`. If `which == 0` and
  `obj->0xb4 != 0`, it copies seq+0x54..0x60 (bound sphere) to animObj+0x08..0x14.
  `U8(0x8d) = skin->skeletonNum (+0x2c)`.
- **AnimSeqGetInfo**: see 2.1 (0x14420). Xbox inlines it in AnimGetFirstTrans, AnimGetFrameTransRaw and
  AnimDistanceTableCreate.
- **AnimSeqDelete** (PS2 0x1c7a50, inlined on Xbox, e.g. in AnimScriptDelete): frees +0x6c (unpak cache), then
  +0x68 (pose), then the struct.
- **AnimSeqGetHdrBuf** (PS2 0x1c8108) returns 0. AnimCacheInit/Enable/Disable/Dump (PS2 0x1c6b00..0x1c6b20)
  are empty stubs, and no Xbox counterparts exist.

### 2.8 Root motion: AnimGetFirstTrans 0x15d30 / AnimGetFrameTransRaw 0x15e40

- **AnimGetFrameTransRaw** (PS2 0x1c7b40) cdecl `(HASHCODE hash, int frame, float out[3])`. It takes a zeroed
  0x90-byte temp seq, runs the inlined GetInfo, sets `S16(0x86)=1; S16(0x88)=numFrames`, then calls
  `AnimFrameCopy(tmp, seq, NULL, (float)frame)` and returns `tmp[0]`, i.e. **bone 0's raw translation at
  `frame`**. Note: the temp seq's +0x8d is the file's skeleton byte.
- **AnimGetFirstTrans** (PS2 0x1c7ab8) cdecl `(sAnimScript_tag *s, vec3 *out) -> bool`. If `s->0x74 & 0x800000`,
  it runs GetInfo on `s->boneAnimSeq (+0x4c)` (the seq's own frame clamps are kept), calls
  `AnimFrameCopy(tmp, seq, NULL, s->time (+0x8c))` and returns bone 0's translation.
- In play, AnimSeqTick converts bone 0's per-tick change into `seq+0x0c` and zeroes it in the pose.
  AnimFrameResolve (2.10) rotates that delta by the object's matrix (or a script's matrix with flag 0x20000)
  and adds it to `obj+0x24` (position). **For export**, either keep bone 0's raw translation track as the
  root/armature motion, or zero it and move the armature object. The game does the latter, with the per-axis
  masks 0x2000000/0x4000000/0x8000000. Locomotion sets use script flags 0x8d000000, which zero x and z.

### 2.9 Distance table: AnimDistanceTableCreate (Xbox 0x16950), Get (0x174a0), DistanceToFrame2 (0x13420)

Table (malloc_anim_distance_table, `12 + 4*n` bytes), Xbox layout:
`{f32 total; s32 n; u32 0; f32 cum[n]}`. PS2 stores n as a float, but the Xbox writes an int (0x16c01
`mov [eax+4],ebx`) and reads it with `FILD` (0x134a3).

```c
n = loop ? numFrames-1 : numFrames;  total = 0;
for (i = 2; i <= n+1; i++) {
    j = (!loop && i > n) ? 2 : i;                          // non-looping: the last step repeats frame 2-1
    d = FrameCopy(j).trans[0] - FrameCopy(j-1).trans[0];   // raw root translation
    zero the components whose axis bit (1=x,2=y,4=z) is clear;
    m = |d|;  if (m < 1e-5) m = 0.01;
    total += m;  cum[i-2] = total;
}
```

AnimSeqSet only ever uses axes = 4 (z) and loop = 0.

**AnimDistanceTableDistanceToFrame2** 0x13420 is custom: **EAX = obj, EDI = animObj, EBX = script**, and it
returns ST0 (PS2 0x1c49a8). It is script speed mode 1: animation playback locked to distance travelled.

```
if (!obj || !obj->animState || !script->boneAnimSeq || !(tab = seq+0x64)) return 0;
d = animObj+0x64 (phase) + animObj+0x5c (distance this tick);  wrap d into [0, total)
i = (int)(d * n / total);  adjust i so cum[i-1] <= d < cum[i]
frame = (i+1) + (i>0 ? (d-cum[i-1])/(cum[i]-cum[i-1]) : d/cum[0]);
animObj+0x64 = d;
delta = frame - script->time(+0x8c);
if moving forward (animObj+0x5c >= 0) and delta < 0: delta += n     (wrap)
if moving backward and delta >= 0: delta = -((n - frame) + time)
return delta;                          // AnimScriptTick uses it as this tick's speed
```

### 2.10 AnimFrameResolve — Xbox 0x160a0, PS2 0x1c3d30 (KEY; the cross-fade part is also in spec-script.md 5.7)

cdecl `(obj_tag *obj, sAnimObject_tag *animObj)`. It builds lists with AnimListBuild 0x122e0
(`GlbAnimList` count 0x1d77a0, entries 0x1d77a4).

1. List 0x100 mode 1 ("blend-append" scripts S): for each S, AnimFrameBlend(out = a =
   `S->0x48(parent script)->boneAnimSeq->pose`, b = `S->boneAnimSeq->pose`, obj, nb, t = `F32(S,0x84)`,
   list = `S->0x54`). nb is the bone count of the first entry's seq skeleton.
2. List 0x6800 mode 2: advances the cross-fade timers. If `S8(S,0xb2) > 0`, `S+0xa4 += FRAME_RATE_MUL`. If it is
   `< 0`, `S+0xa4 -= FRAME_RATE_MUL` and flag 0x200000 is set when the timer reaches 0 or below. The timer is
   clamped to `[0, S+0xa8]`, and `weight S+0x98 = S+0xa4 / S+0xa8`.
3. List 0x100 mode 2 (base scripts): sets `animObj+0x50 |= 1`.
   - One script: memcpy its seq pose (`animObj+0x88` bytes) to `animObj+0x34`, rootDelta = seq+0x0c,
     rootPos = seq+0x00.
   - Several: sort by `S+0x80` ascending, then for k = 0..n-2:
     `AnimFrameBlend(out=animObj+0x34, a = k ? animObj+0x34 : list[0].seq.pose, b = list[k+1].seq.pose,
     t = 1 - list[k].weight, no list)`.
   - Root delta and position are blended the same way with a clamped lerp.
   - If `list[0]->0x74 & 0x8000` (k = 0), the rotation of bone 0 is copied from b into a first. If
     `list[k+1]->0x74 & 0x80000000`, it is copied from a into b.
4. List 0x2000: if a pose was already written, AnimFrameBlend(pose, pose, first.seq.pose, t = 1.0, list =
   first+0x54). Since t = 1 this copies the whole pose (see 2.5). Otherwise it copies it and takes rootPos.
5. List 0x4000: the same with t = `F32(first,0xa4)` and no list.
6. If any pose was written: `animObj+0x54 = rootPos.y + animObj+0x58` (times 0.8627321 in multiplayer unless
   `animObj+0x50 & 0x400`). rootDelta is transformed by the matrix of the first script with flag 0x20000 (or
   obj+0x70), passed to the callback `animObj+0x18`, and added to `obj+0x24`. Scripts with flags
   0x200000|0x100000, or with `S+0xb2 == 0`, are deleted.
7. If `skin+0x2a` (morph count) is set and the object is close (or ScriptCam), AnimFacialBlend runs. Then the
   callback at animObj+0x28 is called with `(obj, animObj, nb, pose, pose + 12*nb)`.

### 2.11 KeyFrame_Interp 0xd8ac0 (PS2 0x1e1910) and Spline_Interp 0xd8df0 (PS2 0x1e2028)

These are **not used by skeletal sequences**. The callers are Door_Interp and SS_Update (scripted objects). They
take a `framelist_tag` array of 28-byte keys `{vec3 pos; quat q (xyzw)}`, a frame range `(u16 from, u16 to)`
(which may run backwards) and a float frame, and write a _MATRIX.
- KeyFrame_Interp takes keys `floor/ceil` of the frame, lerps pos with moveTowardsValue and
  Quat_Slerp_Acc's the rotation (unless the two are equal), then Quat_QuatToMat plus translation at m[12..14].
- Spline_Interp uses 4 keys (i-1, i, i+1, i+2) for a spline on the position, with SPLINE_INTERP mode as the
  last arg.

## 3. Runtime struct `sAnimSeq_tag` (Xbox, 0x90 bytes)

Size: AnimSeqNew `Mem_Malloc(0x90)` + `rep stosd` of 0x24 dwords; AnimGetFrameTransRaw clears a 0x90 stack copy
the same way.

| off | size | type | name (INVENTED unless noted) | evidence | conf. |
|---|---|---|---|---|---|
| 0x00 | 12 | vec3 | rootPrev: raw bone-0 translation at the last tick | AnimSeqTick `Vec_Copy(pose, seq)`; FrameResolve reads it as rootPos (`.y` -> animObj+0x54) | confirmed |
| 0x0c | 12 | vec3 | rootDelta: root motion of this tick | AnimSeqTick; zeroed by AnimSeqSet; FrameResolve local_c | confirmed |
| 0x18 | 12 | vec3 | firstTrans (buggy: quat[0].xyz of the first decode) | FrameCopy end | confirmed |
| 0x24 | 12 | vec3 | transRelFirst (same bug) | FrameCopy end | confirmed |
| 0x30 | 12 | vec3 | objPosAtSet | AnimSeqSet `Vec_Copy(obj+0x24, seq+0x30)` | confirmed |
| 0x3c | 24 | f32[6] | rootBox (raw+0x20..0x37) | GetInfo | confirmed copy; meaning likely |
| 0x54 | 16 | f32[4] | boundSphere (raw+0x10..0x1f) | GetInfo; AnimSeqSet -> animObj+0x08 | confirmed copy |
| 0x64 | 4 | float* | distanceTable (Ghidra "animDistTable") | AnimSeqSet; DistanceToFrame2 `[EBX+0x4c]->+0x64` | confirmed |
| 0x68 | 4 | void* | pose buffer (Ghidra "rawData"): vec3[nb] + quat[nb], or float[numElems] for morph | AnimSeqNew; FrameSet/FrameCopy output | confirmed |
| 0x6c | 4 | u32* | unpak cache (Ghidra "animUnpakOpt"), numElems*8 bytes | FrameCopy; ProcessScriptCmds 0x17e36; freed by AnimScriptDelete | confirmed |
| 0x70 | 4 | fn* | frame callback `(obj, skin, seq)`, replaces AnimFrameSet | AnimSeqTick; AnimSetUpdate sets FUN_000171a0 | confirmed |
| 0x74 | 4 | sAnimRawSeq_tag* | rawSeq (the hashtable item) | GetInfo; FrameCopy `raw[2]=raw[0]` | confirmed |
| 0x78 | 4 | u32 | hashcode | GetInfo | confirmed |
| 0x7c | 4 | u32 | flags: bits 0-3 from file (0-2 distance axes, 3 morph); 0x100000 firstTrans captured; 0x200000 unpak cache on; 0x2000000/0x4000000/0x8000000 zero root delta x/y/z; 0x10000000, 0x20000000 (just set: reuse delta), 0x40000000 (looped: reuse delta), 0x80000000 copied from script | FrameCopy, SeqTick, AnimSeqSet, ProcessScriptCmds | confirmed (0x10000000/0x80000000 meaning: see spec-script.md) |
| 0x80 | 4 | float | current frame (1-based) (Ghidra "animDistanceTable") | SeqTick `= scriptTime - start + 1`; FrameCopy -1 path; FrameSet | confirmed |
| 0x84 | 2 | s16 | start time in script frames | ProcessScriptCmds `*(u16*)(seq+0x84) = cmd[1]`; SeqTick | confirmed |
| 0x86 | 2 | s16 | first frame clamp (= 1) | AnimSeqSet, FrameCopy | confirmed |
| 0x88 | 2 | s16 | last frame clamp (= numFrames) | AnimSeqSet, FrameCopy, FrameSet | confirmed |
| 0x8a | 2 | s16 | numFrames (raw+0x38) | GetInfo | confirmed |
| 0x8c | 1 | u8 | raw+0x3e | GetInfo | confirmed copy, use unknown |
| 0x8d | 1 | u8 | skeletonNum (Ghidra name) | GetInfo, then AnimSeqSet from skin+0x2c; FrameCopy, FrameResolve | confirmed |
| 0x8e | 1 | u8 | numElems (raw+0x3a low byte) | GetInfo; FrameCopy morph count; cache size | confirmed |
| 0x8f | 1 | - | pad | | |

Relationship to PS2 (0xb0 bytes, 16-byte vectors): PS2 offset = Xbox offset + 0x1c for the fields from
distanceTable (PS2 0x80) onwards, e.g. flags 0x98, frame 0x9c, start 0xa0, clamps 0xa2/0xa4, numFrames 0xa6,
skelNum 0xa9, numElems 0xaa. The vec3s at the top are 16 bytes each on PS2.

`sAnimRawSeq_tag` at runtime is the file itself, with its first three words replaced by
`{trackData*, trackDataSize, cursor}` (section 1).

## 4. How to produce the pose for frame f (reimplementation / export recipe)

1. Load the seq. Its skeleton `sk = SKEL[file+0x3c]` (in play, the skin's skeleton, which matches) and the
   skin (for the hierarchy and skin scale).
2. For integer f in 1..numFrames: decode every track at t = f-1 (section 1.1). Assign the tracks to bones in
   order (1.2). Derive w, then multiply translations by skinScale except bone 0.
3. For a fractional f (the game's normal case), decode floor(f) and min(floor(f)+1, numFrames), then lerp the
   translations and Quat_Slerp_Acc the rotations by `frac` (skip when |frac| <= 0.0002).
4. Bone 0's translation is root motion (raw units). The game zeroes it after taking the delta.
5. Compose the world pose with the skin's parent bytes: root `M = QuatTransToMat(q,t) * objMtx`; child
   `q_w = q_parent (x) q_local` (MultiplyQuaternionAltOrder) and `pos_w = parentMtx * t_local`
   (see spec-pose.md).
6. Timing: 60 animation frames per second at speed 1.

`decode_seq.py` implements steps 1-4: `Sequence.frame(f, skel)`, `Sequence.all_frames(skel)` and
`Sequence.pose(f_float, skel)`, plus `root_motion()`, `distance_table()` and `--csv` export
(frame,bone,tx,ty,tz,qx,qy,qz,qw). It loads skeletons from `lvl/07000001.bin`, which holds all 45 skeletons;
spec-load.md shows they are byte-identical across the 32 levels.

## 5. Validation results (`python decode_seq.py --validate <dump>` / `--validate-bundles lvl`)

Dump folder (582 files, 9,934,272 bytes):
- 582/582 clean: header, every track's size word equals its parsed length, every pending half-word is 0,
  tail is zero padding < 32 bytes, file size is a multiple of 32, and the hashcode equals the file name.
- 551 bone sequences + 31 morph sequences (flag 8, 22 tracks each = 22 facial morph weights, skeleton byte 0
  or 1). Every bone sequence's numTracks equals its skeleton's `3*numBones + 3*popcount(mask)`.
- Frames per sequence: 1 .. 5,896 (04000009); 96,397 frames in total.
- Skeletons used: 0 (446 seqs), 1 (4), 2 (12), 4 (8), 5 (6), 9 (3), 11 (2), 14 (7), 15 (1), 16 (10), 18 (5),
  20 (7), 22 (8), 23 (14), 28 (6), 39 (12). Skeleton 0 = 73-bone human with only bone 0 translated (222 tracks).
- Keys: 565,413 in total. Masks: 0x1: 93,043, 0x3: 84,641, 0x7: 84,344, 0xf: 66,476, 0x1f: 86,041, 0x3f: 150,868.
  No other mask values occur. Key lengths are 0..256 (a 0-length key is legal and only matters at t = 0 or as a
  zero-width segment); the most common is 2 (73,504), and 256 is the longest.
- 5,781,779 quaternions decoded. Only 2 have |q| > 1+1e-3 (x²+y²+z² slightly above 1, so w = 0); the max
  deviation is 1.5e-3. Morph weights span -0.40 .. 1.23 (the polynomial fit overshoots; the game does not clamp them in FrameCopy).

All bundles (`lvl/07*.bin`, every 04 entry deduplicated by name): see section 5.1.

### 5.1 Bundle-wide results

`python decode_seq.py --validate-bundles lvl` (5m50s): every 04 entry of the 32 bundles, deduplicated by name
(no name had differing content between bundles).
- 1,279 sequences, 42,300,992 bytes: 1,134 bone + 145 morph. **All 1,279 parse with every byte accounted for.**
  505,544 frames in total; the longest is 5,896.
- Skeletons referenced: 0 (785), 1 (70), 8 (27), and 36 others with 1-14 sequences each. Every skeleton except 12,
  19, 27, 31 and 32 is used.
- Keys: 3,087,701. Masks 0x1: 463,125, 0x3: 1,011,128, 0x7: 872,904, 0xf: 246,877, 0x1f: 197,925,
  0x3f: 295,742. Again only these six. Lengths 0..256, and 256 is common (154,479: long constant/cubic stretches).
- **12 sequences do not match their skeleton**: 04000514/516/517/519/610/784 (skeleton 26 expects 141 tracks,
  file has 171) and 04000689/68a/68b/68c/68f/6a8 (skeleton 33: 129 vs 159). Both are +30 tracks = the same bone
  count with 14 translated bones instead of 4 or 5, so they look like stale exports from an older skeleton.
  AnimFrameCopy never checks numTracks. With the shipped skeletons the game would read these with the wrong
  channel assignment after the first extra translated bone. They are in bundles 07000009-0b/14/15 and
  07000011-16/23-29/4a/4b. Whether any skin plays them is not known. decode_seq.py reports them and does not
  decode them.
- 26.8M quaternions: 213 have |q| > 1+1e-3. The large max (1.6e14) is **0400078e** (skeleton 42), bones 63 and
  65: every rotation track is the constant 0xd6a7c5ac = -9.2e13, an exporter sentinel for bones without data.
  The game would compute w = 0 and a degenerate matrix there. Presumably these bones are unused by the skin.

## 6. Frame-to-frame continuity

Across the bundles, 61 of 1,134 bone sequences have some frame step above 90 degrees; 26,668 steps in total.
Nearly all of them are in a few long cutscene sequences, each with a range of frames where many bones (the
same set: 1,2,5,6,9,12,15,18,19,20,28,30,32,33,35,42,43,45-48 of skeleton 0) jump every frame:
04000185 (frames 3-372 in the dump; 1-2083 overall), 04000723 (751 frames, 9-502), 0400069d/0400069e (2,027
frames, 3-237/3-507), 0400018b and 040003f9 (4,327 frames, 3478-4327), 0400017d/0400017f. In those ranges
the keys are mostly length-2 linear or length-3 quadratic. Looking at every other frame does not make them
smooth either, so this is not a 30-fps interleave. My reading is that the source data is broken there (or
the characters are off-camera), and the decoder reproduces what the game computes. **For export, check these
ranges.** Details from the dump:
- **04000185** (2,700 frames): 7,990 steps. Frames 1-372 alternate between two unrelated poses on about 27 bones
  (every key there has length 2 and is linear). The root translation also runs from (122,602,-47) down to
  (61,302,-23) and back. From frame 373 on the data is constant. The data is broken for that range. The
  decoder reproduces what the game computes; whether the game ever plays that range is unknown.
- **0400006a**: bone 21 has single-frame 180-degree spikes (frames 41, 55, 61, 69, 147, 155, 161, 263, 267).
  These are w-sign glitches: the stored z crosses the 2.0 threshold for one frame at a key boundary.
- 04000173 f552 (7 bones at once) and 04000198 f9-10: cuts or very fast motion. 04000101, 04000680 and
  04000438 have 1-2 isolated steps.
- Translation steps of up to 1.5 units in one frame (040004d8 f117, bone 43, a first-person weapon skeleton)
  are probably attachment bones being teleported (guess: e.g. a magazine swap in a first-person weapon anim).

## 7. Open questions / possible original bugs

1. seq+0x18/+0x24 capture `quat[0].xyz`, not a translation (FrameCopy's pointer ends past the translation
   array). This is present on PS2 too. No reader was found, so it is probably harmless.
2. The morph path of AnimFrameCopy multiplies the first 3 morph weights by `Glb_InvSkinScale`.
3. The unpak cache is sized by the u8 numElems (numTracks & 0xff). It would overflow for more than 255
   tracks (skeleton 36).
4. The unpak cache resumes from the last key and is wrong if the frame decreases (reverse play or rewinding),
   because a key found earlier cannot be un-found. The enabling conditions (non-looping, flag 0x10000, more
   than 256 frames) probably keep it forward-only, but reverse speed was not checked.
5. The users of seq+0x3c..0x50 (root box) and seq+0x8c are not found in the anim module (they may be in game
   code). The meaning of the root box's z ordering (max z first) suggests "forward = -z".
6. Long cutscene sequences contain garbage frame ranges (section 6). Whether the cutscene scripts play those
   ranges needs checking against the 06 scripts that reference them.
10. 12 sequences have 30 more tracks than their skeleton (section 5.1) and 0400078e carries a -9.2e13 sentinel
    on two bones.
7. AnimFrameBlend's bone list is a list of byte pairs of which only the first byte is read. The format of the
   list (script+0x54, set outside the anim module) needs confirming with its writer.
8. In AnimFrameResolve step 4 (list 0x2000) the blend is done with t = 1.0, which makes the bone list a no-op.
   The disassembly confirms it (0x166ad `push 0x3f800000`), so either the lists are handled elsewhere or this
   is an original oversight.
9. Ghidra DB: bytes 0x11000-0x11005 read "Hello\0". The real bytes of AnimReadF32's prologue are missing from
   the database (function FUN_00011006 in the list).
