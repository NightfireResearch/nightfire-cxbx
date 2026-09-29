# Animation load-time data formats: skeletons, skins, loader (spec-load)

Scope: AnimLoadFile and friends on the Xbox action engine (`/Xbox_EU/default.xbe`), with the PS2 build
(`/PS2_EU_51258/ACTION.ELF`) used for names and intent. Everything here was read-only research; offsets and sizes
are from the Xbox binary unless marked PS2.

Tools written (all in this folder):
- `edl.py`: reads `disc/eurocom/filesys.dNN` and decompresses EDL sections. It is a Python port of the Xbox
  `Inflate_huffman` 0xd3a80 (the repo has no C++ version of it), plus `Inflate_bitwise` and direct copy. Usage:
  `python edl.py 07000001.bin out.bin`.
- `bundle.py`: walks a level bundle's directory. `extract_all.py` unpacks all 32 level bundles into `lvl/` and
  copies out every skeleton (`lvl/skel/`), skin (`lvl/skin/`) and anim script (`lvl/script/`).
- `parse_skeleton.py` and `parse_skin.py`: the format validators (results in section 6).

## 0. Headline findings

1. **Skeletons are not separate files.** Each one is a directory entry of **type 6**, named `SKELnnnn`, inside a
   level bundle `07xxxxxx.bin`. LoaderProcess 0xbe7b0 passes them to AnimSkeletonProcess with a pointer into the
   bundle's buffer. That is why the dump folder has none: it only captures `psiFileLoad`. A skeleton is a 16-byte
   header followed by `numBones` default local bone offsets. It holds no hierarchy. There are 45 skeleton indices
   (0-44), and every copy is byte-identical across the 32 levels.
2. **The hierarchy lives in the skin**, one byte per bone:
   - bit 7 = "this skin uses the bone"
   - bits 0-6 = parent bone index (0x7f = root)

   These are the "0x80.. bytes". Parents always come before their children.
3. **The skin's 28-byte-per-bone records are inverse bind matrices** (trans + quat). In all 1031 bundle
   copies (150 distinct skins) every quaternion is (0,0,0,-1) to within 3.3e-4, so the rotation is the identity:
   the bind pose is axis-aligned. Each translation is minus the bone's world bind position. For the weapon/arm
   skins that position matches the sum of skeleton offsets × skinScale along the skin's parent chain to within
   1e-4 (one exception: 05000046, bone 39, off by 0.28). Human skins differ from the shared skeleton by up to
   about 1 cm (0500002b by 8 cm), so each skin has its own bind pose. **For export, take the bind position of
   bone b as -t[b] from the skin, not the skeleton offsets.**
4. **Datums are attachment points**, 36 bytes each: `{s32 id; s32 bone; vec3 t; quat q}`. AnimObjectDraw draws
   `datumEntity[i]` at `QuatTransToMat(q,t) * boneMtx[bone]`.
5. **AnimPostLoadInit overwrites the datum table** of every skin on skeleton 0 or 1 with the table of skin
   `0x05000007`, the reference human with 60 datums. One exception: `0x0500002f` gets 05000007's datums followed
   by its own.
6. **Rigged hashcodes with bit 0x00100000 set are sleeves.** Every first-person weapon skin (skeletons 2-44,
   38+ bones) names its arm mesh as `0x021xxxxx`. The loader replaces that hashcode with the first loaded entry of
   `SleeveEnts_SleeveTable` 0x1634c0.
7. **The loader does not patch the file in place.** AnimProcessSkinData allocates a new 0x30-byte `sAnimSkin_tag`,
   points it into the file, and swaps it into the hashtable (`hashtable_modify`). AnimSkeletonProcess and
   AnimProcessScriptData only walk a cursor (`pAnimData` 0x1d6ac8) and change nothing. They look like leftovers
   of old fix-up code.

## 1. Where the data comes from: level bundles

`LoaderLoad` 0xbe910 (called from ResetMap_Load) opens `"%s%8.8x.bin"` of the level hashcode, e.g.
`07000001.bin`. It opens it in single-file mode (`psiFileSetSingleFileMode(1)`). On disc the file is inside
`filesys.dNN`, found by crc32 of the lower-cased, backslashed name, and it is EDL-compressed (see `edl.py`).

Decompressed bundle layout:

