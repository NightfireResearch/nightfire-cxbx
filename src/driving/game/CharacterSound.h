#ifndef DRIVING_GAME_CHARACTERSOUND_H_
#define DRIVING_GAME_CHARACTERSOUND_H_

// ---------------------------------------------------------------------------------------------------------------
// ACharacter: the voice of an AI character (AICharacter::GetACharacter makes it). Say picks a line from bank slot
// 7 by the first letters of what it is asked to say, one at a time: a new line only once the last one is done.
// See CharacterSound.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../audio/Sound.h"             // ABaseSound, ASoundPlayParams
#include "../audio/Voice.h"             // AVoice
#include "../data/CoordConvert.h"       // Coord3
#include "../../common/xbeClass.h"      // offsetof on a derived class without clang's warning

// (0xd0 bytes; vtable 0x0018a520)
class ACharacter : public ABaseSound {
public:
    AVoice *voice;              // +0xc0 the line being said; NULL between lines
    uint8_t unknownC4[0xc];

    // A character in the mix `mixName`, heard by the active views.
    ACharacter* Construct(const char *mixName);                                     // 0x0001c0c0
    void Destruct();                                                                // 0x0011dd30
    ACharacter* Delete(unsigned int flags);                                         // 0x0001c110

    // A line for `what`: "HF", "HY", "HD", "HS" and other "H" lines from their lists in turn, heard over a long
    // distance; an "M" line or another from the next of their lists whose patch the bank has, over a short one.
    // Nothing while a line is being said.
    void Say(const char *what);                                                     // 0x0011dad0

    // The line's patch name, or "Character" (vtable slot 2).
    const char* GetName();                                                          // 0x0011dd00
    // Plays the line for the listener, and frees its voice once it is done (vtable slot 3).
    void Play(ASoundPlayParams *params);                                            // 0x0011ddb0
};
static_assert(offsetof(ACharacter, voice) == 0xc0, "ACharacter::voice");
static_assert(sizeof(ACharacter) == 0xd0, "ACharacter is 0xd0 bytes");

// Say's place in each list of lines, and the "M" and "F" lists' flags (CharacterSound.cpp)
extern int32_t NextFPedLine, NextMPedLine, NextGruntLine, NextScreamLine, NextDeathLine, NextYellLine, NextFallLine;
extern bool MPedLinesWrapped, FPedLinesWrapped;

// The point (x, y, z), returned through `result` (Ghidra: FUN_0001c140; the name is ours).
Coord3* MakeCoord3(Coord3 *result, float x, float y, float z);                      // 0x0001c140

#endif // DRIVING_GAME_CHARACTERSOUND_H_
