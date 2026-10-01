# Movie selector (P_FMVTEST / P_FMVPLAYER) - action engine, Xbox EU `default.xbe`

Researched 29 Sept 2026 from Ghidra `/Xbox_EU/default.xbe` (read only), the retail XBE and the front-end
level bundle `07000048.bin` (HT_Level_Menu_Pre), extracted from the disc with the scratch `edl.py`.

**Verdict: dead code on a retail disc.** Both pages exist in the shipped menu data and both handlers are
in the XBE, but nothing ever sends `GoPage(P_FMVTEST)`. There is no code immediate, no reference in the menu
data or scripts, no computed page value that can reach it, and no debug switch. The evidence is below. It
is a leftover developer movie browser.

## What it is

Two pages in the front-end menu set 0x80000002:

| Page | Hashcode | Handler | Menu-data record (in 07000048.bin) |
|---|---|---|---|
| `P_FMVTEST` | 0x4000004f | 0x85a50 | 0x18be76, 0xc1 bytes, no background movie: a window, plus list control 0x10000229 (type 6, at x=0x86 y=0x4b, 0x174 x 0x14b) |
| `P_FMVPLAYER` | 0x40000050 | 0x85cd0 | 0x18bf4c, 0xd8 bytes: a window, plus button 0x10000165 whose text is still "Autosave " (copy-paste leftover) |

### P_FMVTEST_Handler (0x85a50)
- 0x4c (page entered): `Send(mgr, 0x10000229, 0x17, 0, 0)` (reset the list), then 22 ×
  `Send(mgr, 0x10000229, 0x10 /*add item*/, (int)name, movieHash)`. The names are raw `.rdata` strings, not
  text-bank labels (which also marks it as a dev feature):

| # | Name (address) | Movie hash | File on disc (`eurocom\25_fps\%08x.xmv`) |
|---|---|---|---|
| 0 | Castle - Halo Jump (0x16185c) | 0x07100005 | present |
| 1 | Castle - Gondola (0x161848) | 0x07300008 | present |
| 2 | Estate - Meet Mayhew (0x161830) | 0x07100001 | present |
| 3 | Estate - Mayhew Death (0x161818) | 0x07300004 | present |
| 4 | Tower Infil - Intro (0x161804) | 0x07100009 | present |
| 5 | Tower Infil - Outro (0x1617f0) | 0x0730000b | present |
| 6 | Nuclear Plant - Interview (0x1617d4) | 0x0710000c | present |
| 7 | Nuclear Plant - Entering Vent (0x1617b4) | 0x0730000d | present |
| 8 | Tower Escape - Dom Death (0x161798) | 0x07100011 | present |
| 9 | Tower Escape - Lobby Rescue (0x16177c) | 0x07300013 | present |
| 10 | Evil Base - Descent All (0x161764) | 0x07100014 | present |
| 11 | Evil Base - Kiko Death (0x16174c) | 0x07300017 | present |
| 12 | Space Station - Arrival (0x161734) | **0x07100017** | **missing**: the shipped intro is 0x07100018 (`FMV_INTRO_SPACE_STATION`) |
| 13 | Arrival - Happy Ending (0x16171c) | 0x0730001b | present |
| 14 | Menu - BG Loop (0x16170c) | 0x07330048 | present |
| 15 | Menu - FE Loop (0x1616fc) | 0x07350048 | present |
| 16 | Menu - EA Logo (0x1616ec) | 0x07370048 | present (English ident; the FR/DE/ES swap happens in `psiStartBackgroundMovie`) |
| 17 | Menu - MGM Logo (0x1616dc) | 0x07380048 | present |
| 18 | Menu - DAD Trailer (0x1616c8) | 0x07390048 | present |
| 19 | Menu - Attract Movie (0x1616b0) | 0x073a0048 | present |
| 20 | Menu - Esthero (0x1616a0) | 0x073b0048 | present |
| 21 | Menu - Making Of (0x16168c) | 0x073f0048 | present |

  The stale hash for item 12 suggests the list was never updated after the space-station intro was
  renumbered. (What happens when you pick it, with the file missing, has not been tested.)
- 0x4b (Select; the list has no handler, so the manager passes it on to the page): `movie_hashcode =
  Send(mgr, 0x10000229, 0x35 /*GetScrollValue*/, 0, 0)`, then `__Menu_SendMessage(&manager[mgr], 0x44, P_FMVPLAYER, 0)`.
- Back (B) is the manager's default page-stack pop. The handler has no code for it.

### P_FMVPLAYER_Handler (0x85cd0)
```
0x4c: frame = 0;                                    // DAT_0025df6c
0x50: if (frame == 1 && movie_hashcode) Menu_PlayMovie(movie_hashcode, /*skippable*/1, /*loop*/0,
                                                         /*stop FE music*/1, /*notify*/1, /*no-skip frames*/0);
      frame++;
      if (frame > 1 && psiMovieLoop())              // movie finished: restart the FE music loop, notify the manager
          __Menu_SendMessage(&manager[mgr], 0x5f, 0, 0);   // back to P_FMVTEST
```
The movie starts on the second update frame. `Menu_PlayMovie` (0x7fe00) stores the skip flags
(0x25d7bd skippable, 0x25d7be FE-music, 0x25d7c0 no-skip countdown), drops SFX_MUSIC_FRONT_END_LOOP_01/02,
and calls `psiStartBackgroundMovie(hash, loop, SFXGetVolume())`. That function builds the file name with
`sprintf("%08x.xmv", hash)` and plays it through `BackgroundMoviePlayFile`. While the movie plays,
`MenuManager_Update` (0x94370) calls `Menu_UpdateMovieCounter` every frame and `Menu_StopMovie(0)` on
`ACTION_SKIP_CUTSCENE`. Skipping stops the stream, and the next `psiMovieLoop` check sees it finished and
returns to the list.

