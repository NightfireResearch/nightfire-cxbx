#include "Banks.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// EA's sound library, module C (docs/driving/sound.md 3.4, 4.3): sample banks - the BNKl v5 headers the game loads
// and hands over, the bank slots, playing a patch (each of its timbres in key and velocity range on its own voices,
// tied together by a key so the sound stops and frees as one) - and the PT tag stream reader (SNDI_gettag/getb),
// the bank timbre parser SNDI_parsetimbre and the stream header parser SNDI_patchtohdr (module D's, 4.4).
//
// Each function is the original at its address, ported from the listing. Calls into modules not ported here go to
// the originals' addresses. devtools/SndTagShadow.cpp compares the parsers with the originals on every PT header on
// the disc and on perturbed copies (sound.md 9.3 step 1).
//
// What the originals leave uninitialised: the tag reader's value before the first tag of length 0..4 (both
// parsers), SNDI_patchtohdr's azimuth offset of channel 1 without a 0x9d tag (stereo, output mode not 2), the
// unconfigured entries of SNDI_parsetimbre's tag tables (read only past six channels) and SNDBANKI_playtimbre's
// voice range when SNDPLATFORM_getvoicerange writes none. The ports read 0 there.
// ---------------------------------------------------------------------------------------------------------------

namespace {

inline uint8_t &U8(uint32_t address) {
    return *(uint8_t *)(uintptr_t)address;
}

inline uint16_t &U16(uint32_t address) {
    return *(uint16_t *)(uintptr_t)address;
}

inline uint32_t &U32(uint32_t address) {
    return *(uint32_t *)(uintptr_t)address;
}

inline SND::Voice *VoiceAt(int index) {
    return (SND::Voice *)(*(uint8_t **)(uintptr_t)0x00244f3cu + index * 0x88);
}
inline int NumVoices() {
    return *(int16_t *)(uintptr_t)0x00244ed8u;
}
inline SND::BankSlot *Slot(int bank) {   // sndbanki_buffer
    return (SND::BankSlot *)(*(uint8_t **)(uintptr_t)0x00244f40u + bank * 8);
}
inline int NumBanks() {
    return U16(0x00244cf0);
}

template <typename T> inline T &At(void *base, int offset) {   // a field the original indexes past its size
    return *(T *)((uint8_t *)base + offset);
}

inline uint8_t *AddBytes(const void *p, int32_t n) {   // pointer + value, as the original's 32-bit ADD
    return (uint8_t *)(uintptr_t)((uint32_t)(uintptr_t)p + (uint32_t)n);
}

inline int32_t Mul(int32_t a, int32_t b) {
    return (int32_t)((uint32_t)a * (uint32_t)b);
}

// x / 127 as the compiler computes it (see Voices.cpp)
inline int32_t Div127(int32_t x) {
    int32_t hi = (int32_t)(((int64_t)x * (int64_t)(int32_t)0x81020409u) >> 32);
    hi = (int32_t)((uint32_t)hi + (uint32_t)x);
    hi >>= 6;
    return hi + (int32_t)((uint32_t)hi >> 31);
}

const uint32_t kOutputMode = 0x00244d10;      // speakers
const uint32_t kChannelAzimuth = 0x00244f64;  // uint16 [channel + 6 x channels]: the default azimuth per layout
const uint32_t kUserDataClients = 0x00244f10; // never registered (count at 0x00244ed6)
const uint32_t kUserDataClientCount = 0x00244ed6;
const uint32_t kNextKey = 0x00245374;
const uint32_t kBankOnExit = 0x00244f2c;      // SNDbank_on_exit_func

// ---- the originals called from here (other modules)

inline void EnterCritical() {
    ((void (*)(void))0x0013b950)();                        // SNDSYS_entercritical
}
inline void LeaveCritical() {
    ((void (*)(void))0x0013b970)();                        // SNDSYS_leavecritical
}
inline int RandRange(int range) {
    return ((int (*)(int))0x00142ea0)(range);              // randrange
}
inline uint32_t Random() {
    return ((uint32_t (*)(void))0x001412e0)();             // iSNDrandom
}
inline void GetVoiceRange(int mode, int *first, int *end) {
    ((void (*)(int, int *, int *))0x0013d900)(mode, first, end);   // SNDPLATFORM_getvoicerange
}
inline int PlatformPlayTimbre(SND::PatchHeader *header, SND::BankHeader *bank, int voice, int timeMult,
                              int lowpass, int opt14, int opt16) {
    return ((int (*)(SND::PatchHeader *, SND::BankHeader *, int, int, int, int, int))0x00142b10)(
        header, bank, voice, timeMult, lowpass, opt14, opt16);   // SNDPLATFORM_playtimbre
}
inline void *MemAlloc(int32_t size) {
    return ((void *(*)(int32_t))0x0013f780)(size);         // SNDMEMI_alloc
}
inline void MemClear(void *p, int size) {
    ((void (*)(void *, int))0x0013f600)(p, size);          // memclr
}
inline void MemMove(void *to, const void *from, int size) {
    ((void *(*)(void *, const void *, int))0x00132270)(to, from, size);   // _memmove
}

// What a user-data client gets
struct UserDataInfo {
    int32_t operation;           // 1 played, 2 bank removed
    uint8_t *data;
    int32_t size;
};

// SNDI_parsetimbre's frame from the reader on: the channel loop reads the azimuth offsets past six into the tag
// tables, as the original's does.
struct ParseFrame {
    SND::TagReader reader;
    int32_t azimuth[6];
    int32_t low[0x26];           // values of tags 0x00..0x25
    int32_t sample[0x28];        // values of tags 0x80..0xa7
    uint8_t *lowData[0x26];      // their data pointers
    uint8_t *sampleData[0x28];
};

// SNDBANKI_playpatch's: past twelve timbres the handles run into the header, as the original's do
struct PlayFrame {
    int32_t handles[12];
    SND::PatchHeader header;
};

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// The bank API
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x0013cc20)
int SNDBANK_play(int bank, int patch, SND::PlayOpts *opts) {
    if (SNDBANKI_valid(bank) < 0)
        return -8;
    SND::BankHeader *header = Slot(bank)->header;
    if (patch < 0 || patch >= (int)header->patchCount)
        return -8;
    return SNDBANKI_playpatch(header, SNDBANKI_getppatch(header, patch), bank, patch, opts);
}

