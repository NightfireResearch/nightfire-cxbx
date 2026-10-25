# EAGL / REAL prior art: public RE work on EA titles of 2000-2005

Web research done 3 Oct 2026 before starting the EAGL reimplementation. Sorted by how useful each source looks
for Nightfire's driving engine. "Checked" means the repo was cloned and grepped; anything else comes from the
project's own README or web page.

## Who built the driving engine

- Wikipedia: "Driving levels were developed by Savage Entertainment and a team at Electronic Arts."
  Savage's own credits say it did *two* of them (the snowmobile escape and the Aston Martin run through the Alps).
- MI6-HQ's making-of: "EA's Vancouver and Redwood Shores studios developed the vehicle-based missions." EA's
  marketing at the time credited "the team behind Need for Speed" (EA Canada, Vancouver). Agent Under Fire's
  driving levels (2001) were credited the same way.
- **The binary settles it: EA Redwood Shores.** The driving engine itself carries `IsEARS` and `IsSavage` flags,
  so the levels were split between EA Redwood Shores and Savage, with no sign of EA Canada in the code. EAGL is
  still shared EA technology that the NFS line also used, so the projects below remain the nearest references,
  but expect Redwood Shores' version to differ from EA Canada's and Black Box's in detail: use them for names and
  for documented quirks, and check every layout against Driving.xbe.
- Before that was known, the web sources pointed to EA Canada's NFS-team tech of 2001-2002. By that reading, its
  nearest siblings by studio and date are **NFS Hot Pursuit 2 for PS2** (EA Canada, Oct 2002; the PC/GC/Xbox versions were EA Seattle's and use
  a different engine) and Agent Under Fire's driving levels. Then comes **NFS Underground** (2003, EA Black Box,
  which spun out of EA Canada). I found no public RE project for HP2, AUF or Underground.
- Fan wikis (NFS Miraheze, the Russian Wikipedia) say "EAGL 1 = HP2/Underground, 2 = U2, 3 = MW/Carbon,
  4 = ProStreet/Undercover". **Don't trust this numbering.** The MW code itself uses the namespaces `EAGL4`,
  `EAGL4Anim` and `EAGL4Internal`, so that wiki scheme can't be EA's own. All it really tells us is that EAGL was
  EA Canada / Black Box technology that ran through the NFS line up to Undercover.

## High relevance

### dbalatoni13/nfsmw: NFS Most Wanted (2005) decompilation (checked)
- https://github.com/dbalatoni13/nfsmw · CC0-1.0 · active matching decomp (dtk/objdiff), progress tracked on
  decomp.dev. Targets GC USA (GOWE69), PS2 alpha 124 prototype, PS2 Black Edition, X360 prototype, PC 1.3.
- **EAGL4Anim is in the source tree**: `src/Speed/Indep/Src/EAGL4Anim/` (about 90 files) has `FnDeltaQ`,
  `FnDeltaQFast`, `FnDeltaSingleQ`, `FnDeltaF1/F3`, `FnTurnBlender`, `FnRunBlender`, `FnPoseBlender`,
  `FnEventBlender`, `FnStatelessQ/F3`, `FnCompoundChannel`, `FnRawPoseChannel`, `AnimBank`, `Skeleton`, `BoneMask`,
  `MemoryPoolManager`, `DeltaCompressedData`, `IK`, plus EAGL4 support code (`eagl4supportdlopen.cpp` =
  `EAGL4::DynamicLoader`, `SymbolPool`, `ConstructorPool`). Our Nightfire class names (`EAGLAnim::FnDeltaQ`,
  `FnDeltaQFast`, `FnTurnBlender`, `FnRunBlender`) match these one for one, which is strong evidence of shared
  lineage. The headers carry DWARF-derived struct layouts with offsets and sizes (for example
  `DeltaQ : AnimMemoryMap` is 0x14 bytes, `DeltaQMinRange` is 0xC, `DeltaQDelta` is a 3-byte bitfield and
  `DeltaQPhysical` is 6 bytes of 15+1-bit fields). How far along the code is varies: `DeltaChan`, `FnDeltaF1/F3`,
  `DeltaCompressedData` and `MemoryPoolManager` are substantial, while several blender `.cpp` files are still
  empty and only their headers are done. **Caveat:** MW is about 3 years newer (EAGL4 era), so field layouts and
  version fields may have changed. Use it as a map and confirm every layout against our binary.