| off | size | meaning |
|---|---|---|
| 0x00 | u32 | dirSize: byte size of the directory that follows the header (LoaderLoad reads `*pDirHeader`) |
| 0x04 | u32 | numFiles (LoaderLoad counts it down as it loads) |
| 0x08 | 24 | not read by the loader (looks like a hash/checksum, then zeros) |
| 0x20 | dirSize | entries `{u32 len; u8 type; char name[] (NUL-terminated)}`. FUN_000be630 parses the hex name into the hashcode (DirFileHash 0x2791c8); type goes to DirFileType 0x279184, len to DirFileLen 0x279174 |
| 0x20+dirSize | ... | each file's data in entry order, `len` bytes each, every len a multiple of 32 (zero padded) |

LoaderLoad state 4 sets `dirFileBuf` (0x279168) to the current file's data and calls LoaderProcess 0xbe7b0. In
single-file mode `psiFileLoad` just returns `dirFileBuf`, so no copy is made: anim data lives inside the bundle
buffer (Malloc_Type_2). LoaderProcess switches on the type:

| type | handler | contents seen in 07000001 |
|---|---|---|
| 0, 2 | parsemap_parsemap(hash,0) | 01xxxxxx graphics/entity packs (hold the 02xxxxxx meshes) |
| 11 | MemType=1, then parsemap(hash,0) | 01xxxxxx |
| 1 | **AnimPostLoadInit()**, then parsemap(hash,1) | the level's 01xxxxxx map file; always the last entry |
| 3, 4, 5 | AnimLoadFile(hash, 0) | 3 = 05 skins, 4 = 04 sequences, 5 = 06 scripts (AnimLoadFile dispatches on the hash, not the type) |
| 6 | **AnimSkeletonProcess(dirFileBuf)** | SKEL0000..SKEL0044 |
| 7 | Script_Load (game scripts, not anim) | 06xxxxxx |
| 8 | MenuManager_Load | 08xxxxxx |
| 12 | registers a LoadableFiles[] entry (max 16) | |

The order in the bundle matters, and the data follows it: 04 sequences (477), then skeletons (45), then 01
graphics (types 0/2/11), then 05 skins (30), then 06 anim scripts (477), then type 7, then type 8, then type 1.
Skins come after the skeletons because AnimProcessSkinData reads `SkeletonTable[skelNum]->numBones`. They come
after the graphics because skins resolve 02xxxxxx hashcodes to celglists. AnimPostLoadInit runs last, when every
skin is loaded.

## 2. Skeleton record (INVENTED NAME for the record: `SkeletonInfo`; Ghidra has a 4-byte guess of that name, PS2 guesses `SkeletonHeader` at 252 bytes, which is wrong)

| off | size | type | name | evidence | conf. |
|---|---|---|---|---|---|
| 0x00 | 2 | s16 | skeletonIdx | 0x11060-0x11075: `SkeletonTable[(short)data[0..1]] = data`; equals the NNNN of `SKELnnnn` in all 270 records | confirmed |
| 0x02 | 1 | u8 | numBones | 0x1107c; read by 13 functions (`movzx [skel+2]`) | confirmed |
| 0x03 | 1 | u8 | numTransBones | equals popcount(transMask) in all 45 skeletons; no Xbox reader found | likely |
| 0x04 | 12 | u32[3] | transMask | AnimFrameCopy 0x14013 `TEST [skel + (b>>5)*4 + 4], 1<<(b&31)`: a set bit means the bone has an animated translation channel (5 channel groups rather than 2) | confirmed |
| 0x10 | 12*numBones | vec3[] | boneOffset | AnimFrameCopy 0x13f7c `skel+0x10`, `Vec_Copy(offs[b]); Vec_Mul(Glb_SkinScale)` for bones whose mask bit is clear: the default local translation (bind pose relative to the parent, unscaled) | confirmed |
| end | | | zero padding to 32 | all records | confirmed |

- Size is `16 + 12*numBones`, padded to a multiple of 32. The mask has room for 96 bones; the largest skeleton has 73.
- `SkeletonTable` 0x1d7578 is `SkeletonInfo*[128]`, cleared by AnimPreLoadInit.
- The data (from `python parse_skeleton.py`):
  - skeletons 0, 1, 8, 12: 73 bones; skeleton 42: 71 bones. These are full humans, translated bone 0 only.
  - skeletons 2-5, 9, 10, 13-18, 20-26, 28-30, 33-41, 43, 44: 38-62 bones. These are first-person arms plus weapon
    bones. Bones 0, 1 and 19 are translated, plus the weapon parts.
  - skeleton 6/7: 6 bones; 11: 9 bones; 19: 11 bones; 27: 33 bones; 31/32: 5 bones.
- The hierarchy is NOT in the skeleton. It comes from the skin's bone bytes (section 3), so every skin on a
  skeleton carries its own copy of the parent table.

