#ifndef DRIVING_DEVTOOLS_CODERETARGET_H_
#define DRIVING_DEVTOOLS_CODERETARGET_H_

#include <stddef.h>
#include <stdint.h>

// Moves every way into some of this module's functions to wrappers, for the test modes that stand in front of
// game code from outside (HeapPad.cpp, X87Count.cpp). Two kinds of reference are moved:
//
//   - rel32 calls, jumps and conditional jumps in the module's code (a byte pattern, as SndCallTrace.cpp's: an
//     E8/E9 inside another instruction would have to hold the exact displacement of one of the targets);
//   - absolute addresses the base relocations list, in code (push/mov of a function's address) and in data
//     (function tables, stored callbacks, the injection table's entries), except those inside 'keep' - the
//     caller's own table of the real addresses, through which its wrappers call.
//
// Targets that the linker folded into one function share a wrapper (the first one given). 'sites' (one per
// target, may be NULL) counts what was moved. Call before any game thread exists.
struct RetargetTarget {
    uintptr_t from;   // the function
    uintptr_t to;     // its wrapper
    int sites;        // moved references
};

int Retarget(RetargetTarget *targets, int count, const void *keep, size_t keepBytes);

#endif // DRIVING_DEVTOOLS_CODERETARGET_H_
