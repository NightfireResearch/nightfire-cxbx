#include "Banks.h"
#include "Platform.h"
#include "System.h"
#include "../../platform/RealPrint.h"
#include "SndGlobals.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// EA's sound library, module C (docs/driving/sound.md 3.4, 4.3): sample banks - the BNKl v5 headers the game loads
// and hands over, the bank slots, playing a patch (each of its timbres in key and velocity range on its own voices,
// tied together by a key so the sound stops and frees as one) - and the PT tag stream reader (SNDI_gettag/getb),
// the bank timbre parser SNDI_parsetimbre and the stream header parser SNDI_patchtohdr (module D's, 4.4).
//
// Each function is the original at its address, ported from the listing. Calls into the other modules are direct
// (the C runtime's _memmove at its address). devtools/SndTagShadow.cpp compares the parsers with the originals on
// every PT header on the disc and on perturbed copies (sound.md 9.3 step 1).
//
// What the originals leave uninitialised: the tag reader's value before the first tag of length 0..4 (both
// parsers), SNDI_patchtohdr's azimuth offset of channel 1 without a 0x9d tag (stereo, output mode not 2), the
// unconfigured entries of SNDI_parsetimbre's tag tables (read only past six channels) and SNDBANKI_playtimbre's
// voice range when SNDPLATFORM_getvoicerange writes none. The ports read 0 there.
// ---------------------------------------------------------------------------------------------------------------

using namespace SND;   // the tags

namespace {

#define NextKey U8_AT(0x00245374)                    // SNDBANKI_findfreekey's running counter

const int32_t kDefaultEnvelope = 0x001d9d28;         // the default envelope's address, in the original's .rdata
const uint32_t kBankRemoveAddress = 0x0013cf60;      // SNDbankremove, as the exit hook holds it

inline uint8_t *AddBytes(const void *p, int32_t n) {   // pointer + value, as the original's 32-bit ADD
    return (uint8_t *)(uintptr_t)((uint32_t)(uintptr_t)p + (uint32_t)n);
}

int32_t Mul(int32_t a, int32_t b) {   // IMUL r32: wraps
    return int32_t(uint32_t(a) * uint32_t(b));
}

// x / 127 as the compiler computes it (see Voices.cpp)
int32_t Div127(int32_t x) {
    int32_t hi = int32_t((int64_t(x) * -0x7efdfbf7) >> 32);   // 0x81020409 as a signed multiplier
    hi = int32_t(uint32_t(hi) + uint32_t(x));
    hi >>= 6;
    return hi + int32_t(uint32_t(hi) >> 31);
}

// ---- the C runtime's _memmove (not ours: sys.crt)
#define MemMove ((void *(*)(void *, const void *, int))0x00132270)

// SNDI_parsetimbre's frame from the reader on: the channel loop reads the azimuth offsets past six into the tag
// tables, as the original's does.
struct ParseFrame {
    TagReader reader;
    int32_t azimuth[6];
    int32_t low[kTagLowEnd];                            // values of tags 0x00..0x25
    int32_t sample[kTagSampleEnd - kTagSampleFirst];    // values of tags 0x80..0xa7
    uint8_t *lowData[kTagLowEnd];                       // their data pointers
    uint8_t *sampleData[kTagSampleEnd - kTagSampleFirst];

    int32_t &Sample(int tag) { return sample[tag - kTagSampleFirst]; }
    uint8_t *&SampleData(int tag) { return sampleData[tag - kTagSampleFirst]; }
};

// SNDBANKI_playpatch's: past twelve timbres the handles run into the header, as the original's do
struct PlayFrame {
    int32_t handles[12];
    PatchHeader header;
};

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// The bank API
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x0013cc20)
int SNDBANK_play(int bank, int patch, SND::PlayOpts *opts) {
    if (SNDBANKI_valid(bank) < 0)
        return -8;
    BankHeader *header = BankArray[bank].header;
    if (patch < 0 || patch >= header->patchCount)
        return -8;
    return SNDBANKI_playpatch(header, SNDBANKI_getppatch(header, patch), bank, patch, opts);
}

// Moves the bank's header to destination (the game keeps only the header of a bank it plays from elsewhere).
// FUNC_AT(0x0013ce20)
int SNDbankheadercopy(SND::BankHeader *destination, int bank) {
    int size = SNDbankheadersize(bank);
    if (size < 0)
        return size;
    MemMove(destination, BankArray[bank].header, size);
    BankArray[bank].header = destination;
    return 0;
}