## 3. Skin file (hashcode type 0x05) - byte layout

Taken from the cursor walk in AnimProcessSkinData 0x144c0. The Xbox reads every multi-byte value
byte-by-byte (unaligned-safe). Floats go through FUN_00011000, whose first 6 bytes read "Hello" in this Ghidra
database; its real body is `push ecx; mov eax,[pAnimData]` and it is a little-endian float reader. PS2 reads
aligned words.

| file off | size | field | goes to |
|---|---|---|---|
| 0x00 | 4 | own hashcode | skipped (cursor += 4) |
| 0x04 | 12 | f32 skinScale[3] | skin+0x10 |
| 0x10 | 1 | numRigged | skin+0x28 |
| 0x11 | 1 | numDiscrete | skin+0x29 |
| 0x12 | 1 | numMorphs | skin+0x2a |
| 0x13 | 1 | numDatums | skin+0x2b |
| 0x14 | 1 | skeletonNum | skin+0x2c |
| 0x15 | numBones | u8 bone[] - bit7 used, bits0-6 parent (0x7f root) | skin+0x20 = pointer to it |
| .. | numDiscrete | u8 discreteBone[] (bone each rigid part hangs on) | skin+0x0c = pointer (only if numDiscrete) |
| .. | 0-3 | align to 4 (only if numDiscrete) | |
| .. | 4*numDiscrete | u32 discreteHash[] (02xxxxxx) | overwritten IN PLACE with celglist*; skin+0x08 = pointer |
| .. | 0-3 | align to 4 (only if numRigged) | |
| .. | 4*numRigged | u32 riggedHash[] (02xxxxxx, or 021xxxxx = sleeve) | overwritten IN PLACE with celglist*; skin+0x04 = pointer |
| .. | 28*numBones | {vec3 t; quat q(x,y,z,w)} per bone, **only if numRigged** | converted into a new `_MATRIX[numBones]` (0x3c each) at skin+0x24 |
| .. | 36*numDatums | datum[] (NOT aligned) | skin+0x1c = pointer |
| .. | | zero padding to a multiple of 32 | |

The hashcode arrays are the only in-place writes to the file. The skin's slot in the hashtable is re-pointed at
the new struct, so the raw file can no longer be reached by hashcode.

Datum record (36 bytes; the stride is 0x24 in AnimDatumGetIndex 0x135bc, AnimPostLoadInit `*9` dwords and
AnimObjectDraw):

| off | type | name | evidence |
|---|---|---|---|
| 0x00 | s32 | id (compared against datumNum; weapon_data.whichBone holds such ids) | AnimDatumGetIndex 0x135b7 |
| 0x04 | s32 | bone (-1 = none, else a palette index) | AnimObjectDraw: `if (bone >= 0) Concat(boneMtxs + bone*15, m)` |
| 0x08 | vec3 | t | `Quat_QuatTransToMat(datum+0x14, datum+8, &m)` |
| 0x14 | quat | q (x,y,z,w) | same |

Observed values:
- Human skins (skeleton 0/1) use ids 0-99. `0x05000007` alone has 60 datums.
- Weapon/arm skins use small ids 0-7, plus a few one-offs (24, 40, 44, 45, 96-100).

Quaternion order: `Quat_QuatTransToMat` 0xd59e0 treats `q[3]` as the scalar: m[0] = 1-2(yy+zz), m[1] = 2xy+2zw,
and so on. So quaternions stored in skins and datums are **XYZW**, not WXYZ as the brief says. Seq data should be
checked separately. The matrix is written row-vector style (translation in m[12..14]). m[3], m[7] and m[11] are
not written.

## 4. `sAnimSkin_tag` (runtime, Xbox): 0x30 bytes

The size is from `Mem_Malloc(0x30, 0x2004)` at 0x144f5. PS2 uses 0x40 because its `_VECTOR` is 16 bytes. Mem_Malloc
does not zero the block, so fields set only conditionally hold garbage otherwise. A reimplementation should
zero-fill.