// Moves the bank's header to destination (the game keeps only the header of a bank it plays from elsewhere).
// FUNC_AT(0x0013ce20)
int SNDbankheadercopy(SND::BankHeader *destination, int bank) {
    int size = SNDbankheadersize(bank);
    if (size < 0)
        return size;
    MemMove(destination, Slot(bank)->header, size);
    Slot(bank)->header = destination;
    return 0;
}

// FUNC_AT(0x0013ce60)
int SNDbankheadersize(int bank) {
    SND::BankHeader *header = Slot(bank)->header;
    return (int)(header->sizeB + header->sizeA);
}

// Hands each of the timbre's user-data blocks (tag 0x14, last first) to the registered clients - none register.
// FUNC_AT(0x0013ce80)
void SNDBANKI_userdatacallback(SND::PatchHeader *header, int operation) {
    if (header->userDataCount > 0) {
        do {
            int8_t n = (int8_t)(header->userDataCount - 1);
            header->userDataCount = n;
            UserDataInfo info;
            info.operation = operation;
            info.data = At<uint8_t *>(header, 0x34 + n * 4);
            info.size = At<int32_t>(header, 0x44 + n * 4);
            for (int i = 0; i < (int8_t)U8(kUserDataClientCount); i++)
                ((void (*)(UserDataInfo *))U32(kUserDataClients + (uint32_t)(i * 4)))(&info);
        } while (header->userDataCount > 0);
    }
}

