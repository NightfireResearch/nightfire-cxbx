# The action engine on the GameCube's data

A spike: our action engine (action_standalone, docs/action-standalone.md) loading the GameCube version's files (the
US release) in place of the Xbox's, behind a build switch, as docs/ps2-data.md (branch `ps2-data`) does for the
PS2's. The game code stays the Xbox port; what changes is how the files are found and how the formats that differ
become what the Xbox code expects.

Status (25 October 2026): the disc's files are served (milestone 1), every level bundle converts to the Xbox's layout
(milestone 2: textures, placements, collision, paths, skins, scripts, sequences) and is checked offline against the
US Xbox disc's twins, and in game HendersonB loads from the GameCube's bundle and plays on the test runner with the
GameCube's textures in its HUD - but no world, since the meshes are not converted yet, and no sound. See
[Milestones](#milestones) and [In game](#in-game).

Sources: the GameCube disc as extracted by the extractor project (Q:\nightfire-re, read only:
`iso_extract/gc_us`), the US Xbox disc (`iso_extract/xbox_us`), the GameCube executable in Ghidra
(`/GameCube_US/Nightfire.elf`; the ELFs on the disc have no symbol table, the names are the project's own, see
docs/gamecube-checks.md) and our Xbox code. Every comparison below was made file by file between the two discs with
`gcdata_check` ([Tools](#tools)).

## The switch

A CMake option, **`NIGHTFIRE_ACTION_DATA_GC`** (default OFF), for the standalone targets only (action_standalone,
action_standalone_test). On, they are compiled with `NIGHTFIRE_DATA_GC=1` and:

- `src/action/engine/FS.cpp` (the Xbox's `filesys.dNN` archives) is replaced by `src/action/gc/GcFs.cpp`;
- the files of `src/action/gc/` are added;
- the seams in game files are guarded with `#if NIGHTFIRE_DATA_GC` ([Seams](#seams)).

Off, nothing is compiled differently. Checked on 25 October 2026 with `tools/pe_compare.py` (from the PS2 branch):
action_standalone and action_standalone_test built with the GameCube work in the tree have the same `.text`,
`.data`, `.reloc`, `.rsrc` and `.rdata` as those built before it, but for the debug directory's timestamp. The one
seam in a game file (Anim.cpp) comes after the file's last runtime `__LINE__` use, so moves no assert's line.
actioninject never gets the switch.

`DiscPath` (settings.ini) names the extracted GameCube disc: the folder holding `Nightfire.elf`, `common\` and
`gc\`.

```
cmake -B build/gcon -A Win32 -DPython3_EXECUTABLE=C:/Python314/python.exe -DNIGHTFIRE_ACTION_DATA_GC=ON
cmake --build build/gcon --config Release --target action_standalone
```

## The disc

No archive: the GameCube's disc file system holds every file loose (an extraction is a folder tree). The action
engine's files are under `gc\` (bundles, sound, movies) and `common\` (text, the pause screen's film); the driving
engine has `driving\` and `driving.elf` (not covered here). Endianness: the GameCube is big-endian, and the data is
not swapped throughout: each format below says which way round it is.

| GameCube | Size | Xbox twin |
|---|---|---|
| `gc\<hash>.bin`, 104 bundles: levels 0700xxxx (EDL-compressed), their sequence bundles 07d0xxxx and 07e0000n, cutscene loadables 078/079/07A0xxxx, `common.bin` (the levels' sequence groups) | 270 MB | `<hash>.bin` in `filesys.dNN` (1.3 GB with everything) |
| `gc\audio\SFX\`: `English\SB_n\SB_m.{sfx,shf,sbf,shg}`, `music\MFX_n\MFX_m.{ssd,smf}`, `SBInfo.sbi`, `MFXInfo.mxi` | 240 MB | the same names in the archives, but no `.SHG` |
| `gc\movies\<hash>.h4m`, 22 movies | 425 MB | `eurocom\30_fps\<hash>.xmv`, the same 22 hashcodes (373 MB) |
| `common\*Txt.dat`, `Woman.rle` | 3.7 MB | the same, byte for byte |
| `common\*TxtU.dat`, `08000001.bin`, `08000002.bin`, `TuningVars.txt`, `def_Startup.cfg`; `gc\data\` (`lightmap.tpl`, `pcl.tpl`, `bumpmap.raw`); `gc\language\american` (empty) | small | none (the GameCube's own, or leftovers) |

`gc\07000001.bin` (HendersonA) is 0 bytes in this extraction; every other level is there.

## Format by format

| Format | Same? | What differs | Known | Unknown | Seam in our code | Size |
|---|---|---|---|---|---|---|
| File system | no | loose files; names mapped by `GcDisc_Path` (`ENGLISH/SB_0/SB_0.SHF` is `gc\audio\SFX\English\SB_0\SB_0.shf`, a text bank `common\<name>`, a bundle `gc\<hash>.bin`) | all | - | FS.cpp, whole (GcFs.cpp) | small, done |
| Level bundle | nearly | EDL-compressed with the big-endian flag (the same bit stream, read from words the other way round); the header's two words big-endian, the directory's entries little-endian as the Xbox's. The anim sequences (type 4) are not in the level's bundle but in its group's (07e0000n, from `common.bin`) and its own (07d0xxxx). Extra entry types: 12 (a loadable, its name a binary hashcode; the Xbox's bundles have none), 13 `GC EXTRA STATIC` and 14 `GC EXTRA DISCARD` (the meshes' data: 1.6-2.7 MB and 0.9-1.4 MB in the three levels looked at) | all | the loadables' mode (as the PS2's) | GcBundle.cpp, in GcFs.cpp's open | small, done |
| Skins, anim scripts, skeletons, scripts, menus (types 3, 5, 6, 7, 8); sequences (4) | yes | byte for byte the Xbox's: over 29 levels 974 skins, 16,097 anim scripts, 1,260 skeletons, 393 of 394 scripts, 29 menus, 15,969 sequences. The Xbox's levels also hold 27 "Spec" skins and 30 scripts the GameCube's lack (HendersonB's two are in its 078/079 loadables) | all | - | none (the sequences: GcBundle_Load) | done |
| Map packs (types 0, 1, 2, 11): container, header, placements, portals, LOD, paths, collision | nearly | block headers little-endian on both; most payloads big-endian (placements, map header, paths, LOD, sounds, entity parameters 0x1f, collision 0x2f), some little-endian as on the Xbox (palette headers, portals, hashlists, AI paths, particles). Collision: the box's quantisation scale is a power of two's exponent (n for 2^-n) where the Xbox has the float | all; after conversion, over 1,831 packs: headers, LOD, portals, hashlists, AI paths, particles, sounds all the Xbox's; placements 1,815 the same (16 apart in float noise or padding); collision 6,850 of 7,322 the same, 393 more within 0.1% of bytes; entity parameters 15,236 the same, 217 within two bytes | the 62 entity parameter blocks and 79 collision blocks that differ more | GcMap.cpp | done |
| Textures | no | block 0x17: a 0x5c-byte big-endian header (name, size, GX format index, mip levels) then the GX's tiled texels, every level and frame. Three formats only in the level bundles: CMPR (16,016 textures), RGB5A3 (1,395), RGBA8 (437); no I4/I8/IA, no C4/C8 palettes. Every size a power of two from 4 to 512 but one (128x132, which the GameCube draws as 32x32). "DT" headers (another texture's frames) count every header of the level from 0, as the Xbox's do from 1 | all (GC psiCreateMapTextures 0x8002fd0c) | - | GcTexture.cpp: block 0x18 (KXT) | done |
| Static meshes | no | block 0x0c (the GameCube's mesh header, 0xac bytes, big-endian, magic 0x0f) where the Xbox has a KXE in 0x0d; its vertices and strips in the bundle's GC EXTRA entries. Not stored display lists: the game builds one the first time it draws the mesh ([Meshes](#meshes)) | the header's offsets and the strip layout, from the draw code | the vertex formats' fraction bits in practice, how the GC EXTRA data is attached to each header, the layer flags | parsemap.cpp case 0x0c (to a KXE) | medium, after research |
| Skinned meshes, morphs | no | the same 0x0c headers; no 0x2b morph blocks on this disc (the PS2's have them) | - | all (psiDrawSkinObjectMatrix 0x8000cec4, psiBuildMatrixPalette 0x8000d0b8) | as meshes | research |
| Text banks | yes | the nine `*Txt.dat` byte for byte the Xbox's; the GameCube adds `*TxtU.dat` | - | what reads `TxtU` | none | none |
| Fonts, button symbols | glyphs probably | the glyph tables are executable data (`GC FontInc.cpp`); the button symbols are the GameCube pad's | - | the GameCube's symbol table in Nightfire.elf (no symbols: find it by the Xbox table's layout) | SpecialChars, as the PS2's | small |
| Sound banks | container yes, codec no | the same files, big-endian (the `.SHF` sample headers' nine words; `.SFX` 98% of bytes the Xbox's once each word is turned round); samples Nintendo DSP ADPCM (8 bytes per 14 samples) with the DSP's 96-byte headers (coefficients, history) in the new `.SHG`; every sample mono, at lower rates ([Sound](#sound)) | the codec, the rates | the `.SFX` fields that differ | SFX.cpp LoadSoundBank, as the PS2's | medium |
| Music | container yes, codec nearly | `.SMF` markers big-endian, positions four times the byte offset; `.SSD` headerless IMA ADPCM, stereo, one byte (two samples) of each channel in turn, low nibble first, 32 kHz | the layout (by decoding) | the exact rate (32,000 bytes a second implies 32 kHz) | GcFs.cpp's reads, as the PS2's | small-medium |
| Streamed speech | n/a | none on either disc's language folder by that name; the GameCube has no streams at all | - | - | GcFs.cpp (a stream that cannot open is given up) | done |
| FMV | no | HVQM4 1.5 (`HVQM4 1.5` magic; 640x480, 33,366 µs a frame) where the Xbox has XMV (WMV2) | the container's header | the decoder (FFmpeg has none) | Fmv.cpp replaced | medium-large |
| Woman (pause background) | yes | `common\Woman.rle` byte for byte the Xbox's | - | - | none | none |
| `SBInfo.sbi`, `MFXInfo.mxi` | yes, turned round | each word big-endian; otherwise the Xbox's (the Xbox's padded to 2 KB) | all | - | GcFs.cpp | done |

## Seams

| Seam | Game file | GameCube code |
|---|---|---|
| The file system (whole file replaced) | engine/FS.cpp out of the build | gc/GcFs.cpp: loose files through GcDisc_Path, bundles converted whole when opened (gc/GcBundle.cpp, GcMap.cpp, GcTexture.cpp); for now silent stand-ins for the sound banks and an empty music list |
| The levels' swapped skins | engine/Anim.cpp AnimObjectNew | gc/GcAnim.cpp: the GameCube's own FixupSkinHashcode (0x80045298: Elite_B becomes Elite in PowerStationA1, Elite_Beret in EvilBase), since the Xbox's swaps in "Spec" skins the GameCube's bundles lack |
| Meshes (to come) | engine/parsemap.cpp case 0x0c | a converter to KXE, with the GC EXTRA entries |
| The cutscene loadables (to come) | engine/Loader.cpp, as the PS2 branch's | the 078/079/07A bundles |
| Sound banks, music (to come) | sound/SFX.cpp LoadSoundBank; GcFs.cpp's reads | DSP ADPCM decoder, Xbox ADPCM encoder (the PS2 branch's) |
| Movies (to come) | engine/Fmv.cpp replaced | an HVQM4 decoder |

## Textures

GcTexture_ToKxt turns block 0x17 into the Xbox's texture header and frames, which psiCreateMapTextures registers
unchanged:

- **CMPR** is DXT1's blocks, four to an 8x8 tile, with the colours big-endian and each row's texels in the other bit
  order: reordered into DXT1, nothing lost.
- **RGB5A3** and **RGBA8** become swizzled A8R8G8B8 (RGB5A3 could go to A1R5G5B5 or A4R4G4B4 exactly, as half the
  memory, but which one changes per texel).
- Every mip level the GameCube has is kept.

Checked (`gcdata_check textures`, every level bundle): all 17,848 textures convert and every KXT decodes to the
GameCube's own texels exactly. Against the Xbox's twins (`all`): of 15,708 pairs compared, 12,456 decode to the same
texels (the DXT1 textures are the Xbox's blocks); the rest, mostly where the Xbox has DXT2-5 with 8-bit alpha and the
GameCube RGB5A3 with 3, average about 29 dB; 71 are at half the Xbox's size, 17 at another. The worst are the
WomanBack and cracked glass textures, whose texels differ outright.

The "DT" headers: `gcdata_check dt` resolves each against the texture the index names, counting the level's texture
headers from 0. 77 of 88 in HendersonB (169 of 212 in SkyRail) name a texture of the same name; the rest name one of
another name, probably the same texels under two names ("white_cloth_64" for "white cloth 64", "SHALWATER_anim1"
for "water_shalwater"). The Xbox's psiCreateMapTextures takes `Tex[index + 1]`, its Tex[0] being the default texture,
so the index is kept as it is.

## Meshes

Read from GC PsiDraw.cpp's draw code (maybe_psiDrawClonedObject 0x8002ea08, called by psiDrawObjectMatrix
0x8002f584); not converted yet. parsemap's case 0x0c keeps the header's address in the cel, as the Xbox's 0x0d does.
The header (big-endian words, offsets from the header):

| Offset | Meaning |
|---|---|
| 0x00 | 0x0f (psiDrawObjectMatrix draws nothing else) |
| 0x18 | strip groups; 0x1c, 0x20 must be non-zero too |
| 0x30 | flags: 0x200 sets a z mode of (1, 0, 3) where it is otherwise (1, 1, 3): the z-write off, by the look of it |
| 0x34 | bit 0: set, the layers' blend and alpha-compare states are left as they are |
| 0x38 | eight layers' flags: bits 0, 1, 2 and 0x40 pick the layer's blend and alpha-compare states (meanings not settled) |
| 0x58 | -1: positions and normals 16 bytes a vertex (floats, by the stride); else 12 bytes (16-bit), with fraction bits at 0x5b and 0x5f |
| 0x60 | a point the draw transforms by the object's matrix (the bounds' centre, by the look of it) |
| 0x70 | the strip groups: five words each. Word 0: vertices (bits 0-15), strips (16-27), **double-sided (28)**, and in bits 29-31 a multiplier on the group's index stride (not understood); then eight halfwords, each layer's texture (-1 for none) |
| 0x74 | each strip's length less two (halfwords) |
| 0x78 | the position-and-normal indices (halfwords; one index serves both) |
| 0x7c | the positions and normals (GX attributes 9 and 10) |
| 0x80 | the texture coordinate indices; 0x84 the coordinates (attribute 13, 4 bytes) |
| 0x88 | the colour indices; 0x8c the colours (attribute 11, 4 bytes; 0 for none) |

Each strip goes into the display list as a triangle strip command, its count, then per vertex the position index
twice (position and normal), the colour index if any and the coordinate index. A converter makes the Xbox's KXE
from these (vertices, strips joined, a primitive per texture and layer state), as the PS2 branch's Ps2Mesh.cpp does
from VIF packets.

### Culling

The data does say which surfaces are double-sided. Each strip group's bit 28 sets the GameCube's cull mode as the
group is drawn: set, `GX_CULL_NONE`; clear, `GX_CULL_BACK` (GC GX.cpp's state flush 0x80036244 writes the GX
generation mode through the GX's own table, 0 2 1 3 at 0x8030f2e4). So the bit is the Xbox's primitive state bit 3
(double-sided), and a converter should carry it over.

The particles are drawn otherwise: GC psiDrawParticleList (0x8002af38) sets `GX_CULL_BACK` (its state 1, through
the GX's cull mode call, 0x80181688) for every particle quad. The Xbox's psiDrawParticleList sets no cull mode that
we have found and draws with whatever the last mesh primitive left. That is where the GameCube can lose billboarded
effects the Xbox shows, such as the bullet-impact gas on drones and the explosions in SpaceStationD: a quad built
facing away (mirrored, or wound the other way by the effect code) is culled on the GameCube and drawn on the Xbox
whenever the last primitive was double-sided. Whether those effects are particles or effect meshes with bit 28 clear
is still to check. In our port the particles keep the Xbox's behaviour whatever the data, so this GameCube fault
does not carry over.

## Sound

What the GameCube's sound data is, against the Xbox's (`.SHF` headers of every bank, paired sample by sample: the
Xbox's 11 stereo samples are two mono samples each on the GameCube, which makes the counts agree, 7,750 to 7,750):

| Xbox | GameCube | Samples |
|---|---|---|
| 32,000 Hz mono | 19,000 Hz | 4,719 |
| 32,000 Hz mono | 11,025 Hz | 1,406 |
| 22,050 Hz mono | 12,000 Hz | 1,554 |
| 19,000 Hz mono | 19,000 Hz | 46 |
| 32,000 Hz stereo | 11,025 Hz, two mono | 18 (9 pairs) |
| 44,100 Hz stereo | 19,000 Hz, two mono | 4 (2 pairs) |
| 32,000 Hz mono | 22,050 Hz | 3 |

- **Format:** every GameCube sample is 4-bit DSP ADPCM (the GameCube's audio DSP's), the Xbox's 4-bit Xbox ADPCM
  (IMA). The lengths agree (median 0.4% apart, 95% within 2%), so the samples were resampled, not cut.
- **Sizes:** the banks are 78 MB of sample data on the GameCube against 148 MB on the Xbox.
- **Why it sounds worse:** most effects play at 19 kHz or less (11-12 kHz for a third of them) where the Xbox's
  play at 22-32 kHz, so everything above about 9.5 kHz (5.5-6 kHz for the 11-12 kHz third) is gone; the stereo
  effects are split into two mono samples, played as two voices.
- **Music:** headerless IMA ADPCM, stereo, 32 kHz (32,000 bytes a second against the Xbox's 49,613 for 44.1 kHz
  stereo Xbox ADPCM); `.SMF` markers the Xbox's, big-endian, with positions in units of a quarter byte and each
  section start's decoder state as two (step index, sample) halfword pairs.

So in sound the GameCube's data is the poorer: a GameCube build can only play what is there. Converting means
decoding DSP ADPCM with the `.SHG` coefficients and encoding Xbox ADPCM at the GameCube's rates, as the PS2 branch
does from SPU ADPCM; the music is the same codec family as the Xbox's and wants re-blocking and its marker
positions moved.

## In game

On the test runner (vr-desktop, `C:\nightfire-gc\run`: `DiscPath=C:/nightfire-gc/gcdisc`, a copy of the bundles,
`common\` and the two sound tables HendersonB needs; `DumpEvery=250`), `action_standalone.exe -level 07000002`, no
input, 100 s: the GameCube's bundle (12.8 MB decompressed) converts to 18.1 MB of the Xbox's layout with 621
sequences added, the level loads and runs to the end of the 100 s without a fault. The frames show the HUD drawn
from the GameCube's textures (the health and armour iris, the crosshair, the weapon's name and ammunition) over a
black world: the meshes are left out. The log has the scripts the GameCube keeps in its loadables (0x0600007f,
0x06000084) as not loaded, and "EMITTER TEXTURE MISSING" once.

Before GcAnim.cpp the run stopped in Drone_PostLoad_Init: the Xbox's FixupSkinHashcode had swapped in a "Spec"
skin that the GameCube's bundle does not have.

## Milestones

| # | Milestone | Needs | Size |
|---|---|---|---|
| 1 | The disc's files are served: names to paths, bundles decompressed | - | done |
| 2 | Bundles converted: the directory, the sequences merged, every map block, textures to KXT; checked against the Xbox's twins offline | 1 | done |
| 3 | A level's static world: block 0x0c and the GC EXTRA entries to KXE, the double-sided bit carried over | 2 | research, then medium (3-5 days) |
| 4 | Placements and objects in play: the 078/079/07A loadables (cutscene scripts and sequences) as the PS2 branch's LoadableLoad; collision and paths confirmed with the Xbox's time demos | 3 | medium (2-3 days) |
| 5 | Sound: DSP ADPCM banks to Xbox ADPCM, the `.SFX` fields, the music re-blocked and its markers | 1 | medium (2-3 days) |
| 6 | Text, fonts, menus: text needs nothing; the GameCube's button symbols from Nightfire.elf | 2 | small (1 day) |
| 7 | Skinned characters and morphs | 3 | research, then medium (4-6 days) |
| 8 | Movies: an HVQM4 decoder, or the Xbox's movies in its place | 1 | medium-large (3-5 days), or skip |

## Research items

- **The meshes' data:** which part of the GC EXTRA entries each 0x0c header's offsets point into (the extractor
  project found the STATIC entry's pieces in order, sized by a header word); the fraction bits and vertex formats
  in practice; what the layer flags select. GC psiCreateEntityGfx's counterpart (who fixes up the offsets) is not
  found yet: start from the readers of a cel's geometry pointer after parsemap's case 0x0c.
- **Skinning and morphs:** psiDrawSkinObjectMatrix (0x8000cec4) and psiBuildMatrixPalette (0x8000d0b8).
- **The culled effects:** whether the drone gas and SpaceStationD's explosions are particles or effect meshes, and
  for meshes whether their groups have bit 28 clear.
- **The `.SFX` fields that differ** after the words are turned round (2% of bytes): halfwords or bytes inside
  words, or values that changed with the stereo samples split in two.
- **The HVQM4 movies:** the audio track's format, a decoder to port.
- **`07000001.bin`** (HendersonA) is empty in the extraction; check the disc image.
- **The `*TxtU.dat` banks and `08000001/2.bin`:** what reads them on the GameCube.
- **Entity parameters and collision blocks that differ** beyond float noise (62 and 79 over all levels).

## Tools

`gcdata_check` (src/action/devtools/GcDataCheck.cpp, `cmake --build . --config Release --target gcdata_check`): the
GameCube code's conversions (the same GcBundle, GcMap, GcTexture and GcDisc code GcFs.cpp links) run offline over
the extracted disc, no game.

```
gcdata_check <gc disc> files                          every bundle: compressed or not, its entries by type
gcdata_check <gc disc> bundle 07000002                a bundle's directory and its packs' blocks
gcdata_check <gc disc> textures 07000002 [<out>]      every texture to a KXT, decoded back; TGAs into <out>
gcdata_check <gc disc> dt 07000002                    the "DT" headers' indices against the names they point at
gcdata_check <gc disc> convert 07000002 <xbox disc>   the level as the Xbox's, against the Xbox's bundle
gcdata_check <gc disc> all <xbox disc>                convert, for every level, totals only
gcdata_check <gc disc> names <xbox disc>              every name the Xbox build asks for, found on both discs
gcdata_check <gc disc> xbox <xbox disc> <name> <out>  a file out of the Xbox's archives
```

`<xbox disc>` is an extracted Xbox disc (the folder with `eurocom\filesys.d00`): the checker reads its archives
itself. `GCDATA_SHOW=1` prints the first difference of each block that differs.

`tools/pe_compare.py` compares two builds of an executable section by section: the default build before and after a
GameCube seam must come out identical.