| off | size | type | name | set by / evidence | conf. |
|---|---|---|---|---|---|
| 0x00 | 4 | HASHCODE | hashcode | NOT set at load; AnimObjectNew 0x11440 writes the fixed-up hashcode (`*(u32*)skin = hc`) on every object creation; AnimGetCollData 0x136f4 compares it with 0x05000078 (astronaut) | confirmed |
| 0x04 | 4 | celglist_tag** | riggedBodyList | 0x1466a (only if numRigged) | confirmed |
| 0x08 | 4 | celglist_tag** | discreteObjList | 0x145e1 (only if numDiscrete) | confirmed |
| 0x0c | 4 | u8* | discreteBone ("glistToBoneLinkage") | 0x145ba (only if numDiscrete) | confirmed |
| 0x10 | 12 | _VECTOR | skinScale | 0x1451c-0x1452c | confirmed |
| 0x1c | 4 | datum* | datums ("animDatums") | 0x1490c (only if numDatums); replaced by AnimPostLoadInit | confirmed |
| 0x20 | 4 | u8* | bones ("boneHierarchy"): bit7 used, low7 parent | 0x14596; AnimGetCollData 0x1374c `& 0x7f`; AnimFrameCopy tests the sign bit | confirmed |
| 0x24 | 4 | _MATRIX* | invBindMtx[numBones] ("matrixList"), stride 0x3c | 0x14733, Mem_Malloc(numBones*0x3c, 0x2104) (only if numRigged) | confirmed |
| 0x28 | 1 | u8 | numRigged | 0x1453c | confirmed |
| 0x29 | 1 | u8 | numDiscrete | 0x1454c | confirmed |
| 0x2a | 1 | u8 | numMorphs | 0x1455c; AnimObjectNew allocates numMorphs floats at animObj+0x38 | confirmed |
| 0x2b | 1 | u8 | numDatums | 0x1456c | confirmed |
| 0x2c | 1 | u8 | skeletonNum | 0x14581 | confirmed |
| 0x2d | 3 | - | padding | | |

How the other structs use it:
- AnimObjectNew allocates `animObj+0x2c` (celglist*[numDatums]: entity drawn at each datum) and `animObj+0x3c`
  (u8[numDatums]: 0 hidden, 0xff always, n = frames left, decremented per draw). Here animObj = AnimState+0x58.
- For each discrete part it creates a child object via `Control_CreateObjEx(celglist = discreteObjList[i])`,
  stored in `animObj+0x48`.
- If numRigged is non-zero, the object's graphics are set to riggedBodyList[0]. AnimObjectDraw draws every
  rigged body with `psiDrawSkinObjectMatrix`, replacing body 0 with the sleeve glist at `animObj+0x30` when that
  is set (AnimObjectSetSleeveType).
- numBones > 70 sets animObj flag 2 (human), plus 0x400 when skeletonNum != 1.

### Where the vertex and weight data lives (for Blender export)

The skin holds no geometry. `riggedHash[]` and `discreteHash[]` are 0x02xxxxxx entity hashcodes (checked on all
1031 bundle copies: 2853 discrete hashes, all 0x020xxxxx; rigged hashes are 488 0x020xxxxx and 537 0x021xxxxx
(sleeve); 0xffffffff, meaning NULL, is handled by the code but never occurs). `hashtable_hashcode_to_celglist` resolves them.
They are defined inside the 01xxxxxx graphics packs of the same bundle, as `KXE\x06` chunks followed by the
hashcode and a Maya name. For example, 020009d0 (the arms mesh of 050000ab) is at 0x244ac in `010001F8.bin` with
name "polySurface1", and 020004c2 (the body of 05000007) is in `01000092.bin`.

For export:
- The rigged mesh's vertices must carry palette indices into the skeleton's bones, in the same bone numbering,
  plus weights. `psiBuildMatrixPalette` 0xdd290 combines `invBindMtx[b]` with `boneMtxs[b]`.
- Discrete parts are rigid: part i follows bone `discreteBone[i]`.
- The KXE entity format has not been decoded yet; that is the next step.

## 5. Functions

Registers are listed as the real Xbox convention. "cdecl" means stack arguments, caller cleans up.

