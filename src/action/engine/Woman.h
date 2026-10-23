#ifndef WOMAN_H_
#define WOMAN_H_

// The pause menu background's whole RLE file, loaded by LoadWoman; psiResetResources drops it at each
// level change (the heap it lives in is about to be wiped).
extern void *MemoryForWoman;

void psiDecompressWoman(void);
void LoadWoman(void);
void LoadWoman_Thunk(void);

#endif // WOMAN_H_
