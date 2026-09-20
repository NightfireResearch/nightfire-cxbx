# Building the Windows binaries on macOS

The whole project cross-compiles from macOS: `action.exe`, `actioninject.dll`, `drivinginject.dll` and the
two CXBX launchers, all as 32-bit Windows PE files, with no Windows machine involved. This is what that
needs, what it is worth, and the handful of places where the result is not identical to an MSVC build.

Validated 2026-09-20 on macOS 26.6.2, Apple M2 Max, with mingw-w64 13.0.0 (GCC 15.2.0 sysroot) and Apple
clang 21. The cross build is exercised by CI on every push, so it either works or you hear about it.

## What you need

```sh
brew install mingw-w64 ninja
```

That is the whole list. Apple's own `clang` does the compiling, and it is already there with the command
line tools. Python is needed too, for `tools/preprocess.py`, and macOS has it as `python3`.

## Building

```sh
cmake --preset macos
cmake --build --preset macos
```

Everything lands in `build/macos/`. The presets are in `CMakePresets.json`; `cmake --preset windows` is the
same two commands for the native MSVC build, so neither platform is the awkward one any more.

To use a mingw-w64 that did not come from Homebrew, point the toolchain file at it:

```sh
cmake --preset macos -DNF_MINGW_ROOT=/path/to/sysroot
```

## clang, not mingw-w64's GCC

This matters more than it sounds, and it is why `cmake/mingw-w64-i686.cmake` - a sample GCC toolchain file
that had been sitting in the tree since 2023 - never went anywhere. It has been removed rather than left as
a trap, because no amount of fixing makes it build this.

The reimplemented functions use `__declspec(naked)` and MSVC's `__asm { }` Intel-syntax blocks at about a
dozen sites: the custom-calling-convention trampolines like `View_CaptureScene` and `Script_Run`, the
kernel stub entry in `src/loader/kernel.cpp`, the DirectSound one in `dsndSeam.cpp`. **GCC supports neither
on x86** - `naked` is not an x86 attribute there, and GCC has no MS-style asm blocks at all. There is no
flag that fixes this; the code would have to be rewritten in AT&T extended asm.

clang accepts both, with `-fms-extensions -fasm-blocks`. That is all it takes.

### Do not add `-fms-compatibility`

It looks like the natural companion to `-fms-extensions` and it is a trap. It defines `_MSC_VER`, which
makes mingw-w64's own headers take their MSVC branches - and they are written for GCC. Adding it took the
build from 77 failing translation units to 88, with 1169 `unknown type name 'va_list'` errors and the
vector intrinsics collapsing on top. It belongs on a real `clang-cl` + Windows SDK path and nowhere near
this one.

## What the source changes were

Almost everything that had to change was MSVC being permissive rather than anything genuinely
platform-specific, and most of it was worth fixing regardless:

- **`game.h` declared `asBytes` in two anonymous unions.** Only MSVC tolerates the repeated name. This one
  error cascaded into roughly seventy others, because every `static_assert` on `sizeof(sNightFireShared_tag)`
  then failed against a struct that had not formed.
- **`void main(...)`.** C++ requires `main` to return `int`; clang enforces it and no warning flag turns it
  off. The game's own `main` is now `Game_Main` and is injected by address with `FUNC_AT` rather than by the
  name `preprocess.py` would look up in the Ghidra export.
- **Five headers under `game/obj/` used `#include "../actionhelpers.h"` from two levels down.** MSVC searches
  the directory of every file in the include stack; everyone else searches only the including file's own.
- **`actionhelpers.h` included `game/obj/player.h`,** which is `Player.h`. Harmless on a case-insensitive
  filesystem, fatal anywhere else.
- **Two implicit function-pointer-to-`void*` conversions,** and one `NULL` that arrived only through MSVC's
  headers.

Worth saying plainly: **no struct layout differences turned up.** Once the union was fixed, every
`static_assert` on a memory-mapped game structure passed. mingw defaults to `-mms-bitfields`, so the
bitfield packing matches too.

## Where the build system had to learn a second toolchain

`CMakeLists.txt` now guards the MSVC-only link options (`/BASE`, `/FIXED`, `/DYNAMICBASE:NO`, `/SAFESEH:NO`,
`/MAP`) and gives GNU ld equivalents. `/SAFESEH:NO` has no counterpart and needs none - MinGW does not emit
the table of safe exception handlers that option exists to switch off.

Three other things are less obvious:

**The C runtime's startup objects have to move.** MinGW links `crt2.o` ahead of the target's own objects,
which puts `mainCRTStartup` at the front of `.text` - *inside* the range `Xbe_Map` copies the XBE over. The
loader would run, and then return into memory it had already overwritten. `-nostartfiles` drops them from
the front and CMake names them again after our objects, so `reserve.cpp`'s reservation array really is the
first thing in `.text`.

This is worth dwelling on because nothing about it fails at build time, and the three checks
`check_loader_image` used to make - base address, size of image, ASLR - all *passed* on the broken binary.
The check now also asserts that the entry point lies above the XBE's end, which is the part of "the array
comes first" that the PE header states outright. Verified by deliberately relinking without the fix.

**`#pragma comment(lib, ...)` does nothing outside MSVC.** GNU ld ignores it, so the CMake target names
`xinput9_1_0`, `user32` and XAudio2 itself. The pragmas are now `#ifdef _MSC_VER`, because clang emits the
directive into `.drectve` regardless and ld then warns that it cannot read it.

**`preprocess.py` ran at the wrong time.** It was a `PRE_BUILD` custom command, and CMake only honours
`PRE_BUILD` on the Visual Studio generators - everywhere else it is quietly downgraded to `PRE_LINK`, which
runs *after* the compilation that needed the generated tables. Under Ninja the build silently used whatever
the previous run left behind. It is a custom target with a dependency edge now, which is correct on every
generator, and it was a latent bug for anyone using Ninja on Windows too.

## What differs from an MSVC build

One thing, and it is small:

**No reverb.** The XAudio2 reverb APO needs `XAudio2CreateReverb` and the `XAUDIO2FX_REVERB_*` parameter
structures. mingw-w64's `xaudio2fx.h` is generated from a WIDL IDL that declares only `CreateAudioReverb`
and `CreateAudioVolumeMeter`, so none of them exist. Those structures are a published ABI and could be
written out by hand, but a field in the wrong place would hand the APO garbage rather than fail to build,
so the cross build says so and carries on - `3D voices will be dry`. Everything else in the audio seam is
unaffected.

There is also a mingw header defect worth knowing about: its `x3daudio.h` declares `X3DAudioInitialize` and
`X3DAudioCalculate` without `extern "C"`, so a C++ caller asks for mangled names the import library has
never heard of. `xaudio2Backend.cpp` wraps that one include; the Windows SDK's header does not need it, and
must not get it, because it pulls in others.

Binaries are also linked against a static libstdc++ and libgcc, so there is no `libstdc++-6.dll` to ship
beside them. Without that they fail to load with a bare `error 126`, which names nothing.

## Running what you built

Building and running are separate problems on macOS. See `docs/macOS-D3D9-setup.md` for the Wine and DXVK
setup, including the Wine 11.5 pin. A cross-built `action.exe` has been run that way: it maps the XBE,
claims `0x00010000..0x0030b660` as its own image, resolves all 96 kernel imports, loads the injected DLL,
brings up Direct3D 9 on the M2 Max and XAudio2, and reaches the game's own `Mem_Init`.