// FUNC_AT(0x0013ce60)
int SNDbankheadersize(int bank) {
    BankHeader *header = BankArray[bank].header;
    return header->extraSize + header->headerSize;
}

// Hands each of the timbre's user-data blocks (tag 0x14, last first) to the registered clients - none register.
// FUNC_AT(0x0013ce80)
void SNDBANKI_userdatacallback(SND::PatchHeader *header, int operation) {
    if (header->userDataCount > 0) {
        do {
            int8_t n = header->userDataCount - 1;
            header->userDataCount = n;
            UserDataInfo info;   // the handle and request words left unset, as the original leaves them
            info.operation = operation;
            info.data = header->userData[n];
            info.size = header->userDataSize[n];
            for (int i = 0; i < NumUserDataClients; i++)
                UserDataClients[i](&info);
        } while (header->userDataCount > 0);
    }
}

// Always 8; the slot is not checked.
// FUNC_AT(0x0013cef0)
int SNDbankadd(int *bank, SND::BankHeader *data) {
    BankExitHook = (int (*)(int))kBankRemoveAddress;
    int index = SNDBANKI_alloc();
    *bank = index;
    BankSlot *slot = &BankArray[index];
    slot->header = data;
    slot->flag = 0;
    return 8;
}

// FUNC_AT(0x0013cf20)
int SNDbankpatpresent(int bank, int patch) {
    if (SNDBANKI_valid(bank) < 0)
        return -8;
    return SNDBANKI_getppatch(BankArray[bank].header, patch) != NULL ? 1 : 0;
}

// -1: every bank. Stops the bank's voices, tells the user-data clients about every timbre, frees the slot.
// FUNC_AT(0x0013cf60)
int SNDbankremove(int bank) {
    if (bank == -1) {
        for (int i = 0; i < NumBanks; i++)
            SNDbankremove(i);
        return 0;
    }
    if (SNDBANKI_valid(bank) != 0)
        return -8;
    SNDSYS_entercritical();
    BankHeader *header = BankArray[bank].header;
    for (int i = 0; i < NumVoices; i++) {
        Voice *v = &VoiceArray[i];
        if (v->bank == bank)
            SNDstop(v->handle);
    }
    for (int patch = 0; patch < header->patchCount; patch++) {
        uint8_t *cursor = SNDBANKI_getppatch(header, patch);
        if (cursor != NULL) {
            cursor += 4;
            PatchHeader timbre;
            int more;
            do {
                more = SNDI_parsetimbre(&cursor, &timbre);
                SNDBANKI_userdatacallback(&timbre, 2);
            } while (more != 0);
        }
    }
    BankArray[bank].header = NULL;
    BankArray[bank].flag = 0;
    SNDSYS_leavecritical();
    return 0;
}

