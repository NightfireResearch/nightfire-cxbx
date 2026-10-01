#ifndef COMMON_XBERELOCATE_H_
#define COMMON_XBERELOCATE_H_

// ---------------------------------------------------------------------------------------------------------------
// Moving a table of the game's into our source.
//
// A replacement function takes over by a jump at its entry; a table the game's code reads and writes in place
// has no entry to jump from. To own one - the menu's M_ITEM lists, which unlock code still run as the original
// writes into - our array replaces it everywhere: every instruction that holds an address inside the old table
// is rewritten to hold the same offset into ours. tools/data_refs.py finds those instructions in the binary
// (tools/data_refs_<side>.json, from the blocks in tools/relocations_<side>.json), and tools/preprocess.py
// writes an XbeRelocate call per reference for every array tagged
//
//     // RELOCATE
//     M_ITEM sp_level[12] = { ... };
//
// into the injection table, with a check that the array is the size of the block it replaces.
// ---------------------------------------------------------------------------------------------------------------

// Points the code reference at 'site' (the address of an instruction's 4-byte displacement or immediate) from
// the block [oldBase, oldBase + size) to the same offset in newBase. The dword there must lie inside the block;
// anything else is reported and left alone.
bool XbeRelocate(unsigned site, unsigned oldBase, unsigned size, const void *newBase);

#endif // COMMON_XBERELOCATE_H_
