# Writes the build's version into a source file, NightfireVersion (src/common/version.h), run with cmake -P at
# configure time and before every build:
#
#   cmake -DSOURCE_DIR=<checkout> -DOUTPUT=<file.cpp> [-DVERSION=<x.y.z>] -P cmake/version.cmake
#
# VERSION (the NIGHTFIRE_VERSION cache variable, which the release job sets from its tag) wins. Otherwise it comes
# from git describe: a tag vX.Y.Z on HEAD gives "X.Y.Z"; commits past it give "X.Y.Z+N.gHASH", build metadata, so
# that the build compares equal to its tag rather than below it as a pre-release would; no tag gives
# "0.0.0+gHASH"; uncommitted changes add ".dirty"; no git at all gives "0.0.0+unknown". The file is rewritten only
# when the text changes, so an unchanged version recompiles nothing.

if(NOT VERSION)
  find_package(Git QUIET)
  set(describe "")
  if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags --match "v[0-9]*" --long --dirty --always
                    WORKING_DIRECTORY ${SOURCE_DIR} OUTPUT_VARIABLE describe ERROR_QUIET
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
  endif()
  set(dirty "")
  if(describe MATCHES "-dirty$")
    set(dirty ".dirty")
    string(REGEX REPLACE "-dirty$" "" describe "${describe}")
  endif()
  if(describe MATCHES "^v([0-9]+\\.[0-9]+\\.[0-9]+(-[0-9A-Za-z.-]+)?)-([0-9]+)-g([0-9a-f]+)$")
    set(base ${CMAKE_MATCH_1})
    set(count ${CMAKE_MATCH_3})
    set(hash ${CMAKE_MATCH_4})
    if(count EQUAL 0 AND NOT dirty)
      set(VERSION ${base})
    else()
      set(VERSION "${base}+${count}.g${hash}${dirty}")
    endif()
  elseif(describe MATCHES "^[0-9a-f]+$")
    set(VERSION "0.0.0+g${describe}${dirty}")
  else()
    set(VERSION "0.0.0+unknown")
  endif()
endif()

set(text "// Written by cmake/version.cmake: the build's version (src/common/version.h)\n\nextern const char NightfireVersion[] = \"${VERSION}\";\n")
if(EXISTS ${OUTPUT})
  file(READ ${OUTPUT} old)
else()
  set(old "")
endif()
if(NOT old STREQUAL text)
  file(WRITE ${OUTPUT} "${text}")
endif()
