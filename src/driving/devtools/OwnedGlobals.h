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
//                                Pool, FactoryBlock, ScratchBuffers: SHARED in the generator) is not lent: from the
//                                first window on, every original instruction naming it names ours instead, so
//                                there is one free list, as before it was ours. Two would hand out a block twice.
//                                The globals the standalone prep owned (SINGLE_COPY) are shared the same way: the
//                                shadows were written while the game's copy was the only one, and name ours.
//   OwnedGlobals_LendAll()       every owned variable's bytes into the game's copy, unconditionally.
//   OwnedGlobals_Snapshot(), OwnedGlobals_Restore()
//                                both copies of every lent global, around each side of a comparison.
//   OwnedGlobals_Ours(), OwnedGlobals_Game(), OwnedGlobals_AsOurs(), OwnedGlobals_TranslateWords()
//                                an address in one copy for the same byte of the other. An original's result that
//                                holds the address of an owned global (a pointer field, an argument it passed)
//                                holds the game's copy where the port's holds ours: translate before comparing.
//                                The table also has our copies of the game's vtables and code tables (translation
//                                only). OwnedGlobals_AsGame() goes the other way, and also takes our replacement of
//                                an original (a callback the port stores) to the original's entry.
//   OwnedGlobals_Canonical(), OwnedGlobals_CanonicalWords()
//                                a word as the port would hold it - a game copy's address as ours, a replaced
//                                original's entry as the replacement - for a log or a dump made on both sides: the
//                                same on either side, so a number that happens to look like an address stays equal.
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
    bool shared;            // the originals work on ours (the redirects): one copy, nothing to translate
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
uint32_t OwnedGlobals_OursWord(uint32_t game);                // the same as a number
uint32_t OwnedGlobals_Game(const void *ours);                 // 0: not in one of our owned variables
uint32_t OwnedGlobals_AsOurs(uint32_t word);                  // the word unless it is an address in a game's copy
void OwnedGlobals_TranslateWords(void *bytes, size_t size);   // AsOurs on each aligned word, in place
uint32_t OwnedGlobals_AsGame(uint32_t word);                  // the word unless in our copies or a replacement
uint32_t OwnedGlobals_Canonical(uint32_t word);               // the word as the port would hold it
void OwnedGlobals_CanonicalWords(void *bytes, size_t size);   // Canonical on each aligned word, in place
// An original's word and the port's the same once translated (an owned global, a vtable, a callback)
bool OwnedGlobals_SameWord(uint32_t original, uint32_t port);
// Each aligned word of an original's result that differs from the port's only by translation is made the port's
// (where translating every word would also take numbers for addresses)
void OwnedGlobals_MatchWords(void *original, const void *port, size_t size);
// The first byte where an original's result differs from the port's other than by such a word; `size` if none
size_t OwnedGlobals_FirstDifference(const void *original, const void *port, size_t size);

// Not an owned global, but the same kind of difference: the original passes the game's zero vector (0x00243030)
// where a port passes a kZero of its own. A vector argument's word for a call log: 0x00243030 for a zero Coord3 in
// our image, else its address.
uint32_t OwnedGlobals_VectorWord(const void *vector);

#endif  // DRIVING_DEVTOOLS_OWNEDGLOBALS_H_
