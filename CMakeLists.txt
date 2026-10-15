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

# On MinGW, "lib" will get prepended to libraries which will break out ability
# to inject our DLL.
set(CMAKE_SHARED_LIBRARY_PREFIX "")
set(CMAKE_STATIC_LIBRARY_PREFIX "")
set(CMAKE_IMPORT_LIBRARY_PREFIX "")

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

# Statically link UCRT, improves ability to run on XP when compiled with v141_xp
#set(CMAKE_CXX_FLAGS_RELEASE "/MT")
#set(CMAKE_CXX_FLAGS_DEBUG "/MTd")

# The Xbox / PS2 implementations were of another era, one where memory safety was apparently a foreign concept to game developers
# We'll use the original unsafe versions, and tell the compiler to stop nagging us, because YOLO
add_definitions(-D_CRT_SECURE_NO_WARNINGS)

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
  src/action/devtools/UpgradeShadow.cpp
  src/action/devtools/MemShadow.cpp
  src/action/devtools/ScoreShadow.cpp
  src/action/devtools/DroneTablesCheck.cpp
  src/action/devtools/DroneShadow.cpp
  src/action/devtools/WeaponTableShadow.cpp
  src/action/devtools/MatrixShadow.cpp
  src/action/devtools/CameraShadow.cpp
  src/action/devtools/Teleport.cpp
  src/action/game.cpp
  src/action/main.cpp
  src/common/launchInfo.cpp
  src/action/gfx/Sprite.cpp
  src/action/gfx/LowLevel.cpp
  src/action/engine/AINetwork.cpp
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
  src/common/gfx/devtools/shaderCheck.cpp
  src/common/gfx/devtools/shaderHlsl.cpp
  src/common/gfx/fxaa.cpp
  src/common/gfx/textureReplace.cpp
  src/action/game/Autoaim.cpp
  src/action/game/weapon_stats.cpp
  src/action/math/math.cpp
  src/action/input.cpp
  src/action/memory.cpp
  src/action/util/bin.cpp
  src/action/util/crc.cpp
  src/action/util/hashtable.cpp
  src/action/util/LList.cpp
  src/action/util/Random.cpp
  src/action/util/Stack.cpp
  src/action/ui/HUD.cpp
  src/action/ui/ui.cpp
  src/action/ui/ui_dossier.cpp
  src/action/ui/ui_mp.cpp
  src/action/ui/ui_credits.cpp
  src/action/ui/ui_secrets.cpp
  src/action/ui/ui_nightfire.cpp
  src/action/ui/ui_codenames.cpp
  src/action/ui/ui_cheats.cpp
  src/action/ui/MenuUnlocks.cpp
  src/action/ui/ui_score.cpp
  src/action/ui/Menu.cpp
  src/action/autofunc.cpp
  src/action/game/Upgrade.cpp
  src/action/game/view.cpp
  src/action/game/drone/BOT.cpp
  src/action/game/drone/Behaviour.cpp
  src/action/game/drone/Drone.cpp
  src/action/game/drone/DroneSM.cpp
  src/action/game/drone/NDrone2.cpp
  src/action/game/drone/NDrone2_Hostage.cpp
  src/action/game/mp/multiplayer.cpp
  src/action/game/obj/Break.cpp
  src/action/game/obj/Door.cpp
  src/action/game/obj/Hint.cpp
  src/action/game/obj/build.cpp
  src/action/game/obj/bullet.cpp
  src/action/game/obj/car.cpp
  src/action/game/obj/control.cpp
  src/action/game/obj/Copter.cpp
  src/action/game/obj/Gas.cpp
  src/action/game/obj/Grapple.cpp
  src/action/game/obj/GT.cpp
  src/action/game/obj/GunImp.cpp
  src/action/game/obj/Ladder.cpp
  src/action/game/obj/Light.cpp
  src/action/game/obj/PCQWorm.cpp
  src/action/game/obj/Player.cpp
  src/action/game/obj/Rotor.cpp
  src/action/game/obj/ScriptPlayer.cpp
  src/action/game/obj/Searchlight.cpp
  src/action/game/obj/Sensor.cpp
  src/action/game/obj/SpaceLaser.cpp
  src/action/game/obj/SpaceMissile.cpp
  src/action/game/obj/Switch.cpp
  src/action/game/sp/Locks.cpp
  src/action/game/sp/Mission.cpp
  src/action/game/sp/PlayerStats.cpp
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

add_library(drivinginject SHARED
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
  src/common/gfx/d3d9Backend.cpp
  src/common/gfx/nv2aPixelShader.cpp
  src/common/gfx/nv2aVertexShader.cpp
  src/common/gfx/shaderBuilder.cpp
  src/common/gfx/devtools/shaderCheck.cpp
  src/common/gfx/devtools/shaderHlsl.cpp
  src/common/gfx/fxaa.cpp
  src/common/gfx/textureReplace.cpp
)