// A key no voice in use has, from a running counter (1..255).
// FUNC_AT(0x00140200)
int SNDBANKI_findfreekey(void) {
    uint8_t key = NextKey;
    int count = NumVoices;
    Voice *voices = VoiceArray;
    for (;;) {
        key++;
        NextKey = key;
        if (key == 0) {
            key = 1;
            NextKey = key;
        }
        bool used = false;
        for (int i = 0; i < count; i++) {
            if (voices[i].inUse != 0 && voices[i].key == key) {
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
    int vol = opts->vol, bend = opts->bend, fx = opts->fxLevel;
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
            SNDPLATFORM_getvoicerange(mode, &first, &end);
            index = SNDVOICEI_alloc(header->channels, header->priority, &result, first, end);
        } while (index < 0);

        Voice *v = &VoiceArray[index];
        int p = header->pan;
        if (header->panRandom != 0) {
            p += randrange(header->panRandom);
            if (p < 0)
                p = 0;
            else if (p > 0x7f)
                p = 0x7f;
        }
        v->builtinAzimuth = SNDI_pantoazimuth(p);
        uint32_t detune = header->tune;
        v->detune = detune;
        detune += Mul(key - header->rootKey, 100);
        v->detune = detune;
        if (detuneRandom != 0) {
            detune += detuneRandom;
            v->detune = detune;
        }
        if (header->tuneRandom != 0)
            v->detune = v->detune + randrange(int16_t(header->tuneRandom));   // the range sign-extended
        v->volTable = header->volTable;
        v->bendTable = header->bendTable;
        v->fadeStep = 0;
        v->fade = uint32_t(vol) << 16;
        int level = header->volume;
        if (header->volumeRandom != 0)
            level += randrange(header->volumeRandom);
        if (level > 0x7f)
            level = 0x7f;
        else if (level < 0)
            level = 0;
        v->builtinVol = Div127(Mul(level, velocity));
        v->bend = bend;
        v->bendRange = Mul(header->bendRange, 100);
        v->envTable = header->envTable;
        v->env = uint32_t(header->envStart) << 16;
        EnvSegment *envelope = v->envTable;
        v->envCurrent = 0;
        v->envCount = header->envCount;
        v->envRelease = header->envRelease;
        v->envTicks = envelope[0].ticks;
        if (envelope[0].ticks < 0)
            v->envTicks = 0x7fffffff;
        v->envStep = int32_t((uint32_t(envelope[0].level) << 16) - uint32_t(v->env)) / v->envTicks;
        v->volLfo = header->volLfo;
        v->pitchLfo = header->pitchLfo;
        v->volLfoLength = header->volLfoLength;
        v->pitchLfoLength = header->pitchLfoLength;
        v->pitchLfoDepth = header->pitchLfoDepth;
        if (header->volLfoRandom != 0)
            v->volLfoPos = iSNDrandom() % uint32_t(header->volLfoRandom);   // the byte sign-extended
        else
            v->volLfoPos = 0;
        if (header->pitchLfoRandom != 0)
            v->pitchLfoPos = iSNDrandom() % header->pitchLfoLength;
        else
            v->pitchLfoPos = 0;
        v->timeMult = opts->timeMult;
        v->pitchMult = opts->pitchMult;
        v->detuneLinear = 0;
        iSNDcalcpitch(index);
        v->bank = bank;
        v->patch = patch;
        v->azimuth = opts->azimuth + v->builtinAzimuth;
        v->elevation = opts->elevation;
        v->sustainEnd = header->loopEnd;
        v->frames = header->frames;
        v->sampleRate = header->sampleRate;
        v->sampleRep = header->sampleRep;
        v->channels = header->channels;
        v->renderMode = mode;
        for (int i = 0; i < header->channels; i++)
            v->channelAzimuth[i] = header->azimuth[i];
        v->progVol = opts->progVol;
        v->builtinFxLevel = header->fxLevel;
        v->fxLevel = fx;
        SNDI_calcfxlevel(0, index);
        iSNDcalcvol(index);
        // (the sample offsets are from the bank's start)
        if (SNDPLATFORM_playtimbre(header, reinterpret_cast<uint8_t *>(bankHeader), index, opts->timeMult,
                                   opts->distort, opts->lowpass, opts->highpass) >= 0)
            return result;
        for (int i = 0; i < header->channels; i++)
            SNDVOICEI_free(VoiceArray[index].platformVoices[i]);
    }
}

// Every timbre of the patch in range of the play options' key and velocity; a sound of several voices shares a
// key. The last timbre's handle, or the error.
// FUNC_AT(0x001405d0)
int SNDBANKI_playpatch(SND::BankHeader *bankHeader, uint8_t *patch, int bank, int patchIndex,
                       SND::PlayOpts *opts) {
    int result = -9;
    Voice *last = NULL;
    int detuneRandom = 0;
    int randomised = 0;
    if (patch == NULL)
        return -8;
    if (bankHeader == NULL)
        bankHeader = (BankHeader *)patch;
    int velocity = opts->velocity;
    int key = opts->key;
    uint8_t *cursor = patch + 4;
    int soundKey = SNDBANKI_findfreekey();
    PlayFrame f;
    int32_t *handles = f.handles;   // past twelve, on into the header
    int count = 0;
    int more;
    do {
        more = SNDI_parsetimbre(&cursor, &f.header);
        if (randomised == 0) {
            if (f.header.detuneRandom != 0)
                detuneRandom = randrange(int16_t(f.header.detuneRandom));
            randomised = 1;
        }
        if (velocity >= f.header.velocityLow && velocity <= f.header.velocityHigh && key >= f.header.keyLow &&
            key <= f.header.keyHigh) {
            result = SNDBANKI_playtimbre(bank, patchIndex, bankHeader, opts, &f.header, key, velocity, detuneRandom);
            if (result < 0) {
                for (int i = 0; i < count; i++)
                    SNDstop(handles[i]);
                return result;
            }
            handles[count] = result;
            count++;
            SNDBANKI_userdatacallback(&f.header, 1);
        }
    } while (more != 0);

    int playing = 0;
    for (int i = 0; i < count; i++) {
        int handle = handles[i];
        if (SNDover(handle) == 0)
            handles[playing++] = handle;
    }
    if (playing == 0)
        return result;
    if (playing == 1) {
        int index = SNDVOICEI_get(handles[0]);
        if (index >= 0) {
            Voice *v = &VoiceArray[index];
            v->key = 0;
            v->keyLast = 0;
        }
        return result;
    }
    for (int i = 0; i < playing; i++) {
        int index = SNDVOICEI_get(handles[i]);
        if (index >= 0) {
            last = &VoiceArray[index];
            last->key = soundKey;
            last->keyLast = 0;
        }
    }
    last->keyLast = 1;
    return result;
}

// FUNC_AT(0x001407e0)
int SNDBANKI_alloc(void) {
    int count = NumBanks;
    for (int i = 0; i < count; i++)
        if (BankArray[i].header == NULL)
            return i;
    return -9;
}

// FUNC_AT(0x00140810)
uint8_t* SNDBANKI_getppatch(SND::BankHeader *bankHeader, int patch) {
    if (patch >= bankHeader->patchCount)
        return NULL;
    uint32_t offset = bankHeader->patchOffsets[patch];
    if (offset == 0)
        return NULL;
    return AddBytes(&bankHeader->patchOffsets[patch], offset);
}

// 0 for a playable bank, -18 for one flagged, -8 for none
// FUNC_AT(0x00140840)
int SNDBANKI_valid(int bank) {
    if (bank < 0 || bank >= NumBanks || BankArray[bank].header == NULL)
        return -8;
    return BankArray[bank].flag != 0 ? -0x12 : 0;
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
        header->stretchData[i] = NULL;
        header->stretchDataSizes[i] = 0;
    }
    f.low[kTagVelocityHigh] = 0x7f;
    f.low[kTagKeyHigh] = 0x7f;
    f.low[kTagVolume] = 0x7f;
    f.low[kTagEnvStart] = 0x7f;
    f.low[kTagRootKey] = 0x3c;
    f.low[kTagEnvRelease] = -1;
    f.low[kTagEnvCount] = 1;
    f.low[kTagPan] = 0x40;
    header->userDataCount = 0;
    f.low[kTagEnvTable] = kDefaultEnvelope;
    f.low[kTag1a] = -1;
    f.low[kTagPanMult] = 1;
    f.Sample(kTagPlatformVersion) = 2;
    f.Sample(kTagChannels) = 1;
    f.Sample(kTagSampleRate) = 24000;
    f.Sample(kTagLoopStart) = -1;
    f.Sample(kTagLoopEnd) = -1;
    f.Sample(kTagSampleRep) = 8;
    int more = 0;
    if (SNDI_gettag(&f.reader) != 0) {
        do {
            int tag = f.reader.tag;
            if (tag < kTagLowEnd) {
                f.low[tag] = f.reader.value;
                f.lowData[tag] = f.reader.data;
                if (tag == kTagUserData) {
                    header->userData[header->userDataCount] = f.reader.data;
                    header->userDataSize[header->userDataCount] = f.reader.length;
                    header->userDataCount++;
                }
            } else if (tag >= kTagSampleFirst && tag < kTagSampleEnd) {
                f.Sample(tag) = f.reader.value;
                f.SampleData(tag) = f.reader.data;
                uint8_t *data = f.reader.data;
                uint16_t length = f.reader.length;
                int channel;
                switch (tag) {
                case kTagStretchData0: channel = 0; break;
                case kTagStretchData1: channel = 1; break;
                case kTagStretchData2: channel = 2; break;
                case kTagStretchData3: channel = 3; break;
                case kTagStretchData4: channel = 4; break;
                case kTagStretchData5: channel = 5; break;
                default: channel = -1; break;
                }
                if (channel >= 0) {
                    header->stretchData[channel] = data;
                    header->stretchDataSizes[channel] = length;
                }
            } else if (tag == kTagNextTimbre) {
                more = 1;
                break;
            }
        } while (SNDI_gettag(&f.reader) != 0);
    }

    header->velocityLow = f.low[kTagVelocityLow];
    header->velocityHigh = f.low[kTagVelocityHigh];
    header->keyLow = f.low[kTagKeyLow];
    header->keyHigh = f.low[kTagKeyHigh];
    header->priority = f.low[kTagPriority];
    header->rootKey = f.low[kTagRootKey];
    header->envRelease = f.low[kTagEnvRelease];
    header->envCount = f.low[kTagEnvCount];
    header->bendRange = f.low[kTagBendRange];
    header->pan = f.low[kTagPan];
    header->panRandom = f.low[kTagPanRandom];
    header->volume = f.low[kTagVolume];
    header->volumeRandom = f.low[kTagVolumeRandom];
    header->tune = f.low[kTagTune];
    header->tuneRandom = f.low[kTagTuneRandom];
    header->volTable = (int8_t *)AddBytes(f.lowData[kTagVolTable], f.low[kTagVolTable]);
    header->fxLevel = f.low[kTagFxLevel];
    header->bendTable = (int8_t *)AddBytes(f.lowData[kTagBendTable], f.low[kTagBendTable]);
    header->envTable = (EnvSegment *)AddBytes(f.lowData[kTagEnvTable], f.low[kTagEnvTable]);
    header->tag1a = f.low[kTag1a];
    header->envStart = f.low[kTagEnvStart];
    header->volLfo = (int8_t *)AddBytes(f.lowData[kTagVolLfo], f.low[kTagVolLfo]);
    header->volLfoLength = f.low[kTagVolLfoLength];
    header->volLfoRandom = f.low[kTagVolLfoRandom];
    header->pitchLfo = (int8_t *)AddBytes(f.lowData[kTagPitchLfo], f.low[kTagPitchLfo]);
    header->pitchLfoLength = f.low[kTagPitchLfoLength];
    header->pitchLfoDepth = f.low[kTagPitchLfoDepth];
    header->pitchLfoRandom = f.low[kTagPitchLfoRandom];
    header->detuneRandom = f.low[kTagDetuneRandom];
    header->panMult = f.low[kTagPanMult];
    header->platformVersion = f.Sample(kTagPlatformVersion);
    header->channels = f.Sample(kTagChannels);
    header->sampleRate = f.Sample(kTagSampleRate);
    header->frames = f.Sample(kTagFrames);
    header->loopStart = f.Sample(kTagLoopStart);
    header->loopEnd = f.Sample(kTagLoopEnd);
    header->sampleOffsets[0] = f.Sample(kTagSampleOffset0);
    header->sampleOffsets[1] = f.Sample(kTagSampleOffset1);
    header->renderMode = f.Sample(kTagRenderMode);
    header->sampleData = f.SampleData(kTagSampleData);
    header->sampleOffsets[2] = f.Sample(kTagSampleOffset2);
    header->sampleOffsets[3] = f.Sample(kTagSampleOffset3);
    header->sampleOffsets[4] = f.Sample(kTagSampleOffset4);
    header->sampleOffsets[5] = f.Sample(kTagSampleOffset5);
    header->sampleRep = f.Sample(kTagSampleRep);
    f.azimuth[0] = f.Sample(kTagAzimuth0);
    f.azimuth[1] = f.Sample(kTagAzimuth1);
    f.azimuth[2] = f.Sample(kTagAzimuth2);
    f.azimuth[3] = f.Sample(kTagAzimuth3);
    f.azimuth[4] = f.Sample(kTagAzimuth4);
    f.azimuth[5] = f.Sample(kTagAzimuth5);

    int8_t channels = header->channels;
    if (channels > 0) {
        int i = 0;
        do {
            if (OutputMode == 2 && header->channels == 2)
                f.azimuth[i] = 0;
            uint16_t base = ChannelAzimuth[header->channels][i];
            header->azimuth[i] = base + f.azimuth[i];
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
    if (*reader->cursor == kTagPadding) {
        do {
            reader->cursor++;
        } while (*reader->cursor == kTagPadding);
    }
    uint8_t *p = reader->cursor;
    reader->tag = *p;
    if (reader->tag == kTagEnd)
        return 0;
    p++;
    reader->cursor = p;
    if (reader->tag == kTagMarker || reader->tag == kTagNextTimbre)
        return 1;
    reader->length = *p;
    if (reader->length == 0xff) {   // the long form
        reader->length = SNDI_getb(p + 1, 4);
        reader->cursor = reader->cursor + 4;
    }
    p = reader->cursor + 1;
    reader->cursor = p;
    reader->data = p;
    if (uint32_t(reader->length) <= 4)
        reader->value = SNDI_getb(p, reader->length);
    reader->cursor = AddBytes(reader->cursor, reader->length);
    return 1;
}

// Big-endian, sign-extended for 1..3 bytes
// FUNC_AT(0x00144a90)
int SNDI_getb(const uint8_t *data, int length) {
    uint32_t value = 0;
    for (uint32_t n = length; n != 0; n--)
        value = (value << 8) + *data++;
    int32_t v = value;
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
    TagReader reader;
    memset(&reader, 0, sizeof(reader));
    memclr(format, sizeof(*format));
    memclr(attributes, sizeof(*attributes));
    memclr(layout, sizeof(*layout));
    SND_attrsetdef(attributes);
    format->sampleRate = 24000;
    format->channels = 1;
    format->sampleRep = 8;
    reader.cursor = pt + 4;
    layout->frames = 0;
    int userData = 0;   // past four, on into the sizes and what follows, as the original's pointer runs
    while (SNDI_gettag(&reader) != 0) {
        int tag = reader.tag;
        int32_t value = reader.value;
        int channel = -1;
        switch (tag) {
        case kTagSampleRep: format->sampleRep = value; break;
        case kTagAzimuth0: azimuth[0] = value; break;
        case kTagAzimuth1: azimuth[1] = value; break;
        case kTagAzimuth2: azimuth[2] = value; break;
        case kTagAzimuth3: azimuth[3] = value; break;
        case kTagAzimuth4: azimuth[4] = value; break;
        case kTagAzimuth5: azimuth[5] = value; break;
        case kTagStretchData0: channel = 0; break;
        case kTagStretchData1: channel = 1; break;
        case kTagStretchData2: channel = 2; break;
        case kTagStretchData3: channel = 3; break;
        case kTagStretchData4: channel = 4; break;
        case kTagStretchData5: channel = 5; break;
        case kTagPlatformVersion: attributes->platformVersion = value; break;
        case kTagChannels: format->channels = value; break;
        case kTagSampleRate: format->sampleRate = value; break;
        case kTagFrames: layout->frames = value; break;
        case kTagSampleData: break;
        case kTagFxLevel: attributes->fxLevel = value; break;
        case kTagBendRange: attributes->bendRange = value; break;
        case kTagPriority: attributes->priority = value; break;
        case kTagRenderMode: renderMode = value; break;
        case kTagSampleOffset0: layout->offsets[0] = value; break;
        case kTagSampleOffset1: layout->offsets[1] = value; break;
        case kTagSampleOffset2: layout->offsets[2] = value; break;
        case kTagSampleOffset3: layout->offsets[3] = value; break;
        case kTagSampleOffset4: layout->offsets[4] = value; break;
        case kTagSampleOffset5: layout->offsets[5] = value; break;
        case kTagUserData:
            attributes->userData[userData] = reader.data;
            attributes->userDataSize[userData] = reader.length;
            userData++;
            break;
        default: break;
        }
        if (channel >= 0) {
            uint8_t *copy = (uint8_t *)SNDMEMI_alloc(reader.length);
            attributes->stretchData[channel] = copy;
            memcpy(copy, reader.data, uint32_t(reader.length));
            attributes->stretchDataSizes[channel] = reader.length;
        }
    }
    attributes->renderMode = renderMode;
    for (int i = format->channels - 1; i >= 0; i--) {
        if (OutputMode == 2 && format->channels == 2)
            azimuth[i] = 0;
        uint16_t defaultAzimuth = ChannelAzimuth[format->channels][i];
        attributes->azimuth[i] = defaultAzimuth + azimuth[i];
        uint16_t mode = attributes->renderMode;
        if ((mode & 0x14) != 0 || mode == 0)
            layout->offsets[i] += base;
    }
}