### AnimLoadFile - Xbox 0x14a20, PS2 0x1be668 (KEY)
`void __cdecl AnimLoadFile(HASHCODE hc, char noSeqProcess)`, stack args. Only caller: LoaderProcess 0xbe82d, which
passes (DirFileHash, 0).
```c
switch (hc & 0xff000000) {
case 0x04000000:                                   // animation sequence
    sprintf(buf, "%s%8.8x.bin", "", hc);           // 0x15d2fc, prefix 0x15d308 (empty)
    sprintf(buf, "%s%8.8x.%s", "", hc, "bin");     // 0x15d2ec / 0x15d2f8 (redundant second sprintf)
    p = psiFileLoad(buf, 0x1e04 /*malloc_anim_seq_data*/, &AnimFileSize /*0x1d6ac0*/);
    if (!hashtable_getitem(hc)) hashtable_additem(hc, p);
    pAnimFileData /*0x1d6a20*/ = p;
    AnimFileSizes.seqBytes /*0x1d6a48*/ += AnimFileSize;  AnimFileSizes.numSeqs /*0x1d6a54*/++;
    if (!noSeqProcess) AnimProcessSeqData(hc);     // 0x138e0, cdecl (other agent)
    AnimFileSizes.seqSum /*0x1d6a5c*/ += *(short *)(hashtable_getitem(hc) + 0x38);
    break;
case 0x05000000:                                   // skin
    loadAnimFileCapturingSize(/*EAX*/0x1c04, /*ESI*/hc);
    AnimFileSizes.skinBytes /*0x1d6a44*/ += AnimFileSize;  AnimFileSizes.numSkins /*0x1d6a50*/++;
    AnimProcessSkinData(/*EAX*/hc);
    break;
case 0x06000000:                                   // animation script
    loadAnimFileCapturingSize(/*EAX*/0x1f04, /*ESI*/hc);
    AnimFileSizes.scriptBytes /*0x1d6a4c*/ += AnimFileSize;  AnimFileSizes.numScripts /*0x1d6a58*/++;
    AnimProcessScriptData(/*EAX*/hc);
    break;
}
```
The PS2 differs in two ways: skins are only loaded if `MP_NeedSkin(hc)` (a multiplayer filter; the Xbox has no
such check), and the PS2's AnimFileSizes has the same 7 fields.

### loadAnimFileCapturingSize - Xbox 0x11160 (INVENTED NAME, inlined on PS2)
Registers: EAX = malloc flags, ESI = hashcode. Returns nothing (the result goes to pAnimFileData).
```c
sprintf(buf, "%s%8.8x.bin", "", hc);
if (mf != 0x1c04) { if (mf == 0x1e04) sprintf(buf, "%s%8.8x.%s", "", hc, "bin"); else sprintf(buf, "%s%8.8x.bin", "", hc); }
p = psiFileLoad(buf, mf, &AnimFileSize);            // 0xdca30
if (!hashtable_getitem(hc)) hashtable_additem(hc, p); // 0x6b500 / 0x6b420
pAnimFileData = p;                                   // 0x1d6a20
```

### AnimSkeletonProcess - Xbox 0x11050, PS2 0x1bda50 (KEY)
`void __cdecl AnimSkeletonProcess(u8 *data)`, stack arg. Only caller: LoaderProcess (dir type 6, `dirFileBuf`).
```c
pAnimData = data;                                    // 0x1d6ac8, cursor
SkeletonTable[*(s16 *)data] = (SkeletonInfo *)data;  // 0x1d7578; no bounds check (idx < 128 by data)
pAnimData = data + 0x10 + data[2] * 12;              // skip header + offsets; nothing else is changed
```
The data stays in the bundle buffer, which must outlive the level. The unrolled "4 at a time" loop at 0x1108e
only advances the cursor.

### AnimProcessSkinData - Xbox 0x144c0, PS2 0x1bdc28 (KEY)
Register: EAX = hashcode. Only caller: AnimLoadFile. Uses the global cursor `pAnimData` 0x1d6ac8.
```c
if ((hc & 0xff000000) != 0x05000000) return;
u8 *p = hashtable_getitem(hc); pAnimData = p; if (!p) return;
sAnimSkin_tag *s = Mem_Malloc(0x30, 0x2004 /*malloc_anim_skin_structs*/, 0);
hashtable_modify(hc, s);                              // 0x6b520: hashtable now maps hc -> struct
p += 4;                                               // skip own hashcode
s->skinScale.x = rdf32(p); s->skinScale.y = rdf32(p+4); s->skinScale.z = rdf32(p+8); p += 12;
s->numRigged = *p++; s->numDiscrete = *p++; s->numMorphs = *p++; s->numDatums = *p++;
s->skeletonNum = *p++;
int nb = SkeletonTable[s->skeletonNum]->numBones;     // skeleton must already be loaded
s->bones = p; p += nb;                                // (+0x20)
if (s->numDiscrete) {
    s->discreteBone = p; p += s->numDiscrete;         // (+0x0c)
    p = align4(p);
    s->discreteObjList = (celglist_tag **)p;          // (+0x08)
    for (i = 0; i < s->numDiscrete; i++, p += 4) {
        u32 h = rdu32(p);
        ((celglist_tag **)s->discreteObjList)[i] = (h == 0xffffffff) ? NULL : hashtable_hashcode_to_celglist(h); // 0x6b610
    }
}
if (s->numRigged) {
    p = align4(p);
    s->riggedBodyList = (celglist_tag **)p;           // (+0x04)
    for (i = 0; i < s->numRigged; i++, p += 4) {
        u32 h = rdu32(p);
        if (h == 0xffffffff) { s->riggedBodyList[i] = NULL; continue; }
        if (h & 0x00100000) h = AnimSleeveGetEntity(0); // inlined: first of SleeveEnts_SleeveTable[0..7]
                                                         // present in the hashtable, else 0
        s->riggedBodyList[i] = hashtable_hashcode_to_celglist(h);
    }
    s->invBind = Mem_Malloc(nb * 0x3c, 0x2104 /*malloc_anim_skinmat_structs*/, 0); // (+0x24)
    for (b = 0; b < nb; b++, p += 28) {
        _VECTOR t = { rdf32(p), rdf32(p+4), rdf32(p+8) };
        quaternion_tag q = { rdf32(p+12), rdf32(p+16), rdf32(p+20), rdf32(p+24) };   // x,y,z,w
        Quat_QuatTransToMat(&q, &t, &s->invBind[b]);  // 0xd59e0
    }
}
if (s->numDatums) {
    s->datums = p;                                    // (+0x1c), no alignment
    p += 36 * s->numDatums;                           // Xbox byte-reads each field (dead byteswap code)
}
pAnimData = p;
```
PS2 differences:
- The PS2 calls `AnimSleeveGetEntity(0)` rather than inlining it.
- It stores floats with aligned loads.
- It uses a 0x40-byte struct with every field 4 bytes further on (16-byte `_VECTOR`), and a `numBones << 6`
  matrix buffer (64-byte PS2 MATRIX).

