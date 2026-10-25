#ifndef COMMON_XBERELOCATE_H_
#define COMMON_XBERELOCATE_H_

// ---------------------------------------------------------------------------------------------------------------
// Moving a global of the game's into our source.
//
// A replacement function takes over by a jump at its entry; a global the game's code reads or writes in place
// has no entry to jump from. To own one while original code can still run - a shadow test's original, the
// all-original runs - our definition replaces it everywhere: every instruction that holds an address inside the
// game's copy is rewritten to hold the same offset into ours, so both read the same data and store the same
// pointers. tools/data_refs.py finds those instructions in the binary (tools/data_refs_<side>.json), and
// tools/preprocess.py writes an XbeRelocate call per reference into the injection table for every global whose
// tag says so, with a check that it is the size of the block it replaces:
//
//     // XBE_GLOBAL(0x0016199c, 0x4) readonly relocate
//     const float TurretRange = 25000000.0f;
//
// References from the game's data (a pointer in one of its tables), through a base folded outside the block, and at
// an odd offset through a register (a base folded from another table, more likely than a byte field) are left
// alone: for a read-only table they read the game's identical copy. The block's end is moved only where a function
// that uses the block compares with it.
// ---------------------------------------------------------------------------------------------------------------

// Points the code reference at 'site' (the address of an instruction's 4-byte displacement or immediate) from
// the block [oldBase, oldBase + size) to the same offset in newBase. The dword there must lie inside the block, or
// be its end (a loop's bound); anything else is reported and left alone.
bool XbeRelocate(unsigned site, unsigned oldBase, unsigned size, const void *newBase);

#endif // COMMON_XBERELOCATE_H_