// Always 8; the slot is not checked.
// FUNC_AT(0x0013cef0)
int SNDbankadd(int *bank, SND::BankHeader *data) {
    U32(kBankOnExit) = 0x0013cf60;   // SNDbankremove, as the original stores it
    int index = SNDBANKI_alloc();
    *bank = index;
    SND::BankSlot *slot = Slot(index);
    slot->header = data;
    slot->flag = 0;
    return 8;
}

// FUNC_AT(0x0013cf20)
int SNDbankpatpresent(int bank, int patch) {
    if (SNDBANKI_valid(bank) < 0)
        return -8;
    return SNDBANKI_getppatch(Slot(bank)->header, patch) != NULL ? 1 : 0;
}

// -1: every bank. Stops the bank's voices, tells the user-data clients about every timbre, frees the slot.
// FUNC_AT(0x0013cf60)
int SNDbankremove(int bank) {
    if (bank == -1) {
        for (int i = 0; i < NumBanks(); i++)
            SNDbankremove(i);
        return 0;
    }
    if (SNDBANKI_valid(bank) != 0)
        return -8;
    EnterCritical();
    SND::BankHeader *header = Slot(bank)->header;
    for (int i = 0; i < NumVoices(); i++) {
        SND::Voice *v = VoiceAt(i);
        if (v->bank == bank)
            SNDstop(v->handle);
    }
    for (int patch = 0; patch < (int)header->patchCount; patch++) {
        uint8_t *cursor = SNDBANKI_getppatch(header, patch);
        if (cursor != NULL) {
            cursor += 4;
            SND::PatchHeader timbre;
            int more;
            do {
                more = SNDI_parsetimbre(&cursor, &timbre);
                SNDBANKI_userdatacallback(&timbre, 2);
            } while (more != 0);
        }
    }
    Slot(bank)->header = NULL;
    Slot(bank)->flag = 0;
    LeaveCritical();
    return 0;
}

// A key no voice in use has, from a running counter (1..255).
// FUNC_AT(0x00140200)
int SNDBANKI_findfreekey(void) {
    uint8_t key = U8(kNextKey);
    int count = NumVoices();
    uint8_t *voices = *(uint8_t **)(uintptr_t)0x00244f3cu;
    for (;;) {
        key++;
        U8(kNextKey) = key;
        if (key == 0) {
            key = 1;
            U8(kNextKey) = key;
        }
        bool used = false;
        for (int i = 0; i < count; i++) {
            SND::Voice *v = (SND::Voice *)(voices + i * 0x88);
            if (v->inUse != 0 && v->key == key) {
                used = true;
                break;
            }
        }
        if (!used)
            return key;
    }
}

