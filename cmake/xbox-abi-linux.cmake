# The game's code on a 32-bit Linux host keeps the ABI it has on Windows (docs/driving/portable.md, "The two ABIs").
#
# Its layouts and conventions are MSVC's: methods are __thiscall (`this` in ECX, the callee pops), and the code
# relies on it - game vtables called through __fastcall pointers with a dummy EDX, register adaptors, the checks
# against the XBE's conventions; doubles and 64-bit integers are 8-aligned in structures, where the System V i386
# ABI aligns them to 4; bitfields are laid out MSVC's way. Clang gives all of that for the MinGW target, and the
# "-elf" object format makes it emit ELF objects that link into a Linux executable like any other. So the game's
# sources are compiled for i686-pc-windows-gnu-elf with Linux's headers and Linux's predefined macros in place of
# Windows', and the sources that talk to Linux libraries (the host layer, SDL, FFmpeg, dxvk's D3D9) are compiled
# for Linux. Between the two sides only free functions are called, with no structure that lays out differently.
#
# Sets NF_XBOX_ABI_OPTIONS, the compile options of a source on the game's side.

# Linux's own include directories for this compiler in -m32, in order
execute_process(COMMAND ${CMAKE_CXX_COMPILER} -m32 -x c++ -E -v -
                INPUT_FILE /dev/null OUTPUT_QUIET ERROR_VARIABLE nf_search RESULT_VARIABLE nf_result)
if(NOT nf_result EQUAL 0)
  message(FATAL_ERROR "${CMAKE_CXX_COMPILER} -m32 does not run: ${nf_search}")
endif()
string(REGEX MATCH "#include <\\.\\.\\.> search starts here:\n(.*)\nEnd of search list" nf_search "${nf_search}")
string(REGEX REPLACE "\n +" ";" nf_search "${CMAKE_MATCH_1}")
string(STRIP "${nf_search}" nf_search)

# No stack probes: the MinGW target calls _alloca (__chkstk) for large frames, and Linux stacks need none.
set(NF_XBOX_ABI_OPTIONS --target=i686-pc-windows-gnu-elf -fno-short-wchar -mno-stack-arg-probe -nostdinc
                        -isystem ${CMAKE_SOURCE_DIR}/src/common/linux)   # MSVC's <intrin.h>, then Linux's headers
foreach(directory ${nf_search})
  string(STRIP "${directory}" directory)
  if(directory)
    list(APPEND NF_XBOX_ABI_OPTIONS -isystem ${directory})
  endif()
endforeach()
foreach(macro _WIN32 WIN32 __WIN32 __WIN32__ WINNT __WINNT __WINNT__ _X86_ __MINGW32__ __MSVCRT__)
  list(APPEND NF_XBOX_ABI_OPTIONS -U${macro})
endforeach()
list(APPEND NF_XBOX_ABI_OPTIONS -D__linux__ -D__linux -D__gnu_linux__ -D__unix__ -D__unix -D_GNU_SOURCE)