### AnimProcessScriptData - Xbox 0x110c0, PS2 0x1bdb00
Register: EAX = hashcode. It only walks the command list, leaving pAnimData just past it; nothing is changed:
```c
if ((hc & 0xff000000) != 0x06000000) return;
u8 *p = hashtable_getitem(hc); if (!p) return;
int n = rdu16(p + 6) & 0x1ff;  p += 8;
while (n--) { u8 op = p[0], k = p[1]; p += 2; if (op == 0) k += 3; else if (op <= 4) k += 2; p += 2 * k; }
```
Script header as far as the loader touches it:

| off | size | meaning |
|---|---|---|
| +0 | u32 | hashcode |
| +4 | u16 | not read here; common values 2..150, likely a length or frame count |
| +6 | u16 | low 9 bits = command count; high 7 bits are something else (often 0x3c or 0x7c) |
| +8 | ... | commands: `{u8 op; u8 k; u16 args[k + (op==0 ? 3 : op<=4 ? 2 : 0)]}` |

This walk ends exactly at the zero padding in all 582 dumped 06 files. Script playback belongs to the other agent.

### AnimPreLoadInit - Xbox 0x14b80, PS2 0x1c6b28
`void __cdecl AnimPreLoadInit(char keepSkeletons)`. Callers: ResetMap_GameInit 0xbed81, LoadableReload 0xc0001.
```c
if (!keepSkeletons) {
    AnimScriptTimeStamp /*0x1d7570*/ = 1;  AnimTagTimeStamp /*0x1d6a60*/ = 0x80000001;
    Glb_SkinScale /*0x1d7794*/ = {1,1,1};  Glb_InvSkinScale /*0x1d6a28*/ = {1,1,1};
    memset(SkeletonTable, 0, 128 * 4);
    *(u16 *)0x1d6ac4 = 0;    // PS2 calls AnimDistanceTableInit() here instead
}
memset(&AnimFileSizes /*0x1d6a44*/, 0, 7 * 4);
Glb_AnimPostLoadDone /*0x1d6a65*/ = 0;
```