// One timbre on voices of the first render mode that has room (and plays): the voice record from the header and
// the play options, then the platform driver. The handle, or -9.
// FUNC_AT(0x00140260)
int SNDBANKI_playtimbre(int bank, int patch, SND::BankHeader *bankHeader, SND::PlayOpts *opts,
                        SND::PatchHeader *header, int key, int velocity, int detuneRandom) {
    int vol = opts->vol, pan = opts->pan, fx = opts->fxLevel;
    int modeIndex = 0;
    int result;
    for (;;) {
        result = -9;
        int mode, index;
        do {
            mode = SNDI_validrendermode(&modeIndex, header);
            if (mode == 0)
                return result;
            int first = 0, end = 0;
            GetVoiceRange(mode, &first, &end);
            index = SNDVOICEI_alloc(header->channels, header->priority, &result, first, end);
        } while (index < 0);

        SND::Voice *v = VoiceAt(index);
        int p = header->pan;
        if (header->panRandom != 0) {
            p += RandRange(header->panRandom);
            if (p < 0)
                p = 0;
            else if (p > 0x7f)
                p = 0x7f;
        }
        v->builtinAzimuth = (uint16_t)SNDI_pantoazimuth(p);
        uint32_t detune = header->tune;
        v->detune = (int16_t)detune;
        detune += (uint32_t)Mul(key - header->rootKey, 100);
        v->detune = (int16_t)detune;
        if (detuneRandom != 0) {
            detune += (uint32_t)detuneRandom;
            v->detune = (int16_t)detune;
        }
        if (header->tuneRandom != 0)
            v->detune = (int16_t)(v->detune + (int16_t)RandRange((int16_t)header->tuneRandom));
        v->volTable = header->volTable;
        v->bendTable = header->bendTable;
        v->fadeStep = 0;
        v->fade = (int32_t)((uint32_t)vol << 16);
        int level = header->volume;
        if (header->volumeRandom != 0)
            level += RandRange(header->volumeRandom);
        if (level > 0x7f)
            level = 0x7f;
        else if (level < 0)
            level = 0;
        v->builtinVol = (int8_t)Div127(Mul(level, velocity));
        v->bend = (int8_t)pan;
        v->bendRange = (int16_t)Mul(header->bendRange, 100);
        v->envTable = header->envTable;
        v->env = (int32_t)((uint32_t)(int32_t)header->envStart << 16);
        int32_t *envelope = v->envTable;
        v->envCurrent = 0;
        v->envCount = header->envCount;
        v->envRelease = header->envRelease;
        v->envTicks = envelope[0];
        if (envelope[0] < 0)
            v->envTicks = 0x7fffffff;
        v->envStep = (int32_t)(((uint32_t)envelope[1] << 16) - (uint32_t)v->env) / v->envTicks;
        v->volLfo = header->volLfo;
        v->pitchLfo = header->pitchLfo;
        v->volLfoLength = header->volLfoLength;
        v->pitchLfoLength = header->pitchLfoLength;
        v->pitchLfoDepth = (int16_t)header->pitchLfoDepth;
        if (header->volLfoRandom != 0)
            v->volLfoPos = (uint8_t)(Random() % (uint32_t)(int32_t)(int8_t)header->volLfoRandom);
        else
            v->volLfoPos = 0;
        if (header->pitchLfoRandom != 0)
            v->pitchLfoPos = (uint8_t)(Random() % (uint32_t)header->pitchLfoLength);
        else
            v->pitchLfoPos = 0;
        v->timeMult = opts->timeMult;
        v->pitchMult = opts->pitchMult;
        v->detuneLinear = 0;
        iSNDcalcpitch(index);
        v->bank = (int16_t)bank;
        v->patch = (int16_t)patch;
        v->azimuth = (uint16_t)(opts->azimuth + v->builtinAzimuth);
        v->elevation = (int16_t)opts->elevation;
        v->sustainEnd = header->loopEnd;
        v->frames = header->frames;
        v->sampleRate = header->sampleRate;
        v->sampleRep = header->sampleRep;
        v->channels = header->channels;
        v->renderMode = (uint16_t)mode;
        for (int i = 0; i < header->channels; i++)
            At<uint16_t>(v, 0x4c + i * 2) = At<uint16_t>(header, 0xa8 + i * 2);
        v->progVol = opts->progVol;
        v->dry = (int8_t)header->fxLevel;
        v->fxLevel = (int8_t)fx;
        SNDI_calcfxlevel(0, index);
        iSNDcalcvol(index);
        if (PlatformPlayTimbre(header, bankHeader, index, opts->timeMult, opts->lowpass, opts->opt14, opts->opt16) >= 0)
            return result;
        for (int i = 0; i < header->channels; i++)
            SNDVOICEI_free(At<int16_t>(VoiceAt(index), 4 + i * 2));
    }
}

