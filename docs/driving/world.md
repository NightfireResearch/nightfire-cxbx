# The driving engine's world

The rest of `engine.world` above the collision system ([collision.md](collision.md)): the world object that opens,
resets and closes a track, the trigger volumes that fire mission events, the road network and the navigator that
AI traffic, pedestrians, helicopters and the spline camera drive along, target picking, sound groups and the
visibility curtains. Ported on 7 October 2026 by five packages, then merged (`src/driving/world/`). Every address
is Driving.xbe's.

**Status:** 144 functions ported (142 the coverage tool listed, plus two entry points Ghidra had not made
functions: WorldSceneObjectList::Deallocate 0x000d1550 and AWorldSound::Delete 0x000cca00). `engine.world` is 94%
ours by bytes; what is left is WRender (0x000c7330..0x000c8940), the world's drawing, with its unnamed helpers.
Checked by five shadow tests against the originals on the loaded track (missions 1 and 4) and by lockstep runs of
missions 1-8, every dumped frame identical to the baseline.

## Layout

| File | What |
|---|---|
| `world/World.h/.cpp` | WWorld (`fgWorld` at 0x0023f310): InitSingleton, SetTrackName, LoadTrackFile, Open, Reset, Close, the instance lookups; the `std::vector` of WorldSceneObject (0x4c bytes, our name) that Open fills and Reset uses to make each proc-anim scene object again |
| `world/VisCurtain.h/.cpp` | the active curtain list and IsVisibleAgainstCurtains (WRender calls them every frame) |
| `world/SoundMap.h/.cpp` | the two `std::map` head allocators the linker folded together for every map with 0x18-byte nodes and every URefCounter map (0x00094030, 0x00094070) |
| `world/Trigger.h/.cpp`, `CollisionInstance.h` | WTrigger (0x40 bytes; its layout is in CollisionInstance.h, where the collision system first needed it): FireEvents, TestDirection, UpdateRotPos |
| `world/TriggerManager.h/.cpp` | WTriggerManager (`fgTriggerManager` at 0x0023e260): Init, Restart, Update, the five CheckCollide and four Process overloads |
| `world/SimpleZone.h/.cpp` | WSimpleZone, the targeting system's zones |
| `world/RoadNetwork.h/.cpp` | the road data from the track's "RNgp" records - WRoadSegment ("rs", 0x64), WRoadNode ("rn", 0x20), WRoadIntersection ("ri", 0x30), WRoadJunction ("rj", 0x10), WRoad ("rr", 0x30); the names are ours - the class's statics (`fgRoadNetworkData`, 0x0023e030), and every WRoadNetwork query |
| `world/RoadNav.h/.cpp` | WRoadNav (0xc0 bytes): placing a navigator at a point or segment, stepping it along the network, lane changes, and the four ways to choose the next segment (random, by direction, by lane, along the sidewalk) |
| `world/Targeting.h/.cpp` | WTargetable and WTargetPicker: screen and off-screen positions, distances, selection, auto-drive targeting, the shared pointer-list erase |
| `world/SoundGroup.h/.cpp` | WSoundGroup, WSound / AWorldSound / ABaseSound as far as they are read, and WSoundMap (`std::map<int, WSound *>`) |
| `render/RPathHandle.hpp` | the path engine's handle, as the world and the trigger manager read it |

## Tests

| Variable | When | What |
|---|---|---|
| `NIGHTFIRE_WORLDSHADOW` | first tick | the curtains (random and degenerate, past the 63-curtain cap, visibility of spheres behind them), WSound::SetUnknown118, the map heads, the instance lookups for every instance, random sequences on the scene object vector |
| `NIGHTFIRE_TRIGGERSHADOW` | first tick | every CheckCollide and TestDirection over every real trigger and randomised copies, the four Process overloads on the live trigger array (given empty event lists, so a firing shows as the enabled flag cleared), FireEvents' event record, WSimpleZone |
| `NIGHTFIRE_ROADNETSHADOW` | first tick | every query over every segment and node (lane types, widths, weights, points on segments, lane indices at the lane table's boundaries), lines and probes, Init/Restart/Shutdown with the live tables set aside |
| `NIGHTFIRE_ROADNAVSHADOW` | first tick | navigators placed at up to 300 points in every mode and driven for up to 40 operations each, the navigator, its spline, the random generator and every segment stamp compared after each; `=2` names each case before it runs |
| `NIGHTFIRE_TARGETSHADOW` | first tick | screen-space maths against the live camera, the WTargetable constructors and queries, the picker on a copy of the live one, the sound map through 600 scripted steps |

Open, Reset, Close, the managers' Init/Restart/Update, event lists that run events, sound groups and the per-frame
targeting updates are covered by the lockstep runs.

## What the port taught

- **A test of the wrong pointer.** FireEvents tests the address of a trigger's event entries (the list plus 0x10),
  not the list: with no list it faults reading the count. The first port tested the list and skipped; it now does
  what the original does (no trigger in the game has no list).
- **Which register holds what.** In CalcNextSegmentSidewalk, after GetAttachedDirectionalSegment returns the road
  beyond an exit, every path that takes the exit reads that road's index and lanes from EAX. The port took the exit
  segment itself. Only mission 1's network showed it.
- **Shadows can drive the original where the game never goes.** Calling a CalcNextSegment* on its own, or setting
  a navigator's curve onto a random segment, leaves states from which the original IncNavPosition loops for ever.
  The road navigation shadow compares those calls but starts a fresh navigator after them.
- **First use allocates.** GetSegmentCurveStep constructs a static spline on its first call (an allocation and an
  atexit registration). A side-by-side test must make it before both runs, or the two allocations differ.

## Odd things in the original, kept

- Open compares the 'Audi' lookup with the end of the root group instead of the 'Map ' group's, so the check never
  fails; Reset looks up 'Wmap' and never uses it; Close leaves the scene objects, the sound group and the trigger
  manager global set.
- WTrigger::Size's only caller calls it on a CARP::Instance. Box triggers keep their depth as a float in the word
  other code reads as the packed size, never register a ray shell thinner than 0.01, and become cylinders in
  UpdateRotPos, which ignores its rotation.
- Restart builds a "Validating stream" line and never prints it.
- PathForwardRoadSegment's inner loop compares the outer index's neighbour; FindClosestSegmentInd returns 0, not -1,
  for an empty grid cell; several CalcNextSegment* paths use a null GetAttachedDirectionalSegment result unchecked.
- WRoadNetwork::Init fills 32 lane-weight row pointers of which 10 are used; GetLinePointIntersect normalises its
  direction twice.
