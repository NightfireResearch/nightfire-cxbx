#ifndef DRIVING_DEVTOOLS_HEAPPAD_H_
#define DRIVING_DEVTOOLS_HEAPPAD_H_

// NIGHTFIRE_HEAP_PAD=<bytes>: every allocation the game makes asks for that many bytes more, so that blocks are laid
// out as they would be if the game's structures were larger (a 64-bit build's native pointers,
// docs/driving/64-bit.md 7.2). See HeapPad.cpp.

// From Inject(), after the injection table, before the game starts.
void HeapPad_Install(void);

// NIGHTFIRE_PARTICLE_ORDER=1: from Teleport_Tick, once per tick, prints the order of the particle systems' map.
void HeapPad_Tick(void);

#endif // DRIVING_DEVTOOLS_HEAPPAD_H_
