# Modding: a design

How the reimplementation is meant to be wrapped, extended and improved without giving up what makes it worth
having: a core that is the original game, checked against the original. Nothing here is built yet. This is the
design and its order of work: first written 25 October 2026, and revised the same day with the user's decisions
(section 8).

Related:
- docs/core-deviations.md: what the core does differently today;
- docs/original-bug-fixes.md: the `FIX_ORIGINAL_GAME_BUGS` cases;
- docs/install-and-update.md: the launcher;
- docs/action-timedemo.md and docs/deterministic-maths.md: how sameness is checked;
- docs/style-guide.md: how game code must read.

## Summary

- **Mods are first-party.** They are built-in modules, C++ in this repository, compiled into the one executable
  we ship and switched on at run time, plus data replacements through a file overlay. We load no external DLLs
  and build no sandbox. The source is open, so anyone can compose and check their own build.
- **Three kinds of hook, all outside game code:**
  - run code *before* a function;
  - *replace* a function with a new version;
  - run code *after* it, with its return value.

  A generated dispatcher stands in front of each listed game function, the way the injection tables stand in
  front of the originals today. The game's code gains nothing.
- **With no mods enabled, the game is the core.** Recordings carry their mod list. Playback applies that list,
  and refuses a recording whose mods it cannot match. CI plays the authentic recordings on the shipped
  executable with mods off.
- **Two profiles:**
  - **Default:** PC controls (keyboard and mouse) and the bug fixes, with other quality-of-life modules allowed.
  - **Speedrun:** the bug fixes, time-demo recording and an autosplit module, and nothing else.
- **What moves out of the core:** the non-authentic features: the keycode fix, keyboard and mouse, the field of
  view, the frame-rate override.
- **What stays:** crash fixes with no gameplay effect (`Menu_UpdateWheel`), and platform quality of life
  (widescreen, render scale, anti-aliasing).
- **The PS2 and GameCube data stay build options.** The launcher offers them as a choice of game data and
  explains what differs.

---------------------------------------------------------------------------------------------------------------

## 1. Goals and non-goals

### Goals

1. **With no mods enabled, the game must play bit for bit as the core does.** The shadow tests, lockstep runs,
   time demos and all-original runs keep their meaning. This goal outranks the others.
2. **What the core has that the original lacks, apart from platform affordances and crash fixes, moves out of
   the core** (section 5).
3. **Each feature can be switched on and off at run time**, from the launcher, with its own settings, and without
   a rebuild.
4. **The same modules work on every platform we ship:** Windows, Linux and macOS, on x86, x64 and arm64. They do
   because they are compiled with the game.
5. **Both engines.** The action and driving executables read the same mod list, and each applies the modules
   written for it.
6. **A modded session can be told apart from an authentic one,** and a speedrun session from both.

### Non-goals

- **Code from outside the repository.** We load no DLLs or shared libraries, offer no scripting, and keep no
  stable binary ABI. Revisit only if outside authors ask (section 2.4).
- **Distributing game data.** A mod carries its own content or differences, never the disc's files, as the
  launcher's rule already says.
- **New levels, a level editor or a model importer.** The formats are documented (docs/level, docs/anim on the
  `blender-exports` branch), and tools can come later. Nothing here blocks them.
- **Network play.** It is not designed here (section 6 says what a mod list would mean for it).

### How goal 1 is guaranteed

| Layer | The guarantee | Checked by |
|---|---|---|
| Game code | The core's functions compile from the same source as today. A hooked function keeps its body, and only the name it is linked under changes (section 2.2). | an object-code diff of the game's compilation units, with and without the dispatchers: the code may differ only in symbol names (the comparison the 64-bit work used, renamed symbols mapped: docs/driving/64-bit.md) |
| Dispatchers | With no hook installed, a dispatcher only tail-calls the core function. That changes no value, x87 values in ST0 included. | the recordings and lockstep runs, played on the shipped executable with mods off |
| Mod list | Each recording stores the mods it was made with. Playback applies the same list, or refuses to play. | the recording format (below) |
| Settings | Modules have their own settings file. Nothing in the core reads it. | review: a module's keys never appear in `settings.ini` |
| Determinism of modules | A gameplay module reads host input only through recorded channels, and takes its maths from DetMath. | the module's own recordings, played on AMD and Intel |

**Recordings.**

- **Format:** time demos get a `MODS` section before `STRM`, and the format version goes to `NFDEMO 0 3`. The
  section lists each active module or data mod: its id, its version (the game's build for a module, a hash of
  the files for a data mod) and its settings.
- **Recording** writes the active list as it stands. With mods off, the list is empty: an *authentic* recording.
- **Playback** applies the recording's list in place of the user's, as it already applies the recording's
  `settings.ini`. If a listed gameplay mod is missing or different, playback refuses before frame 0, with a new
  exit code 5 ("the recording's mods are not available").