- **REAL is there too, under its real name**: `realcore` 6.21/6.24 (FILESYS, `bigfile.cpp`, `syncfile.cpp`,
  `hlafile.cpp`, `timer.cpp`, `addtimer.cpp`, `systask.cpp`, `threads.cpp`, `mutex.cpp`), `realgraph` 6.09
  (SHAPE = the FSH/"shape" loader, FONT as `oldfont*`), `realmemcard`, `rcmp` (movie player) and `snd` 9.06
  (SND, SNDSTRM, MIX, SFILTER). There are partial headers and sources for them in `src/Speed/Indep/Libs/`.
- `symbols/PS2/`: a **debug-info dump of the PS2 alpha 124 build**. It has typed function signatures with
  register allocation (for example
  `int SNDSTRM_create(SNDPLAYOPTS*, int maxrequests, int maxchunks, void* pmem, int memsize)`), struct layouts
  (`FILEOPERATION` 0x48, `TIMERCLIENT`, `LOCALE_HEADER/INDEX/LANGUAGE`), enums (`ASYNCFILE_STATUS`), and a list
  of the original source paths, which gives realcore/realgraph/snd file names and module boundaries. The GC
  release ELF carries full DWARF (the README dumps it with `dtk dwarf dump`; you need your own disc). It's the
  best single source for REAL/SND names and types. **Caveat:** SND 9.x is several major versions newer than what
  Nightfire links against.
- The README also points to the **Xbox 360 PDBs of NFS ProStreet** (open them with resym), which have namespaces
  and visibility for EAGL4-era code.

### ssxdecomp: SSX (2000), SSX Tricky (2001), SSX 3 (2003, PS2 + GC) (checked)
- https://github.com/ssxdecomp (repos `ssx`, `ssxdvd` = Tricky, `ssx3`, `ssx3_gc`). They're splat/dtk matching
  decomps. `ssx3_gc` is CC0. I didn't find a LICENSE file in `ssx`, `ssxdvd` or `ssx3`.
- These are EA Canada titles from the same years, and they link **REAL**: `ssx/config/eac_symbol_addrs.txt`
  ("EAC/REAL/SND/SPCH symbols", FILESYS_*), `ssx/include/eac/real/{core/memory.h,file/filesys.h}`, and
  `ssx3/config/symbol_addrs.txt` names `SYNCTASK_*`, `ASYNCFILE_release`, `FILESYS_*`, `MEM_*`, `SHAPE_unpack`,
  `SHAPE_locate` and `SHAPE_cloneat`. Tricky and SSX 3 are probably the closest *public* match to our REAL
  version by date.
- **No EAGL**: SSX 3's renderer and animation are its own (`cAnimModel`, `cRiderAnimBase`). It's useful for REAL
  and for the file formats, not for EAGL.
- `ssx3/docs/notes/subsystems/asset-formats.md` and `audio.md` describe how the loaders handle BIGF, RefPack
  members, SSH textures and EA audio, as notes on the loaders. For full layouts they point to SSX-Library (below).

## Medium relevance: format references and tools