### AnimPostLoadInit - Xbox 0x11200, PS2 0x1be8d0 (KEY)
`void AnimPostLoadInit(void)`. Only caller: LoaderProcess, dir type 1 (the level map file, last in the bundle).
It builds no new tables: it shares the datum table of the reference human skin.
```c
if (Glb_AnimPostLoadDone) return;  Glb_AnimPostLoadDone = 1;          // 0x1d6a65
u32 n = hashtable_get_hashtype_count(0x05000000);                     // 0x6b700
sAnimSkin_tag *ref;
if (!n || !(ref = hashtable_getitem(0x05000007))) return;
HASHCODE list[128];  hashtable_get_hashtype_data(0x05000000, list);   // 0x6b740 - no bound check (bug if > 128 skins)
for (i = 0; i < n; i++) {
    sAnimSkin_tag *s = hashtable_getitem(list[i]);
    if (s->skeletonNum >= 2) continue;                                // only the human skeletons 0 and 1
    if (list[i] == 0x0500002f) {                                      // prepend the reference datums
        u8 own = s->numDatums;  void *ownp = s->datums;
        s->numDatums = ref->numDatums + own;                          // u8 wrap if > 255
        s->datums = Mem_Malloc(s->numDatums * 0x24, 0x2004, 0);
        memcpy(s->datums, ref->datums, ref->numDatums * 0x24);
        memcpy((u8 *)s->datums + ref->numDatums * 0x24, ownp, own * 0x24);
    } else {
        s->numDatums = ref->numDatums;  s->datums = ref->datums;      // shared pointer, own datums discarded
    }
}
```
Globals: Glb_AnimPostLoadDone 0x1d6a65, SkeletonTable 0x1d7578, AnimFileSizes 0x1d6a44 (7 dwords: skinBytes,
seqBytes, scriptBytes, numSkins, numSeqs, numScripts, seqSum), AnimFileSize 0x1d6ac0, pAnimFileData 0x1d6a20,
pAnimData 0x1d6ac8.

### maybeFixupSkinHashcode - Xbox 0x11350 (INVENTED NAME; inlined in PS2 AnimObjectNew 0x1bf038 with a different list)
Register: EAX = skin hashcode in and out. Only caller: AnimObjectNew 0x1146b. It swaps skins per level, keyed on
`GameState.CurrentLevelHashcode` 0x1f658c; both jump tables were decoded from memory:
- levels 0x07000008, 09, 0a, 18 (Henderson A/B/C, Tower2A): 0x05000036 becomes 0x050000c3; 0x05000038 becomes
  0x050000c2.
- levels 0x0700000e, 0f, 1b, 1d (CastleIndoors1/2, EvilBase, EvilBaseC): 0x05000015 becomes 0x050000bd;
  0x05000018 becomes 0x050000c5; 0x0500003b becomes 0x050000bf; 0x0500005e becomes 0x050000be.

### AnimSleeveGetEntity - Xbox 0x118b0, PS2 0x1c6d00
Register: EAX = start index; returns the hashcode in EAX. Starting at `idx` (reset to 0 if > 7) it tries up to 8
entries of `SleeveEnts_SleeveTable` 0x1634c0 (wrapping to 0) and returns the first one present in the hashtable,
or 0 if none is. The table holds 020009d0, 02000900, 020001f7, 02000929, 020009e6, 020009ff, 02000a14, 02000a13.

### AnimObjectSetSleeveType - Xbox 0x118f0, PS2 0x1c6d80
`void __cdecl (obj_tag *obj, int sleeveNum)`. `h = AnimSleeveGetEntity(sleeveNum); if (h && (g =
hashtable_hashcode_to_celglist(h))) obj->animState->animObj.sleeveGlist /*AnimState+0x88 = animObj+0x30*/ = g;`

### AnimDatumGetIndex - Xbox 0x13590, PS2 0x1c7f68
`int __cdecl (obj_tag *obj, int datumId)`. Returns -1 if obj or its animState is NULL. Otherwise it scans
`skin = animState+0x74 (animObj.skinPtr)`, `skin->datums[i].id` (stride 0x24) for i < `skin+0x2b`, and returns the
first i that matches, else -1.

### AnimDatumSetEntity - Xbox 0x135d0, PS2 0x1c7fb8
`void __cdecl (obj_tag *obj, int datumId, HASHCODE ent, u8 frames)`. It calls
`i = AnimDatumGetIndex(obj, datumId)` and does nothing if i < 0. Otherwise:
- if `ent` is set: `((celglist**)(AnimState+0x84))[i] = hashtable_hashcode_to_celglist(ent)` and
  `((u8*)(AnimState+0x94))[i] = frames`;
- else it sets the byte to 0.

(AnimState+0x84/+0x94 are animObj+0x2c/+0x3c.)

### AnimDatumGetWeaponInfo - Xbox 0x13630, PS2 0x1c8070
`int __cdecl (obj_tag *obj, HASHCODE *gfx, HASHCODE *flash, u16 *extra)`. It reads
`w = (s8)animState->currentWeaponId (+0x52)` and returns -1 if w > 0x72. `weapon_data` entries are 0x10c bytes.
It writes:
- `*gfx = [0x18d020 + w*0x10c]` (wpn3rdPersonGfx)
- `*flash = [0x18cfec + ...]` (muzzleFlash3rdPerson)
- `*extra = u16 [0x18d000 + ...]`