- **Version 2 recordings** (all that exist today) count as made with what today's default build compiles in
  (section 5.4).
- **A module's own input** goes through the recording, as the mouse does today (`MouseLook_DemoExchange`). A new
  event tag, 0x80, carries it: module index, length, bytes.
- **Lockstep baselines** (the driving engine, `NIGHTFIRE_LOCKSTEP`) are made with `-nomods`. Their file names
  carry the mod list's hash when it is not empty.

**CI.**

- The authentic recordings play on the shipped executable with mods off, every frame.
- Each gameplay module has its own short recordings, made with that module alone. They check the module and the
  hook dispatch together.
- The recordings are kept alive for testing: they are rebased when the core changes, as now.

---------------------------------------------------------------------------------------------------------------

## 2. What a mod can be

| Kind | What it changes | Mechanism |
|---|---|---|
| **Built-in module** | behaviour, controls, menus, drawing, observers | C++ in `src/mods/<id>/`, compiled into the executable, switched on at run time through hooks |
| **Data mod** | files the game reads: textures, sounds, text, level bundles | the file overlay (section 2.1). Ours ship in the repository; a player's own replacement folder works the same way |

A data mod can also switch built-in modules on and set their settings. A pack of HD HUD art, for example, can
turn on the HD UI module.

Each mod declares **what it affects**, which decides how strictly it is treated:

| Affects | Meaning | Examples | In a recording |
|---|---|---|---|
| `observer` | reads the game's state; changes nothing in it or on screen | autosplits, a stats logger | listed; playback warns if it is absent, and plays on |
| `cosmetic` | changes only what is drawn or heard, never the game's state | HD textures, the HD UI art | listed; playback warns if it is absent, and plays on |
| `gameplay` | can change the game's state | bug fixes, keyboard and mouse, FOV, ADS, branding limits | listed; playback refuses if it is absent or different |

The time demos check the declaration: a mod declared `cosmetic` that changes a checksum is a bug in the mod.
Field of view counts as `gameplay`, because what is drawn feeds the AI and auto-aim (docs/action-timedemo.md,
"Settings").

### 2.1 Data: the file overlay

The engines reach their files through two layers:

- **The host path layer,** `Xbox_ResolvePath` (src/common/xboxPath.cpp). It turns an Xbox path into a host one,
  and serves every file of both engines: the driving engine's loose files, the action engine's archives
  `filesys.dNN`, its movies, and the saves.
- **The action engine's archive,** src/action/engine/FS.cpp. It finds a file's entry by the CRC32 of its
  normalised name (`FS_MatchFilenameToHeader`) and reads it out of an archive.

The overlay works at both:

| What a mod replaces | Where it lives in the mod | Served by |
|---|---|---|
| A loose file of either engine (`driving/...`, `eurocom/25_fps/<hash>.xmv`, ...) | `files/<path on the disc>` | `Xbox_ResolvePath`: the first enabled mod that has the path wins, then the disc |
| A file inside the action archive, by name (`07xxxxxx.bin`, `UKTxt.dat`, ...) | `archive/<name>` | *before* hooks on FS.cpp's name-taking entry points (`FS_GetFileSize`, `FS_LoadFileIfReady`, `FS_AllocateAndLoadBlocking`, `psiAsyncOpenFile` and its reads): a name the overlay has is read from the host file, otherwise the core runs |
| A texture, by hash | `textures/<hash>.dds` | the existing replacement (src/common/gfx/textureReplace.cpp), which today reads one `textures/` folder beside the executable and would read each enabled mod's folder in order |
| A level bundle's item, by hashcode (later) | `items/<hashcode>.bin` | a hook on `LoaderProcess`'s registration of each item, as nightfire-port's plan does it (its docs/mods.md, phase 2) |

- **Saves are never overlaid.** Writes always go to `user/`, and the disc stays read-only.
- **The overlay is built once at start:** the enabled mods' file lists are merged into one sorted table. With no
  mod enabled, the table is empty.
- **The launcher shows where two mods replace the same file.**

The texture replacement is already a data mod in all but name. It is not in docs/core-deviations.md, and should
be listed there until it moves (section 5).

### 2.2 Code: hooks

#### Three kinds of hook

| Hook | What it does | Typical use |
|---|---|---|
| **Before** | runs before the function, with its arguments. It may change them, or skip the function by giving the return value itself | the mouse's turn added before `Player_ViewClamping` clamps; the data overlay serving a file |
| **Replace** | the function is replaced by a new version, the module's copy | a bug fix; ADS's aiming; a feature that needs to change the middle of a function |
| **After** | runs after the function has returned, with its arguments and its return value. It may change the value returned | events (a pickup taken, a level loaded); adjusting a result |

#### The dispatcher

