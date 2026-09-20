cmake_minimum_required(VERSION 3.5)

project(nightfiRE LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# On MinGW, "lib" will get prepended to libraries which will break out ability
# to inject our DLL.
set(CMAKE_SHARED_LIBRARY_PREFIX "")
set(CMAKE_STATIC_LIBRARY_PREFIX "")
set(CMAKE_IMPORT_LIBRARY_PREFIX "")

# Statically link UCRT, improves ability to run on XP when compiled with v141_xp
#set(CMAKE_CXX_FLAGS_RELEASE "/MT")
#set(CMAKE_CXX_FLAGS_DEBUG "/MTd")

set(LAUNCHWRAPPER
  # Must be kept at the top to ensure correct memory mapping
  #src/memory.cpp

  src/main.cpp
  #src/memory.h
  src/res/res.rc
  src/wndproc.cpp
  src/wndproc.h

  src/cxbx/cxbxbinding.cpp
  src/cxbx/cxbxbinding.h
)

# The CXBX-hosted launchers. Both carry the suffix, including driving_cxbx, which has no standalone
# counterpart yet: the point of the name is to say that it still needs cxbx-reloaded, which a plain
# "driving.exe" beside a standalone "action.exe" would quietly deny. action_cxbx is kept as the reference to
# bisect the standalone path against; both go when CXBX does. See docs/driving-engine-plan.md.
add_executable(action_cxbx WIN32 ${LAUNCHWRAPPER})
add_executable(driving_cxbx WIN32 ${LAUNCHWRAPPER})

target_link_libraries(action_cxbx PRIVATE
  shlwapi
)

target_link_libraries(driving_cxbx PRIVATE
  shlwapi
)

# The Xbox / PS2 implementations were of another era, one where memory safety was apparently a foreign concept to game developers
# We'll use the original unsafe versions, and tell the compiler to stop nagging us, because YOLO
add_definitions(-D_CRT_SECURE_NO_WARNINGS)

# We aren't currently loading the XBE on our end, so this is unnecessary
#target_link_options(action_cxbx PRIVATE
#  /INCREMENTAL:NO
#  /BASE:0x10000
#  /FIXED
#  /DYNAMICBASE:NO
#  /SAFESEH:NO
#)

add_library(actioninject SHARED
  src/dllmain.cpp
  src/inject_action.cpp
  src/inject.h
  src/action/game.cpp
  src/action/main.cpp
  src/common/launchInfo.cpp
  src/action/gfx/Sprite.cpp
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
  src/action/engine/mouseLook.cpp
  src/action/engine/psiInput.cpp
  src/action/engine/psiLight.cpp
  src/action/engine/psiSave.cpp
  src/action/engine/Script.cpp
  src/action/engine/Text.cpp
  src/action/engine/Vision.cpp
  src/action/engine/Woman.cpp
  src/action/engine/XboxSettings.cpp
  src/action/engine/XboxPaths.cpp
  src/action/engine/XboxFile.cpp
  src/action/engine/XboxStartup.cpp
  src/action/engine/Direct3D/d3dhelpers.cpp
  src/action/engine/Direct3D/d3dSeam.cpp
  src/action/engine/Direct3D/d3d9Backend.cpp
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
  src/action/ui/ui_score.cpp
  src/action/ui/Menu.cpp
  src/action/autofunc.cpp
  src/action/game/Upgrade.cpp
  src/action/game/view.cpp
  src/action/game/drone/Drone.cpp
  src/action/game/drone/NDrone2.cpp
  src/action/game/drone/NDrone2_Hostage.cpp
  src/action/game/mp/multiplayer.cpp
  src/action/game/obj/Break.cpp
  src/action/game/obj/Door.cpp
  src/action/game/obj/build.cpp
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
  src/action/sound/xadpcm.cpp
  src/action/sound/xaudio2Backend.cpp
  src/action/sound/dsndStream.cpp
  src/action/sound/music.cpp
  src/action/sound/SFX.cpp
  src/action/sound/Sound.cpp
)

# The seams' native backends (Direct3D/d3d9Backend.cpp, sound/xaudio2Backend.cpp)
target_link_libraries(actioninject PRIVATE d3d9 d3dcompiler winmm xaudio2)

# The standalone loader (stage B 4.2/4.3): maps the XBE itself and runs it in-process, with no emulator in
# the process at all. This is "action.exe" - the canonical way to run the action engine - and the CXBX
# launcher above is the one that now carries a suffix. Linked with /FIXED and no ASLR because the XBE is full
# of absolute addresses and has to land at its own base; see src/loader/loadermain.cpp.
add_executable(action
  # reserve.cpp must stay first: its array has to be the first thing in .text so that the XBE, copied over
  # the start of the image, lands on the array rather than on the loader's own code. Xbe_Map checks this.
  src/loader/reserve.cpp
  src/loader/loadermain.cpp
  src/loader/xbe.cpp
  src/loader/kernel.cpp
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
target_link_options(action PRIVATE
  /BASE:0x10000 /FIXED /DYNAMICBASE:NO /SAFESEH:NO /INCREMENTAL:NO)

# Symbol maps for both binaries that appear in crash backtraces.
#
# When a fault escapes the loader's own handler - which happens if the corruption is bad enough that the
# exception cannot be dispatched at all - the host prints raw module offsets instead, as Wine did with
# "actioninject (+0x16643)". Without a map that offset names nothing. The maps are written beside the
# binaries, cost nothing at runtime and are not shipped in the executable, so there is no reason not to.
target_link_options(action PRIVATE /MAP)
target_link_options(actioninject PRIVATE /MAP)

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
  src/res/res.rc
)
target_compile_definitions(driving PRIVATE IS_DRIVING)
target_link_options(driving PRIVATE
  /BASE:0x10000 /FIXED /DYNAMICBASE:NO /SAFESEH:NO /INCREMENTAL:NO /MAP)

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
  src/driving/engine/FileNameList.cpp
  src/driving/engine/UFileLoader.cpp
  src/driving/platform/XboxStartup.cpp
  src/driving/platform/XboxTimer.cpp
)


# winmm: the game's tick now comes from Windows' multimedia timer (src/driving/platform/XboxTimer.cpp).
target_link_libraries(drivinginject PRIVATE winmm)

target_compile_definitions(action_cxbx PRIVATE UNICODE _UNICODE IS_ACTION)
target_compile_definitions(driving_cxbx PRIVATE UNICODE _UNICODE IS_DRIVING)
target_compile_definitions(actioninject PRIVATE UNICODE _UNICODE)
target_compile_definitions(drivinginject PRIVATE UNICODE _UNICODE)


# Run preprocess.py before building actioninject and drivinginject
add_custom_command(TARGET actioninject PRE_BUILD
                   COMMAND python tools/preprocess.py
                   WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
