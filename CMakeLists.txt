cmake_minimum_required(VERSION 3.10)

project(nightfiRE LANGUAGES CXX)

# tools/preprocess.py runs as a pre-build step. "python" is not a command on every host - macOS has only
# "python3" - so ask CMake for an interpreter rather than assuming a name.
find_package(Python3 COMPONENTS Interpreter REQUIRED)

# Building for Windows from something that is not Windows. Everything guarded by this is a MinGW-ism that
# exists only to reproduce what MSVC does by default; see docs/macos-build.md for why each one is needed.
if(CMAKE_CROSSCOMPILING)
  message(STATUS "Cross-compiling for Windows with ${CMAKE_CXX_COMPILER_ID} (${CMAKE_CXX_COMPILER_TARGET})")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Added for developers using IDEs that get confused (e.g. Zed) without this hint
# file while using a linter (e.g. clangd).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Fixes for bugs in the original game (docs/original-bug-fixes.md lists each one). Off: every reimplemented function
# behaves exactly as the original, bugs included - the build to compare against the original code. Set before any
# target so both engines, their loaders and their tests all see the same value.
option(NIGHTFIRE_FIX_ORIGINAL_GAME_BUGS "Fix the original game's bugs listed in docs/original-bug-fixes.md" ON)
if(NIGHTFIRE_FIX_ORIGINAL_GAME_BUGS)
  add_compile_definitions(FIX_ORIGINAL_GAME_BUGS=1)
else()
  add_compile_definitions(FIX_ORIGINAL_GAME_BUGS=0)
endif()

# A native Linux build (the linux-i386 preset, cmake/toolchain-linux-i386.cmake) builds the standalone driving
# engine alone: the loaders and injection DLLs map an XBE into a Windows process and have no Linux form. See
# docs/driving/portable.md.
if(WIN32)
# On MinGW, "lib" will get prepended to libraries which will break out ability
# to inject our DLL.
set(CMAKE_SHARED_LIBRARY_PREFIX "")
set(CMAKE_STATIC_LIBRARY_PREFIX "")
set(CMAKE_IMPORT_LIBRARY_PREFIX "")
endif()

# MinGW links its C++ and GCC support libraries dynamically by default, which would make every binary here
# depend on libstdc++-6.dll and libgcc_s_*.dll sitting beside it. MSVC builds carry no such companions, and
# a DLL that cannot find them fails to load with a bare "error 126", so link them in instead.
if(MINGW)
  # -pthread rather than naming the library: statically linked libstdc++ and libgcc both call into
  # winpthreads, and the driver appends them after everything CMake writes, so a -lpthread placed among
  # the target's own libraries would come too early to resolve either. -pthread lets the driver put it
  # where it belongs. -static is what makes that the archive: without it the linker takes
  # libwinpthread.dll.a first and the binary needs libwinpthread-1.dll at load time. The system and DirectX
  # libraries are unaffected, since mingw ships them only as import archives.
  add_link_options(-static -static-libgcc -static-libstdc++ -pthread)
endif()

# No fused multiply-adds, as in MSVC's code and the original's. Clang fuses a * b + c within an expression by default
# (GCC across expressions): the x86 builds target no FMA, so there it only reorders instructions, but arm64 has it
# (docs/driving/macos.md 4). The game's `#pragma fp_contract(off)` lines are MSVC's spelling of the same.
if(NOT MSVC)
  add_compile_options(-ffp-contract=off)
endif()


# Statically link UCRT, improves ability to run on XP when compiled with v141_xp
#set(CMAKE_CXX_FLAGS_RELEASE "/MT")
#set(CMAKE_CXX_FLAGS_DEBUG "/MTd")

# The Xbox / PS2 implementations were of another era, one where memory safety was apparently a foreign concept to game developers
# We'll use the original unsafe versions, and tell the compiler to stop nagging us, because YOLO
add_definitions(-D_CRT_SECURE_NO_WARNINGS)