It returns the datum id `u8 [0x18cff0 + ...]` (whichBone), or -1 if that is 0xff. AnimObjectDraw uses it to
attach the current weapon: `AnimDatumSetEntity(obj, whichBone, wpn3rdPersonGfx, 1)` every frame.

### AnimGetCollData - Xbox 0x136b0, PS2 0x1c5208
`HitBox *__cdecl (obj_tag *obj, u16 *numBoxes)`. It requires animObj.flags (AnimState+0xa8) bits 1 and 2 to be
set, otherwise it sets `*numBoxes = 0` and returns NULL. Then:
1. It calls `psiBuildMatrixPalette(obj, animObj, 0)`.
2. It picks the bone table: `HumanCollisionBones` 0x163198, or `AstronautCollisionBones` 0x1632f0 if
   `skin->hashcode == 0x05000078`. Table entries are 20 bytes, `{s16 bone; s16 parentOverride; f32 unused;
   f32 dims[3]}`, terminated by bone < 0.
3. For each entry i it writes `HitList2` 0x1d6bf0, stride 0x4c:
   - It works out the parent: `parent = parentOverride >= 0 ? parentOverride : skin->bones[bone] & 0x7f`.
   - `+0x00` gets `Mat_Copy(boneMtxs[parent])` (15 floats).
   - `+0x30` gets a position. For i==0 it is `bonePos + (0, bonePos.y - parentPos.y, 0)`; otherwise it is the
     midpoint of bone and parent.
   - `+0x3c` gets `dims[k] * skinScale[k]`.
   - `+0x48` gets the s16 bone.

It returns HitList2 and sets `*numBoxes` = count. Latent bug: a root bone listed without an override would index
bone 0x7f.

### PS2-only: AnimSeqGetHdrBuf 0x1c8108 returns 0; AnimCacheInit 0x1c6b00 is empty. AnimSeqGetInfo 0x1c8110 copies raw-seq header fields into sAnimSeq_tag (other agent's area); no skin/skeleton format content.

## 6. Validation

- `python parse_skeleton.py`: **1305/1305 skeleton records clean** (every byte accounted for, including zero
  padding) across all 32 bundles. There are 45 distinct indices, and the copies are identical between levels.
- `python parse_skin.py` (the 24 dump files plus every skin in every level bundle): **1055/1055 clean, 150
  distinct skins**.
  - Only 6 skins have no rigged body (for example 05000005, a 9-part rigid object on skeleton 11).
  - `-v` prints every field.
- The 24 dumped 05 files are byte-identical to the same hashcodes inside the bundles, where present.
- Parser checks per skin:
  - exact end-of-data, with only zero padding and less than 32 bytes of it;
  - alignment bytes are zero;
  - parents precede children, and bone 0 is the only root;
  - hashcodes are type 02 or 0xffffffff;
  - datum bones are in range, and datum quaternions have unit length;
  - inverse bind check: with the skin's parent table, q·worldBind + t ≈ 0 for every bone. This is reported with
    `-v`, not as an error; see finding 3 for the per-skin deviations.
- AnimProcessScriptData walk: 582/582 dumped 06 files end exactly at their padding.
- `edl.py`: the decompressed lengths equal the archive's uncompressedLen for all 32 bundles. The dir entries of
  07000999 match the dumped 010001D9.bin byte for byte.

## 7. Open questions and suspected original bugs

- Skeleton +0x03 (= number of translated bones) has no Xbox reader found; its meaning comes only from the data.
- AnimPostLoadInit:
  - it discards the own datums of any skin on skeleton 0/1 other than 0x0500002f. In the data, only 05000007 (60 datums) and 0500002f (4 datums, ids 84-87) have
    datums of their own on skeleton 0/1, so nothing is actually lost;
  - its `HASHCODE list[128]` has no bound check;
  - a skin on skeleton 0/1 loaded without 0x05000007 keeps its own datums.
- A skin with numRigged == 0 leaves skin+0x04 and +0x24 unset and not zeroed (Mem_Malloc does not clear). Readers
  guard on numRigged. Likewise +0x00 until AnimObjectNew.
- Sleeve substitution happens once, at load, taking the first sleeve present. AnimObjectSetSleeveType can override
  it per object via animObj+0x30.
- The rigged mesh (KXE entity in the 01 packs) and how its vertices index the palette have not been decoded yet;
  that is needed for Blender export.
- The first 6 bytes of FUN_00011000 in the Ghidra database read "Hello\0" instead of code
  (`51 A1 C8 6A 1D 00` = `push ecx; mov eax,[0x1d6ac8]` is what fits). Worth fixing by hand in Ghidra, together
  with the function's name (INVENTED: AnimReadFloat).
