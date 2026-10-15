# A native x86-64 Linux build with Clang (docs/driving/64-bit.md 7.11): the standalone driving engine on SDL3 and
# dxvk-native, as on 64-bit Windows. One ABI on x86-64, so the game's sources compile as ordinary Linux code.
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

# Position-independent code linked at a fixed address (-no-pie): the executable lies in the game window
# (CMakeLists.txt), whose base may be above 4 GB, where 32-bit absolute addresses do not reach.
set(CMAKE_C_FLAGS_INIT "-fPIE")
set(CMAKE_CXX_FLAGS_INIT "-fPIE -fms-extensions -fno-exceptions -Wno-invalid-offsetof -Wno-ignored-attributes")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld -no-pie")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-fuse-ld=lld")
