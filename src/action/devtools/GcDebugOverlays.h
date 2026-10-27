#ifndef ACTION_DEVTOOLS_GCDEBUGOVERLAYS_H_
#define ACTION_DEVTOOLS_GCDEBUGOVERLAYS_H_

// The GameCube build's debug overlays - the heap map, the heap's block counts and the texture viewer - drawn over
// the frame when settings.ini asks for them. See GcDebugOverlays.cpp and docs/gc-debug-overlays.md.

// Called by psiPostDraw before the swap
void GcDebugOverlays_Draw(void);

#endif // ACTION_DEVTOOLS_GCDEBUGOVERLAYS_H_