For comparison, `P_TRAILER_Handler` (0x8dca0) is the retail feature built on the same machinery. It plays
0x07390048 on entry and returns on Select or when the movie ends. It is reached from the AV-options page
(see below).

## Reachability: every way a page can be entered, checked

A page becomes current only through `Manager_SendMessage(mgr, 0x44 GoPage, pageHash, flags)` (0x92c40). That
searches the pages the manager built from the loaded menu file and switches only on an exact hashcode
match. `Menu_ChangePageCloseIris` and `MenuManager_Create`'s start page both end in the same message. The
possible sources of the hashcode:

1. **Code immediates.** A byte search of the whole XBE for the dword 0x4000004f finds **no** occurrence in
   any section. So there is no `PUSH`/`MOV` of it, and no table in `.data`/`.rdata` holds it (a jump table
   or page list would contain it). 0x40000050 appears once in code, the `PUSH` inside P_FMVTEST_Handler;
   the other two raw hits are inside texture data. In `Handler_HandleMessage` (0x8e320) the cases for
   P_FMVTEST/P_FMVPLAYER only *dispatch* to the handlers.
2. **Menu data and scripts.** Menu files (file type 8 -> `MenuManager_Load` 0x92b50 -> parsed by
   `MenuManager_Create` 0x93960) store hashcodes as little-endian dwords. Page transitions are script
   messages (record 0xfffffff4: target, message byte, arg1, arg2) or creation-time manager messages (record
   0xfffffff3). For example, the trailer entry in `P_CNAVOPTIONS` is `f4ffffff fdffffff 44 4e000040 00000000`
   = GoPage(P_TRAILER) at 0x1a4e50. In the front-end bundle 07000048.bin the dword 0x4000004f occurs only
   twice: in its own page header (0x18be7a) and in its own window record (0x18be8f). **No script or control
   points at it.** P_FMVPLAYER likewise occurs only in its own records. The same search over the dumped
   level bundles (dump_levels/*.glb, the in-game pause menus) and every file in build/windows/Release/dump
   finds only unaligned noise inside texture or mesh data, never next to a 0x44 message byte or a
   0xfffffff4 record. Those menu sets do not contain the page anyway, so a GoPage there would find nothing.
3. **Data-driven page values in code.** Handlers that jump to a page held in data: `C_SBCNOPTIONS` item 6 and
   `P_CNMENU` 0x6a use `ls.field2` <- `thisControl->field_0x20` (a control identifier from the menu data,
   see 2). `GameState.ReloadMenupage` (0x1f659c), the start page when the front end is rebuilt, is written
   only with the constants P_START, P_NFMAP, P_NFRESULTS, P_MPDEBRIEFING, P_LANGUAGE, P_ESTHERO
   (C_LBPMMAP, P_ENDMISSION, Mission_Update, MP_Update, bootup, ResetMap_Load, Boot_LoadPTPData after the
   driving engine returns). The one computed write, in C_GCPAUSE (0x815a5: `(x & 0x17) + 0x4000001c`), is
   bounded to 0x4000001c-0x40000033.
4. **Handler return values.** Handlers return a bool "handled" only. None of them returns a page.
5. **Cheat or debug flags, button combos, config.** The Secret Unlocks table (docs/ui/secrets.md) only
   touches missions and the bonus bits. `CheatInfo` holds Immortal/AllWeapons/UnlimitedAmmo only. There are
   no strings that name the page or a debug menu ("FMV", "movie", "test" appear only as the 22 list names
   and the `%08x.xmv` format). `P_TWEAKS`/`P_TWEAKS2` have handlers but their pages are not in the retail
   front-end data either. The dev `Debug%d.txt` path does not feed the menu.

Result: none of these can produce 0x4000004f on a retail disc, so P_FMVTEST (and P_FMVPLAYER, which only
P_FMVTEST can open) is unreachable in normal play. You would need to patch or inject something (e.g. a
reimplementation or a debug hook that sends `GoPage(0x4000004f)` to manager 0 while the front end is up).
The page, its list control and its handlers are all intact, so that would work. Item 12 would try the
missing `07100017.xmv`.

## Reimplementation notes

- Both handlers are small and have no side effects beyond `movie_hashcode` and the frame counter
  (0x25df6c). Reimplement them with the 22 (name, hash) pairs as a static table. If they should be usable,
  fix item 12 to 0x07100018 behind a switch that is off by default.
- To expose it for testing, a devtools unit (`src/action/devtools/`) can send
  `Manager_SendMessage(&manager[0], 0x44, 0x4000004f, 0)` from a debug key while the front end is loaded.
  The retail menu data already holds the page.