if(WIN32)   # the action engine and both loaders
add_library(actioninject SHARED
  src/dllmain.cpp
  src/inject_action.cpp
  src/inject.h
  src/common/xbeOriginal.cpp
  src/common/xbeRelocate.cpp
  src/action/devtools/MenuProbe.cpp
  src/action/devtools/LoadScreenShadow.cpp
  src/action/devtools/FileDump.cpp
  src/action/devtools/SecretsShadow.cpp
  src/action/devtools/UnlocksShadow.cpp
  src/action/devtools/MPMenusShadow.cpp
  src/action/devtools/UpgradeShadow.cpp
  src/action/devtools/MemShadow.cpp
  src/action/devtools/ScoreShadow.cpp
  src/action/devtools/SaveShadow.cpp
  src/action/devtools/CodenameShadow.cpp
  src/action/devtools/DroneTablesCheck.cpp
  src/action/devtools/DroneShadow.cpp
  src/action/devtools/DroneAIShadow.cpp
  src/action/devtools/AIHintsShadow.cpp
  src/action/devtools/NavShadow.cpp
  src/action/devtools/WeaponTableShadow.cpp
  src/action/devtools/MatrixShadow.cpp
  src/action/devtools/ScriptShadow.cpp
  src/action/devtools/RandomShadow.cpp
  src/action/devtools/ShadowHarness.cpp
  src/action/devtools/TriggerShadow.cpp
  src/action/devtools/DoorShadow.cpp
  src/action/devtools/BootShadow.cpp
  src/action/devtools/InflateShadow.cpp
  src/action/devtools/SecurityShadow.cpp
  src/action/devtools/PlrStatShadow.cpp
  src/action/devtools/DroneStateShadow.cpp
  src/action/devtools/DroneCivilianShadow.cpp
  src/action/devtools/DroneDamageShadow.cpp
  src/action/devtools/DroneWeaponShadow.cpp
  src/action/devtools/DroneVisionShadow.cpp
  src/action/devtools/DroneAlertShadow.cpp
  src/action/devtools/DroneOpponentShadow.cpp
  src/action/devtools/DroneMoveShadow.cpp
  src/action/devtools/DroneCollisionShadow.cpp
  src/action/devtools/DroneRouteShadow.cpp
  src/action/devtools/DroneModeShadow.cpp
  src/action/devtools/DroneInitShadow.cpp
  src/action/devtools/DroneAnimShadow.cpp
  src/action/devtools/DroneCoreShadow.cpp
  src/action/devtools/DroneCombatShadow.cpp
  src/action/devtools/DroneCoverShadow.cpp
  src/action/devtools/DroneReactShadow.cpp
  src/action/devtools/DroneRoleShadow.cpp
  src/action/devtools/PlayerShadow.cpp
  src/action/devtools/PlayerWeaponShadow.cpp
  src/action/devtools/AnimPortShadow.cpp
  src/action/devtools/CollidePortShadow.cpp
  src/action/devtools/EffectPortShadow.cpp
  src/action/devtools/ViewMathShadow.cpp
  src/action/devtools/ObjectPortShadow.cpp
  src/action/devtools/EnginePortShadow.cpp
  src/action/devtools/HudPortShadow.cpp
  src/action/devtools/SoundPortShadow.cpp
  src/action/devtools/MenuPortShadow.cpp
  src/action/devtools/MenuControlShadow.cpp
  src/action/devtools/MenuHandlerShadow.cpp
  src/action/devtools/CameraShadow.cpp
  src/action/devtools/MathExactShadow.cpp
  src/action/devtools/BotCoreShadow.cpp
  src/action/devtools/MPFlagShadow.cpp
  src/action/devtools/MPGoldenEyeShadow.cpp
  src/action/devtools/DroneMPShadow.cpp
  src/action/devtools/MPMatchShadow.cpp
  src/action/devtools/BotStateShadow.cpp
  src/action/devtools/MPObjectsShadow.cpp
  src/action/devtools/MPObjectivesShadow.cpp
  src/action/devtools/Teleport.cpp
  src/action/devtools/Demo.cpp
  src/action/engine/LaunchOptions.cpp
  src/action/game.cpp
  src/action/main.cpp
  src/common/launchInfo.cpp
  src/action/gfx/Font.cpp
  src/action/gfx/Sprite.cpp
  src/action/gfx/LowLevel.cpp
  src/action/engine/AINetwork.cpp
  src/action/engine/AIPath.cpp
  src/action/engine/Anim.cpp
  src/action/engine/Camera.cpp
  src/action/engine/Collide.cpp
  src/action/engine/EDL.cpp
  src/action/engine/FS.cpp
  src/action/engine/Inflate.cpp
  src/action/engine/Loader.cpp
  src/action/engine/parsemap.cpp
  src/action/engine/psiFile.cpp
  src/action/engine/psiGraphics.cpp
  src/action/engine/psiDraw.cpp
  src/action/engine/mouseLook.cpp
  src/action/engine/mouseSteer.cpp
  src/action/engine/viewFov.cpp
  src/action/engine/psiInput.cpp
  src/action/engine/psiLight.cpp
  src/action/engine/psiSprite.cpp
  src/action/engine/psiSave.cpp
  src/action/engine/LoadSave.cpp
  src/action/engine/Script.cpp
  src/action/engine/Text.cpp
  src/action/engine/Vision.cpp
  src/action/engine/Woman.cpp
  src/action/engine/XboxSettings.cpp
  src/common/xboxPath.cpp
  src/action/engine/XboxFile.cpp
  src/action/engine/XboxError.cpp
  src/action/engine/XboxSystem.cpp
  src/action/engine/Fmv.cpp
  src/common/fmv/Ffmpeg.cpp
  src/common/fmv/FmvAudio.cpp
  src/action/engine/XboxStartup.cpp
  src/action/engine/Direct3D/d3dhelpers.cpp
  src/action/engine/Direct3D/d3dSeam.cpp
  src/action/engine/Direct3D/xboxMatrix.cpp
  src/action/engine/Direct3D/backendHost.cpp
  src/common/gfx/d3d9Backend.cpp
  src/common/gfx/nv2aPixelShader.cpp
  src/common/gfx/nv2aVertexShader.cpp
  src/common/gfx/shaderBuilder.cpp
  src/common/gfx/shaderSpirv.cpp
  src/common/gfx/devtools/shaderCheck.cpp
  src/common/gfx/devtools/shaderHlsl.cpp
  src/common/gfx/fxaa.cpp
  src/common/gfx/textureReplace.cpp
  src/action/game/Autoaim.cpp
  src/action/game/weapon_stats.cpp
  src/action/math/math.cpp
  src/action/math/X87Ops.cpp
  src/action/input.cpp
  src/action/memory.cpp
  src/action/util/bin.cpp
  src/action/util/crc.cpp
  src/action/util/hashtable.cpp
  src/action/util/DList.cpp
  src/action/util/LList.cpp
  src/action/util/Random.cpp
  src/action/util/CirBuf.cpp
  src/action/util/Sort.cpp
  src/action/util/Stack.cpp
  src/action/ui/HUD.cpp
  src/action/ui/TextBox.cpp
  src/action/ui/ui.cpp
  src/action/ui/ui_dossier.cpp
  src/action/ui/ui_mp.cpp
  src/action/ui/ui_credits.cpp
  src/action/ui/ui_frontend.cpp
  src/action/ui/ui_tweaks.cpp
  src/action/ui/ui_options.cpp
  src/action/ui/ui_pause.cpp
  src/action/ui/ui_secrets.cpp
  src/action/ui/ui_nightfire.cpp
  src/action/ui/ui_codenames.cpp
  src/action/ui/ui_cheats.cpp
  src/action/ui/ui_keypad.cpp
  src/action/ui/MenuUnlocks.cpp
  src/action/ui/ui_score.cpp
  src/action/ui/Menu.cpp
  src/action/ui/MenuScript.cpp
  src/action/ui/LinkedList.cpp
  src/action/ui/Label.cpp
  src/action/ui/Button.cpp
  src/action/ui/Checkbox.cpp
  src/action/ui/Scroll.cpp
  src/action/ui/List.cpp
  src/action/ui/Combo.cpp
  src/action/ui/Spin.cpp
  src/action/ui/Radio.cpp
  src/action/ui/Memo.cpp
  src/action/ui/Page.cpp
  src/action/ui/Window.cpp
  src/action/ui/Process.cpp
  src/action/ui/Manager.cpp
  src/action/ui/MenuManager.cpp
  src/action/autofunc.cpp
  src/action/game/Spline.cpp
  src/action/game/Upgrade.cpp
  src/action/game/view.cpp
  src/action/game/drone/BOT.cpp
  src/action/game/drone/BOTSTATE.cpp
  src/action/game/drone/Behaviour.cpp
  src/action/game/drone/Drone.cpp
  src/action/game/drone/DroneAnim.cpp
  src/action/game/drone/DroneSM.cpp
  src/action/game/drone/NDrone2.cpp
  src/action/game/drone/NDrone2_Death.cpp
  src/action/game/drone/NDrone2_Hostage.cpp
  src/action/game/drone/NDrone2_Talk.cpp
  src/action/game/drone/NDrone2_Nav.cpp
  src/action/game/drone/NDrone2_States.cpp
  src/action/game/drone/NDrone2_Civilian.cpp
  src/action/game/drone/NDrone2_Damage.cpp
  src/action/game/drone/DroneWeapon.cpp
  src/action/game/drone/DroneVision.cpp
  src/action/game/drone/DroneVision_Alerts.cpp
  src/action/game/drone/NDrone2_Opponent.cpp
  src/action/game/drone/DroneMove.cpp
  src/action/game/drone/DroneCollision.cpp
  src/action/game/drone/DroneTracking.cpp
  src/action/game/drone/NDrone2_Route.cpp
  src/action/game/drone/DroneModes.cpp
  src/action/game/drone/DroneInit.cpp
  src/action/game/drone/NDrone2_Combat.cpp
  src/action/game/drone/NDrone2_Cover.cpp
  src/action/game/drone/NDrone2_Alert.cpp
  src/action/game/drone/NDrone2_Special.cpp
  src/action/game/drone/NDrone2_Reactions.cpp
  src/action/game/drone/NDrone2_Roles.cpp
  src/action/game/obj/DroneSpawner.cpp
  src/action/game/mp/multiplayer.cpp
  src/action/game/mp/multiplayer_goldeneye.cpp
  src/action/game/mp/multiplayer_modes.cpp
  src/action/game/mp/multiplayer_flag.cpp
  src/action/game/mp/multiplayer_objects.cpp
  src/action/game/obj/Break.cpp
  src/action/game/obj/Door.cpp
  src/action/game/obj/Drone_AIHints.cpp
  src/action/game/obj/DynamicObject.cpp
  src/action/game/obj/FuseBox.cpp
  src/action/game/obj/Hint.cpp
  src/action/game/obj/Hurt.cpp
  src/action/game/obj/build.cpp
  src/action/game/obj/bullet.cpp
  src/action/game/obj/car.cpp
  src/action/game/obj/control.cpp
  src/action/game/obj/Copter.cpp
  src/action/game/obj/Gas.cpp
  src/action/game/obj/Destroy.cpp
  src/action/game/obj/Effect.cpp
  src/action/game/obj/Env.cpp
  src/action/game/obj/Explode.cpp
  src/action/game/obj/Swoosh.cpp
  src/action/game/obj/Grapple.cpp
  src/action/game/obj/GT.cpp
  src/action/game/obj/GunImp.cpp
  src/action/game/obj/Ladder.cpp
  src/action/game/obj/Light.cpp
  src/action/game/obj/LeafGen.cpp
  src/action/game/obj/Lock.cpp
  src/action/game/obj/OneSided.cpp
  src/action/game/obj/Pickup.cpp
  src/action/game/obj/RainBox.cpp
  src/action/game/obj/Ripples.cpp
  src/action/game/obj/BodyGlow.cpp
  src/action/game/obj/Creature.cpp
  src/action/game/obj/Sub.cpp
  src/action/game/obj/MiniSub.cpp
  src/action/game/obj/Emitter.cpp
  src/action/game/obj/Flicker.cpp
  src/action/game/obj/Mine.cpp
  src/action/game/obj/Shooter.cpp
  src/action/game/obj/Apocalypse.cpp
  src/action/game/obj/Cloud.cpp
  src/action/game/obj/Corona.cpp
  src/action/game/obj/Lightning.cpp
  src/action/game/obj/PCQWorm.cpp
  src/action/game/obj/Player.cpp
  src/action/game/obj/PlayerTraverse.cpp
  src/action/game/obj/PlayerWeapon.cpp
  src/action/game/obj/Wire.cpp
  src/action/game/obj/CreepWall.cpp
  src/action/game/obj/ThirdCam.cpp
  src/action/game/obj/ThirdIcon.cpp
  src/action/game/obj/CamSubject.cpp
  src/action/game/obj/RigidBody.cpp
  src/action/game/obj/Rotor.cpp
  src/action/game/obj/ScriptPlayer.cpp
  src/action/game/obj/Searchlight.cpp
  src/action/game/obj/Sensor.cpp
  src/action/game/obj/SS.cpp
  src/action/game/obj/SpaceLaser.cpp
  src/action/game/obj/SpaceMissile.cpp
  src/action/game/obj/Switch.cpp
  src/action/game/obj/Trigger.cpp
  src/action/game/obj/Monitor.cpp
  src/action/game/sp/Locks.cpp
  src/action/game/sp/Mission.cpp
  src/action/game/sp/PlayerStats.cpp
  src/action/game/sp/SSys.cpp
  src/action/game/sp/SwitchChannels.cpp
  src/action/sound/dsndSeam.cpp
  src/action/sound/psiSound.cpp
  src/common/sound/xadpcm.cpp
  src/action/sound/xaudio2Backend.cpp
  src/action/sound/music.cpp
  src/action/sound/SFX.cpp
  src/action/sound/Sound.cpp
)

# The seams' native backends (common/gfx/d3d9Backend.cpp, sound/xaudio2Backend.cpp)
#
# xinput9_1_0 and user32 come from #pragma comment(lib) in the sources, which only MSVC's linker acts on -
# GNU ld ignores it silently - so they are named here as well. That is a harmless duplicate under MSVC.
# The XAudio2 import library is "xaudio2" in the Windows SDK; mingw-w64 ships per-version libraries and
# has no such alias, so the 2.9 one (Windows 10's, which is what the backend targets) is named instead.
if(MINGW)
  set(XAUDIO2_LIB xaudio2_9)
else()
  set(XAUDIO2_LIB xaudio2)
endif()
target_link_libraries(actioninject PRIVATE
  d3d9 d3dcompiler winmm ${XAUDIO2_LIB} xinput9_1_0 user32)


