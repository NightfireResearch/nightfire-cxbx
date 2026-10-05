# The driving engine's collision system

The game's spatial questions against the loaded track: ground height and normal under a point, segment hits against
the world, OBB and cylinder intersections with placed objects and barriers, breakable windows, which collision
instances, objects and barriers are near a point, and per-object collision regions. Ported on 5 October 2026 by
four packages, then merged (`src/driving/world/`). Every address is Driving.xbe's.

**Status:** 196 functions ported (WCollisionMgr, WCollider, WCollisionInstance/Object, WGrid, WTree, WWorldMath,
WWorldPos, FasterSegmentIntersect, the physics Util_* helpers, OBB). Checked by four shadow tests against the
originals on the loaded track and by lockstep runs of missions 1-8, every dumped frame identical to the baseline.
The rest of `engine.world` - triggers, road network and navigation, targeting, sound zones, visibility curtains,
world rendering, WWorld itself - is still original.

## Layout

| File | What |
|---|---|
| `world/CollisionTypes.h` | the shared records: track data (StripVertex/StripTriangle, CollisionStrip, CollisionBarrier, windows, CollisionArticle), WCollisionInstance and WCollisionObject, WorldCollisionInfo, the instance/barrier/object lists, grid cells |
| `world/CollisionManager.h/.cpp` | WCollisionMgr (one instance, `fgCollisionMgr` at 0x00239a70): lists, CheckHitWorld, StepCheckHitWorld, CheckHitWindow, Init/Restart/Shutdown, the window and article maps |
| `world/CollisionQueries.h/.cpp` | WCollisionMgr's queries: ground collision and height, world normal, triangle-strip and instance face searches, barrier, cylinder and OBB intersection |
| `world/Collider.cpp`, `CollisionInstance.cpp` | per-object collision regions (WCollider), instance and object matrices and positions |
| `world/Grid.cpp`, `Tree.cpp` | the spatial grid (cells, dynamic elements that move each frame) and the scene tree |
| `world/WorldMath.cpp`, `WorldPos.cpp` | segment/plane/circle maths; WWorldPos, the closest face to a point |
| `engine/PhysicsUtil.cpp`, `engine/OBB.cpp` | the physics Util_* helpers (ray/sphere tests, matrices, random perturbation) and the oriented-box overlap tests |

## Tests

| Variable | When | What |
|---|---|---|
| `NIGHTFIRE_GEOMSHADOW` | injection time, and the first tick | the geometry maths bit for bit over 116,080 random and edge inputs; WWorldPos's face searches at 300 points around the car |
| `NIGHTFIRE_COLQUERYSHADOW` | first tick | every query at probes over the track and across its barriers, every instance's strips, random boxes, the containers |
| `NIGHTFIRE_COLLISTSHADOW` | first tick | the lists, CheckHitWorld/StepCheckHitWorld, CheckHitWindow (event creation recorded), the window map, SetCollisionArticle |
| `NIGHTFIRE_COLGRIDSHADOW` | first tick | grid searches, the scene tree, instance matrices, colliders, the dynamic grid |

Each runs the original and the port on identical inputs, restoring the state a query writes between the two (the
manager, instance query stamps, scratch areas, the grid's dynamic nodes).

## What the port taught

- **Return width.** CheckHitWorld and StepCheckHitWorld return a full-register 0 or 1, and their callers test all
  of EAX. Declared `bool`, the port set only AL: a miss could read as a hit, AI cars reacted to collisions that
  never happened (mission 1 drifted from tick 100) and the target picker read a null instance (mission 2 crashed).
  `tools/narrow_returns.py` now checks every port declared to return a byte type against its original callers.
- **Stack garbage.** The original leaves uninitialised stack words in a few records (WorldCollisionInfo +0x20..+0x3f
  from its constructor, a face's +0x0c word copied by FindClosestFace, matrix w words in Util_GenerateMatrix, pad
  bytes in grid records). Nothing found reads them; the ports write zeros there and the shadows mask them.
- **NaNs.** On inputs mixing infinities and NaNs, the x87 and SSE pick different NaNs; real collision data is
  finite, and the shadows count "both NaN, different payload" separately (as docs/driving/maths.md does).
- **Query stamps.** GetInstanceList marks every instance it visits with the manager's stamp and skips those that
  carry it, so a side-by-side test must give each side a fresh stamp, not just restore the manager.