// Every timbre of the patch in range of the play options' key and velocity; a sound of several voices shares a
// key. The last timbre's handle, or the error.
// FUNC_AT(0x001405d0)
int SNDBANKI_playpatch(SND::BankHeader *bankHeader, uint8_t *patch, int bank, int patchIndex,
                       SND::PlayOpts *opts) {
    int result = -9;
    SND::Voice *last = NULL;
    int detuneRandom = 0;
    int randomised = 0;
    if (patch == NULL)
        return -8;
    if (bankHeader == NULL)
        bankHeader = (SND::BankHeader *)patch;
    int velocity = opts->velocity;
    int key = opts->key;
    uint8_t *cursor = patch + 4;
    int soundKey = SNDBANKI_findfreekey();
    PlayFrame f;
    int count = 0;
    int more;
    do {
        more = SNDI_parsetimbre(&cursor, &f.header);
        if (randomised == 0) {
            if (f.header.detuneRandom != 0)
                detuneRandom = RandRange((int16_t)f.header.detuneRandom);
            randomised = 1;
        }
        if (velocity >= f.header.velocityLow && velocity <= f.header.velocityHigh && key >= f.header.keyLow &&
            key <= f.header.keyHigh) {
            result = SNDBANKI_playtimbre(bank, patchIndex, bankHeader, opts, &f.header, key, velocity, detuneRandom);
            if (result < 0) {
                for (int i = 0; i < count; i++)
                    SNDstop(At<int32_t>(f.handles, i * 4));
                return result;
            }
            At<int32_t>(f.handles, count * 4) = result;
            count++;
            SNDBANKI_userdatacallback(&f.header, 1);
        }
    } while (more != 0);

    int playing = 0;
    for (int i = 0; i < count; i++) {
        int handle = At<int32_t>(f.handles, i * 4);
        if (SNDover(handle) == 0)
            At<int32_t>(f.handles, playing++ * 4) = handle;
    }
    if (playing == 0)
        return result;
    if (playing == 1) {
        int index = SNDVOICEI_get(f.handles[0]);
        if (index >= 0) {
            SND::Voice *v = VoiceAt(index);
            v->key = 0;
            v->keyLast = 0;
        }
        return result;
    }
    for (int i = 0; i < playing; i++) {
        int index = SNDVOICEI_get(At<int32_t>(f.handles, i * 4));
        if (index >= 0) {
            last = VoiceAt(index);
            last->key = (uint8_t)soundKey;
            last->keyLast = 0;
        }
    }
    last->keyLast = 1;
    return result;
}

// FUNC_AT(0x001407e0)
int SNDBANKI_alloc(void) {
    int count = NumBanks();
    for (int i = 0; i < count; i++)
        if (Slot(i)->header == NULL)
            return i;
    return -9;
}

// FUNC_AT(0x00140810)
uint8_t* SNDBANKI_getppatch(SND::BankHeader *bankHeader, int patch) {
    if (patch >= (int)bankHeader->patchCount)
        return NULL;
    uint32_t offset = At<uint32_t>(bankHeader, 0x14 + patch * 4);
    if (offset == 0)
        return NULL;
    return AddBytes(bankHeader, (int32_t)(offset + (uint32_t)(patch * 4) + 0x14));
}

// 0 for a playable bank, -18 for one flagged, -8 for none
// FUNC_AT(0x00140840)
int SNDBANKI_valid(int bank) {
    if (bank < 0 || bank >= NumBanks() || Slot(bank)->header == NULL)
        return -8;
    return Slot(bank)->flag != 0 ? -0x12 : 0;
}

// ---------------------------------------------------------------------------------------------------------------
// The PT tag stream
// ---------------------------------------------------------------------------------------------------------------

