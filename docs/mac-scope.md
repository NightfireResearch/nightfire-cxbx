# Work for the Mac: deterministic maths and the host fixes on Apple Silicon

Branch `mac-scope`, made from dev on 25 October 2026. It is for the agent on the M2 Max (docs/driving/macos.md
sections 8-11 cover the setup and the agent's usual brief). It holds this page and two reference sets that let the
Mac check its results against the x86 hosts' without access to them.

## What changed on dev that needs the Mac

- **DetMath** (docs/deterministic-maths.md) is now on by default in every C build of both engines (CMake
  `NIGHTFIRE_DET_MATH`; the macos-arm64 preset is a C build). The game's transcendentals come from fdlibm's
  algorithms in plain C++, and give the same bits on every host as long as the compiler fuses no multiply-adds,
  takes no fast-math liberties and keeps default rounding with no flush-to-zero. It has been probed on x86 (MSVC
  32/64, gcc, clang) and on aarch64 only under emulation; **the M2 is its first real arm64 hardware.**
- **RSQRTSS** (src/common/math/Sse.h): C builds take `_mm_rsqrt_ss`'s estimate from a table of the Intel runner's
  results. That fixed an AMD/Intel divergence in the driving engine's sound. On the Mac, NIGHTFIRE_SSE is off
  already, so the table was in use before; check it still is.
- **Host fixes** outside the maths (docs/deterministic-maths.md, "Other host differences"):
  - directory searches sorted, a-z as A-Z, on every host (HostPosix.cpp);
  - the bots' weapon-changed message carries its sender through `PtrTo32`/`PtrFrom32`
    (NDrone2_Bot.cpp, PlayerWeapon.cpp). On the Mac the game window lies above 4 GB, so before this a bot reacted
    to its own message. **This was a real Mac bug, and the Mac is the only host that can show the fix working.**
  - movies in a time demo end on their audio's length even without a sound device;
  - float-to-int of infinity and NaN gives x86's result on every host (src/common/floatToInt.h).
- The time-demo and lockstep harnesses print the floating-point control state (FPCR on arm64) whenever it is not
  the expected one.

## The work

1. **Build** both engines with the `macos-arm64` preset (docs/driving/macos.md 6). Check that the configure shows
   `NIGHTFIRE_DET_MATH=ON` and that the toolchain still passes `-ffp-contract=off`.
2. **The maths probe on real arm64.** Build and run `tools/detmath/detprobe.cpp`, as its header says (the clang++
   line), then check every output against the x86-64 reference in `tools/detmath/reference_x86_64.sha256`:

       cd out_m2 && shasum -a 256 -c ../tools/detmath/reference_x86_64.sha256

   Every one of the 26 files should match. The reference was made from dev's DetMath at 9b48bfc0 by clang x86-64,
   and it equals MSVC x64's run. If a file differs, find the first differing result: the file holds the inputs'
   results in order, so a byte compare gives the input. Report the function and input, and whether the
   difference comes from the code or from the compiler (look for `fmadd` in the disassembly).
3. **No fused multiply-adds.** Run `tools/arm64_probe.sh`, or `objdump -d` the Apple clang objects of DetMath.cpp
   and the game's maths, and confirm there are no `fmadd`/`fmsub`/`fnmadd`/`fnmsub` where the source has a separate
   multiply and add.
4. **The 12 action recordings** (tests/demos/action) on `action_standalone_test`, as in docs/action-portable.md
   (macOS section). All 12 should match every frame, as they do on Windows 32/64 and Linux. There should be no
   "UNEXPECTED floating-point control" line in any log.
5. **The driving lockstep runs**, all 16 (8 missions, idle and accelerating), with `RUNNER_HOST=local
   tools/runner/round.sh` (docs/test-runner.md). Compare their sound traces with the runner's C baseline `lbase_c`,
   which this branch carries compressed in `tests/lockstep/lbase_c/`:

       for d in tests/lockstep/lbase_c/*/; do n=$(basename $d); mkdir -p build/baselines/$n; \
           cp $d/drive.log build/baselines/$n/; xz -dc $d/sndtrace.log.xz > build/baselines/$n/sndtrace.log; done
       python3 tools/runner/compare.py --sound build/baselines/lbase_c_m1_idle build/remote/<prefix>_m1_idle

   The frames will differ, because Metal renders them, but the sound traces should match exactly. Windows 32/64 and
   Linux x64 all match `lbase_c`. Use one `settings.ini` for these runs: the runner's has `Widescreen=0`. Copy
   Release/settings.ini's other keys as the Mac has them, but set `Widescreen=0` and the runner's resolution if
   the comparison shows a difference that starts in the settings.
6. **The bot fix in play.** Start a multiplayer match with bots, for example from the front end with three or more
   bots on a small map, and watch for bots switching weapons. Before the fix, a bot reacted to its own
   weapon-changed message (treated it as another player's). Describe what you see. A multiplayer recording made on
   the Mac, played twice, also shows the result is repeatable.
7. **Optional:** audit the remaining unsigned float-to-int conversions (`(unsigned)` of a negative float differs on
   arm64: x86 wraps, arm64 saturates to 0). Grep the casts in src/action and src/driving, and list any that can see
   a negative value, with a proposed fix through src/common/floatToInt.h. Don't change the x87 builds' code.

## Rules

- Commit on this branch, with messages in the repo's style ending `Co-Authored-By: Claude Opus 5.5
  <noreply@anthropic.com>`, and push the branch. Don't push to dev or main.
- If a fix is needed, make it so that the x87 and injected builds compile to the same code (they are the
  reference against the original), and say so. The Windows side will check by comparing objects.
- Record the results in docs/deterministic-maths.md, in a section for Apple Silicon: the probe, the recordings,
  the lockstep sound and the bots.
- `tests/lockstep/lbase_c/` is here for the Mac's comparison only. Leave it out of anything that is merged into
  dev, or delete it when done.

## Report

For each step: the result, and anything that differed, with the first differing value and its cause.