# winmm for the multimedia timer the game's tick now comes from (src/driving/platform/XboxTimer.cpp);
# d3d9 for the shared graphics backend the D3D8 seam dispatches to; d3dcompiler for its shader check
# (src/common/gfx/devtools/shaderCheck.cpp).
# XAUDIO2_LIB is xaudio2 or xaudio2_9 - see actioninject above.
target_link_libraries(drivinginject PRIVATE d3d9 d3dcompiler winmm xinput9_1_0 ${XAUDIO2_LIB} shell32)

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
    FetchContent_Declare(SDL3
                         GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
                         GIT_TAG release-3.2.30
                         GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(SDL3)
  endif()
  target_sources(drivinginject PRIVATE
    src/driving/platform/HostWindowSdl.cpp
    src/driving/platform/HostInputSdl.cpp
    src/driving/sound/sdlAudioDriving.cpp
    src/common/fmv/FmvAudioSdl.cpp)
  target_compile_definitions(drivinginject PRIVATE NIGHTFIRE_SDL)
  target_link_libraries(drivinginject PRIVATE SDL3::SDL3)
  add_custom_command(TARGET drivinginject POST_BUILD
                     COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:drivinginject>)
else()
  target_sources(drivinginject PRIVATE
    src/driving/platform/HostInputWin32.cpp
    src/driving/sound/xaudio2Driving.cpp
    src/common/fmv/FmvAudio.cpp)
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
target_compile_definitions(drivinginject PRIVATE NF_ANTIALIASING)


# FMV playback through FFmpeg (src/action/engine/Fmv.cpp, src/driving/engine/PlayMPC.cpp, src/common/fmv): the DLLs
# and headers in third_party/ffmpeg-prebuilt, built from the third_party/ffmpeg submodule by tools/fmv/build_ffmpeg.sh
# and committed. The DLLs are loaded at run time and copied beside the executables with FFmpeg's licence and build
# note, so the build output is complete.
set(NF_FFMPEG_DIR ${CMAKE_SOURCE_DIR}/third_party/ffmpeg-prebuilt CACHE PATH "FFmpeg DLLs and headers")
if(NOT EXISTS ${NF_FFMPEG_DIR}/include/libavformat/avformat.h)
  message(FATAL_ERROR "No FFmpeg in ${NF_FFMPEG_DIR}: it is committed in third_party/ffmpeg-prebuilt, "
                      "and tools/fmv/build_ffmpeg.sh rebuilds it")
endif()
file(GLOB NF_FFMPEG_FILES ${NF_FFMPEG_DIR}/bin/*.dll ${NF_FFMPEG_DIR}/FFmpeg-*.txt)
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
add_dependencies(actioninject preprocess)
add_dependencies(drivinginject preprocess)

# The standalone driving engine (docs/driving/standalone.md): our code alone, with no Driving.xbe mapped or read, no
# injection and no shadow tests - drivinginject's sources less the devtools, the injection and the XBE's patching
# (each seam's and the start-up's patching is #ifndef NIGHTFIRE_STANDALONE). driving_standalone has none of the
# devtools (standalone/NoDevtools.cpp and StandaloneHooks.cpp answer the hooks game code calls);
# driving_standalone_test links the lockstep and dump harness (NIGHTFIRE_LOCKSTEP, DUMP_TICKS, SNDTRACE, SNDLOCKSTEP,
# the teleports) and the crash test (NIGHTFIRE_CRASHTEST), so that a standalone run can be compared with the injected
# build's baselines. Both have the crash reporter (standalone/CrashReport.h).
# tools/standalone_poison.py (the standalone_poison target) mirrors src/ with every Driving.xbe address still in the
# worklist poisoned, to list what would stop this compiling.
get_target_property(nf_driving_sources drivinginject SOURCES)
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
foreach(source ${nf_driving_sources})
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
add_executable(driving_standalone ${nf_standalone_sources} src/driving/standalone/NoDevtools.cpp)
add_executable(driving_standalone_test EXCLUDE_FROM_ALL ${nf_standalone_sources} ${nf_standalone_harness})
target_compile_definitions(driving_standalone_test PRIVATE NIGHTFIRE_STANDALONE_TEST)
foreach(target driving_standalone driving_standalone_test)
  add_dependencies(${target} preprocess)
  target_include_directories(${target} PRIVATE ${NF_FFMPEG_DIR}/include)
  target_compile_definitions(${target} PRIVATE UNICODE _UNICODE NF_ANTIALIASING NIGHTFIRE_STANDALONE)
  target_link_libraries(${target} PRIVATE d3d9 winmm xinput9_1_0 ${XAUDIO2_LIB} shell32)
  if(NIGHTFIRE_SDL)
    target_compile_definitions(${target} PRIVATE NIGHTFIRE_SDL)
    target_link_libraries(${target} PRIVATE SDL3::SDL3)
  endif()
  if(MSVC)
    target_link_options(${target} PRIVATE /MAP)
  else()
    target_link_options(${target} PRIVATE -Wl,-Map,${target}.map)
  endif()
endforeach()
# The driving engine's x87 code (docs/driving/64-bit.md 4.1, docs/driving/portable.md). On: the 43 functions
# whose results come from FSIN, FCOS, FPTAN, FPATAN and the like stay the original instructions, so the injected
# build's shadow tests and the lockstep baselines (lbase) stay exact. Off: each is C in double (<math.h>), as any
# host without an x87 needs, and the particle systems are ordered by a number given to each emitter rather than by
# its address (docs/driving/original-quirks.md); the baselines of that build are lbase_c. The injected build's
# shadows of these functions differ with it off.
option(NIGHTFIRE_X87 "Driving engine: keep the original x87 instructions (off: portable C)" ON)
foreach(target drivinginject driving_standalone driving_standalone_test)
  if(NIGHTFIRE_X87)
    target_compile_definitions(${target} PRIVATE NIGHTFIRE_X87=1)
  else()
    target_compile_definitions(${target} PRIVATE NIGHTFIRE_X87=0)
  endif()
endforeach()
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
  src/common/gfx/nv2aVertexShader.cpp
  src/common/gfx/nv2aPixelShader.cpp
)
target_link_libraries(shadercheck PRIVATE d3d9 d3dcompiler user32)