A list file, `src/mods/hooks_action.txt` (and `hooks_driving.txt`), names each game function that modules may
hook. For each one, the build generates a dispatcher with the function's name and signature:

```cpp
// Generated; Locks_Init_Core is the game's own definition, compiled under that name
void Locks_Init(void) {
    if (!ModHooks_Locks_Init.any)                  // nothing installed: straight on, as a tail call
        return Locks_Init_Core();
    for (BeforeHook *h = ModHooks_Locks_Init.before; h; h = h->next)
        if (h->fn() == MOD_SKIP)
            goto after;                            // a before hook gave the result itself
    (ModHooks_Locks_Init.replacement ? ModHooks_Locks_Init.replacement : Locks_Init_Core)();
after:
    for (AfterHook *h = ModHooks_Locks_Init.after; h; h = h->next)
        h->fn();
}
```

**Where the code comes from:**

- The game's source does not change. The one tagged definition line already has the shape the preprocessor
  reads. It is compiled under a second name (`Locks_Init` becomes `Locks_Init_Core`), and the dispatcher takes
  the original name.
- **Calls from other files** reach the dispatcher.
- **Calls between functions of the same file** go straight to the core. The list says so where it matters.
- **Member functions** (the driving engine) also need the class to declare the second name. The generator adds
  that declaration in a build copy of the class's header. This is the part to prototype first (phase 2).
- **Inlining:** the game is compiled without inlining today (docs/action-standalone.md). If that ever changes,
  every listed function must stay out of line, or its callers' inlined copies would bypass the hooks.

#### Why "after" is straightforward

The concern with "after" hooks is early returns. A function with five `return` statements seems to need five
places to run the hook. With a dispatcher it does not:

- The dispatcher **calls the whole function.** Whichever `return` the core takes, early or late, it returns to the
  dispatcher.
- The after hooks then run once, with the value it returned.
- Nothing is inserted into the function's body, so its returns never have to be found or edited.

What an after hook **can** see and do:

- the arguments as the function received them (after any before hook changed them). For a pointer argument,
  that means the object as the function left it;
- the return value, which it may replace;
- the game's state as the function left it: globals, objects, the player;
- calls into the game, under the same rules as game code (below).

What it **cannot** do:

- see the function's local variables, or which `return` was taken, or the state at any moment inside the call;
- undo what the function did: a sound started, a random number drawn, an object created. To change those, it
  needs a *before* hook (to change or skip the call) or a *replacement*;
- know, when it is on a function a same-file caller reaches directly, about those calls (above).

A function that returns a float or double through ST0 on the x87 builds passes its value through the dispatcher
unrounded. On the path with no hook, that is guaranteed by the tail call. When an after hook is installed, the
dispatcher returns the value as the declared type, which can round it once more. That is acceptable, because the
session is modded anyway.

#### Order and conflicts

- Before hooks run in the mod list's order, and after hooks in reverse, so that the hooks nest.
- A function has **one replacement at most.** Two modules that replace the same function conflict, and the
  launcher refuses to enable both. A module that only needs to add to a function uses before or after hooks,
  which combine freely.
- **A replacement** is a copy of the core's function with the change in it. A small CI check,
  `tools/fixdiff.py`, compares each copy with its core function. It expects them to differ only between
  `// mod:` markers, so a copy that falls behind a core cleanup is caught.

#### Why bug fixes should not become run-time branches

The obvious way to switch `FIX_ORIGINAL_GAME_BUGS` at run time is to turn each `#if` into an `if (setting)`. We
do not:

- **The style guide forbids it:** "No settings checks, logging or test branches inside reimplemented functions."
- **It changes the core's code.** Both branches are compiled into the function, so its registers, stack and
  scheduling change. The function is then no longer the one the shadow tests checked, even with the setting off.
  On the x87 builds, a different register allocation can mean a different spill, and so a different rounding.
