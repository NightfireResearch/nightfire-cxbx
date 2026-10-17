# A native 32-bit x86 Linux build with Clang (docs/driving/portable.md): the standalone driving engine on SDL3 and
# dxvk-native. 32-bit because the game's structures and its x87 code are; Clang because the x87 helpers are MSVC
# __asm blocks, which Clang accepts with -fms-extensions and GCC does not.
#
# Works on an x86-64 distribution with the i386 multiarch libraries (Debian and Ubuntu: dpkg --add-architecture
# i386, then gcc-multilib, g++-multilib and the :i386 packages docs/driving/portable.md lists).
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

# SSE2 for float and double arithmetic, as MSVC's /arch:SSE2 (its default) does it: the ports' floating point is
# exact against the MSVC build. The game's sources add the options of cmake/xbox-abi-linux.cmake.
# The game's code keeps 4-byte stack alignment, as on Windows, where Linux code assumes 16 and may keep SSE values on
# the stack: C code (SDL, built here) realigns its stack on entry, and so does the host library
# (cmake/xbox-abi-linux.cmake).
set(CMAKE_C_FLAGS_INIT "-m32 -msse2 -mfpmath=sse -mstackrealign")
set(CMAKE_CXX_FLAGS_INIT "-m32 -msse2 -mfpmath=sse -fms-extensions -fasm-blocks -fno-exceptions -Wno-invalid-offsetof")
# Not position-independent: the game's objects are built for a target without PIC (cmake/xbox-abi-linux.cmake).
set(CMAKE_EXE_LINKER_FLAGS_INIT "-m32 -no-pie")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-m32")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-m32")

# pkg-config (SDL's optional dependencies) answers for the i386 libraries
set(ENV{PKG_CONFIG_LIBDIR} "/usr/lib/i386-linux-gnu/pkgconfig:/usr/share/pkgconfig")
set(CMAKE_LIBRARY_ARCHITECTURE i386-linux-gnu)
