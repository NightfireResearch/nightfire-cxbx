# ---------------------------------------------------------------------------------------------------------------
# Building the Windows binaries from macOS (or any other Unix) with clang and the mingw-w64 sysroot.
#
# The game is 32-bit x86 and the loader has to share its address space, so this targets i686 and cannot
# target anything else. See docs/macos-build.md for what to install and what the resulting binaries can and
# cannot do.
#
# clang rather than mingw-w64's own GCC, and this is not a preference: the reimplemented functions use
# __declspec(naked) and MSVC's __asm { } blocks at a dozen sites, and GCC supports neither on x86. clang
# accepts both with -fms-extensions -fasm-blocks.
#
# Note the absence of -fms-compatibility. It looks like the obvious companion to -fms-extensions and is a
# trap: it defines _MSC_VER, which makes mingw-w64's own headers take their MSVC paths and collapse.
#
#   cmake -B build/macos -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-macos-mingw.cmake
#   cmake --build build/macos
# ---------------------------------------------------------------------------------------------------------------

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86)

set(NF_MINGW_TRIPLE i686-w64-mingw32)

# The sysroot. Homebrew's mingw-w64 keeps the two word sizes in separate trees, hence toolchain-i686. A
# different installation can be pointed at with -DNF_MINGW_ROOT=...
if(NOT NF_MINGW_ROOT)
  if(DEFINED ENV{NF_MINGW_ROOT})
    set(NF_MINGW_ROOT "$ENV{NF_MINGW_ROOT}")
  else()
    execute_process(COMMAND brew --prefix mingw-w64
                    OUTPUT_VARIABLE NF_BREW_MINGW OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET RESULT_VARIABLE NF_BREW_RESULT)
    if(NF_BREW_RESULT EQUAL 0 AND EXISTS "${NF_BREW_MINGW}/toolchain-i686")
      set(NF_MINGW_ROOT "${NF_BREW_MINGW}/toolchain-i686")
    endif()
  endif()
endif()

if(NOT NF_MINGW_ROOT OR NOT EXISTS "${NF_MINGW_ROOT}/${NF_MINGW_TRIPLE}/include/windows.h")
  message(FATAL_ERROR
    "Could not find a 32-bit mingw-w64 sysroot.\n"
    "On macOS: brew install mingw-w64\n"
    "Otherwise pass -DNF_MINGW_ROOT=/path/to/sysroot (the directory holding ${NF_MINGW_TRIPLE}/include).")
endif()

set(CMAKE_C_COMPILER   clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET   ${NF_MINGW_TRIPLE})
set(CMAKE_CXX_COMPILER_TARGET ${NF_MINGW_TRIPLE})

# windres for the .rc. clang has its own resource compiler, but windres is what the sysroot ships with.
find_program(NF_WINDRES ${NF_MINGW_TRIPLE}-windres REQUIRED)
set(CMAKE_RC_COMPILER ${NF_WINDRES})

# -fms-extensions  : __declspec(naked), and the MSVC spelling of other extensions the sources use
# -fasm-blocks     : MSVC's __asm { } Intel-syntax blocks
# -fno-exceptions  : nothing here throws, and it keeps the unwinder out of the binaries entirely
set(NF_MINGW_FLAGS "--sysroot=${NF_MINGW_ROOT} -B${NF_MINGW_ROOT}/bin -fms-extensions -fasm-blocks -fno-exceptions")
set(CMAKE_C_FLAGS_INIT   "${NF_MINGW_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${NF_MINGW_FLAGS}")

set(CMAKE_FIND_ROOT_PATH ${NF_MINGW_ROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