# The loader: maps the XBE itself and runs it in-process. This is "action.exe", the way to run the action
# engine. Linked with /FIXED and no ASLR because the XBE is full of absolute addresses and has to land at its
# own base; see src/loader/loadermain.cpp.
add_executable(action
  # reserve.cpp must stay first: its array has to be the first thing in .text so that the XBE, copied over
  # the start of the image, lands on the array rather than on the loader's own code. Xbe_Map checks this.
  src/loader/reserve.cpp
  src/loader/loadermain.cpp
  src/loader/xbe.cpp
  src/loader/kernel.cpp
  src/loader/file.cpp
  src/common/xboxPath.cpp
  # The same icon the other executables carry. A .rc contributes nothing to .text, so it does not disturb
  # the ordering the reservation array depends on.
  src/res/res.rc
)
# /BASE plus /FIXED is how the loader gets the XBE's address range at all: the kernel maps the executable at
# its preferred base before anything else exists in the process, which is the only way to be there first.
# VirtualAlloc cannot do it, even into a suspended process - see src/loader/reserve.cpp.
# /SAFESEH:NO because the mapped XBE ends up inside this image's address range, so every SEH handler the
# game registers looks to Windows like one of this executable's - and would be rejected for not being in its
# table of safe handlers.
# /NXCOMPAT:NO because an Xbox title's memory is all executable and this one means it: EAGL compiles its
# render methods into allocated memory and calls them, which under DEP faults the moment a model draws
# ("tried to access <heap address>, protection PAGE_READWRITE"). Marking the image as not DEP-aware turns
# DEP off for the process, which is the console's own model; the loader's memory shims hand out executable
# pages to match (src/loader/kernel.cpp).
#
# A function because both loaders - this one and "driving" below - are linked exactly the same way.
function(nightfire_loader_link target)
  if(MSVC)
    target_link_options(${target} PRIVATE
      /BASE:0x10000 /FIXED /DYNAMICBASE:NO /SAFESEH:NO /NXCOMPAT:NO /INCREMENTAL:NO)
  else()
    # The same things GNU ld's way. --disable-reloc-section is /FIXED: with no relocation directory the
    # image cannot be moved. There is no equivalent of /SAFESEH:NO and none is needed, because MinGW does
    # not emit the table of safe handlers that option exists to switch off. /INCREMENTAL has no counterpart
    # either.
    #
    # And no /NXCOMPAT:NO. This build runs under Wine, which answers an image that is not DEP-aware by making
    # every page in the process executable - Wine's and DXVK's own included - and under Rosetta a write to an
    # executable page costs a fault and a flush of translated code. That took the action engine from 50 fps to
    # 11 and the driving engine to under one. The loader's memory shims (src/loader/kernel.cpp) already hand
    # the game executable pages, which is all EAGL needs; the process-wide switch only ever added the cost.
    #
    # --stack sets MSVC's default. GNU ld's is 2 MB, and every thread in the process - the game's, D3D's, the
    # driver's, XAudio2's - reserves its stack at the size in the executable's header, so under the default
    # they took twice the address space. Most of them land below 0x10000000, which is where the game's
    # contiguous memory has to be (see MmAllocateContiguousMemoryEx in src/loader/kernel.cpp), and with 2 MB
    # stacks it ran out: the action engine crashed at its first movie.
    target_link_options(${target} PRIVATE
      -Wl,--image-base,0x10000 -Wl,--disable-dynamicbase -Wl,--disable-reloc-section
      -Wl,--stack,0x100000)
  endif()

  # MinGW links its C runtime's startup objects ahead of the target's own, which would put mainCRTStartup
  # inside the range the XBE is copied over - the loader would run, then return into memory it had already
  # overwritten. -nostartfiles drops them from the front and they are named again below, after our objects,
  # so that reserve.cpp's array really is the first thing in .text. tools/check_loader_image.py enforces the
  # result, because nothing about this fails at build time.
  if(MINGW)
    target_link_options(${target} PRIVATE -nostartfiles)
    # The compiler knows where its startup objects are; asking beats hardcoding a sysroot layout. It needs
    # the same --sysroot the compile flags carry, or it answers with the bare filename it was given.
    separate_arguments(nf_cxx_flags NATIVE_COMMAND "${CMAKE_CXX_FLAGS}")
    foreach(crt crt2.o crtbegin.o crtend.o)
      execute_process(
        COMMAND ${CMAKE_CXX_COMPILER} --target=${CMAKE_CXX_COMPILER_TARGET} ${nf_cxx_flags}
                -print-file-name=${crt}
        OUTPUT_VARIABLE crt_path OUTPUT_STRIP_TRAILING_WHITESPACE)
      if(NOT EXISTS "${crt_path}")
        message(FATAL_ERROR "could not locate the MinGW startup object ${crt}: got '${crt_path}'")
      endif()
      target_link_libraries(${target} PRIVATE "${crt_path}")
    endforeach()
  endif()

  # winmm for the millisecond clock behind KeTickCount (src/loader/kernel.cpp).
  target_link_libraries(${target} PRIVATE winmm)
endfunction()

nightfire_loader_link(action)

# Symbol maps for both binaries that appear in crash backtraces.
#
# When a fault escapes the loader's own handler - which happens if the corruption is bad enough that the
# exception cannot be dispatched at all - the host prints raw module offsets instead, as Wine did with
# "actioninject (+0x16643)". Without a map that offset names nothing. The maps are written beside the
# binaries, cost nothing at runtime and are not shipped in the executable, so there is no reason not to.
if(MSVC)
  target_link_options(action PRIVATE /MAP)
  target_link_options(actioninject PRIVATE /MAP)
else()
  target_link_options(action PRIVATE -Wl,-Map,action.map)
  target_link_options(actioninject PRIVATE -Wl,-Map,actioninject.map)
endif()

# The same loader, pointed at Driving.xbe and drivinginject.dll - step 1 of docs/driving-engine-plan.md
# section 7. Nothing in the loader was ever specific to the action engine: both XBEs have base 0x10000, the
# reservation array spans the larger of the two, and the kernel imports are the same shape. Only the two
# default file names differ, and IS_DRIVING is what picks them; both are still overridable on the command
# line. This is the "driving.exe" the plan reserved the name for.
add_executable(driving
  # reserve.cpp first, for the same reason as the action loader above.
  src/loader/reserve.cpp
  src/loader/loadermain.cpp
  src/loader/xbe.cpp
  src/loader/kernel.cpp
  src/loader/file.cpp
  src/common/xboxPath.cpp
  src/res/res.rc
)
target_compile_definitions(driving PRIVATE IS_DRIVING)
nightfire_loader_link(driving)
if(MSVC)
  target_link_options(driving PRIVATE /MAP)
else()
  target_link_options(driving PRIVATE -Wl,-Map,driving.map)
endif()
endif()   # WIN32: the action engine and both loaders

# The driving engine's graphics backend behind the D3D8 seam's D3D9_* entry points (src/common/gfx/d3d9Backend.h):
# d3d9, Direct3D 9 (Windows' own, or dxvk-native's on Linux), or sdlgpu, SDL3's GPU API (its Vulkan driver on Windows
# and Linux; Metal on macOS) - src/common/gfx/sdlGpuBackend.cpp, docs/driving/macos.md packages G1 and G2. sdlgpu
# needs NIGHTFIRE_SDL. The action engine stays on Direct3D 9.
set(NIGHTFIRE_GFX d3d9 CACHE STRING "Driving engine graphics backend: d3d9 or sdlgpu")
set_property(CACHE NIGHTFIRE_GFX PROPERTY STRINGS d3d9 sdlgpu)
if(NIGHTFIRE_GFX STREQUAL "sdlgpu")
  set(nf_gfx_backend src/common/gfx/sdlGpuBackend.cpp src/common/gfx/fixedFunction.cpp src/common/gfx/shaderMsl.cpp)
  set(nf_gfx_devtools)
  set(nf_gfx_host_sources ${nf_gfx_backend})
elseif(NIGHTFIRE_GFX STREQUAL "d3d9")
  set(nf_gfx_backend src/common/gfx/d3d9Backend.cpp)
  set(nf_gfx_devtools src/common/gfx/devtools/shaderCheck.cpp src/common/gfx/devtools/shaderHlsl.cpp
                      src/common/gfx/fxaa.cpp src/common/gfx/textureReplace.cpp)
  set(nf_gfx_host_sources src/common/gfx/d3d9Backend.cpp src/common/gfx/fxaa.cpp src/common/gfx/textureReplace.cpp)
else()
  message(FATAL_ERROR "NIGHTFIRE_GFX is d3d9 or sdlgpu, not ${NIGHTFIRE_GFX}")
