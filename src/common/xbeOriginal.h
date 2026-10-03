#ifndef COMMON_XBEORIGINAL_H_
#define COMMON_XBEORIGINAL_H_

// ---------------------------------------------------------------------------------------------------------------
// Running the original of a function we have replaced, for a shadow test.
//
// A replacement is a five-byte jump written over the original's entry, so once a function is ours the original
// cannot be called - its first instruction is gone. For comparing a reimplementation against the original on
// live data, the injector records the five bytes each jump overwrote, and XbeOriginalScope puts them back for
// as long as it lives: inside the scope a call to the original's address runs the original, as shipped.
//
// The swap is process-wide, so it is only safe on the thread that calls the function and only while nothing
// else can call it: fine for the game's own main-thread code (animation, logic), wrong for anything a second
// thread reaches. The original must not reach its own replacement again inside the scope, either - it would run
// the original a second time rather than ours, which is harmless but not what the test means.
// ---------------------------------------------------------------------------------------------------------------

// Called by the injector just before it writes a jump over 'at'.
void XbeOriginal_Record(unsigned at);

// Swaps the original's entry bytes back in (true) or the jump (false). False if 'at' was never patched.
bool XbeOriginal_Restore(unsigned at, bool original);

// Points the jump already written over 'at' somewhere else - a test harness standing in front of a replacement
// it then calls itself. The recorded original bytes are kept, so XbeOriginalScope still reaches the original.
// False if 'at' was never patched.
bool XbeOriginal_Redirect(unsigned at, const void *to);

// Every patched address in [lo, hi) swapped at once; the number swapped.
int XbeOriginal_RestoreRange(unsigned lo, unsigned hi, bool original);

struct XbeOriginalScope {
    unsigned at;
    bool ok;
    explicit XbeOriginalScope(unsigned address) : at(address) { ok = XbeOriginal_Restore(at, true); }
    ~XbeOriginalScope() { if (ok) XbeOriginal_Restore(at, false); }
};

#endif // COMMON_XBEORIGINAL_H_
