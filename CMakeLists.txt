cmake_minimum_required(VERSION 3.5)

project(nightfiRE LANGUAGES CXX)

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

add_executable(action WIN32 ${LAUNCHWRAPPER})
add_executable(driving WIN32 ${LAUNCHWRAPPER})

target_link_libraries(action PRIVATE
  shlwapi
)

target_link_libraries(driving PRIVATE
  shlwapi
)

# We aren't currently loading the XBE on our end, so this is unnecessary
#target_link_options(action PRIVATE
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
  src/action/psiFile.cpp
  src/common/launchInfo.cpp
  src/action/math/math.cpp
  src/action/input.cpp
  src/action/memory.cpp
  src/action/util/crc.cpp
)

add_library(drivinginject SHARED
  src/dllmain.cpp
  src/inject_driving.cpp
  src/inject.h
  src/driving/logging.cpp
  src/common/launchInfo.cpp
  src/driving/memory.cpp
)


target_compile_definitions(action PRIVATE UNICODE _UNICODE IS_ACTION)
target_compile_definitions(driving PRIVATE UNICODE _UNICODE IS_DRIVING)
target_compile_definitions(actioninject PRIVATE UNICODE _UNICODE)
target_compile_definitions(drivinginject PRIVATE UNICODE _UNICODE)


# Run preprocess.py before building actioninject and drivinginject
add_custom_command(TARGET actioninject PRE_BUILD
                   COMMAND python tools/preprocess.py
                   WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

add_custom_command(TARGET drivinginject PRE_BUILD
                   COMMAND python tools/preprocess.py
                   WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(drivinginject PRIVATE UNICODE _UNICODE)