- **It does not scale** to features like ADS, which touch half a dozen functions in several places each
  (nightfire-port's `player_ads.h`).

A fix is therefore a **replacement:**

- the core keeps only today's `#else` branch, the original;
- the fixed function lives in the bug-fixes module;
- the `#if FIX_ORIGINAL_GAME_BUGS` blocks, the CMake option and the define go away.

Run-time branches are allowed only in our own code that is not the game's: the host layer, the backends,
devtools, and modules.

#### Where the hooks sit

**Seams that exist already.** These are our own code, not game code:

| Seam | Where | Events and uses |
|---|---|---|
| The frame | `mainloop` around `GameFlow_Main` (`Demo_FrameStart` / `Demo_FrameEnd`) | frame start and end |
| The frame's input | `Input_UpdateWithMouse` (src/action/input.cpp), around the game's `Input_Update` | input before the game polls it; steering after |
| The pads' poll | `MenuProbe_PollDevices` | the keyboard pad and key bindings, which the menus see too |
| Menu messages | `MenuProbe_HandleMessage` | watching or replacing a menu's handling; alternate menus |
| Engine switch | `psiLaunchDriving`, common/launchInfo.cpp | passing the mod list to the other engine |
| Files | `Xbox_ResolvePath`, texture upload | the data overlay |
| Present | the backends' `GfxHost_*` callbacks | overlay drawing (the radial selector, the HD UI, menus); VR's second eye |

**Listed game functions** (dispatchers). The list is small to begin with, around a hundred functions chosen for
the modules we know of, out of 2,320 tagged action functions. A listed function is a promise to the modules of
its meaning and signature, and the list tells reviewers what a change to the core can break.

**Events,** emitted by after hooks or seams. Starting candidates, by the functions the code has:

| Event | From |
|---|---|
| level load, level start | `ResetMap_Load`, `Mission_Init` |
| mission status (won, failed) | `Mission_Status` |
| pickup taken, pickup spawned | `Pickup_Handler`, `Pickup_Create` |
| weapon changed, fired | `Player_WeaponChange`, `Player_WeaponFiring` |
| load screen up, down | `LoadScreen_Draw`, `LoaderLoad` |
| on foot to driving and back | `psiLaunchDriving`, the driving engine's start |
| damage, death | the player's and drones' damage functions (chosen when the first module needs them) |

A module never edits a core function to make room for itself:

- If it needs a point that no listed function gives, the function goes on the list.
- If the point lies in the middle of a function, the module replaces the function.

#### What a module can do

| Ability | How |
|---|---|
| Watch events | after hooks and seams |
| Change arguments, skip a call, change a result | before and after hooks |
| Change how a function works | a replacement |
| Draw over the game | the overlay pass at present: quads, text and textures at the output resolution, after the game's frame |
| Take input | the input seams. A module can consume a key or button before the game sees it (a menu, the radial selector). Gameplay input goes through the recording (event 0x80) |
| Call the game | directly, since modules are C++ with the game's headers (`Player_WeaponSelect`, `Rand_Random`...). The same rules as game code apply: the same random draws, DetMath |
| Keep settings | declared in its manifest, edited by the launcher, read through the mod layer |
| Keep state | in `user/mods/.state/<id>/`, never in the game's saves (section 6) |

### 2.3 One executable

Each engine ships as **one executable:** the core's sources, the dispatchers and the modules. Mods off, it is the
core, by construction (the object-code diff) and by test (the recordings). It also has:

- **Time-demo recording,** which the speedrun profile uses. Today `action_standalone` answers the demo hooks with
  NoDevtools.cpp's stubs, and only `action_standalone_test` has Demo.cpp. Recording moves into the shipped
  executable: off, its hooks give the game its own clock, pads and sound answers, as `action_standalone_test`
  already does when it plays all 13 recordings. Playback, `NIGHTFIRE_ORIGINAL` and the shadows stay in the test
  builds.
- **No injected build.** The injected build (the shadows, the all-original runs) never has the mod layer. A
  build without dispatchers stays in CI as the reference for the object-code diff.

### 2.4 Why first-party only

We considered external native plug-ins (a C ABI for DLLs and shared libraries), scripting (Lua) and built-in
modules:

| | Native plug-ins | Scripting (Lua) | Built-in modules |
|---|---|---|---|
| **Safety** | arbitrary code from the internet, running with the user's rights | needs a sandbox | open source in our repository, reviewed, built by our CI |
| **Platforms** | one binary per OS and CPU from each author; macOS refuses unsigned libraries | portable | every platform we build |
| **Determinism** | anything goes | can be made deterministic | held to the core's rules, checked by recordings |
| **Maintenance** | a frozen ABI, with wrappers for every structure (32/64-bit layouts differ) | an API to keep | the compiler finds every module a core change breaks, in the same pull request |
| **Reach** | only what the ABI wraps | events and a few actions | anything, with the game's own headers |

**Decided:**

- Built-in modules only, with no defensive sandboxing.
- The repository is open, so anyone can read every module, compose their own build, and check what a build
  contains.
- External mods, perhaps scripted then, are revisited only if outside authors want to ship their own.

---------------------------------------------------------------------------------------------------------------

## 3. The package

### Manifest

Every module and data mod has a manifest. For a module it is compiled into the executable; a data mod has it as
`mod.json` at its root.

```json
{
  "id": "pc-controls",
  "name": "Keyboard and mouse",
  "description": "One line.",
  "kind": "module",
  "affects": "gameplay",
  "engines": ["action", "driving"],
  "data": ["xbox", "ps2"],
  "requires": [],
  "conflicts": [],
  "settings": [
    {"key": "MouseSensitivity", "type": "float", "default": 1.0, "min": 0.1, "max": 10.0, "label": "Mouse speed"},
    {"key": "MouseZoom", "type": "choice", "choices": ["hold", "toggle"], "default": "hold", "label": "Zoom button"}
  ]
}
```

| Field | Meaning |
|---|---|
| `id` | lower-case letters, digits and hyphens; unique; never changes |
| `kind` | `module` or `data` |
| `affects` | `observer`, `cosmetic` or `gameplay` (section 2) |
| `engines` | `action`, `driving` or both |
| `data` | the game data it works with: `xbox`, `ps2`, `gamecube` (section 5.5) |
| `requires`, `conflicts` | resolved by the launcher. Replacing the same function is a conflict the build finds by itself |
| `enables` | (data mods) modules this mod turns on, with their settings |
| `settings` | what the launcher shows: `bool`, `int`, `float`, `choice`, `key` (a binding), each with a default and a range |

A module's version is the game's. A data mod in the repository has the release's version, and a player's own
folder is identified by the hash of its files.

### Folders and mods.ini

Under the launcher's per-user folder (docs/install-and-update.md):

```
Nightfire/
  app/<version>/mods/       the data mods we ship, read-only, updated with the game
  user/
    settings.ini            the core's settings, as today
    mods.ini                the profile, which mods are on and in what order, and their settings
    mods/<id>/              a player's own data mods (files/, archive/, textures/)
    mods/.state/<id>/       state a module keeps
```

```ini
[Mods]
Profile=Default
Enabled=fixes,pc-controls,fov

[mod:pc-controls]
MouseSensitivity=1.0
MouseZoom=hold
```

- **Order:** the `Enabled` line is the load order. An earlier mod wins a file conflict, and before hooks run in
  this order.
- **How the game is told:** both engines read `user/mods.ini` at start (the working directory, beside
  `settings.ini`). The command line overrides it: `-nomods` (safe mode) or `-mods <file>` (tests).
- **The other engine:** when one engine starts the other (common/launchInfo.cpp), it passes the list it runs
  with, so that a session never changes mods half-way.
- **The game prints the active list at start** and writes it into crash logs.
- **Marks on the pause screen:** a small "modded" mark when a gameplay mod outside the Default profile is active,
  and a "speedrun" mark under the Speedrun profile. Both are the mod layer's drawing, not the core's.

---------------------------------------------------------------------------------------------------------------

## 4. Profiles and the launcher

### Profiles (decided)

| Profile | Mods | Others allowed | Purpose |
|---|---|---|---|
| **Default** | PC controls (keyboard and mouse), the bug fixes | yes: other quality-of-life modules (FOV, the HD UI, the radial selector, ...) | what a new install plays |
| **Speedrun** | the bug fixes, time-demo recording, the autosplit module | **no**: every other mod is refused | runs that can be checked |
| **Authentic** (safe mode) | none | no | the core alone: tests, comparisons, and when something goes wrong |

The Speedrun profile:

- **The launcher locks its list.** At start, the game checks that the active list is exactly the profile's,
  and drops the profile otherwise, with a message.
- **Every session is recorded** (`demos/<date>-<time>.nfdemo`, as `DemoRecord` does today). The recording's
  `MODS` section shows the profile, so a run can be checked by playing it back.
- **The driving engine is not recorded** (docs/action-timedemo.md): a recording ends at a driving mission and a
  new one starts after it. A run's car missions have only the autosplit's timing until driving recordings exist.
- **The autosplit module** is to be written. It is an observer: level, load and mission events, sent to LiveSplit
  over its server protocol, and a fixed, marked block in memory for autosplitter scripts that read memory.
- **Keyboard and controller bindings** that speedrunners can adjust: open (section 8). The profile has none
  until the speedrunners have been asked.

### The launcher's Mods page

A **Mods** page beside Play and Settings (docs/install-and-update.md, "The launcher"):

| Feature | Behaviour |
|---|---|
| Profile | Default, Speedrun or Authentic on the Play screen |
| List | the built-in modules and data mods, each with name, description and its `affects` badge. Under Default, the quality-of-life modules can be switched on and off; Speedrun and Authentic are fixed |
| Order and conflicts | drag to reorder; the files two enabled data mods both replace |
| Settings | a form generated from each manifest's `settings`, written to `mods.ini` |
| A player's own data mods | "Add a folder" copies one into `user/mods/`, marked as the player's own |
| Updates | none of its own: modules and our data mods come with the game's releases |
| Safe mode | **Play without mods** (`-nomods`). After a session with mods ends in a crash, the next start offers it |

There is no index of outside mods for now: every module and shipped data mod is in this repository. If outside
authors start shipping mods and want them listed, the launcher can add a source then, such as a curated index
signed like the releases (open, section 8).

---------------------------------------------------------------------------------------------------------------

## 5. Moving today's deviations

### 5.1 The table (decided)

| Today | Where it is | Becomes | What changes in the core | Order |
|---|---|---|---|---|
| `Locks_Init`'s keycode digits (`% 9`; the fix is `% 10`) | game/sp/Locks.cpp | **fixes** module: a replacement (`gameplay`) | only the original stays | 1 |
| `Sprite_Create2` / `psiDrawSprites` lose the last texel | gfx/Sprite.cpp, engine/psiSprite.cpp | **fixes**: two replacements (`cosmetic`: drawing only, and the recordings match with and without them) | the originals only | 1 |
| `Scheduler::Run` loses a fractional timeScale | driving/Scheduler.cpp, devtools/Teleport.cpp | **fixes**, driving side: a replacement (`gameplay`) | the original only. Teleport's lockstep one-tick rule moves into the module's copy, which keeps `HostSleep(1)` as the core does | 3 (needs member dispatchers) |
| `Menu_UpdateWheel` reads item 999 | ui/Menu.cpp | **stays in the core:** a crash fix with no gameplay effect (5.2) | the `#if` becomes the code; the entry moves to core-deviations.md | 1 |
| The CMake option `NIGHTFIRE_FIX_ORIGINAL_GAME_BUGS` | CMakeLists.txt | removed | AllOriginal.cpp's list of fixed functions goes; they then compare as any other port | after the above |
| The keyboard as a pad | psiInput.cpp's `BuildKeyboardPadState` (listed under "Platform" today) | **pc-controls** module (`gameplay`), through the pads' poll seam | the core reads host controllers only. core-deviations.md moves the keyboard from "Platform" to the mod table | 2 |
| Mouse look | engine/mouseLook.cpp/.h; `Input_UpdateWithMouse`; `Player_ViewClamping` (Player.cpp); the mouse buttons and wheel in `BuildKeyboardPadState` | **pc-controls** | `Player_ViewClamping` goes back to the original, which is only the pitch clamp: the module adds the mouse's turn in a before hook, and the original clamps it. `MouseLook*` keys move from settings.ini to `[mod:pc-controls]` | 2 |
| Mouse steering of remote vehicles | engine/mouseSteer.cpp/.h; `Input_UpdateWithMouse`; `MouseSteer_IsRemoteControl` in `Player_ViewClamping` | **pc-controls** | the call after `Input_Update` becomes the module's input seam; the remote-control test leaves `Player_ViewClamping` | 2 |
| Field of view (`FOV`) | engine/viewFov.cpp/.h; Camera.cpp, car.cpp, GunImp.cpp, ui_cheats.cpp, Player.cpp | **fov** module, a quality-of-life module for Default (`gameplay`) | the call sites pass the original's fixed field of view. The module hooks `Camera_CalcViewAngles` (the angle) and `Camera_SetToPlayer` (a real scope's zoom correction) before they run. To confirm when it moves: that those two cover every site | 2 |
| `FPS` override (settings.ini) | `mainloop` (game.cpp), the backends' pacing | **frame-rate** module (`gameplay`: it changes the rate the game is stepped at) | `mainloop` takes the region's 50 or 60 only. Not listed in core-deviations.md today; add it now | 2 |
| Walking forwards and backwards with the scope up (removed 17 October) | was in `Player_Move` | part of the **ADS** module (nightfire-port's ADS walks at 85% while aiming) | none (already out) | 5 |
| Texture replacement (`textures/` beside the executable) | common/gfx/textureReplace.cpp | stays in the backend as the overlay's texture path; the folder becomes each data mod's `textures/` (`cosmetic`) | none in game code | 4 |
| The HUD crosshair's red dot (a candidate bug) | the original's data | a **data mod** replacing the sheet, as original-bug-fixes.md already suggests (`cosmetic`) | none | 4 |
| The other candidate bugs (sound, bots, drones, menus) | docs/original-bug-fixes.md, "Candidates" | each a replacement in **fixes**, if it is ever fixed; a crash with no gameplay effect stays in the core instead (5.2) | none until then | - |
| `Widescreen`, `RenderWidth` / `RenderHeight` | settings.ini; the backends | **stay in the core:** quality of life. Widescreen is the original's own Xbox setting. Render scale is the backend's: the game keeps its 640x480, and visibility counts are scaled back | none | - |
| Anti-aliasing, `Reverb`, `Region`, `Language` | backends; settings.ini | **stay** | none | - |
| A drone's breath phase; sorted directory listings; DetMath; uninitialised stack as zero | core-deviations.md, "Platform" | **stay** | none | - |
| Time demos, `-level`, teleport and dump keys, `NIGHTFIRE_ORIGINAL`, shadows | devtools | **stay** devtools, off unless asked for. Only demo recording joins the shipped executable (2.3) | none | - |

### 5.2 Crash fixes stay in the core

`Menu_UpdateWheel` reads an item's enabled byte 999 entries past the list. On the Xbox that read lands in
whatever memory follows; on a host it may fall outside the allocation and crash.

The core keeps the fix, for three reasons:

- **The original's result can't be reproduced.** It depends on memory that no host lays out as the Xbox did.
- **Nothing visible depends on it.** The slot is blank either way.
- **It is the same case as uninitialised stack,** which the core already writes as zero.

The rule for the future: a fix that only stops a host crash, with no effect on play, stays in the core and goes
in core-deviations.md. A fix that changes play is a module.

### 5.3 Defaults

After the move, the core has neither the fixes nor keyboard and mouse. New installs get the Default profile,
which has both. A developer's build reads `mods.ini` as a player's does, and with no `mods.ini` there are no
mods.

Authentic has no keyboard, so it needs a controller. The launcher says so when Authentic is chosen and no
controller is connected.

### 5.4 The 13 recordings (decided: kept alive for testing)

They were made with `FIX_ORIGINAL_GAME_BUGS` on, and some with the mouse or keyboard:

- **The keycode fix changes the game.** The all-original runs keep `Locks_Init` as ours for that reason
  (devtools/AllOriginal.cpp).
- **The sprite fix changes only the picture.** The wheel fix stays in the core.
- **Keyboard and mouse input change the game.**

Plan:

1. On adding the `MODS` section, version 2 recordings read as `fixes` plus `pc-controls` (with the recording's
   own mouse settings) plus `fov` (from its `settings.ini`).
2. Each is rebased onto an empty list where its session survives: no keypad typed, nothing that needs the
   mouse. A pad-only session keeps its input; a keyboard session's input is recorded after the keyboard pad is
   built, so it replays as pad input. Each rebase is checked as docs/action-timedemo.md asks: the session must
   still show what it was for.
3. The rest keep their mod lists and become the modules' recordings.

The authentic set is then the core's reference in CI.

### 5.5 The PS2 and GameCube data (decided: build options, chosen in the launcher)

The PS2 build (`NIGHTFIRE_ACTION_DATA_PS2`, branch `ps2-data`) is not a mod:

- **It replaces whole loaders,** FS.cpp and Fmv.cpp, adds conversions at load, and puts `#if` seams in a dozen
  game files. A run-time choice would put branches in game functions. The PS2 doc measured that the switch leaves
  the default build's code byte for byte the same.
- **It is what the original did:** the PS2 and Xbox builds are one source with `#ifdef`s.
- **Its recordings and baselines** are per data source in any case.

The GameCube data goes the same way when it gets there.

**In the launcher, it is a choice of game data:**

- First-time setup accepts each disc as a source, each in its own folder (`disc/`, `disc-ps2/`,
  `disc-gamecube/`).
- The release ships an executable for each data source (`action_standalone_ps2`, ...), and Play starts the one
  that matches.
- Manifests' `data` field hides the mods that don't fit: a texture pack made for the Xbox's textures, under the
  PS2's data.

**Explaining the differences, and steering towards the best assets.** A page in setup and in Settings compares
the sources the player has, in plain terms:

| | Xbox | PS2 | GameCube |
|---|---|---|---|
| Textures | DXT1/3/5 and 32-bit | 16 or 256 colours, no mip levels | (to be filled in from the spike) |
| Sound | Xbox ADPCM | SPU ADPCM, 22-48 kHz | of noticeably lower quality |
| Movies | WMV2 | MPEG-2 | (spike) |
| Status | the reference: verified against the original | plays; checked by the Xbox's recordings | a spike |

- **Steering:** with more than one source, the launcher recommends the Xbox's, and says why in one line. Setup
  with only a PS2 or GameCube disc mentions that an Xbox disc gives better textures or sound.
- **The table is kept with the code:** each data branch's doc says what its source lacks, and the launcher's
  wording is taken from there.

---------------------------------------------------------------------------------------------------------------

## 6. Multiplayer and saves

**Multiplayer.** Every player in a split-screen game shares one machine, and so one mod list. Two modules change
only multiplayer:

- **Lifting the branding limits** (who may play whom): replacements of the character-select and claim checks.
  `P_MPSETUP_Handler` is where the one-per-game characters are dropped today.
- **The bot count** is a constant, `NUM_BOTS = 6` (game/mp/MPLimits.h), which sizes arrays in the game's
  structures. It stays a build constant (decided), with the other limits that size structures.

If network play is ever added, every peer must run the same core and the same gameplay mods with the same
settings. The handshake compares a hash of that list. Cosmetic and observer mods may differ.

**Saves.** The core's save format never changes:

- A module never writes into a save anything the original could not have written. State the original has no
  place for (a module's options, the radial selector's layout) goes to `user/mods/.state/<id>/`.
- A save made with gameplay mods outside the Default profile gets a sidecar, `<save>.mods.json`, listing them.
  The core ignores it. The mod layer warns when such a save is loaded with a different list.
- A module that changes what may be saved checks that the original can load the result. Lifting the branding
  limits, for example, could store a character combination the original never allowed; that module validates
  on save, or keeps the choice in its own state.

---------------------------------------------------------------------------------------------------------------

## 7. Phased plan

| # | Phase | Contents | Size | Needs |
|---|---|---|---|---|
| 0 | Paperwork | core-deviations.md: add the `FPS` override and the texture replacement, move the keyboard to the mod table, move `Menu_UpdateWheel` in (5.2) | small | - |
| 1 | The mod list | `mods.ini`, profiles, `-nomods` / `-mods`, the list passed between engines, printed and in crash logs; the recordings' `MODS` section, exit code 5, event 0x80; version 2 recordings read as in 5.4 | small to medium | - |
| 2 | Dispatchers, action engine | the hook list; the generator for before, replace and after hooks on free functions; the object-code diff in CI; the 13 recordings on the shipped executable with mods off | medium | 1 |
| 3 | The fixes module | the action replacements, `tools/fixdiff.py`, the option removed (the driving engine's `Scheduler::Run` waits for 6) | small | 2 |
| 4 | PC controls, FOV, frame rate | keyboard, mouse look and steering out of `Player_ViewClamping` and psiInput; the FOV sites restored; `mainloop`'s override out; settings migrated once from settings.ini; the recordings rebased (5.4) | medium | 2 |
| 5 | Speedrun profile | demo recording in the shipped executable, the profile's lock and mark, the autosplit module (events to LiveSplit, the memory block) | small to medium | 1, 2 |
| 6 | The data overlay | `Xbox_ResolvePath`, the FS hooks, textures per mod, conflict reporting | medium | 1, 2 |
| 7 | Dispatchers, driving engine | member functions; `Scheduler::Run` into fixes | medium | 2 |
| 8 | The launcher's Mods page | profiles, the list, settings, the player's own data folders, safe mode; the data-source page (5.5) | medium | the launcher's steps 1-4 |
| 9 | Overlay and input capture | the present-time overlay pass in both backends, input consumption, the menu message seam as a module API | large | 2 |
| 10 | Branding limits | replacements in the multiplayer setup; save validation | small to medium | 2 |
| 11 | nightfire-port features | the radial selector (medium); ADS with scoped walking (large: `Player_Aiming`, `Player_Move`, `Player_WeaponRecoil`, `HUD_UpdateCrossHair`, the zoom); the modern menus (large: their src/ui is about 11,000 lines on their own renderer, and needs our overlay pass); the HD UI (medium: data plus overlay) | large | 9 |
| 12 | VR | stereo in the backends (two eyes from one frame's draws); head pose into the camera through a `Camera_SetToPlayer` before hook (looking around is `cosmetic`, aiming by head is `gameplay`); the HUD on a quad; OpenXR loaded at run time on Windows and Linux | large, research first | 9 |

Phases 0-5 deliver the main promise: the core is the original, today's departures are run-time switches, and
both profiles work.

---------------------------------------------------------------------------------------------------------------

## 8. Decisions

### Decided (25 October 2026)

1. **Profiles:** Default (PC controls and the bug fixes, other quality-of-life modules allowed) and Speedrun
   (the bug fixes, time-demo recording, an autosplit module, nothing else). Authentic is safe mode.
2. **One executable,** with configurable hooks and file replacements.
3. **First-party modules only:**
   - no external DLLs and no defensive sandboxing;
   - modules live in this repository, where anyone can read them and compose their own build;
   - external mods, perhaps scripted, are revisited only if outside authors want them.
4. **What moves and what stays:**
   - **Mods:** the non-authentic features: the keycode fix, keyboard and mouse, the field of view, the frame-rate
     override.
   - **Core:** crash fixes with no gameplay effect (`Menu_UpdateWheel`), and quality of life such as widescreen
     and render scale.
5. **PS2 and GameCube data** stay build options. The launcher offers them as a choice of game data, explains the
   differences, and recommends the best source the player has.
6. **Mods are first-party for now,** with no outside index. The design is restructured if outside authors start
   shipping mods.
7. **The recordings are kept alive for testing,** rebased onto no mods where the session survives.
8. **`NUM_BOTS` and similar limits** stay build-time.

### Open

1. **Adjustable keyboard and controller bindings for speedrunners.** Whether the Speedrun profile allows rebinding,
   and how: a bindings module that writes only the pad state (so a recording is unaffected), or none at all. The
   user will ask the speedrunners.
2. **Outside mods.** If outside authors start shipping, decide between a curated index (signed like the releases,
   hashes pinned by review) and installing from authors' GitHub releases, and whether scripting comes with it.
