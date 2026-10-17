# The arm64 compile probe (docs/driving/macos.md, package M2): the standalone driving engine compiled for
# aarch64-linux-gnu with Clang in the nf-cross-arm64 image (tools/arm64_probe.sh), to find what does not compile for
# arm64 and to count fused multiply-adds in the objects before the Mac. Compile only: nothing links (no arm64
# dxvk-native).
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_C_FLAGS_INIT "-fPIE")
set(CMAKE_CXX_FLAGS_INIT "-fPIE -fms-extensions -fno-exceptions -Wno-invalid-offsetof -Wno-ignored-attributes")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld")
set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
