#ifndef ACTION_DEVTOOLS_ALLORIGINAL_H_
#define ACTION_DEVTOOLS_ALLORIGINAL_H_

// NIGHTFIRE_ORIGINAL=game: every function of the game's own code (tools/subsystems_action.txt: all but the platform
// layer and the libraries) runs as the Xbox's original, for playing the time demos through the original game. Each
// original's entry is swapped back in and a jump written over our port's, so a call from our code that stays (the
// platform layer, and the functions the demos hang on, kKeep) reaches the original too. Every global we own has
// the game's code's references to it pointed at ours (the sites in tools/data_refs_action.json, our copy found by
// name in the DLL's map), so the originals and what stays of ours read and write one copy.
// docs/action-standalone.md.
void AllOriginal_Install(void);

#endif // ACTION_DEVTOOLS_ALLORIGINAL_H_