// One timbre's tags into the header, from *cursor to the end (0xff, returns 0) or to 0xfe (another timbre follows,
// returns 1); *cursor is left after it.
// FUNC_AT(0x00140aa0)
int SNDI_parsetimbre(uint8_t **cursor, SND::PatchHeader *header) {
    ParseFrame f;
    memset(&f, 0, sizeof(f));
    f.reader.cursor = *cursor;
    header->start = *cursor;
    for (int i = 0; i < 6; i++) {
        header->blobs[i] = NULL;
        header->blobSizes[i] = 0;
    }
    f.low[0x02] = 0x7f;
    f.low[0x04] = 0x7f;
    f.low[0x0e] = 0x7f;
    f.low[0x1c] = 0x7f;
    f.low[0x07] = 0x3c;
    f.low[0x08] = -1;
    f.low[0x09] = 1;
    f.low[0x0c] = 0x40;
    header->userDataCount = 0;
    f.low[0x19] = 0x001d9d28;   // the default envelope
    f.low[0x1a] = -1;
    f.low[0x25] = 1;
    f.sample[0x00] = 2;
    f.sample[0x02] = 1;
    f.sample[0x04] = 24000;
    f.sample[0x06] = -1;
    f.sample[0x07] = -1;
    f.sample[0x20] = 8;
    int more = 0;
    if (SNDI_gettag(&f.reader) != 0) {
        do {
            int tag = f.reader.tag;
            if (tag < 0x26) {
                f.low[tag] = f.reader.value;
                f.lowData[tag] = f.reader.data;
                if (tag == 0x14) {
                    At<uint8_t *>(header, 0x34 + header->userDataCount * 4) = f.reader.data;
                    At<int32_t>(header, 0x44 + header->userDataCount * 4) = f.reader.length;
                    header->userDataCount++;
                }
            } else if (tag >= 0x80 && tag < 0xa8) {
                f.sample[tag - 0x80] = f.reader.value;
                f.sampleData[tag - 0x80] = f.reader.data;
                uint8_t *data = f.reader.data;
                uint16_t length = (uint16_t)f.reader.length;
                if (tag == 0x98) {
                    header->blobs[0] = data;
                    header->blobSizes[0] = length;
                } else if (tag == 0x99) {
                    header->blobs[1] = data;
                    header->blobSizes[1] = length;
                } else if (tag == 0x9a) {
                    header->blobs[2] = data;
                    header->blobSizes[2] = length;
                } else if (tag == 0x9b) {
                    header->blobs[3] = data;
                    header->blobSizes[3] = length;
                } else if (tag == 0xa4) {
                    header->blobs[4] = data;
                    header->blobSizes[4] = length;
                } else if (tag == 0xa5) {
                    header->blobs[5] = data;
                    header->blobSizes[5] = length;
                }
            } else if (tag == 0xfe) {
                more = 1;
                break;
            }
        } while (SNDI_gettag(&f.reader) != 0);
    }

    header->velocityLow = (int8_t)f.low[0x01];
    header->velocityHigh = (int8_t)f.low[0x02];
    header->keyLow = (int8_t)f.low[0x03];
    header->keyHigh = (int8_t)f.low[0x04];
    header->priority = (int8_t)f.low[0x06];
    header->rootKey = (int8_t)f.low[0x07];
    header->envRelease = (uint8_t)f.low[0x08];
    header->envCount = (uint8_t)f.low[0x09];
    header->bendRange = (int8_t)f.low[0x0a];
    header->pan = (int8_t)f.low[0x0c];
    header->panRandom = (int8_t)f.low[0x0d];
    header->volume = (int8_t)f.low[0x0e];
    header->volumeRandom = (int8_t)f.low[0x0f];
    header->tune = (uint16_t)f.low[0x10];
    header->tuneRandom = (uint16_t)f.low[0x11];
    header->volTable = (int8_t *)AddBytes(f.lowData[0x12], f.low[0x12]);
    header->fxLevel = (uint8_t)f.low[0x13];
    header->bendTable = (int8_t *)AddBytes(f.lowData[0x17], f.low[0x17]);
    header->envTable = (int32_t *)AddBytes(f.lowData[0x19], f.low[0x19]);
    header->tag1a = f.low[0x1a];
    header->envStart = (int8_t)f.low[0x1c];
    header->volLfo = (int8_t *)AddBytes(f.lowData[0x1d], f.low[0x1d]);
    header->volLfoLength = (uint8_t)f.low[0x1e];
    header->volLfoRandom = (uint8_t)f.low[0x1f];
    header->pitchLfo = (int8_t *)AddBytes(f.lowData[0x20], f.low[0x20]);
    header->pitchLfoLength = (uint8_t)f.low[0x21];
    header->pitchLfoDepth = (uint16_t)f.low[0x22];
    header->pitchLfoRandom = (uint8_t)f.low[0x23];
    header->detuneRandom = (uint16_t)f.low[0x24];
    header->tag25 = (uint8_t)f.low[0x25];
    header->tag80 = (uint8_t)f.sample[0x00];
    header->channels = (int8_t)f.sample[0x02];
    header->sampleRate = (uint16_t)f.sample[0x04];
    header->frames = f.sample[0x05];
    header->loopStart = f.sample[0x06];
    header->loopEnd = f.sample[0x07];
    header->sampleOffsets[0] = f.sample[0x08];
    header->sampleOffsets[1] = f.sample[0x09];
    header->renderMode = (uint16_t)f.sample[0x0c];
    header->sampleData = f.sampleData[0x0a];
    header->sampleOffsets[2] = f.sample[0x14];
    header->sampleOffsets[3] = f.sample[0x15];
    header->sampleOffsets[4] = f.sample[0x22];
    header->sampleOffsets[5] = f.sample[0x23];
    header->sampleRep = (uint8_t)f.sample[0x20];
    f.azimuth[0] = f.sample[0x1c];
    f.azimuth[1] = f.sample[0x1d];
    f.azimuth[2] = f.sample[0x1e];
    f.azimuth[3] = f.sample[0x1f];
    f.azimuth[4] = f.sample[0x26];
    f.azimuth[5] = f.sample[0x27];

    int8_t channels = header->channels;
    if (channels > 0) {
        int i = 0;
        do {
            if (U8(kOutputMode) == 2 && header->channels == 2)
                At<int32_t>(f.azimuth, i * 4) = 0;
            uint16_t base = U16(kChannelAzimuth + (uint32_t)((i + header->channels * 6) * 2));
            At<uint16_t>(header, 0xa8 + i * 2) = (uint16_t)(base + (uint16_t)At<int32_t>(f.azimuth, i * 4));
            i++;
        } while (i < header->channels);
    }
    *cursor = f.reader.cursor;
    return more;
}