| Source | Covers | License / maturity | Notes |
|---|---|---|---|
| [bartlomiejduda/EA-Graphics-Manager](https://github.com/bartlomiejduda/EA-Graphics-Manager) + [ReverseBox](https://github.com/bartlomiejduda/ReverseBox) | FSH/SSH/PSH/XSH/GSH/MSH "shape" containers: parse, preview, export, import | GPL-3.0, Python, active (190+ commits) | The most complete public shape-format code. It handles PS2 and Xbox swizzles and palettes. Specs at [RE Wiki: EA SSH FSH Image (Type 1)](https://rewiki.miraheze.org/wiki/EA_SSH_FSH_Image_(Type_1)) and [(Type 2)](https://rewiki.miraheze.org/wiki/EA_SSH_FSH_Image_(Type_2)). It's the format that realgraph's SHAPE_ loads. |
| [Denis Auroux: Unofficial NFS resources](https://www.cmls.polytechnique.fr/cmat/auroux/nfs/index.html) ([nfsspecs.txt](https://www.cmls.polytechnique.fr/cmat/auroux/nfs/nfsspecs.txt)) | Original FSH/QFS (RefPack) specs, FSHTool (v1.22, Dec 2002, **supports NFS HP2**), QFS editing suite with source | Freeware plus source, old but stable | The classic FSH reference. Its HP2 support suggests HP2-era FSH was readable by the generic tool. |
| [GlitcherOG/SSX-Library](https://github.com/GlitcherOG/SSX-Library) | C# readers for SSX 1-3, Blur, On Tour: models (MPF/MNF/MXF), world, SSH, BIG, RefPack, EA audio | GPL-3.0, mid-refactor | It's what the SSX 3 decomp uses for layouts. The SSX model formats aren't EAGL, so it's useful for the container and compression formats only. |
| [refpack crate (Rust)](https://github.com/actioninja/refpack-rs) | RefPack/QFS compress and decompress, header variants are generic | MPL-2.0, maintained (v5.x) | Clean modern reference. Also [qfs-compression (npm)](https://npmjs.com/package/qfs-compression). |
| vgmstream (`src/meta/ea_schl.c`, EA-XA decoders) and FFmpeg `libavformat/electronicarts.c` | EA SCHl/SCxl streams (the stream files SNDSTRM plays), EA-XA ADPCM R1/R2/R3 | vgmstream: ISC-style; FFmpeg: LGPL | [MultimediaWiki: Electronic Arts SCxl](https://wiki.multimedia.cx/index.php/Electronic_Arts_SCxl). Useful for checking our stream parsing and decode, not for SND's internals. |
| [xan1242/MPFmaster](https://github.com/xan1242/MPFmaster) | EA "Pathfinder" interactive-music MPF files | Open source | Only matters if Nightfire's driving music uses Pathfinder (MW does: `SFXObj_Pathfinder`). Unverified for us. |
| OpenSAGE / C&C Generals BIG tools | `BIGF` archive (same magic as our `.viv`) | OpenSAGE LGPL-3 | BIGF is simple and well documented. Use these only to cross-check the directory format (big-endian sizes and offsets). |

## Low relevance

- [berkayylmao/NFSPluginSDK](https://github.com/berkayylmao/NFSPluginSDK) (BSD-3) and
  [berkayylmao/OpenSpeed](https://github.com/berkayylmao/OpenSpeed): reversed game-side types for PC MW, Carbon
  and ProStreet. They're gameplay structs, not EAGL internals.
- [SpeedReflect/Binary](https://github.com/SpeedReflect/Binary), [nlgxzef/NFSU2Unlimiter](https://github.com/nlgxzef/NFSU2Unlimiter),
  PryHUB (U2 assets): NFSU/U2/MW PC modding. Their TPK/bChunk texture packs belong to Black Box's later
  pipeline, not to FSH/EAGL-1-era data.
- **Nothing public found** for: NFS HP2 (PS2), NFS Underground or U2 decomps, Agent Under Fire, Everything or
  Nothing, Medal of Honor Frontline/Rising Sun, Def Jam, Sims Bustin' Out, Harry Potter, Freedom Fighters, or the
  2001-2005 EA Sports titles. That's from web and GitHub searches only; Discord-only projects wouldn't show up.
  The Xentax/ZenHAX forums (the historic home of EA format threads) went offline in 2023. Archived copies exist
  but I didn't search them.
- Burnout 1-3 is Criterion's RenderWare work, not EAGL, so I skipped it.

## Suggested use

1. Before naming or laying out any `EAGLAnim::Fn*` class, open the matching file in
   `nfsmw/src/Speed/Indep/Src/EAGL4Anim/` (and `symbols/PS2/PS2_types.nothpp`). Expect the vtable shape and
   method names to carry over, and the fields to need checking. Record each match as "EAGL4 analogue" and verify
   it against our Xbox code.
2. For REAL (FILESYS/ASYNCFILE/SYNCTASK/TIMER/SHAPE/FONT/LOCALE) and SND/SNDSTRM, grep MW's
   `symbols/PS2/PS2_functions.nothpp` for parameter names and types, and use SSX 3's `symbol_addrs.txt` as a
   second, closer-in-time witness.
3. For the FSH/shape data EAGL's texture side loads, use EA-Graphics-Manager and the RE Wiki specs. For RefPack
   use the refpack crate. Read their code for reference only; both are GPL, so write our own code.
