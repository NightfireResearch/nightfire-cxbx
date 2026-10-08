# The driving engine's cameras

The cameras, on top of the world, physics and rendering already ported ([world.md](world.md),
[physics.md](physics.md)): the camera classes, the frame's rendering through a world camera, the player's camera
with all its modes, and the tuning file, splines and director that drive it. Ported on 7 October 2026 by five
packages, then merged (`src/driving/camera/`). Every address is Driving.xbe's.

**Status:** 226 functions ported, including three entry points Ghidra had not made functions
(RViewCamera's ApplyPerspectiveFunction 0x00096e20, the symbol callback NoSymbolCallback 0x0007ae90, and
BoostDiagonalRotation 0x000846d0, a fastcall helper). `engine.camera` is 99% ours by bytes: what the coverage tool
still counts live are two pieces Ghidra cut off the middle of TriggerAIPathAnimationCamera (0x000898ee) and
CameraLockOn (0x00081619), ported inside them, and an exception funclet. Checked by five shadow tests against the
originals on copies of the live camera (missions 1 and 4) and by lockstep runs of missions 1-8, every dumped frame
identical to the baseline.

## Layout

| File | What |
|---|---|
| `camera/Camera.h/.cpp` | the shared base classes: RCamera (0xc0: matrix, RHCS conversion, copy), RViewCamera (0x4c: extents, guard band, LOD, aspect, transform modes, ApplyPerspectiveFunction), RWorldCamera (0x130, from RCamera: eye, look-at and offset, the anchor); views of the AI spline path and 'Cams' animation records the cameras read |
| `camera/WorldCamera.h/.cpp` | RWorldCamera's methods (anchors and their queries, viewing transforms, zoom, animations, input), RRenderWorldCamera (the frame: culling through WRender's tree walks, the static world, cars and deferred objects, effects, tyre tracks, bullet streaks, headlights), RPlayerViewCamera |
| `camera/CameraIniLoader.h/.cpp` | the camera tuning file and its tables (`fgCameraTables`, `fgCameraModeIndices`, `fgCameraConstants`): mode, bumper, heli and heli-arm, spline, ellipse, fixed, dashboard and auto-drive-arm records named after the file's keys |
| `camera/CameraSpline.h/.cpp` | RCameraMath, RCameraSpline (the spline the road network and the navigator also use), FindPointInList, the std::list copy; CrtAcosTimes/CrtAsinTimes, adaptors over the C runtime's `_CIacos`/`_CIasin` at their addresses |
| `camera/DirectorQueue.h/.cpp` | RDirectorQueue and RDirectorQueueData: queued camera changes |
| `camera/PlayerCamState.h/.cpp` | RPlayerCamState: the player's camera input |
| `camera/PlayerCamera.h` | RPlayerCamera (0x370, from RWorldCamera) and the camera view table (`CameraViews`, 0x001ec488) |
| `camera/PlayerCameraA/B/C.cpp` | RPlayerCamera's methods by address: small methods, bumper, dashboard and animation cameras (A); pitch and yaw limits, zoom, mode changes, transitions, the spline, fixed and auto-drive cameras (B); collisions, shake, the heli, tumble and ellipse cameras, construction and restart (C) |

## Tests

| Variable | When | What |
|---|---|---|
| `NIGHTFIRE_VIEWCAMSHADOW` | first tick | every RCamera method, the view's extents, LOD, aspect and transform functions, every anchor query against every car, shell, missile, grenade and perturbed copies, the culling module, ConfigureView |
| `NIGHTFIRE_CAMDATASHADOW` | first tick | the camera maths and splines on random inputs, the tuning file loaded 24 times from the disc with every table compared, the director queue and RPlayerCamState with their callees recorded, the linker strays among them |
| `NIGHTFIRE_PLAYERCAMSHADOWA/B/C` | first tick | each part of RPlayerCamera on copies of the live camera with its state, queue and navigator, in every mode that uses it, with perturbed positions, flags, inputs and modes; `B=2` names each case before it runs |

Construction, restart, the Set*Camera functions, rendering and whole-frame behaviour are covered by the lockstep runs.

## What the port taught

The first shadow run found a real bug in four of the five packages, none of which mission 1's opening reaches (its
lockstep frames matched before the fixes):

- **NaN takes the original's other branch.** FloorToInt's `FCOMP 0; TEST AH,5; JP` sends an unordered value down
  the truncation path; the port's `value >= 0` sent it to the "minus one" path.
- **A stack slot that is part of a matrix.** In UpdateAIPathAnimationCam the path's position is written into row 3
  of the frame GetOrientMat filled, then multiplied by the spline's placement; the port kept it aside, untransformed.
- **A push still outstanding.** In UpdateAutoDriveCam a `mov ecx,[esp+0x1c]` runs with one push pending, so it loads
  `this`, not the arm: the original multiplies the camera's frame, the port multiplied the arm's bytes.
- **The wrong row.** The heli cameras' ground probe runs along the anchor's forward row (+0x20), not its up row.
- **Stack garbage, again.** Several cameras set a vector's w from a local the original never writes (cameraOffset.w,
  eye.w); the ports write zero and the shadows mask those words.

## Odd things in the original, kept

- SetOldCameraMode constructs a whole temporary RPlayerCamera on the stack, runs the target mode's update on it and
  destroys it.
- UpdateSplineCam eases toward an uninitialised stack vector on a one-lane road (the port uses zero).
- The 4:3 aspect constant is 0x3faaaaaa, one unit below the nearest float to 4/3.
- The heli "Cinematic" index is stored one past its heli record; 16 bytes are copied over its Heli_Sideways,
  Heli_Height, Heli_Distance and the word after.
- GetAnchorLinearVelocity uses the player car's AI spline path velocity for any rigid anchor when there is one;
  GetAnchorAcceleration calls GetRigidBody three times.
- SetAutoDriveRotation's 97-entry response table has no bounds check; the dashboard camera repeats a look-back test
  it has already returned from; LimitPitchYaw reuses a stale band middle when the bands end early.