// The next tag: 0xfc padding skipped; 0xff ends (0); 0xfd and 0xfe are bare markers; otherwise a length byte (0xff:
// a 4-byte big-endian length follows), the data, and for lengths up to 4 the value.
// FUNC_AT(0x001428d0)
int SNDI_gettag(SND::TagReader *reader) {
    if (*reader->cursor == 0xfc) {
        do {
            reader->cursor++;
        } while (*reader->cursor == 0xfc);
    }
    uint8_t *p = reader->cursor;
    reader->tag = *p;
    if (reader->tag == 0xff)
        return 0;
    p++;
    reader->cursor = p;
    if (reader->tag == 0xfd || reader->tag == 0xfe)
        return 1;
    reader->length = *p;
    if (reader->length == 0xff) {
        reader->length = SNDI_getb(p + 1, 4);
        reader->cursor = reader->cursor + 4;
    }
    p = reader->cursor + 1;
    reader->cursor = p;
    reader->data = p;
    if ((uint32_t)reader->length <= 4)
        reader->value = SNDI_getb(p, reader->length);
    reader->cursor = AddBytes(reader->cursor, reader->length);
    return 1;
}

// Big-endian, sign-extended for 1..3 bytes
// FUNC_AT(0x00144a90)
int SNDI_getb(const uint8_t *data, int length) {
    uint32_t value = 0;
    for (uint32_t n = (uint32_t)length; n != 0; n--)
        value = (value << 8) + *data++;
    int32_t v = (int32_t)value;
    if (length == 1) {
        if (v > 0x7f)
            v -= 0x100;
    } else if (length == 2) {
        if (v > 0x7fff)
            v -= 0x10000;
    } else if (length == 3) {
        if (v > 0x7fffff)
            v -= 0x1000000;
    }
    return v;
}

