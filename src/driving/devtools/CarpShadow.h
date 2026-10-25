#ifndef DRIVING_DEVTOOLS_CARPSHADOW_H_
#define DRIVING_DEVTOOLS_CARPSHADOW_H_

// NIGHTFIRE_CARPSHADOW=1: the data layer (DAFI, StringToNumber, the symbol table and its trees, CARP resolving,
// paths, damage zones, packed dimensions) against the originals, on the disc's own files and on random inputs.
// Needs the game's memory manager up: call it once after start-up (after Bond_StartUpSystem). See CarpShadow.cpp.
void CarpShadow_Run(void);

#endif // DRIVING_DEVTOOLS_CARPSHADOW_H_
