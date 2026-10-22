#ifndef ACTION_DEVTOOLS_ALLORIGINAL_H_
#define ACTION_DEVTOOLS_ALLORIGINAL_H_

#include <stddef.h>

// NIGHTFIRE_ORIGINAL=game: every function of the game's own code (tools/subsystems_action.txt: all but the platform
// layer and the libraries) runs as the Xbox's original, for playing the time demos through the original game. Each
// original's entry is swapped back in and a jump written over our port's, so a call from our code that stays (the
// platform layer, and the functions the demos hang on, kKeep) reaches the original too. Every global we own has
// the game's code's references to it pointed at ours (the sites in tools/data_refs_action.json, our copy found by
// name in the DLL's map), so the originals and what stays of ours read and write one copy.
// docs/action-standalone.md.
void AllOriginal_Install(void);

// The owned globals alone: the game's code's references to its copies pointed at ours. Every run of the injected
// build, so that an original a shadow test runs reads and writes the copy our code uses.
void AllOriginal_RelocateOwned(void);

// Our copy of the owned global holding a game address, or 0; *left is the bytes from there to that global's end (or
// to the next owned global's start, for 0)
unsigned AllOriginal_Ours(unsigned game, unsigned *left);

// A game address inside a global we own as the same place in ours; any other address as it is. For the shadow
// tests, which name the game's state by its addresses.
unsigned XbeOurs(unsigned game);

// A span of the game's addresses copied out or in, each part from or to wherever XbeOurs puts it
void XbeOursRead(void *to, unsigned game, size_t size);
void XbeOursWrite(unsigned game, const void *from, size_t size);
void XbeOursFill(unsigned game, int value, size_t size);

// A run of the game's addresses that the shadow tests treat as one block though it may hold several of our globals:
// each element is wherever XbeOurs puts it. game() is the block for a shadow's region list, which splits it the same
// way (ShadowRegions::Add).
template <typename T> struct GameSpan {
    unsigned at;
    T &operator[](unsigned i) const { return *(T *)(size_t)XbeOurs(at + i * (unsigned)sizeof(T)); }
    T &operator*() const { return (*this)[0]; }
    GameSpan operator+(unsigned n) const { return {at + n * (unsigned)sizeof(T)}; }
    template <typename U> GameSpan<U> As() const { return {at}; }
    void *game() const { return (void *)(size_t)at; }
};

#endif // ACTION_DEVTOOLS_ALLORIGINAL_H_