// A stream's PT header (pt points at "PT"): the format, the attributes and the sample layout, the 0x98..0x9b,
// 0xa4, 0xa5 blobs copied into the sound heap. Unlike SNDI_parsetimbre it reads through 0xfd/0xfe to 0xff. base is
// added to the channels' sample offsets for the mixer's render modes (& 0x14, or none).
// FUNC_AT(0x0013f1c0)
void SNDI_patchtohdr(int base, uint8_t *pt, SND::StreamFormat *format, SND::Attributes *attributes,
                     SND::StreamLayout *layout) {
    int32_t renderMode = 0;
    int32_t azimuth[6] = { 0, 0, 0, 0, 0, 0 };   // the original leaves [1] uninitialised
    SND::TagReader reader;
    memset(&reader, 0, sizeof(reader));
    MemClear(format, 4);
    MemClear(attributes, 0x68);
    MemClear(layout, 0x1c);
    SND_attrsetdef(attributes);
    format->sampleRate = 24000;
    format->channels = 1;
    format->sampleRep = 8;
    reader.cursor = pt + 4;
    layout->frames = 0;
    uint8_t *userData = (uint8_t *)attributes + 0x58;
    while (SNDI_gettag(&reader) != 0) {
        int tag = reader.tag;
        int32_t value = reader.value;
        int blob = -1;
        switch (tag) {
        case 0xa0: format->sampleRep = (uint8_t)value; break;
        case 0x9c: azimuth[0] = value; break;
        case 0x9d: azimuth[1] = value; break;
        case 0x9e: azimuth[2] = value; break;
        case 0x9f: azimuth[3] = value; break;
        case 0xa6: azimuth[4] = value; break;
        case 0xa7: azimuth[5] = value; break;
        case 0x98: blob = 0; break;
        case 0x99: blob = 1; break;
        case 0x9a: blob = 2; break;
        case 0x9b: blob = 3; break;
        case 0xa4: blob = 4; break;
        case 0xa5: blob = 5; break;
        case 0x80: attributes->tag80 = (uint8_t)value; break;
        case 0x82: format->channels = (uint8_t)value; break;
        case 0x84: format->sampleRate = (uint16_t)value; break;
        case 0x85: layout->frames = value; break;
        case 0x8a: break;
        case 0x13: attributes->fxLevel = (uint8_t)value; break;
        case 0x0a: attributes->bendRange = (uint8_t)value; break;
        case 0x06: attributes->priority = (uint8_t)value; break;
        case 0x8c: renderMode = value; break;
        case 0x88: layout->offsets[0] = value; break;
        case 0x89: layout->offsets[1] = value; break;
        case 0x94: layout->offsets[2] = value; break;
        case 0x95: layout->offsets[3] = value; break;
        case 0xa2: layout->offsets[4] = value; break;
        case 0xa3: layout->offsets[5] = value; break;
        case 0x14:
            *(uint8_t **)(userData - 0x10) = reader.data;
            *(int32_t *)userData = reader.length;
            userData += 4;
            break;
        default: break;
        }
        if (blob >= 0) {
            uint8_t *copy = (uint8_t *)MemAlloc(reader.length);
            attributes->blobs[blob] = copy;
            memcpy(copy, reader.data, (size_t)(uint32_t)reader.length);
            attributes->blobSizes[blob] = reader.length;
        }
    }
    attributes->renderMode = (uint16_t)renderMode;
    for (int i = (int)format->channels - 1; i >= 0; i--) {
        if (U8(kOutputMode) == 2 && format->channels == 2)
            azimuth[i] = 0;
        uint16_t defaultAzimuth = U16(kChannelAzimuth + (uint32_t)((i + format->channels * 6) * 2));
        At<uint16_t>(attributes, 0xc + i * 2) = (uint16_t)(defaultAzimuth + (uint16_t)azimuth[i]);
        uint16_t mode = attributes->renderMode;
        if ((mode & 0x14) != 0 || mode == 0)
            At<int32_t>(layout, 4 + i * 4) += base;
    }
}
