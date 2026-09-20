# Running the D3D9 backend on macOS / Apple Silicon

How to set up an environment that runs the project's native Direct3D 9 backend at full
speed on Apple Silicon, and what currently blocks doing that from scratch.

Validated 2026-09-20 on macOS 26.6.2 (25G83), Apple M2 Max.

> **Read this first.** The working configuration requires **Wine 11.5**, which is no
> longer published anywhere. Every Wine release from 11.6 onward crashes a few seconds
> into gameplay. If you already have Wine 11.5 installed, *preserve it* — see
> "Preserve your Wine 11.5 build". If you do not, this setup cannot currently be
> reproduced from scratch, and the Wine regression in "Known instability" needs to be
> fixed or worked around first.

## Why this is needed

Wine's default Direct3D implementation (WineD3D) is far too slow for this game. The
first single-player mission issues roughly 1500–2000 draw calls per frame, and under
WineD3D each one costs 60–160 µs. Routing D3D9 through DXVK instead brings that to
~7 µs, and the game holds its 50 Hz cap with ~44% headroom.

| Configuration | µs / draw call | fps | working ms/frame |
|---|---|---|---|
| WineD3D → OpenGL (Wine's default) | 155.5 | 3.3 | 304.1 |
| WineD3D → Vulkan (`renderer=vulkan`) | 65.2 | 7.8 | 127.9 |
| **DXVK D3D9 (d9vk macOS fork)** | **7.4** | **50.0 (capped)** | **11.1** |

Measured in the first single-player mission. Native Windows D3D9 on the same content
is ~2.5 µs/draw, so DXVK on Metal lands within about 3× of native.

Representative output from the working configuration:

```
[perf] 50.0 fps (asked for 50), 11.1 ms working + 8.9 ms waiting per frame
[perf]   7.3 us per draw call
[perf]   constants: 168 uploads of 5006 registers per frame (29 per upload)
[perf]   draw mix: 1505 indexed, 1 direct, 6 immediate, 55 vertices each on average
[perf]   per frame: 1512 draws, 0 texture uploads, 1590 texture lookups
```

## The working configuration

| Component | Version | Notes |
|---|---|---|
| macOS | 26.6.2, Apple Silicon | needs Rosetta 2 |
| Wine | **11.5** (wine-devel, `org.winehq.wine-devel`) | **11.6+ crashes.** No longer downloadable |
| MoltenVK | 1.4.1 | 1.4.0 too old; 1.4.2 also works |
| DXVK | d9vk macOS fork 1.10.3-20250511, **32-bit** `d3d9.dll` | DXVK 3.1 does not work |
| Prefix | 64-bit (`#arch=win64`) | the 32-bit game runs via WoW64 |

## Setup

### 1. Rosetta 2

```sh
softwareupdate --install-rosetta --agree-to-license
```

### 2. Wine 11.5

The build used is `Wine Devel.app`, `org.winehq.wine-devel`, reporting `wine-11.5`.

It originally came from the Homebrew cask `wine@devel`, which sourced
`wine-devel-11.5-osx64.tar.xz` from <https://github.com/Gcenx/macOS_Wine_builds>.
**Neither source can supply it any more:**

- The Homebrew cask was **disabled on 2026-09-01** for failing the macOS Gatekeeper check.
- The Gcenx releases now only go back to **11.6_1**; the 11.5 tag and its asset return 404.
- Homebrew's Caskroom entry is only a symlink to `/Applications`, so no cached tarball exists.

If you have it installed, preserve it now (below). If you do not, see "Known
instability" — you will hit the crash on every obtainable release.

These builds are new-WoW64: `lib/wine/` contains `i386-windows`, `x86_64-unix` and
`x86_64-windows` but no `i386-unix`, so the 32-bit game runs as 32-bit PE code on
64-bit unix libraries. That is fine and needs no special handling.

### 3. MoltenVK 1.4.1

Wine bundles its own MoltenVK at
`Contents/Resources/wine/lib/libMoltenVK.dylib`. Wine 11.5's bundled copy is 1.4.1 and
works as shipped. If you need to replace it:

```sh
curl -LO https://github.com/KhronosGroup/MoltenVK/releases/download/v1.4.1/MoltenVK-macos.tar
tar xf MoltenVK-macos.tar MoltenVK/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib
cp MoltenVK/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib \
   "/Applications/Wine Devel.app/Contents/Resources/wine/lib/libMoltenVK.dylib"
```

That dylib is universal (x86_64 + arm64); Wine uses the x86_64 slice.

**Do not `brew install molten-vk`.** Homebrew's build is arm64-only, and an arm64
dylib cannot be loaded by the x86_64 Wine process running under Rosetta. It is not
redundant, it is unusable.

**`VK_ICD_FILENAMES` has no effect.** Wine `dlopen`s `libMoltenVK.dylib` directly from
`win32u.so`; there is no Vulkan loader in the path, so ICD JSON files are never read.
(The `CX_LIBVULKAN` override string present in the binary is also inert in this build —
verified by pointing it at a nonexistent path and watching MoltenVK load anyway.)

MoltenVK **1.4.0 is too old** and fails at startup with
`DxvkBuffer: Failed to create buffer`. Note that Wine 11.17 bundles 1.4.0.

### 4. DXVK — the d9vk macOS fork

Upstream DXVK cannot be used. Its D3D9 requires the Vulkan features `geometryShader`
and `shaderCullDistance`, neither of which Metal provides:

```
info:    geometryShader                         : 1
info:    shaderCullDistance                     : 1
[mvk-error] VK_ERROR_FEATURE_NOT_PRESENT: vkCreateDevice(): Requested physical device
           feature specified by the 5th flag in VkPhysicalDeviceFeatures is not available
err:   DxvkAdapter: Failed to create device
[d3d9] CreateDevice failed: 0x8876086a
```

The DXVK bundled inside CrossOver 26 fails identically. Use the macOS fork, which
patches that requirement out:

```sh
curl -LO https://github.com/Sikarugir-App/d9vk/releases/download/v1.10.3-20250511/d9vk-macOS-async-v1.10.3-20250511.tar.gz
tar xzf d9vk-macOS-async-v1.10.3-20250511.tar.gz
cp d9vk-macOS-async-v1.10.3-20250511/x32/d3d9.dll /path/to/game/
```

Copy the **32-bit** (`x32/`) DLL next to `action.exe`. The `x64/` build and the bundled
`dxvk.conf` are not used.

| file | size | sha256 |
|---|---|---|
| `d9vk-macOS-async-v1.10.3-20250511.tar.gz` | 2,607,867 | `13a088e96c90501705c26326044ccc57e2b03adda3ed0dd7061e89249d4c176e` |
| `x32/d3d9.dll` | 4,035,369 | `22511d1fbb15cdbc5365dfcb231f028af427533bfb6957505720dac0aca98e3e` |

### 5. Tell Wine to use it

```sh
wine reg add 'HKCU\Software\Wine\DllOverrides' /v d3d9 /t REG_SZ /d native /f
```

The per-launch equivalent is `WINEDLLOVERRIDES="d3d9=n"`. Setting it in the registry
means the game needs no special launch environment.

### 6. `settings.ini`

```ini
GraphicsBackend=d3d9
```

Add `PerfLog=on` for the `[perf]` diagnostics quoted here.

## Verify it actually worked

**Do not assume it worked because the settings were applied** — the failure modes are
silent. All four of these must appear on stdout:

```
info:  DXVK: v1.10.3-20250511-async (macOS)          <- DXVK is live
[mvk-info] MoltenVK version 1.4.1, ...               <- must not be 1.4.0
[d3d9] adapter: Apple M2 Max                         <- not "NVIDIA GeForce 6800"
[d3d9] device created on window ... (640x480 backbuffer, ...)
```

At the main menu the game should report **24 draws per frame**. In the first mission,
~1500–2000 draws at ~7 µs each.

Failure signatures:

| symptom | cause |
|---|---|
| no `info: DXVK:` line | WineD3D in use; the override did not take |
| `adapter: NVIDIA GeForce 6800` | WineD3D's spoofed card name on the OpenGL renderer |
| `err: DxvkAdapter: Failed to create device` | wrong DXVK (upstream or CrossOver's, not the d9vk fork) |
| `err: DxvkBuffer: Failed to create buffer` | MoltenVK too old (1.4.0) |
| 3 draws/frame and a black window | DXVK 3.1; not supported, see below |
| crash a few seconds into a mission | Wine 11.6+, see below |

## Known instability — Wine 11.6 and later

On every Wine release from 11.6 onward, the game starts, renders correctly and reaches
full speed, then crashes within a few seconds of in-mission play.

```
Unhandled exception: page fault on read access to 0x00002ec5 in wow64 32-bit code (0x7bf21135).
 EIP:7bf21135 ESP:00fd9b28 EBP:01b1fa48
Backtrace:
=>0 0x7bf21135 (0x01b1fa48)          <- not inside any loaded module
  1 0x7b32c168 in d3d9 (+0x1c168)
  2 0x7b335f07 in d3d9 (+0x25f07)
  3 0x7b676643 in actioninject (+0x16643)
...
0144:err:seh:NtRaiseException Exception frame is not in stack limits => unable to dispatch exception.
```

The faulting address sits in the gap between `kernel32` and `ntdll` — no loaded module
covers it — the disassembly there is garbage, and `ESP`/`EBP` are megabytes apart. The
exception frame is outside the thread's stack limits. This is stack corruption in
Wine's WoW64 layer, not a rendering bug.

### What was tested

| Wine | DXVK | MoltenVK | prefix | result |
|---|---|---|---|---|
| 11.5 | d9vk 1.10.3 | 1.4.1 bundled | existing | **good** |
| 11.5 | d9vk 1.10.3 | 1.4.1 official | **fresh** | **good** — three levels |
| 11.6_1 | d9vk 1.10.3 | 1.4.1 | fresh | crash |
| 11.7 | d9vk 1.10.3 | 1.4.1 | fresh | crash |
| 11.8 | d9vk 1.10.3 | 1.4.1 | fresh | crash |
| 11.10 | d9vk 1.10.3 | 1.4.1 | fresh | crash |
| 11.17 | d9vk 1.10.3 | 1.4.1 | fresh | crash |
| 11.17 | d9vk 1.10.3 | 1.4.2 | fresh | crash |
| 11.17 | d9vk 1.10.3 | 1.4.1 | fresh + `renderer=vulkan` | crash, different address |
| 11.17 | **DXVK 3.1 (metalsharp)** | 1.4.1 | fresh | black screen, then crash |

The correlation with the Wine version is perfect across two DXVK major versions, two
MoltenVK versions and two prefixes. **11.6_1 is the oldest release still published, and
it already crashes**, so the regression landed between 11.5 and 11.6.

### Secondary observation

One crash (Wine 11.6_1) additionally printed a DXVK assertion:

```
stl_vector.h:1263: std::vector<std::optional<_D3DLIGHT9>>::operator[](size_type):
Assertion '__n < this->size()' failed.
```

An out-of-range index into DXVK's D3D9 light array, on what looks like an empty vector.

**Checked, and it is not coming from this project.** The D3D9 backend never calls
`SetLight` or `LightEnable` at all - there is not one reference to either in the tree.
Lighting is done entirely through vertex shader constants (`Gfx_LightConstantBlock` in
`d3dSeam.cpp`, which feeds constants 0x69-0x71), because the game's own lighting is
programmable rather than fixed-function. DXVK's light vector is therefore untouched by
anything here, which makes that assertion a *symptom* of the corruption - a `std::vector`
reporting a nonsense size once memory is already damaged - rather than a cause. It
appearing in only one of the crashes fits that reading, and it strengthens rather than
weakens the conclusion that this is a Wine regression.

### Symbolising the backtrace

Wine prints raw module offsets, such as `actioninject (+0x16643)`. The build emits
`Release/actioninject.map` and `Release/action.map` alongside the binaries, which turn
those into function names - but mind the arithmetic, because getting it wrong silently
names the wrong function:

- Wine's `+0xNNNN` is an **RVA**, an offset from the module base.
- The map's first column is `section:offset_within_section`, and section 1 starts at
  RVA `0x1000`. So subtract `0x1000` from the RVA and find the nearest entry at or
  below the result.
- The map's third column is the same address again with the preferred load base
  (`0x10000000`) added, which is often the easier one to match against.

So `+0x16643` is section offset `0x15643`; the nearest `0001:000156xx` entry at or
below it is the function, and the last column names the object file it came from.

The map must come from **the same build** as the binary that crashed - offsets move
with every code change.

### Next steps for this regression

- Bisect Wine between the 11.5 and 11.6 tags and file at <https://bugs.winehq.org>.
  The backtraces in this repository's issue tracker are a starting point.
- `wine-staging` builds of 11.6+ were not tested and may behave differently.
- DXVK 3.1 is not a workaround: it renders a black window. `d3d9.deferSurfaceCreation`
  gets it past the initial stall (draws go from 3/frame to the expected 24/frame) but
  the window stays black, and it still crashes.

## Preserve your Wine 11.5 build

Because 11.5 cannot be downloaded any more, an existing install is the only source.
Archive it before Homebrew upgrades or removes it:

```sh
XZ_OPT='-T0 -6' tar cJf ~/wine-devel-11.5-osx64.tar.xz -C /Applications "Wine Devel.app"
brew pin wine@devel
```

(`XZ_OPT` just enables multithreaded compression; it takes a few minutes on an M2 Max
and produces roughly 370 MB from a 1.4 GB app.)

An archive made this way has been verified end to end: extracted to a scratch
directory, `wine --version` reports `wine-11.5`, the bundled `libMoltenVK.dylib` is
intact (1.4.1, x86_64), and the game launches from it with DXVK live and a device
created. Restoring is just:

```sh
tar xJf wine-devel-11.5-osx64.tar.xz -C /Applications
```

Keep the archive somewhere durable and consider attaching it to the project's releases,
since it is currently the only way to reproduce these results.

The copy made on the original test machine:

| file | size | sha256 |
|---|---|---|
| `wine-devel-11.5-osx64.tar.xz` | 367,800,452 | `43597c7fb81158e259bbd732616d57229818143346e9acb8bc636e2cdaa8a855` |

Note this is a repack of the installed `.app`, not the original Gcenx tarball, so its
checksum will not match anything upstream.

## Not required

- **`HKCU\Software\Wine\Direct3D` `renderer=vulkan`** — no effect once DXVK handles
  D3D9. WineD3D is still loaded at startup because Wine's builtin `d3dcompiler_47.dll`
  imports it, but it never creates a device (no `validate_state_table` or
  `wined3d_sampler_vk` activity). Harmless to set, and worth keeping as a fallback:
  without `d3d9.dll`, Vulkan-backed WineD3D is still ~2.4× faster than Wine's OpenGL
  default. It does **not** affect the crash.
- **CrossOver** — useful only for investigation. Its DXVK hits the geometry-shader
  wall, and its D3DMetal is x86_64-only, exporting only `d3d11`, `d3d12` and `dxgi`.
  There is no D3D9-to-Metal path in any shipping product.

## Traps

**A builtin-stamped DLL loads silently as the wrong thing.** CrossOver's DXVK DLLs are
stamped `Wine builtin DLL` at file offset `0x40`. Wine refuses such a file under
`d3d9=n`, and under `d3d9=n,b` it **silently loads Wine's own `d3d9.dll` instead** —
the game runs, WineD3D is in use, and nothing in the log says so. The `info: DXVK:`
banner is the only reliable check. The Sikarugir d9vk build is not stamped, so it loads
as native cleanly.

**`WINEDLLPATH` does not help** — it is not consulted for PE builtins in Wine 11.

## Side effects and benign log noise

`action.dxvk-cache` appears next to `action.exe` — DXVK's pipeline state cache. The
first mission run stutters while pipelines compile; later runs reuse it and start
clean. Keep it; do not delete it between benchmark runs.

The following are harmless and were verified not to correspond to any visible
rendering defect:

```
err:   validateGammaRamp: ramp inverted or flat
warn:  D3D9DeviceEx::SetRenderState: Unhandled render state 26     # D3DRS_DITHERENABLE
[mvk-error] VK_SUCCESS: Found attribute with size (8) larger than it's binding's stride (6).
            Changing descriptor format from MTLVertexFormatShort4 to MTLVertexFormatShort3.
[mvk-warn]  VK_ERROR_FEATURE_NOT_PRESENT: Metal does not support disabling primitive restart.
```

MoltenVK logs the third at error level with a `VK_SUCCESS:` prefix despite it succeeding.

## Note on the shader-constant optimisation

The "upload only changed shader constants" change measurably does its job here —
roughly 168 uploads for ~1500 draws, so about 89% of draws skip the upload — but it
changed per-draw cost by nothing under Wine: 62.6 µs/draw before, 65.2 µs/draw after,
both on WineD3D. It is not the limiter under DXVK either.

On macOS the cost was WineD3D's own per-draw path executing as Rosetta-translated x86.
A sampling profile of the running game put Metal, the AGX driver and MoltenVK together
at well under 1% of samples — essentially nothing was happening on the GPU.
