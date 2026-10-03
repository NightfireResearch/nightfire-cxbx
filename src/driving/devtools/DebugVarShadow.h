#ifndef DRIVING_DEVTOOLS_DEBUGVARSHADOW_H_
#define DRIVING_DEVTOOLS_DEBUGVARSHADOW_H_

// NIGHTFIRE_DBVARSHADOW=1: the debug variables, the tuning files, the ini reader and the C++ library's string
// streams and numeric facets (src/driving/data/DebugVariables.cpp, Tuning.cpp, IniFiles.cpp, StdStreams.cpp,
// CoordConvert.cpp) against their originals, on the disc's tuning and ini files and on synthetic inputs. Needs the
// game's heap and C runtime: call it after Bond_StartUpSystem; the parts that open files through the game's file
// system also need a mission archive mounted. See DebugVarShadow.cpp.
void DebugVarShadow_Run(void);

#endif // DRIVING_DEVTOOLS_DEBUGVARSHADOW_H_
