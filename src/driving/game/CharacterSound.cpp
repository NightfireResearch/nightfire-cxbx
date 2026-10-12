#pragma fp_contract(off)

#include "CharacterSound.h"

#include "../audio/Bank.h"
#include "../audio/Mix.h"
#include "../engine/UMemory.hpp"

// ---------------------------------------------------------------------------------------------------------------
// ACharacter, and the code the linker placed beside its constructor: see CharacterSound.h.
// ---------------------------------------------------------------------------------------------------------------

// Each list's next line (names ours)
// XBE_GLOBAL(0x00243964, 4)
int32_t NextFPedLine;
// XBE_GLOBAL(0x0024396c, 4)
int32_t NextMPedLine;
// XBE_GLOBAL(0x00243970, 4)
int32_t NextGruntLine;
// XBE_GLOBAL(0x00243974, 4)
int32_t NextScreamLine;
// XBE_GLOBAL(0x00243978, 4)
int32_t NextDeathLine;
// XBE_GLOBAL(0x0024397c, 4)
int32_t NextYellLine;
// XBE_GLOBAL(0x00243980, 4)
int32_t NextFallLine;
// Set once the "M" lines have all been tried: from then on Say takes the next whether the bank has it or not. The
// "F" lines' flag is never set.
// XBE_GLOBAL(0x00243968, 1)
bool MPedLinesWrapped;
// XBE_GLOBAL(0x00243960, 1)
bool FPedLinesWrapped;

namespace {

const uint32_t kACharacterVtable = 0x0018a520;

const int kSpeechBankSlot = 7;  // (name ours)

// The lines. Two entries of the original's lists lack the comma between them: "SFX_HScream3" "SFX_HScream4" and
// "SFX_HDeath3" "SFX_HDeath4" are one name each, and the lists' remaining entries NULL.
const char *const kFPedLines[5] = {"SFX_FPed1a", "SFX_FPed1b", "SFX_FPed1c", "SFX_FPed1d", "SFX_FPed1e"};
const char *const kMPedLines[10] = {"SFX_MPed1a", "SFX_MPed1b", "SFX_MPed1c", "SFX_MPed2a", "SFX_MPed2b",
                                    "SFX_MPed2c", "SFX_MPed3a", "SFX_MPed3b", "SFX_MPed3c", "SFX_MPed3d"};
const char *const kGruntLines[8] = {"SFX_HGrunt1", "SFX_HGrunt2", "SFX_HGrunt3", "SFX_HGrunt4",
                                    "SFX_HGrunt5", "SFX_HGrunt6", "SFX_HGrunt7", "SFX_HGrunt8"};
const char *const kScreamLines[4] = {"SFX_HScream", "SFX_HScream2", "SFX_HScream3" "SFX_HScream4"};
const char *const kDeathLines[11] = {"SFX_HDeath1", "SFX_HDeath2", "SFX_HDeath3" "SFX_HDeath4"};
const char *const kYellLines[3] = {"SFX_HYell1", "SFX_HYell2", "SFX_HYell3"};
const char *const kFallLines[3] = {"SFX_HenchFall1", "SFX_HenchFall2", "SFX_HenchFall3"};

// The list's next line; the list moves on, back to its start after its last.
template <int N> const char* NextLine(const char *const (&lines)[N], int32_t &next) {
    const char *line = lines[next];
    if (++next >= N)
        next = 0;
    return line;
}

constexpr float kDefaultMinDistance = 3.0f;
constexpr float kDefaultMaxDistance = 25.0f;

}  // namespace

// FUNC_AT(0x0001c0c0)
ACharacter* ACharacter::Construct(const char *mixName) {
    ABaseSound::Construct(mixName, kSoundViewsActive);
    vtable = kACharacterVtable;
    voice = NULL;
    minDistance = kDefaultMinDistance;
    maxDistance = kDefaultMaxDistance;
    maxDistanceSq = kDefaultMaxDistance * kDefaultMaxDistance;
    return this;
}

// FUNC_AT(0x0001c110)
ACharacter* ACharacter::Delete(unsigned int flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this, sizeof(ACharacter));
    return this;
}

// FUNC_AT(0x0001c140)
Coord3* MakeCoord3(Coord3 *result, float x, float y, float z) {
    result->x = x;
    result->y = y;
    result->z = z;
    return result;
}

// FUNC_AT(0x0011dad0)
void ACharacter::Say(const char *what) {
    if (voice != NULL)
        return;
    ABank *bank = fgBanks[kSpeechBankSlot];
    const char *line;
    if (what[0] == 'H') {
        switch (what[1]) {
        case 'F':
            line = NextLine(kFallLines, NextFallLine);
            break;
        case 'Y':
            line = NextLine(kYellLines, NextYellLine);
            break;
        case 'D':
            line = NextLine(kDeathLines, NextDeathLine);
            break;
        case 'S':
            line = NextLine(kScreamLines, NextScreamLine);
            break;
        default:
            line = NextLine(kGruntLines, NextGruntLine);
            break;
        }
        minDistance = 50.0f;
        maxDistance = 300.0f;
        maxDistanceSq = 300.0f * 300.0f;
    } else {
        if (what[0] == 'M') {
            do {
                line = NextLine(kMPedLines, NextMPedLine);
                if (NextMPedLine == 0)
                    MPedLinesWrapped = true;
            } while (!bank->IsPatchPresent(bank->index.Lookup(line)) && !MPedLinesWrapped);
        } else {
            do {
                line = NextLine(kFPedLines, NextFPedLine);
            } while (!bank->IsPatchPresent(bank->index.Lookup(line)) && !FPedLinesWrapped);
        }
        minDistance = 10.0f;
        maxDistance = 75.0f;
        maxDistanceSq = 75.0f * 75.0f;
    }
    void *memory = UMemory::FastAlloc(sizeof(AVoice), "AVoice");
    voice = memory != NULL ? static_cast<AVoice *>(memory)->Construct(mix, bank->handle, bank->index.Lookup(line))
                           : NULL;
    for (int i = 0; i < 3; i++)
        voice->views[i].loop = 0;
}

// FUNC_AT(0x0011dd00)
const char* ACharacter::GetName() {
    if (voice != NULL)
        return ABank::GetPatchName(voice->views[0].bank, voice->views[0].patch);
    return "Character";
}

// FUNC_AT(0x0011dd30)
void ACharacter::Destruct() {
    vtable = kACharacterVtable;
    if (voice != NULL) {
        // AVoice's destructor, inlined: its views', last first
        for (int i = 3; i-- > 0;)
            voice->views[i].Destruct();
        UMemory::FastFree(voice, sizeof(AVoice));
    }
    ABaseSound::Destruct();
}

// FUNC_AT(0x0011ddb0)
void ACharacter::Play(ASoundPlayParams *params) {
    if (voice == NULL)
        return;
    voice->Play(params->listener->view, float(mix->GetVolume() * volume * params->volume), params->pitch,
                params->azimuth, 0.0f, 0.0f);
    if (voice->IsFinished()) {
        voice->Destruct();
        UMemory::FastFree(voice, sizeof(AVoice));
        voice = NULL;
    }
}
