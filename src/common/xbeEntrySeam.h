#ifndef COMMON_XBEENTRYSEAM_H_
#define COMMON_XBEENTRYSEAM_H_

// ---------------------------------------------------------------------------------------------------------------
// Standing in front of a library that is statically linked into the XBE.
//
// Both of the driving engine's big seams work the same way: take a library the game calls - D3D8, DirectSound -
// and replace it at its own entry points, so that the game's code above it runs exactly as built. That suits a
// codebase like EAGL, which is most of the binary and largely unnamed: there is no thin layer above the
// library to reimplement instead, and the library boundary is a real interface with known semantics.
//
// What this file provides is the part both seams need and neither should own: the table, the patching, and a
// stub for every entry point that has no implementation yet. The stub is what makes bring-up incremental -
// it reports itself once, cleans up the caller's stack and returns zero, so one run names everything the game
// actually reaches rather than stopping at the first gap.
//
// The tables themselves are generated from the binary by tools/xbe_entry_points.py, which reads each entry
// point's address and the number of argument bytes it pops. That number is what a stub needs in order to
// return without corrupting the caller's stack, and it cannot be guessed: these are __stdcall functions and
// the callee does the cleaning.
// ---------------------------------------------------------------------------------------------------------------

struct XbeEntry {
    const char *name;
    unsigned    address;
    unsigned    stackBytes;   // what the original pops on return; XBE_ENTRY_STACK_UNKNOWN if it could not be read
    void       *replacement;  // null until one is registered
    unsigned    calls;        // stub calls only, for the report
    bool        reported;
};

#define XBE_ENTRY_STACK_UNKNOWN 0xffffffffu

// One library's worth of entry points. Built from a generated .inc with the XBE_ENTRY macros, for example:
//
//     static XbeEntry g_entries[] = {
//     #define XBE_ENTRY(name, address, stack)        { #name, address, stack, 0, 0, false },
//     #define XBE_ENTRY_UNKNOWN_STACK(name, address) { #name, address, XBE_ENTRY_STACK_UNKNOWN, 0, 0, false },
//     #include "d3d8Entries.inc"
//     #undef XBE_ENTRY
//     #undef XBE_ENTRY_UNKNOWN_STACK
//     };
struct XbeEntrySeam {
    XbeEntry   *entries;
    unsigned    count;
    const char *tag;          // what its messages are prefixed with, "d3dSeam" or the like
};

// Attaches a replacement to every entry point of that name, and says how many it found. Names rather than
// addresses, because the addresses are in the generated table and a name that is not in it is a mistake worth
// hearing about rather than a patch that silently lands nowhere. A few names appear twice in a binary (an
// inlined copy beside the library one), and patching both is what the caller means.
//
// stackBytes is how many bytes of arguments the replacement pops - sizeof its parameters for a __stdcall
// function, 0 for a naked one that takes its arguments in registers. It is checked against the generated
// table, and a mismatch is reported, because getting it wrong is silent and lethal: a replacement that pops
// twelve bytes where the original popped four unwinds the caller's frame and returns into whatever was on
// the stack. That is not a hypothetical - it cost an afternoon, arriving as a jump into the middle of a
// vertex buffer two calls later.
int XbeSeam_Replace(XbeEntrySeam *seam, const char *name, void *replacement, unsigned stackBytes);

// Patches every entry point: the ones with a replacement to it, the rest to a stub. Call once, after the
// replacements are registered.
void XbeSeam_Install(XbeEntrySeam *seam);

// Which stubbed entry points have been reached, and how often. Nothing is printed if none have.
void XbeSeam_ReportMissing(XbeEntrySeam *seam);

#endif // COMMON_XBEENTRYSEAM_H_
