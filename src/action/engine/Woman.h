#ifndef WOMAN_H_
#define WOMAN_H_

// The pause menu background's whole RLE file, loaded by LoadWoman; maybePsiResetResources drops it at each
// level change (the heap it lives in is about to be wiped).
#define MemoryForWoman (*(void**)0x002ae2dc)

void psiDecompressWoman(void);

#endif // WOMAN_H_