endif()
if(NIGHTFIRE_GFX STREQUAL "sdlgpu")
  add_compile_definitions(NIGHTFIRE_GFX_SDLGPU)   # the driving targets, all made below
  # SDL's Metal driver takes MSL, not SPIR-V: SPIRV-Cross translates each module (src/common/gfx/shaderMsl.cpp). It is
  # built on every host, so that the Vulkan builds compile and can exercise it too: a release tag, static, its MSL
  # backend (and the GLSL one that derives from) only, with C++ exceptions for its errors.
  include(FetchContent)
  foreach(part CLI ENABLE_TESTS ENABLE_HLSL ENABLE_CPP ENABLE_REFLECT ENABLE_C_API ENABLE_UTIL SHARED)
    set(SPIRV_CROSS_${part} OFF CACHE BOOL "" FORCE)
  endforeach()
  foreach(part STATIC ENABLE_GLSL ENABLE_MSL SKIP_INSTALL FORCE_PIC)
    set(SPIRV_CROSS_${part} ON CACHE BOOL "" FORCE)
  endforeach()
  FetchContent_Declare(spirv_cross
                       GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Cross.git
                       GIT_TAG vulkan-sdk-1.4.363.0
                       GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(spirv_cross)
  if(NOT MSVC)
    foreach(target spirv-cross-core spirv-cross-glsl spirv-cross-msl)
      target_compile_options(${target} PRIVATE -fexceptions)
    endforeach()
    set_property(SOURCE src/common/gfx/shaderMsl.cpp APPEND PROPERTY COMPILE_OPTIONS -fexceptions)
  endif()
endif()

set(nf_driving_sources
  src/dllmain.cpp
  src/driving/autofunc.cpp
  src/inject_driving.cpp
  src/inject.h
  src/driving/logging.cpp
  src/common/launchInfo.cpp
  src/driving/main.cpp
  src/driving/memory.cpp
  src/driving/EventManager.cpp
  src/driving/Scheduler.cpp
  src/driving/render/RGlareManager.cpp
  src/driving/render/RSceneObj.cpp
  src/driving/render/Colorize.cpp
  src/driving/render/DebugView.cpp
  src/driving/render/Decals.cpp
  src/driving/render/Draw.cpp
  src/driving/render/Fog.cpp
  src/driving/render/Gain.cpp
  src/driving/render/LensFlare.cpp
  src/driving/render/Lightning.cpp
  src/driving/render/Lights.cpp
  src/driving/render/Materials.cpp
  src/driving/render/OffscreenBuffer.cpp
  src/driving/render/ParticleCache.cpp
  src/driving/render/Particles.cpp
  src/driving/render/Particulate.cpp
  src/driving/render/PathEngine.cpp
  src/driving/render/PostProcessing.cpp
  src/driving/render/Reflection.cpp
  src/driving/render/RenderHigh.cpp
  src/driving/render/Renderer.cpp
  src/driving/render/ShadowMap.cpp
  src/driving/render/SkeletalObj.cpp
  src/driving/render/SkyWater.cpp
  src/driving/render/StateManager.cpp
  src/driving/render/TextureContext.cpp
  src/driving/render/TimeData.cpp
  src/driving/render/WorldCulling.cpp
  src/driving/devtools/LightShadow.cpp
  src/driving/devtools/ParticleShadow.cpp
  src/driving/devtools/InputShadow.cpp
  src/driving/devtools/ReflectionShadow.cpp
  src/driving/devtools/OwnedGlobals.cpp
  src/driving/devtools/RenderFxShadow.cpp
  src/driving/devtools/RendererShadow.cpp
  src/driving/devtools/SceneObjShadow.cpp
  src/driving/devtools/TexContextShadow.cpp
  src/driving/devtools/Teleport.cpp
  src/driving/devtools/ShadowTicks.cpp
  src/driving/devtools/FaultGuard.cpp
  src/driving/devtools/MemShadow.cpp
  src/driving/devtools/FileSysShadow.cpp
  src/driving/devtools/FileSysTrace.cpp
  src/driving/devtools/MathShadow.cpp
  src/driving/devtools/RealgraphShadow.cpp
  src/driving/devtools/LoaderShadow.cpp
  src/driving/devtools/AnimShadow.cpp
  src/driving/devtools/SkelShadow.cpp
  src/driving/devtools/SndTagShadow.cpp
  src/driving/devtools/SndDecodeShadow.cpp
  src/driving/devtools/SndFilterShadow.cpp
  src/driving/devtools/SndMixShadow.cpp
  src/driving/devtools/SndSystemShadow.cpp
  src/driving/devtools/SndStreamShadow.cpp
  src/driving/devtools/SndPlatformShadow.cpp
  src/driving/devtools/SndStreamFileShadow.cpp
  src/driving/devtools/StaticInitDump.cpp
  src/driving/devtools/StandaloneTraps.cpp
  src/driving/devtools/CodeRetarget.cpp
  src/driving/devtools/HeapPad.cpp
  src/driving/devtools/X87Count.cpp
  src/driving/Vtables.cpp
  src/common/xbeOriginal.cpp
  src/driving/engine/UFileLoader.cpp
  src/driving/engine/PlayMPC.cpp
  src/driving/engine/StaticInit.cpp
  src/driving/engine/StaticInitTable.cpp
  src/driving/engine/UMemory.cpp
  src/driving/engine/URefCounter.cpp
  src/driving/engine/USingleton.cpp
  src/driving/engine/CoreContainers.cpp
  src/driving/engine/CoreFoundation.cpp
  src/driving/engine/GameLoop.cpp
  src/driving/engine/DebugFont.cpp
  src/driving/engine/SimRandom.cpp
  src/driving/devtools/CoreLoopShadow.cpp
  src/driving/devtools/CoreUtilShadow.cpp
  src/driving/devtools/RestoreRanges.cpp
  src/driving/devtools/FileDump.cpp
  src/driving/world/Collider.cpp
  src/driving/world/CollisionInstance.cpp
  src/driving/world/CollisionManager.cpp
  src/driving/world/CollisionQueries.cpp
  src/driving/world/Grid.cpp
  src/driving/world/Tree.cpp
  src/driving/world/WorldMath.cpp
  src/driving/world/WorldPos.cpp
  src/driving/world/World.cpp
  src/driving/world/VisCurtain.cpp
  src/driving/world/SoundMap.cpp
  src/driving/world/Trigger.cpp
  src/driving/world/TriggerManager.cpp
  src/driving/world/SimpleZone.cpp
  src/driving/world/RoadNetwork.cpp
  src/driving/world/RoadNav.cpp
  src/driving/world/Targeting.cpp
  src/driving/world/SoundGroup.cpp
  src/driving/audio/Sound.cpp
  src/driving/audio/SoundManager.cpp
  src/driving/audio/Mix.cpp
  src/driving/audio/Fader.cpp
  src/driving/audio/Stream.cpp
  src/driving/audio/Voice.cpp
  src/driving/audio/Bank.cpp
  src/driving/audio/Index.cpp
  src/driving/audio/ZoomObj.cpp
  src/driving/anim/Actor.cpp
  src/driving/anim/Controllers.cpp
  src/driving/anim/AnimationDatabase.cpp
  src/driving/anim/Character.cpp
  src/driving/anim/Events.cpp
  src/driving/anim/IK.cpp
  src/driving/anim/Manager.cpp
  src/driving/anim/Model.cpp
  src/driving/anim/Skeleton.cpp
  src/driving/anim/Poser.cpp
  src/driving/anim/Weapon.cpp
  src/driving/anim/AnimEngine.cpp
  src/driving/anim/ProcAnim.cpp
  src/driving/world/Render.cpp
  src/driving/camera/Camera.cpp
  src/driving/camera/WorldCamera.cpp
  src/driving/camera/CameraIniLoader.cpp
  src/driving/camera/CameraSpline.cpp
  src/driving/camera/DirectorQueue.cpp
  src/driving/camera/PlayerCamState.cpp
  src/driving/camera/PlayerCameraA.cpp
  src/driving/camera/PlayerCameraB.cpp
  src/driving/camera/PlayerCameraC.cpp
  src/driving/physics/PhysicsObject.cpp
  src/driving/physics/SimpleRigidBody.cpp
  src/driving/physics/PhysicsNamespace.cpp
  src/driving/physics/RigidBody.cpp
  src/driving/physics/RigidBodyBasics.cpp
  src/driving/physics/RigidBodyResolve.cpp
  src/driving/physics/RigidBodyCollide.cpp
  src/driving/physics/Newton.cpp
  src/driving/game/Vehicle.cpp
  src/driving/game/Helicopter.cpp
  src/driving/game/BondCarSimulate.cpp
  src/driving/game/VehicleSound.cpp
  src/driving/game/VehicleSoundEngine.cpp
  src/driving/game/VehicleSoundKinds.cpp
  src/driving/game/WeaponSound.cpp
  src/driving/game/CharacterSound.cpp
  src/driving/game/FingoDeathSound.cpp
  src/driving/game/DebugAudio.cpp
  src/driving/game/BondCarState.cpp
  src/driving/game/BondCarSnowmobile.cpp
  src/driving/game/SoftZone.cpp
  src/driving/game/BondCarModes.cpp
  src/driving/game/BondCarBasics.cpp
  src/driving/game/BondCarPhysics.cpp
  src/driving/game/VehicleRender.cpp
  src/driving/game/TyreTrack.cpp
  src/driving/game/Debris.cpp
  src/driving/game/VehicleParticle.cpp
  src/driving/game/EmpEffect.cpp
  src/driving/game/WeaponEffects.cpp
  src/driving/game/WeaponCameras.cpp
  src/driving/game/WeaponManager.cpp
  src/driving/game/Missile.cpp
  src/driving/game/Explosion.cpp
  src/driving/game/Shell.cpp
  src/driving/game/Grenade.cpp
  src/driving/game/Human.cpp
  src/driving/game/Mine.cpp
  src/driving/game/Sentry.cpp
  src/driving/game/Smackable.cpp
  src/driving/game/SimulationObjects.cpp
  src/driving/game/SimulationContainers.cpp
  src/driving/game/SimulationSpawn.cpp
  src/driving/game/MissionManager.cpp
  src/driving/game/MissionScores.cpp
  src/driving/game/MissionRules.cpp
  src/driving/game/EventsA.cpp
  src/driving/game/EventsB.cpp
  src/driving/game/FireWeaponEvent.cpp
  src/driving/game/EventsC.cpp
  src/driving/game/EventsD.cpp
  src/driving/game/AICharacter.cpp
  src/driving/game/AIPedestrian.cpp
  src/driving/game/AIHands.cpp
  src/driving/game/AIEnemy.cpp
  src/driving/game/AIEnemyGround.cpp
  src/driving/game/AIEnemyRiders.cpp
  src/driving/game/AIElements.cpp
  src/driving/game/AIVehicle.cpp
  src/driving/game/AISplinePath.cpp
  src/driving/game/AIHelicopter.cpp
  src/driving/game/AIVehicleController.cpp
  src/driving/game/AIRoadSpawn.cpp
  src/driving/game/AIGroundVehicle.cpp
  src/driving/game/AIGroundVehicleDriving.cpp
  src/driving/game/Hud.cpp
  src/driving/game/HudDraw.cpp
  src/driving/game/HudText.cpp
  src/driving/game/HudFrame.cpp
  src/driving/game/HudMenus.cpp
  src/driving/game/HudCrossHairs.cpp
  src/driving/game/Gallery.cpp
  src/driving/game/EffectsGallery.cpp
  src/driving/game/LoadingScreen.cpp
  src/driving/game/Girl.cpp
  src/driving/game/GirlPalette.cpp
  src/driving/game/FrontEndSystem.cpp
  src/driving/game/Subtitles.cpp
  src/driving/game/Gfx.cpp
  src/driving/game/OrthoHudView.cpp
  src/driving/engine/OBB.cpp
  src/driving/engine/PhysicsUtil.cpp
  src/driving/devtools/ColQueryShadow.cpp
  src/driving/devtools/ColListShadow.cpp
  src/driving/devtools/ColGridShadow.cpp
  src/driving/devtools/GeomShadow.cpp
  src/driving/devtools/WorldShadow.cpp
  src/driving/devtools/TriggerShadow.cpp
  src/driving/devtools/RoadNetShadow.cpp
  src/driving/devtools/RoadNavShadow.cpp
  src/driving/devtools/TargetShadow.cpp
  src/driving/devtools/PhysObjShadow.cpp
  src/driving/devtools/RigidBasicsShadow.cpp
  src/driving/devtools/RigidCoreShadow.cpp
  src/driving/devtools/RigidResolveShadow.cpp
  src/driving/devtools/BondCarShadowA.cpp
  src/driving/devtools/BondCarShadowB.cpp
  src/driving/devtools/BondCarShadowC.cpp
  src/driving/devtools/BondCarShadowD.cpp
  src/driving/devtools/GameAudioShadowA.cpp
  src/driving/devtools/GameAudioShadowB.cpp
  src/driving/devtools/GameAudioShadowC.cpp
  src/driving/devtools/GameAudioShadowD.cpp
  src/driving/devtools/GameEffectsShadowA.cpp
  src/driving/devtools/GameEffectsShadowB.cpp
  src/driving/devtools/GameEffectsShadowC.cpp
  src/driving/devtools/GameEffectsShadowD.cpp
  src/driving/devtools/GameWeaponsShadowA.cpp
  src/driving/devtools/GameWeaponsShadowB.cpp
  src/driving/devtools/GameWeaponsShadowC.cpp
  src/driving/devtools/GameWeaponsShadowD.cpp
  src/driving/devtools/GameMissionsShadowA.cpp
  src/driving/devtools/GameMissionsShadowB.cpp
  src/driving/devtools/GameMissionsShadowC.cpp
  src/driving/devtools/GameMissionsShadowD.cpp
  src/driving/devtools/GameEventsShadowA.cpp
  src/driving/devtools/GameEventsShadowB.cpp
  src/driving/devtools/GameEventsShadowC.cpp
  src/driving/devtools/GameEventsShadowD.cpp
  src/driving/devtools/GameAIShadowA.cpp
  src/driving/devtools/GameAIShadowB.cpp
  src/driving/devtools/GameAIShadowC.cpp
  src/driving/devtools/GameAIShadowD.cpp
  src/driving/devtools/GameAIShadowE.cpp
  src/driving/devtools/GameAIShadowF.cpp
  src/driving/devtools/GameFrontEndShadowA.cpp
  src/driving/devtools/GameFrontEndShadowB.cpp
  src/driving/devtools/GameFrontEndShadowC.cpp
  src/driving/devtools/GameFrontEndShadowD.cpp
  src/driving/devtools/CRuntimeShadowA.cpp
  src/driving/devtools/CRuntimeShadowB.cpp
  src/driving/devtools/VehicleShadow.cpp
  src/driving/devtools/RigidCollideShadow.cpp
  src/driving/devtools/RenderShadow.cpp
  src/driving/devtools/GeoPrimShadow.cpp
  src/driving/devtools/DumpTimer.cpp
  src/driving/devtools/ViewCamShadow.cpp
  src/driving/devtools/CamDataShadow.cpp
  src/driving/devtools/PlayerCamShadowA.cpp
  src/driving/devtools/PlayerCamShadowB.cpp
  src/driving/devtools/PlayerCamShadowC.cpp
  src/driving/devtools/SndCallTrace.cpp
  src/driving/devtools/SndLockstep.cpp
  src/driving/devtools/AudioMgrShadow.cpp
  src/driving/devtools/AudioMixShadow.cpp
  src/driving/devtools/AudioStreamShadow.cpp
  src/driving/devtools/AudioVoiceShadow.cpp
  src/driving/devtools/AudioIndexShadow.cpp
  src/driving/devtools/ActorShadow.cpp
  src/driving/devtools/CharacterShadow.cpp
  src/driving/devtools/IKShadow.cpp
  src/driving/devtools/ActModelShadow.cpp
  src/driving/devtools/PoserShadow.cpp
  src/driving/devtools/AnimEngineShadow.cpp
  src/driving/devtools/CarpShadow.cpp
  src/driving/engine/UGroup.cpp
  src/driving/data/UData.cpp
  src/driving/data/SymbolTable.cpp
  src/driving/data/Dafi.cpp
  src/driving/data/Carp.cpp
  src/driving/data/RCARPFile.cpp
  src/driving/data/DebugVariables.cpp
  src/driving/data/Tuning.cpp
  src/driving/data/IniFiles.cpp
  src/driving/data/StdStreams.cpp
  src/driving/data/StdLocale.cpp
  src/driving/data/DebugItem.cpp
  src/driving/data/CoordConvert.cpp
  src/driving/devtools/DebugVarShadow.cpp
  src/driving/data/AttributeValue.cpp
  src/driving/data/AttributeParsers.cpp
  src/driving/data/AttributeContainers.cpp
  src/driving/data/AttributeSystem.cpp
  src/driving/data/AttributeSet.cpp
  src/driving/devtools/AttribShadow.cpp
  src/common/fmv/Ffmpeg.cpp
  src/driving/engine/ActionQueue.cpp
  src/driving/engine/InputDevice.cpp
  src/driving/engine/IOModule.cpp
  src/driving/engine/Feedback.cpp
  src/driving/engine/InputConfig.cpp
  src/driving/platform/Pad.cpp
  src/common/xboxPath.cpp
  src/driving/platform/XboxStartup.cpp
  src/driving/platform/XboxXapi.cpp
  src/driving/platform/RealSystem.cpp
  src/driving/platform/KernelImports.cpp
  src/driving/platform/HostWin32.cpp
  src/driving/platform/GameWindow.cpp
  src/driving/platform/RealPrint.cpp
  src/driving/platform/RealMemory.cpp
  src/driving/platform/FileSys.cpp
  src/driving/platform/RefPack.cpp
  src/driving/platform/RealMath.cpp
  src/driving/platform/VU0Math.cpp
  src/driving/platform/D3DXMath.cpp
  src/driving/eagl/Realgraph.cpp
  src/driving/eagl/Transform.cpp
  src/driving/eagl/Loader.cpp
  src/driving/eagl/RuntimeAlloc.cpp
  src/driving/eagl/GeoPrimState.cpp
  src/driving/eagl/GameCallbacks.cpp
  src/driving/eagl/Tar.cpp
  src/driving/eagl/View.cpp
  src/driving/eagl/RenderContext.cpp
  src/driving/eagl/EaglFont.cpp
  src/driving/eagl/RenderMethod.cpp
  src/driving/eagl/Model.cpp
  src/driving/eagl/Profiler.cpp
  src/driving/eagl/anim/AnimObjects.cpp
  src/driving/eagl/anim/EventTarget.cpp
  src/driving/eagl/anim/AnimDecode.cpp
  src/driving/eagl/anim/AnimChannels.cpp
  src/driving/eagl/anim/AnimDeltaF.cpp
  src/driving/eagl/anim/Skeleton.cpp
  src/driving/eagl/anim/AnimMisc.cpp
  src/driving/eagl/anim/AnimPoseBlend.cpp
  src/driving/eagl/anim/AnimDeltaQ.cpp
  src/driving/eagl/anim/AnimLocoBlend.cpp
  src/driving/platform/XboxTimer.cpp
  src/driving/platform/XboxInput.cpp
  src/driving/platform/LaunchOptions.cpp
  src/driving/platform/CRuntime.cpp
  src/driving/platform/CRuntimeFloat.cpp
  src/driving/platform/CRuntimeFormat.cpp
  src/driving/platform/CRuntimeParse.cpp
  src/driving/platform/CRuntimeStd.cpp
  src/driving/gfx/backendHost.cpp
  src/driving/gfx/d3dSeam.cpp
  src/driving/sound/snd/Voices.cpp
  src/driving/sound/snd/Banks.cpp
  src/driving/sound/snd/Decode.cpp
  src/driving/sound/snd/DecodeUnused.cpp
  src/driving/sound/snd/Filters.cpp
  src/driving/sound/snd/FiltersUnused.cpp
  src/driving/sound/snd/Mixer.cpp
  src/driving/sound/snd/Reverb.cpp
  src/driving/sound/snd/System.cpp
  src/driving/sound/snd/Streams.cpp
  src/driving/sound/snd/Platform.cpp
  src/driving/sound/snd/Stream.cpp
  src/driving/sound/dsndSeam.cpp
  src/common/sound/xadpcm.cpp
  src/common/xbeEntrySeam.cpp
  src/common/xbeProfiler.cpp
  ${nf_gfx_backend}
  src/common/gfx/nv2aPixelShader.cpp
  src/common/gfx/nv2aVertexShader.cpp
  src/common/gfx/shaderBuilder.cpp
  src/common/gfx/shaderSpirv.cpp
  ${nf_gfx_devtools}
)

if(WIN32)
add_library(drivinginject SHARED ${nf_driving_sources})

# winmm for the multimedia timer the game's tick now comes from (src/driving/platform/XboxTimer.cpp);
# d3d9 for the shared graphics backend the D3D8 seam dispatches to; d3dcompiler for its shader check
# (src/common/gfx/devtools/shaderCheck.cpp).
# XAUDIO2_LIB is xaudio2 or xaudio2_9 - see actioninject above.
target_link_libraries(drivinginject PRIVATE d3d9 d3dcompiler winmm xinput9_1_0 ${XAUDIO2_LIB} shell32)
endif()

# The driving engine's window, input and sound output (docs/driving/portable.md). Off: the loader's Win32 window,
# XInput and the keyboard through Win32, XAudio2. On: SDL3 for all three - the window opened by the injection DLL
# for the loader (src/inject_driving.cpp), SDL's gamepads and keyboard, and a voice mixer of ours on an SDL audio
# stream (src/driving/sound/sdlAudioDriving.cpp); the movies' sound goes to the same device.
#
# SDL3 is built from source, pinned to a release tag, so that every host (MSVC, the mingw cross build, a Linux -m32
# build) gets the same version from the same place, with no prebuilt to keep per toolchain; NF_SDL3_DIR takes an
# installed SDL3's CMake package instead (SDL3Config.cmake's directory), for a machine without network access.
option(NIGHTFIRE_SDL "Driving engine window, input and sound output through SDL3" OFF)
if(NIGHTFIRE_SDL)
  set(NF_SDL3_DIR "" CACHE PATH "An installed SDL3's CMake package directory; empty to build the pinned release")
  if(NF_SDL3_DIR)
    find_package(SDL3 REQUIRED CONFIG PATHS ${NF_SDL3_DIR} NO_DEFAULT_PATH)
  else()
    include(FetchContent)
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    foreach(subsystem RENDER GPU CAMERA SENSOR DIALOG TRAY)   # what the game does not use
      set(SDL_${subsystem} OFF CACHE BOOL "" FORCE)
    endforeach()
    if(NIGHTFIRE_GFX STREQUAL "sdlgpu")
      set(SDL_GPU ON CACHE BOOL "" FORCE)
    endif()
    FetchContent_Declare(SDL3
                         GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
                         GIT_TAG release-3.4.18
                         GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(SDL3)
  endif()
  set(nf_output_sources
    src/driving/platform/HostWindowSdl.cpp
    src/driving/platform/HostInputSdl.cpp
    src/driving/sound/sdlAudioDriving.cpp
    src/common/fmv/FmvAudioSdl.cpp)
elseif(WIN32)
  set(nf_output_sources
    src/driving/platform/HostInputWin32.cpp
    src/driving/sound/xaudio2Driving.cpp
    src/common/fmv/FmvAudio.cpp)
else()
  message(FATAL_ERROR "A build for ${CMAKE_SYSTEM_NAME} needs NIGHTFIRE_SDL=ON: the other window, input and sound "
                      "output are Win32's")
endif()
if(NIGHTFIRE_GFX STREQUAL "sdlgpu" AND NOT NIGHTFIRE_SDL)
  message(FATAL_ERROR "NIGHTFIRE_GFX=sdlgpu needs NIGHTFIRE_SDL=ON: SDL owns the window the GPU API presents into")
endif()
if(WIN32)
target_sources(drivinginject PRIVATE ${nf_output_sources})
if(NIGHTFIRE_SDL)
  target_compile_definitions(drivinginject PRIVATE NIGHTFIRE_SDL)
  target_link_libraries(drivinginject PRIVATE SDL3::SDL3)
  add_custom_command(TARGET drivinginject POST_BUILD
                     COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:drivinginject>)
endif()
if(MSVC)
  target_link_options(drivinginject PRIVATE /MAP)   # see the symbol maps above
else()
  target_link_options(drivinginject PRIVATE -Wl,-Map,drivinginject.map)
endif()

target_compile_definitions(actioninject PRIVATE UNICODE _UNICODE)
target_compile_definitions(drivinginject PRIVATE UNICODE _UNICODE)
# Experimental post-process anti-aliasing on the 3D scene, before the HUD and text, in both engines. Chosen by
# AntiAliasing in settings.ini: 0 off, 1 FXAA (src/common/gfx/fxaa.cpp, the default), 2 SMAA (not implemented
# yet). Remove these two lines and fxaa.cpp from both source lists to take it out.
target_compile_definitions(actioninject PRIVATE NF_ANTIALIASING)
# No inlining in the action engine. A shadow test fakes a function by hooking its entry, the original's and our
# port's, and a call the compiler has inlined never reaches that entry, so the fake was bypassed without a word
# (parcel 8 of docs/nightfire-port-lifts.md). And a port that matches only when inlined is exact by luck: MSVC
# returns a float in ST0 and may leave it unrounded when the call is not inlined, which is where the clang, 64-bit
# and macOS builds would part from this one. With inlining off, the time demos and every shadow suite match as they
# do with it on. The game needs a fraction of a modern CPU either way.
if(MSVC)
  target_compile_options(actioninject PRIVATE /Ob0)
endif()
target_compile_definitions(drivinginject PRIVATE NF_ANTIALIASING)
endif()


# FMV playback through FFmpeg (src/action/engine/Fmv.cpp, src/driving/engine/PlayMPC.cpp, src/common/fmv): the DLLs
# and headers in third_party/ffmpeg-prebuilt, built from the third_party/ffmpeg submodule by tools/fmv/build_ffmpeg.sh
# and committed. The DLLs are loaded at run time and copied beside the executables with FFmpeg's licence and build
# note, so the build output is complete.
set(NF_FFMPEG_DIR ${CMAKE_SOURCE_DIR}/third_party/ffmpeg-prebuilt CACHE PATH "FFmpeg DLLs and headers")
if(NOT EXISTS ${NF_FFMPEG_DIR}/include/libavformat/avformat.h)
  message(FATAL_ERROR "No FFmpeg in ${NF_FFMPEG_DIR}: it is committed in third_party/ffmpeg-prebuilt, "
                      "and tools/fmv/build_ffmpeg.sh rebuilds it")
endif()
# The 64-bit standalone build's DLLs are bin-x64's (the same release, configuration and headers).
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(nf_ffmpeg_bin bin-x64)
else()
  set(nf_ffmpeg_bin bin)
endif()
file(GLOB NF_FFMPEG_FILES ${NF_FFMPEG_DIR}/${nf_ffmpeg_bin}/*.dll ${NF_FFMPEG_DIR}/FFmpeg-*.txt)
if(WIN32)
# The movies' sound as the driving engine's player decodes and folds it, without the game
# (src/driving/devtools/FmvAudioCheck.cpp): "fmvaudiocheck disc/driving/misc.viv".
add_executable(fmvaudiocheck EXCLUDE_FROM_ALL
  src/driving/devtools/FmvAudioCheck.cpp
  src/common/fmv/Ffmpeg.cpp
)
target_link_libraries(fmvaudiocheck PRIVATE user32)
foreach(target actioninject drivinginject fmvaudiocheck)
  target_include_directories(${target} PRIVATE ${NF_FFMPEG_DIR}/include)
  add_custom_command(TARGET ${target} POST_BUILD
                     COMMAND ${CMAKE_COMMAND} -E copy_if_different ${NF_FFMPEG_FILES} $<TARGET_FILE_DIR:${target}>)
endforeach()
else()
  # The same release's shared libraries for the host, which the committed headers fit: built by
  # "tools/fmv/build_ffmpeg.sh linux-i386 <prefix>" (or macos-arm64) and named here by <prefix>/lib.
  set(NF_FFMPEG_LIB_DIR "" CACHE PATH "The host's FFmpeg shared libraries (libavutil.so.60...)")
  if(NF_FFMPEG_LIB_DIR AND APPLE)
    set(NF_FFMPEG_FILES ${NF_FFMPEG_LIB_DIR}/libavutil.60.dylib ${NF_FFMPEG_LIB_DIR}/libavcodec.62.dylib
                        ${NF_FFMPEG_LIB_DIR}/libavformat.62.dylib)
  elseif(NF_FFMPEG_LIB_DIR)
    set(NF_FFMPEG_FILES ${NF_FFMPEG_LIB_DIR}/libavutil.so.60 ${NF_FFMPEG_LIB_DIR}/libavcodec.so.62
                        ${NF_FFMPEG_LIB_DIR}/libavformat.so.62)
  endif()
  if(NF_FFMPEG_LIB_DIR)
    file(GLOB nf_ffmpeg_notes ${NF_FFMPEG_DIR}/FFmpeg-*.txt)
    list(APPEND NF_FFMPEG_FILES ${nf_ffmpeg_notes})
  else()
    message(WARNING "NF_FFMPEG_LIB_DIR is not set: the movies need FFmpeg 8.0's shared libraries on the library path")
    file(GLOB NF_FFMPEG_FILES ${NF_FFMPEG_DIR}/FFmpeg-*.txt)
  endif()
endif()

# Run preprocess.py before building actioninject and drivinginject. It writes the injection tables
# (src/*/autogenerated_injections.inc) and the generated wrappers that autofunc.cpp and the UI handler
# switch are built from, all of which are #included by sources in those two targets.
#
# This is a target rather than a PRE_BUILD command on purpose: PRE_BUILD is only honoured by the Visual
# Studio generators, and everywhere else CMake quietly downgrades it to PRE_LINK - which runs after the
# compilation that needed the generated files, so the build uses whatever the previous run left behind.
add_custom_target(preprocess
                  COMMAND ${Python3_EXECUTABLE} tools/preprocess.py
                  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                  COMMENT "Generating injection tables (tools/preprocess.py)")
if(WIN32)
add_dependencies(actioninject preprocess)
add_dependencies(drivinginject preprocess)
endif()

# Direct3D 9 on a host without it: dxvk-native, D3D9 over Vulkan with SDL3 as its window system. Its release
# tarball carries i386 libraries (lib32) as well as x86-64 ones, built for the Steam runtime, so the build takes
# that, pinned by version and hash; NF_DXVK_DIR takes an extracted release (its usr directory) instead. macOS has
# none (docs/driving/macos.md 1.1): only the SDL GPU backend.
if(APPLE AND NOT NIGHTFIRE_GFX STREQUAL "sdlgpu")
  message(FATAL_ERROR "A build for macOS needs NIGHTFIRE_GFX=sdlgpu: there is no dxvk-native for it")
endif()
if(NOT WIN32 AND NOT APPLE)
  set(NF_DXVK_DIR "" CACHE PATH "An extracted dxvk-native release's usr directory; empty to fetch the pinned one")
  if(NOT NF_DXVK_DIR)
    include(FetchContent)
    FetchContent_Declare(dxvk_native
      URL https://github.com/doitsujin/dxvk/releases/download/v3.1.1/dxvk-native-3.1.1-steamrt-sniper.tar.gz
      URL_HASH SHA256=506fa1ea5bb6ce9a89e11436f7ab5191bc0196e8a577e0cf0b16a45bbc9991b7)
    FetchContent_MakeAvailable(dxvk_native)
    set(NF_DXVK_DIR ${dxvk_native_SOURCE_DIR})
  endif()
  if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(NF_DXVK_D3D9 ${NF_DXVK_DIR}/lib32/libdxvk_d3d9.so.0)
  else()
    set(NF_DXVK_D3D9 ${NF_DXVK_DIR}/lib/libdxvk_d3d9.so.0)
  endif()
  add_library(dxvk_d3d9 SHARED IMPORTED)
  set_target_properties(dxvk_d3d9 PROPERTIES IMPORTED_LOCATION ${NF_DXVK_D3D9}
                                             INTERFACE_INCLUDE_DIRECTORIES ${NF_DXVK_DIR}/include/dxvk)
endif()
if(NOT WIN32)
  find_package(Threads REQUIRED)
  # The executable finds SDL3, dxvk and FFmpeg beside itself.
  if(APPLE)
    set(CMAKE_BUILD_RPATH "@executable_path")
  else()
    set(CMAKE_BUILD_RPATH "\$ORIGIN")
  endif()
  if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    include(cmake/xbox-abi-linux.cmake)
  elseif(APPLE)
    # arm64 has one ABI too, and no x86 intrinsics: the game's sources are ordinary macOS code
    set(NF_XBOX_ABI_OPTIONS)
  else()
    # x86-64 has one ABI (docs/driving/64-bit.md 7.11): the game's sources are ordinary Linux code, with MSVC's
    # <intrin.h> from src/common/linux
    set(NF_XBOX_ABI_OPTIONS -isystem ${CMAKE_SOURCE_DIR}/src/common/linux)
  endif()
  # The sources on the host's side of cmake/xbox-abi-linux.cmake: the host layer, SDL, FFmpeg and the D3D9 backend.
  # On 32-bit x86 they are a shared library of their own, bound to its own definitions (-Bsymbolic-functions): an
  # inline function or template instance both sides compile has the same name on both, and each side must call its
  # own, compiled for its own conventions. A class whose methods both sides call (FmvAudio) stays on the game's side.
  # On x86-64 they are part of the executable, as on Windows.
  set(nf_host_abi_sources
    src/driving/platform/HostPosix.cpp
    src/common/linux/win32Calls.cpp
    src/driving/platform/HostWindowSdl.cpp
    src/driving/platform/HostInputSdl.cpp
    src/driving/sound/sdlAudioDriving.cpp
    src/common/fmv/Ffmpeg.cpp
    src/common/xboxPath.cpp
    src/common/gfx/nv2aPixelShader.cpp
    src/common/gfx/nv2aVertexShader.cpp
    src/common/gfx/shaderBuilder.cpp
    src/common/gfx/shaderSpirv.cpp
    ${nf_gfx_host_sources})
  if(APPLE)   # the D3D9 backend's Win32 calls, whose types are dxvk's
    list(REMOVE_ITEM nf_host_abi_sources src/common/linux/win32Calls.cpp)
  endif()
endif()

# The standalone driving engine (docs/driving/standalone.md): our code alone, with no Driving.xbe mapped or read, no
# injection and no shadow tests - drivinginject's sources less the devtools, the injection and the XBE's patching
# (each seam's and the start-up's patching is #ifndef NIGHTFIRE_STANDALONE). driving_standalone has none of the
# devtools (standalone/NoDevtools.cpp and StandaloneHooks.cpp answer the hooks game code calls);
# driving_standalone_test links the lockstep and dump harness (NIGHTFIRE_LOCKSTEP, DUMP_TICKS, SNDTRACE, SNDLOCKSTEP,
# the teleports) and the crash test (NIGHTFIRE_CRASHTEST), so that a standalone run can be compared with the injected
# build's baselines. Both have the crash reporter (standalone/CrashReport.h).
# tools/standalone_poison.py (the standalone_poison target) mirrors src/ with every Driving.xbe address still in the
# worklist poisoned, to list what would stop this compiling.
set(nf_standalone_sources src/driving/standalone/StandaloneMain.cpp src/driving/standalone/StandaloneHooks.cpp
                          src/driving/standalone/CrashReport.cpp)
if(WIN32)
  list(APPEND nf_standalone_sources src/driving/standalone/CrashReportWin32.cpp)
else()
  list(APPEND nf_standalone_sources src/driving/standalone/CrashReportPosix.cpp)
endif()
if(NOT NIGHTFIRE_SDL)
  list(APPEND nf_standalone_sources src/driving/platform/HostWindowWin32.cpp)   # the SDL build's is in drivinginject
endif()
foreach(source ${nf_driving_sources} ${nf_output_sources})
  if(source MATCHES "src/(driving|common/gfx)/devtools/" OR source MATCHES "src/(inject_driving|dllmain)\.cpp$" OR
     source MATCHES "src/common/(xbeOriginal|xbeEntrySeam|xbeProfiler)\.cpp$")
    continue()
  endif()
  list(APPEND nf_standalone_sources ${source})
endforeach()
set(nf_standalone_harness
  src/driving/devtools/Teleport.cpp
  src/driving/devtools/SndLockstep.cpp
  src/driving/devtools/SndCallTrace.cpp
  src/driving/devtools/CodeRetarget.cpp
  src/driving/devtools/HeapPad.cpp
  src/driving/devtools/X87Count.cpp
  src/driving/devtools/CrashTest.cpp)
if(NOT WIN32)
  list(REMOVE_ITEM nf_standalone_sources src/driving/platform/HostWin32.cpp ${nf_host_abi_sources})
  # Moving code references reads the PE image and the x87 counts MSVC's link map
  list(REMOVE_ITEM nf_standalone_harness src/driving/devtools/CodeRetarget.cpp src/driving/devtools/X87Count.cpp)
  list(APPEND nf_standalone_harness src/driving/devtools/RetargetPosix.cpp)
  set_source_files_properties(${nf_standalone_sources} ${nf_standalone_harness} src/driving/standalone/NoDevtools.cpp
                              PROPERTIES COMPILE_OPTIONS "${NF_XBOX_ABI_OPTIONS}")
endif()
if(NOT WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
  list(APPEND nf_standalone_sources ${nf_host_abi_sources})
elseif(NOT WIN32)
  add_library(drivinghost SHARED ${nf_host_abi_sources})
  target_include_directories(drivinghost PRIVATE ${NF_FFMPEG_DIR}/include)
  target_compile_definitions(drivinghost PRIVATE UNICODE _UNICODE NF_ANTIALIASING NIGHTFIRE_STANDALONE NIGHTFIRE_SDL)
  # Called with the game's 4-byte stack alignment: each function realigns to the 16 Linux libraries expect
  target_compile_options(drivinghost PRIVATE -mstackrealign -include ${CMAKE_SOURCE_DIR}/src/common/linux/msvcNames.h)
  if(NIGHTFIRE_GFX STREQUAL "sdlgpu")
    # dxvk's headers only: win32Calls.cpp's Win32 types are its windows.h
    set(nf_host_d3d9)
    target_include_directories(drivinghost PRIVATE ${NF_DXVK_DIR}/include/dxvk)
  else()
    set(nf_host_d3d9 dxvk_d3d9)
  endif()
  target_link_libraries(drivinghost PRIVATE SDL3::SDL3 ${nf_host_d3d9} ${CMAKE_DL_LIBS} Threads::Threads)
  target_link_options(drivinghost PRIVATE -Wl,-Bsymbolic-functions)
  add_custom_command(TARGET drivinghost POST_BUILD   # beside it and the executables
                     COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_SONAME_FILE:SDL3::SDL3>
                             $<$<STREQUAL:${NIGHTFIRE_GFX},d3d9>:${NF_DXVK_D3D9}> ${NF_FFMPEG_FILES} $<TARGET_FILE_DIR:drivinghost>)
endif()
# The 64-bit build (docs/driving/64-bit.md 7.1): the game window, the 4 GB from NIGHTFIRE_WINDOW_BASE that holds every
# address the game's data may keep (platform/Ptr32.h). 0 by default; 0x100000000 as macOS needs it (macos.md 2), where
# nothing of the process may lie below 4 GB. The image is linked at a fixed address 4 MB into the window, so that the
# statics and vtables the data points at lie in it.
set(NIGHTFIRE_WINDOW_BASE 0 CACHE STRING "64-bit driving engine: the game window's base address (0 or 0x100000000)")
function(nf_game_window target)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    math(EXPR image_base "${NIGHTFIRE_WINDOW_BASE} + 0x400000" OUTPUT_FORMAT HEXADECIMAL)
    target_compile_definitions(${target} PRIVATE NIGHTFIRE_WINDOW_BASE=${NIGHTFIRE_WINDOW_BASE})
    if(MSVC)
      target_link_options(${target} PRIVATE /BASE:${image_base} /DYNAMICBASE:NO /HIGHENTROPYVA:NO)
    elseif(WIN32)
      target_link_options(${target} PRIVATE -Wl,--image-base,${image_base} -Wl,--disable-dynamicbase
                                            -Wl,--disable-high-entropy-va)
    elseif(APPLE)
      # Position-independent, at 4 GB plus the slide: __PAGEZERO cannot be made smaller on arm64, and the image
      # cannot be placed (docs/driving/macos.md 2). The arena's set-up stops the game if the image is outside.
    else()
      # Not position-independent (the toolchain), with the position-independent start-up files: the C library's
      # others hold 32-bit absolute addresses, which a window above 4 GB does not reach
      target_link_options(${target} PRIVATE -Wl,--image-base=${image_base} -nostartfiles ${NF_START_FILES})
      target_link_libraries(${target} PRIVATE ${NF_END_FILES})
      if(NOT NIGHTFIRE_WINDOW_BASE EQUAL 0)
        # lld turns a GOT load into a 32-bit immediate address in an executable that is not position-independent
        target_link_options(${target} PRIVATE -Wl,--no-relax)
      endif()
    endif()
  endif()
endfunction()
if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND NOT WIN32 AND NOT APPLE)
  foreach(file Scrt1.o crti.o crtbeginS.o crtendS.o crtn.o)
    execute_process(COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=${file} OUTPUT_VARIABLE path
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(file MATCHES "^crt(endS|n)")
      list(APPEND NF_END_FILES ${path})
    else()
      list(APPEND NF_START_FILES ${path})
    endif()
  endforeach()
endif()

add_executable(driving_standalone ${nf_standalone_sources} src/driving/standalone/NoDevtools.cpp)
add_executable(driving_standalone_test EXCLUDE_FROM_ALL ${nf_standalone_sources} ${nf_standalone_harness})
target_compile_definitions(driving_standalone_test PRIVATE NIGHTFIRE_STANDALONE_TEST)
# NIGHTFIRE_SNDTRACE's wrappers by name (devtools/SndCallTraceRedirect.h): the test executable's sources outside the
# sound library and the wrappers' own file are compiled with that header first. The sources are driving_standalone's
# too, hence the condition on the target.
set(nf_test_only "$<STREQUAL:$<TARGET_PROPERTY:NAME>,driving_standalone_test>")
foreach(source ${nf_standalone_sources} ${nf_standalone_harness})
  if(source MATCHES "src/driving/sound/snd/" OR source MATCHES "devtools/SndCallTrace\\.cpp$")
    continue()
  endif()
  if(MSVC)
    set_property(SOURCE ${source} APPEND PROPERTY COMPILE_OPTIONS
                 "$<${nf_test_only}:/FI${CMAKE_SOURCE_DIR}/src/driving/devtools/SndCallTraceRedirect.h>")
  else()
    set_property(SOURCE ${source} APPEND PROPERTY COMPILE_OPTIONS "$<${nf_test_only}:-include>"
                 "$<${nf_test_only}:${CMAKE_SOURCE_DIR}/src/driving/devtools/SndCallTraceRedirect.h>")
  endif()
endforeach()
# Sanitizer runs of the standalone executables (docs/driving/macos.md, package M2): -DNIGHTFIRE_SANITIZE=
# float-cast-overflow (out-of-range float-to-integer conversions, which x86 and arm64 answer differently) or thread
# (data races), for the Linux x86-64 build, whose host code is in the executables too; SDL and the other libraries
# are left uninstrumented. Reports go where UBSAN_OPTIONS / TSAN_OPTIONS log_path says.
set(NIGHTFIRE_SANITIZE "" CACHE STRING "Clang sanitizers for the standalone executables (-fsanitize=<value>); empty for none")
foreach(target driving_standalone driving_standalone_test)
  add_dependencies(${target} preprocess)
  if(NIGHTFIRE_SANITIZE AND NOT MSVC)
    target_compile_options(${target} PRIVATE -fsanitize=${NIGHTFIRE_SANITIZE} -fno-omit-frame-pointer -g)
    # The runtime as a shared library, so that the game's own operator new and delete win over its; found where
    # Clang keeps it
    execute_process(COMMAND ${CMAKE_CXX_COMPILER} --print-runtime-dir OUTPUT_VARIABLE nf_sanitizer_dir
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    target_link_options(${target} PRIVATE -fsanitize=${NIGHTFIRE_SANITIZE} -shared-libsan
                                          -Wl,-rpath,${nf_sanitizer_dir})
  endif()
  target_include_directories(${target} PRIVATE ${NF_FFMPEG_DIR}/include)
  target_compile_definitions(${target} PRIVATE UNICODE _UNICODE NF_ANTIALIASING NIGHTFIRE_STANDALONE)
  if(WIN32)
    target_link_libraries(${target} PRIVATE d3d9 winmm xinput9_1_0 ${XAUDIO2_LIB} shell32)
  elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    if(NOT APPLE)
      target_link_libraries(${target} PRIVATE dxvk_d3d9)
    endif()
    target_link_libraries(${target} PRIVATE ${CMAKE_DL_LIBS} Threads::Threads)
    target_compile_options(${target} PRIVATE -include ${CMAKE_SOURCE_DIR}/src/common/linux/msvcNames.h)
    add_custom_command(TARGET ${target} POST_BUILD   # beside the executable
                       COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_SONAME_FILE:SDL3::SDL3> ${NF_DXVK_D3D9}
                               ${NF_FFMPEG_FILES} $<TARGET_FILE_DIR:${target}>)
  else()
    target_link_libraries(${target} PRIVATE drivinghost)
    target_compile_options(${target} PRIVATE -include ${CMAKE_SOURCE_DIR}/src/common/linux/msvcNames.h)
  endif()
  if(NIGHTFIRE_SDL)
    target_compile_definitions(${target} PRIVATE NIGHTFIRE_SDL)
    target_link_libraries(${target} PRIVATE SDL3::SDL3)
    if(WIN32)   # beside the executable (the 32-bit Release folder also gets one from drivinginject)
      add_custom_command(TARGET ${target} POST_BUILD
                         COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:${target}>)
    endif()
  endif()
  if(MSVC)
    target_link_options(${target} PRIVATE /MAP)
  elseif(APPLE)
    target_link_options(${target} PRIVATE -Wl,-map,${target}.map)
  else()
    target_link_options(${target} PRIVATE -Wl,-Map,${target}.map)
  endif()
  # The 64-bit build: in the game window, with the FFmpeg DLLs beside it
  nf_game_window(${target})
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    if(MSVC)
      # A PDB, so that a crash.dmp from a run reads with cdb (folding and dead-code removal kept as without it)
      target_compile_options(${target} PRIVATE /Zi)
      target_link_options(${target} PRIVATE /DEBUG /OPT:REF /OPT:ICF)
    endif()
    if(WIN32)
      add_custom_command(TARGET ${target} POST_BUILD
                         COMMAND ${CMAKE_COMMAND} -E copy_if_different ${NF_FFMPEG_FILES} $<TARGET_FILE_DIR:${target}>)
    endif()
  endif()
endforeach()
if(NIGHTFIRE_GFX STREQUAL "sdlgpu")
  if(WIN32)
    set(nf_msl_users drivinginject driving_standalone driving_standalone_test)
  elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)   # the host's sources are the executables' own
    set(nf_msl_users driving_standalone driving_standalone_test)
  else()
    set(nf_msl_users drivinghost)
  endif()
  foreach(target ${nf_msl_users})
    target_link_libraries(${target} PRIVATE spirv-cross-msl)
  endforeach()
  # Every .spv of a folder (spirvdump's, or a run's NIGHTFIRE_SPIRVDUMP=1 spirv/) translated to MSL as the backend
  # translates it, written beside it as .metal, and checked: "spirvmsl <folder>" (src/common/gfx/devtools/spirvMslMain.cpp).
  add_executable(spirvmsl EXCLUDE_FROM_ALL src/common/gfx/devtools/spirvMslMain.cpp src/common/gfx/shaderMsl.cpp)
  target_link_libraries(spirvmsl PRIVATE spirv-cross-msl)
  if(APPLE)
    # spirvmsl's .metal files compiled by this Mac's Metal framework, as SDL's Metal driver compiles them: "mslcompile
    # <folder>" (src/common/gfx/devtools/mslCompileMain.mm)
    set_source_files_properties(src/common/gfx/devtools/mslCompileMain.mm PROPERTIES LANGUAGE CXX
                                COMPILE_OPTIONS "-xobjective-c++;-fobjc-arc")
    add_executable(mslcompile EXCLUDE_FROM_ALL src/common/gfx/devtools/mslCompileMain.mm)
    target_link_libraries(mslcompile PRIVATE "-framework Metal" "-framework Foundation")
  endif()
endif()
# The x86 instructions the driving engine's ports use by intrinsics: SSE's, RDTSC, REP MOVSD/MOVSB, EMMS
# (src/driving/platform/Sse.h, docs/driving/macos.md 3). On: the instructions. Off: C with their results
# (src/driving/platform/SseC.h; RSQRTSS from the runner's table), which a host without them needs; an x86 build can be
# made with it off to test those forms against the baselines. A host that is not x86 is always off.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|ARM|aarch64)" OR CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
  set(NIGHTFIRE_SSE OFF CACHE BOOL "Driving engine: the x86 intrinsics as the instructions (off: C forms)" FORCE)
else()
  # The C forms against the instructions (src/driving/devtools/SseTest.cpp): "sse_test", or "sse_test --rsqrt-table"
  # to print src/driving/platform/SseRsqrtTable.h from this CPU's RSQRTSS.
  find_package(Threads REQUIRED)
  add_executable(sse_test EXCLUDE_FROM_ALL src/driving/devtools/SseTest.cpp)
  target_link_libraries(sse_test PRIVATE Threads::Threads)
endif()
option(NIGHTFIRE_SSE "Driving engine: the x86 intrinsics as the instructions (off: C forms)" ON)
# The driving engine's x87 code (docs/driving/64-bit.md 4.1, docs/driving/portable.md). On: the 43 functions
# whose results come from FSIN, FCOS, FPTAN, FPATAN and the like stay the original instructions, so the injected
# build's shadow tests and the lockstep baselines (lbase) stay exact. Off: each is C in double (<math.h>), as any
# host without an x87 needs, and the particle systems are ordered by a number given to each emitter rather than by
# its address (docs/driving/original-quirks.md); the baselines of that build are lbase_c. The injected build's
# shadows of these functions differ with it off.
# A 64-bit build is always off.
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(NIGHTFIRE_X87 OFF CACHE BOOL "Driving engine: keep the original x87 instructions (off: portable C)" FORCE)
endif()
option(NIGHTFIRE_X87 "Driving engine: keep the original x87 instructions (off: portable C)" ON)
foreach(target drivinginject driving_standalone driving_standalone_test)
  if(NOT TARGET ${target})   # drivinginject is Windows-only
    continue()
  endif()
  if(NIGHTFIRE_X87)
    target_compile_definitions(${target} PRIVATE NIGHTFIRE_X87=1)
  else()
    target_compile_definitions(${target} PRIVATE NIGHTFIRE_X87=0)
  endif()
  if(NIGHTFIRE_SSE)
    target_compile_definitions(${target} PRIVATE NIGHTFIRE_SSE=1)
  else()
    target_compile_definitions(${target} PRIVATE NIGHTFIRE_SSE=0)
  endif()
endforeach()
# The game window and Ptr32 without the game (src/driving/devtools/GameWindowTest.cpp): "gamewindow_test [operations]",
# linked as the standalone executables are.
if(WIN32)
  add_executable(gamewindow_test EXCLUDE_FROM_ALL src/driving/devtools/GameWindowTest.cpp
                 src/driving/platform/GameWindow.cpp src/driving/platform/HostWin32.cpp)
  target_link_libraries(gamewindow_test PRIVATE winmm shell32)
else()
  add_executable(gamewindow_test EXCLUDE_FROM_ALL src/driving/devtools/GameWindowTest.cpp
                 src/driving/platform/GameWindow.cpp src/driving/platform/HostPosix.cpp)
  target_link_libraries(gamewindow_test PRIVATE Threads::Threads)
endif()
nf_game_window(gamewindow_test)
if(WIN32)
set(NF_STANDALONE_SRC ${CMAKE_BINARY_DIR}/standalone.dir)
add_custom_target(standalone_poison
                  COMMAND ${Python3_EXECUTABLE} tools/standalone_poison.py ${NF_STANDALONE_SRC}
                  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                  COMMENT "Mirroring src/ with the XBE's addresses poisoned (tools/standalone_poison.py)")


# The shader check without the game (src/common/gfx/devtools/shaderCheck.h): the bytecode translators against
# the HLSL they replaced, over shader logs and the XBEs' vertex programs. "shadercheck -v disc/Driving.xbe ...".
add_executable(shadercheck EXCLUDE_FROM_ALL
  src/common/gfx/devtools/shaderCheckMain.cpp
  src/common/gfx/devtools/shaderCheck.cpp
  src/common/gfx/devtools/shaderHlsl.cpp
  src/common/gfx/shaderBuilder.cpp
  src/common/gfx/shaderSpirv.cpp
  src/common/gfx/nv2aVertexShader.cpp
  src/common/gfx/nv2aPixelShader.cpp
)
target_link_libraries(shadercheck PRIVATE d3d9 d3dcompiler user32)
endif()   # WIN32: gamewindow_test, standalone_poison, shadercheck

# A 64-bit configuration (the windows-x64 preset) is for the standalone driving engine alone: the loaders and the
# injection DLLs share the 32-bit XBE's address space by construction (docs/driving/64-bit.md).
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  foreach(target action actioninject driving drivinginject)
    if(TARGET ${target})
      set_target_properties(${target} PROPERTIES EXCLUDE_FROM_ALL ON EXCLUDE_FROM_DEFAULT_BUILD ON)
    endif()
  endforeach()
endif()
