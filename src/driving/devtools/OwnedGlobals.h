#ifndef DRIVING_DEVTOOLS_OWNEDGLOBALS_H_
#define DRIVING_DEVTOOLS_OWNEDGLOBALS_H_

// ---------------------------------------------------------------------------------------------------------------
// Every driving-engine global we own beside the game's copy of it (OwnedGlobals.inc, written by
// tools/owned_globals.py from the XBE_GLOBAL tags), for the shadow tests.
//
// Once a global is ours nothing live updates the game's copy, but an original run by a shadow (its entry bytes
// swapped back in) still reads and writes it. The PORT's side is our variable, the ORIGINAL's side the game's copy.
//
//   OwnedGlobals_Install()       at injection (inject_driving.cpp), before anything has touched the game's copies:
//                                takes them as they are, untouched, and installs the window hook.
//   the window hook              every original window (XbeOriginal_Restore, XbeOriginal_RestoreRange,
//                                XbeOriginalScope - common/xbeOriginal.h) lends, as it opens, each owned global the
//                                originals it swaps in can reach (the table's touchers: a function naming an address
//                                in the global, or calling one of the game's we have not replaced that does), so an
//                                original starts from the live state. No shadow needs to do anything for it.
//                                A window is known by its range; the windows open together are one. A global's
//                                copy is left alone for good, for that window, once the shadow is seen to write it
//                                itself (it differs, at the first window, from before anything ran, or, at a later
//                                one, from what the last window over it left): an input the shadow prepares. Else
//                                it is lent at the first window, at the first after another shadow's window had it,
//                                and after that whenever our variable has moved (a sequence of originals, one window
//                                each, carries its own state forward otherwise).
//   the redirects                allocator state the originals use over the same memory as the port (EAGLAnim's
//                                Pool, FactoryBlock, ScratchBuffers: SHARED in the generator) is not lent: while
//                                any window is open, every original instruction naming it names ours instead, so
//                                there is one free list, as before it was ours. Two would hand out a block twice.
//   OwnedGlobals_LendAll()       every owned variable's bytes into the game's copy, unconditionally.
//   OwnedGlobals_Snapshot(), OwnedGlobals_Restore()
//                                both copies of every lent global, around each side of a comparison.
//   OwnedGlobals_Ours(), OwnedGlobals_Game(), OwnedGlobals_AsOurs(), OwnedGlobals_TranslateWords()
//                                an address in one copy for the same byte of the other. An original's result that
//                                holds the address of an owned global (a pointer field, an argument it passed)
//                                holds the game's copy where the port's holds ours: translate before comparing.
//
// Lent as they are: a pointer into one of our globals still points at ours in the game's copy, so an original
// following it reaches our variable. Pointer fields so compare equal on both sides, but an original writing through
// one writes our variable - snapshot both copies (OwnedGlobals_Snapshot) when that matters.
// Not in the table (python tools/owned_globals.py --list): statics inside functions, file statics and anonymous-
// namespace globals no header exports. Constant tables are there for translation only, never lent.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

struct OwnedGlobal {
    uint32_t game;          // the game's copy
    uint32_t size;
    bool lend;              // false: constant (or never to be touched), in the table for translation only
    void *ours;
};

extern const OwnedGlobal kOwnedGlobals[];
extern const uint32_t kOwnedGlobalCount;

void OwnedGlobals_Install();
void OwnedGlobals_LendAll();

struct OwnedGlobalsSnapshot;
OwnedGlobalsSnapshot *OwnedGlobals_Snapshot();
void OwnedGlobals_Restore(const OwnedGlobalsSnapshot *snapshot);
void OwnedGlobals_Free(OwnedGlobalsSnapshot *snapshot);

void *OwnedGlobals_Ours(uint32_t game);                       // NULL: not in an owned global's game copy
uint32_t OwnedGlobals_Game(const void *ours);                 // 0: not in one of our owned variables
uint32_t OwnedGlobals_AsOurs(uint32_t word);                  // the word unless it is an address in a game's copy
void OwnedGlobals_TranslateWords(void *bytes, size_t size);   // AsOurs on each aligned word, in place

// Not an owned global, but the same kind of difference: the original passes the game's zero vector (0x00243030)
// where a port passes a kZero of its own. A vector argument's word for a call log: 0x00243030 for a zero Coord3 in
// our image, else its address.
uint32_t OwnedGlobals_VectorWord(const void *vector);

#endif  // DRIVING_DEVTOOLS_OWNEDGLOBALS_H_
