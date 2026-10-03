#include "SndTagShadow.h"

#include "../sound/snd/Banks.h"
#include "../sound/snd/Voices.h"
#include "../platform/FileSys.h"
#include "../platform/RefPack.h"
#include "../../common/xbeOriginal.h"
#include "../../common/xboxPath.h"

#include <windows.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory>
#include <set>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_SNDTAGSHADOW=1, at injection time on the loader's thread before the game runs: the sound library's
// PT tag parsers and pure voice arithmetic (sound/snd/Banks.cpp, Voices.cpp; docs/driving/sound.md 9.3 steps 1
// and 3) against the originals, on the same inputs, compared byte for byte. The whole library's range is swapped
// back to the originals for the length of the test (common/xbeOriginal.h; swapping per call would cost a scan of
// every patch each time): the original's side calls the original's address, so only original code runs, and ours
// calls the port directly - whose own calls into other modules (SNDMEMI_alloc, memclr) then reach the originals.
//
// - Tags: every PT header on the disc - every patch of every distinct bank (.bnk in the .viv archives, refpack
//   unpacked) and every stream's SCHl header (.mus, .spe: every language) - plus perturbed copies of them (0xfc
//   padding, 4-byte lengths, unknown tags, 0xfd markers, 0xfe splits, new values, lengths over 4 that leave the
//   value stale, the blob, azimuth, user-data and channel tags the disc never uses) and random tag streams. Each
//   is walked by SNDI_gettag (the reader's state after every tag), parsed by SNDI_parsetimbre timbre by timbre
//   (return, cursor, the header and 16 guard bytes, prefilled) and by SNDI_patchtohdr (format, attributes, layout,
//   and its own sound heap - SNDMEMI_init on a zeroed arena for each side - so the blob copies and the heap's
//   bookkeeping compare too), in output modes 1 and 2 (the stereo azimuth branch). SNDI_getb on random bytes.
// - Arithmetic: a voice array of our own (the globals it is reached through are put back afterwards) with random
//   records for iSNDcalcvol, iSNDcalcpitch (and through it iSNDdetunetolinear), SNDI_calcfxlevel; random cents for
//   iSNDdetunetolinear; SNDI_precalcaztospkrvol's table for output modes 1..6 over realistic and random speaker
//   angles (table and globals restored between sides); SNDI_aztospkrvol over random tables and every azimuth row;
//   SNDI_pantoazimuth, SNDI_validrendermode (random mode tables), SND_attrsetdef, SNDCTRLI_getfxbus,
//   SNDBANKI_getppatch (every disc bank, in and out of range), SNDBANKI_findfreekey.
//
// Inputs the originals leave undefined are kept out (a long tag before any short one in a timbre: the reader's
// value is uninitialised; more than six channels; more than four user-data tags) or masked: SNDI_patchtohdr never
// initialises channel 1's azimuth offset, so for two or more channels without a 0x9d tag (and not output mode 2
// with two channels) that azimuth word is left out of the comparison - counted as "masked".
// ---------------------------------------------------------------------------------------------------------------

namespace {   // this file's own types

int g_cases, g_checks, g_differ, g_faults, g_masked, g_skipped;
uint32_t g_seed = 0x5eed1234u;

uint32_t Next() {
    g_seed ^= g_seed << 13;
    g_seed ^= g_seed >> 17;
    g_seed ^= g_seed << 5;
    return g_seed;
}

void Differ(const char *format, ...) {
    if (g_differ++ >= 10)
        return;
    char line[600];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    printf("[sndtagshadow] %s\n", line);
}

uint32_t Hash(const void *data, size_t size) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < size; i++)
        h = (h ^ ((const uint8_t *)data)[i]) * 16777619u;
    return h;
}

bool Same(const char *what, const char *where, const void *o, const void *p, size_t size) {
    g_checks++;
    if (memcmp(o, p, size) == 0)
        return true;
    size_t i = 0;
    while (((const uint8_t *)o)[i] == ((const uint8_t *)p)[i])
        i++;
    Differ("%s (%s): first difference at +%x: original %02x / ours %02x", what, where, (unsigned)i,
           ((const uint8_t *)o)[i], ((const uint8_t *)p)[i]);
    return false;
}

bool SameInt(const char *what, const char *where, intptr_t o, intptr_t p) {
    g_checks++;
    if (o == p)
        return true;
    Differ("%s (%s): original %lx / ours %lx", what, where, (long)o, (long)p);
    return false;
}

// The whole library swapped back to the originals for one side
struct Originals {
    bool on;
    explicit Originals(bool original) : on(original) {
        if (on) {
            XbeOriginal_RestoreRange(0x0013b7b0, 0x0014bee0, true);
            XbeOriginal_RestoreRange(0x00150360, 0x001503e0, true);
        }
    }
    ~Originals() {
        if (on) {
            XbeOriginal_RestoreRange(0x0013b7b0, 0x0014bee0, false);
            XbeOriginal_RestoreRange(0x00150360, 0x001503e0, false);
        }
    }
};

// ---- one call, either side, under SEH

enum Which {
    kGetb, kGettag, kParse, kPatch, kCalcVol, kCalcPitch, kDetune, kAzToSpkr, kPrecalc, kPan, kValidMode,
    kAttrDef, kGetFxBus, kCalcFx, kGetPPatch, kFindKey
};

struct Call {
    Which which;
    bool original;
    intptr_t a[5];
    intptr_t result;
};

void Dispatch(void *context) {
    Call *c = (Call *)context;
    intptr_t *a = c->a;
    bool o = c->original;
    switch (c->which) {
    case kGetb:
        c->result = o ? ((int (*)(const uint8_t *, int))0x00144a90)((const uint8_t *)a[0], (int)a[1])
                      : SNDI_getb((const uint8_t *)a[0], (int)a[1]);
        break;
    case kGettag:
        c->result = o ? ((int (*)(SND::TagReader *))0x001428d0)((SND::TagReader *)a[0])
                      : SNDI_gettag((SND::TagReader *)a[0]);
        break;
    case kParse:
        c->result = o ? ((int (*)(uint8_t **, SND::PatchHeader *))0x00140aa0)((uint8_t **)a[0], (SND::PatchHeader *)a[1])
                      : SNDI_parsetimbre((uint8_t **)a[0], (SND::PatchHeader *)a[1]);
        break;
    case kPatch:
        if (o)
            ((void (*)(int, uint8_t *, SND::StreamFormat *, SND::Attributes *, SND::StreamLayout *))0x0013f1c0)(
                (int)a[0], (uint8_t *)a[1], (SND::StreamFormat *)a[2], (SND::Attributes *)a[3],
                (SND::StreamLayout *)a[4]);
        else
            SNDI_patchtohdr((int)a[0], (uint8_t *)a[1], (SND::StreamFormat *)a[2], (SND::Attributes *)a[3],
                            (SND::StreamLayout *)a[4]);
        break;
    case kCalcVol:
        if (o)
            ((void (*)(int))0x0013e5d0)((int)a[0]);
        else
            iSNDcalcvol((int)a[0]);
        break;
    case kCalcPitch:
        if (o)
            ((void (*)(int))0x0013e700)((int)a[0]);
        else
            iSNDcalcpitch((int)a[0]);
        break;
    case kDetune:
        c->result = o ? ((int (*)(int))0x0013e660)((int)a[0]) : iSNDdetunetolinear((int)a[0]);
        break;
    case kAzToSpkr:
        if (o)
            ((void (*)(int, int16_t *))0x00141230)((int)a[0], (int16_t *)a[1]);
        else
            SNDI_aztospkrvol((int)a[0], (int16_t *)a[1]);
        break;
    case kPrecalc:
        if (o)
            ((void (*)(void))0x00141070)();
        else
            SNDI_precalcaztospkrvol();
        break;
    case kPan:
        c->result = o ? ((int (*)(int))0x00142e90)((int)a[0]) : SNDI_pantoazimuth((int)a[0]);
        break;
    case kValidMode:
        c->result = o ? ((int (*)(int *, SND::PatchHeader *))0x00142830)((int *)a[0], (SND::PatchHeader *)a[1])
                      : SNDI_validrendermode((int *)a[0], (SND::PatchHeader *)a[1]);
        break;
    case kAttrDef:
        c->result = o ? ((int (*)(SND::Attributes *))0x00142970)((SND::Attributes *)a[0])
                      : SND_attrsetdef((SND::Attributes *)a[0]);
        break;
    case kGetFxBus:
        c->result = o ? (intptr_t)((SND::FxBus *(*)(int, int))0x0013cc80)((int)a[0], (int)a[1])
                      : (intptr_t)SNDCTRLI_getfxbus((int)a[0], (int)a[1]);
        break;
    case kCalcFx:
        if (o)
            ((void (*)(int, int))0x001401b0)((int)a[0], (int)a[1]);
        else
            SNDI_calcfxlevel((int)a[0], (int)a[1]);
        break;
    case kGetPPatch:
        c->result = o ? (intptr_t)((uint8_t *(*)(SND::BankHeader *, int))0x00140810)((SND::BankHeader *)a[0], (int)a[1])
                      : (intptr_t)SNDBANKI_getppatch((SND::BankHeader *)a[0], (int)a[1]);
        break;
    case kFindKey:
        c->result = o ? ((int (*)(void))0x00140200)() : SNDBANKI_findfreekey();
        break;
    }
}

bool Guarded(Call *c) {
#ifdef _MSC_VER
    __try {
        Dispatch(c);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    Dispatch(c);   // no SEH: a fault ends the test
    return true;
#endif
}

// One side: the original (at its address) or ours (called directly)
bool Side(Call *c, bool original) {
    c->original = original;
    c->result = (intptr_t)0xcccccccc;
    return Guarded(c);
}

// Both sides ran (or both faulted); false if only one did
bool BothRan(const char *what, const char *where, bool okO, bool okP) {
    g_checks++;
    if (okO == okP) {
        if (!okO)
            g_faults++;
        return okO;
    }
    Differ("%s (%s): %s faulted, %s did not", what, where, okO ? "ours" : "the original", okO ? "the original" : "ours");
    return false;
}

// ---- the library's globals the tests point elsewhere (put back at the end)

struct SavedRange {
    uint32_t at;
    std::vector<uint8_t> bytes;
    SavedRange(uint32_t lo, uint32_t hi) : at(lo), bytes((uint8_t *)(uintptr_t)lo, (uint8_t *)(uintptr_t)hi) {}
    void Restore() const { memcpy((void *)(uintptr_t)at, bytes.data(), bytes.size()); }
};

#define OutputMode   (*(uint8_t *)0x00244d10u)
#define ModeCount    (*(uint8_t *)0x00244d15u)
#define SndHeap      (*(void **)0x00244f6cu)
#define VoiceArray   (*(uint8_t **)0x00244f3cu)
#define NumVoicesG   (*(int16_t *)0x00244ed8u)
#define MasterVolume (*(uint8_t *)0x00244ed1u)
#define NextKey      (*(uint8_t *)0x00245374u)
const uint32_t kGainTable = 0x00245378, kGainTableEnd = 0x00245978;
const uint32_t kAngles = 0x00244d30, kAnglesEnd = 0x00244d90;   // the speaker angles (+ 12 x mode from 0x244d34)
const uint32_t kChannelAz = 0x00244f70, kChannelAzEnd = 0x00244fb8;   // default azimuth per channel layout

// ---- the disc's PT headers

struct Input {
    std::string name;
    uint8_t *pt;      // "PT"
    uint8_t *end;     // the buffer's end
};

std::vector<std::unique_ptr<std::vector<uint8_t>>> g_buffers;
std::vector<Input> g_inputs;
std::vector<uint8_t *> g_banks;   // their headers
int g_bankPatches, g_streams, g_perturbed, g_synthetic;

uint8_t *Keep(std::vector<uint8_t> &&bytes) {
    g_buffers.push_back(std::unique_ptr<std::vector<uint8_t>>(new std::vector<uint8_t>(std::move(bytes))));
    return g_buffers.back()->data();
}

uint32_t LE32(const uint8_t *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

void ReadArchive(const char *path, const char *file, std::set<uint32_t> *seen) {
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;
    uint8_t header[16];
    DWORD got = 0;
    ReadFile(h, header, 16, &got, NULL);
    int dirSize = got == 16 ? BIG_dirsize(header) : 0;
    std::vector<uint8_t> dir(dirSize > 16 ? dirSize : 16);
    SetFilePointer(h, 0, NULL, FILE_BEGIN);
    ReadFile(h, dir.data(), (DWORD)dirSize, &got, NULL);
    for (int i = 0;; i++) {
        int offset, size;
        const char *name = BIG_find(dir.data(), NULL, i, &offset, &size);
        if (name == NULL)
            break;
        std::string n = name;
        bool bank = n.size() > 4 && _stricmp(n.c_str() + n.size() - 4, ".bnk") == 0;
        if (bank) {
            std::vector<uint8_t> raw(size + 16);
            SetFilePointer(h, offset, NULL, FILE_BEGIN);
            ReadFile(h, raw.data(), (DWORD)size, &got, NULL);
            std::vector<uint8_t> data;
            unsigned unpacked = unpacksizez(raw.data());
            if (unpacked != 0) {
                data.assign(unpacked + 64, 0);
                UNPACK_unpack(raw.data(), data.data());
            } else {
                data.assign(raw.begin(), raw.begin() + size);
                data.resize(data.size() + 64, 0);
                unpacked = (unsigned)size;
            }
            if (unpacked < 0x14 || memcmp(data.data(), "BNKl", 4) != 0)
                continue;
            if (!seen->insert(Hash(data.data(), unpacked)).second)
                continue;
            size_t total = data.size();
            uint8_t *b = Keep(std::move(data));
            g_banks.push_back(b);
            uint16_t count = *(uint16_t *)(b + 6);
            for (int p = 0; p < count && 0x18 + 4 * (size_t)p <= total; p++) {
                uint32_t off = LE32(b + 0x14 + 4 * p);
                size_t at = 0x14 + 4 * (size_t)p + off;
                if (off == 0 || at + 4 > total || b[at] != 'P' || b[at + 1] != 'T')
                    continue;
                Input in;
                char where[200];
                snprintf(where, sizeof(where), "%s %s patch %d", file, name, p);
                in.name = where;
                in.pt = b + at;
                in.end = b + total;
                g_inputs.push_back(in);
                g_bankPatches++;
            }
        } else {
            uint8_t head[16];
            if (size < 16)
                continue;
            SetFilePointer(h, offset, NULL, FILE_BEGIN);
            ReadFile(h, head, 16, &got, NULL);
            if (got != 16 || memcmp(head, "SCHl", 4) != 0 || head[8] != 'P' || head[9] != 'T')
                continue;
            uint32_t chunk = LE32(head + 4);
            if (chunk > (uint32_t)size)
                chunk = (uint32_t)size;
            if (chunk > 0x10000)
                chunk = 0x10000;
            if (chunk < 16)
                chunk = 16;
            std::vector<uint8_t> raw(chunk + 32, 0);
            SetFilePointer(h, offset, NULL, FILE_BEGIN);
            ReadFile(h, raw.data(), chunk, &got, NULL);
            size_t total = raw.size();
            uint8_t *s = Keep(std::move(raw));
            Input in;
            in.name = std::string(file) + " " + name;
            in.pt = s + 8;
            in.end = s + total;
            g_inputs.push_back(in);
            g_streams++;
        }
    }
    CloseHandle(h);
}

// ---- a tag walker of our own (SNDI_gettag's rules): what an input contains, whether it is defined input

struct Item {
    uint8_t tag;
    std::vector<uint8_t> data;
    bool marker;      // 0xfd / 0xfe: no length
    bool longLength;  // 0xff + 4 bytes
    int padding;      // 0xfc before it
};

struct Walk {
    bool ok;              // in bounds, ends with 0xff
    bool undefinedValue;  // a long tag before any short one in a timbre
    bool has9d;
    int userData;         // 0x14 tags in all
    int maxChannels;      // the largest channel count in effect (as an unsigned byte) at any timbre's end
    int timbres;
    std::vector<Item> items;
};

Walk WalkTags(const uint8_t *pt, const uint8_t *end) {
    Walk w = {};
    w.ok = false;
    const uint8_t *p = pt + 4;
    bool known = false;
    int32_t value = 0;
    int channels = 1;
    w.timbres = 1;
    int pad = 0;
    for (;;) {
        if (p >= end)
            return w;
        if (*p == 0xfc) {
            p++;
            pad++;
            continue;
        }
        uint8_t t = *p++;
        if (t == 0xff)
            break;
        Item it;
        it.tag = t;
        it.padding = pad;
        it.longLength = false;
        it.marker = false;
        pad = 0;
        if (t == 0xfd || t == 0xfe) {
            it.marker = true;
            w.items.push_back(it);
            if (t == 0xfe) {
                if (channels > w.maxChannels)
                    w.maxChannels = channels;
                channels = 1;
                known = false;
                w.timbres++;
            }
            continue;
        }
        if (p >= end)
            return w;
        uint32_t length = *p;
        if (length == 0xff) {
            if (p + 5 > end)
                return w;
            length = ((uint32_t)p[1] << 24) | (p[2] << 16) | (p[3] << 8) | p[4];
            p += 4;
            it.longLength = true;
        }
        p++;
        if (length > (uint32_t)(end - p))
            return w;
        if (length <= 4) {
            uint32_t v = 0;
            for (uint32_t i = 0; i < length; i++)
                v = (v << 8) + p[i];
            value = (int32_t)v;
            if (length == 1 && value > 0x7f) value -= 0x100;
            if (length == 2 && value > 0x7fff) value -= 0x10000;
            if (length == 3 && value > 0x7fffff) value -= 0x1000000;
            known = true;
        } else if (!known) {
            w.undefinedValue = true;
        }
        it.data.assign(p, p + length);
        p += length;
        if (t == 0x14)
            w.userData++;
        if (t == 0x9d)
            w.has9d = true;
        if (t == 0x82)
            channels = value & 0xff;
        w.items.push_back(it);
    }
    if (channels > w.maxChannels)
        w.maxChannels = channels;
    w.ok = true;
    return w;
}

std::vector<uint8_t> Encode(const std::vector<Item> &items) {
    std::vector<uint8_t> out = { 'P', 'T', 0, 0 };
    for (const Item &it : items) {
        out.insert(out.end(), (size_t)it.padding, (uint8_t)0xfc);
        out.push_back(it.tag);
        if (it.marker)
            continue;
        uint32_t n = (uint32_t)it.data.size();
        if (it.longLength || n >= 0xff) {
            out.push_back(0xff);
            out.push_back((uint8_t)(n >> 24));
            out.push_back((uint8_t)(n >> 16));
            out.push_back((uint8_t)(n >> 8));
            out.push_back((uint8_t)n);
        } else {
            out.push_back((uint8_t)n);
        }
        out.insert(out.end(), it.data.begin(), it.data.end());
    }
    out.push_back(0xff);
    out.insert(out.end(), 32, (uint8_t)0);
    return out;
}

Item Short(uint8_t tag, int length) {
    Item it;
    it.tag = tag;
    it.marker = false;
    it.longLength = false;
    it.padding = 0;
    for (int i = 0; i < length; i++)
        it.data.push_back((uint8_t)Next());
    return it;
}

uint8_t RandomTag() {
    static const uint8_t unknown[] = { 0x26, 0x30, 0x40, 0x7f, 0xa8, 0xb0, 0xc0, 0xf0, 0xfb };
    uint32_t r = Next() % 100;
    if (r < 45)
        return (uint8_t)(Next() % 0x26);
    if (r < 92)
        return (uint8_t)(0x80 + Next() % 0x28);
    return unknown[Next() % sizeof(unknown)];
}

Item RandomItem() {
    uint8_t tag = RandomTag();
    bool blob = (tag >= 0x98 && tag <= 0x9b) || tag == 0xa4 || tag == 0xa5 || tag == 0x14;
    int length;
    uint32_t r = Next() % 100;
    if (blob)
        length = (int)(Next() % 41);
    else if (r < 75)
        length = (int)(Next() % 5);
    else
        length = 5 + (int)(Next() % 8);
    Item it = Short(tag, length);
    if (tag == 0x82)
        it.data.assign(1, (uint8_t)(Next() % 7));
    if (Next() % 10 == 0)
        it.longLength = true;
    if (Next() % 8 == 0)
        it.padding = 1 + (int)(Next() % 3);
    return it;
}

// The constraints that keep an input defined: each timbre starts with a short tag; channel tags 0..6 in one byte;
// at most four user-data tags
void MakeDefined(std::vector<Item> *items) {
    std::vector<Item> out;
    bool needShort = true;
    int userData = 0;
    for (Item it : *items) {
        if (it.marker) {
            out.push_back(it);
            if (it.tag == 0xfe)
                needShort = true;
            continue;
        }
        if (it.tag == 0x14 && ++userData > 4)
            continue;
        if (it.tag == 0x82) {
            if (it.data.size() != 1 || it.data[0] > 6)
                it.data.assign(1, (uint8_t)(Next() % 7));
            it.longLength = false;
        }
        if (needShort && (it.data.size() > 4 || it.longLength)) {
            out.push_back(Short(0x05, 1));
        }
        needShort = false;
        out.push_back(it);
    }
    *items = out;
}

std::vector<Item> Mutate(const std::vector<Item> &source) {
    std::vector<Item> items = source;
    int mutations = 1 + (int)(Next() % 4);
    for (int m = 0; m < mutations; m++) {
        size_t at = items.empty() ? 0 : Next() % (items.size() + 1);
        switch (Next() % 9) {
        case 0:   // padding
            if (!items.empty())
                items[Next() % items.size()].padding = 1 + (int)(Next() % 4);
            break;
        case 1:   // 4-byte lengths
            for (Item &it : items)
                if (!it.marker && Next() % 3 == 0)
                    it.longLength = true;
            break;
        case 2:   // unknown tags
            items.insert(items.begin() + at, Short(RandomTag(), (int)(Next() % 7)));
            break;
        case 3: { // markers
            Item it;
            it.tag = Next() % 3 == 0 ? 0xfe : 0xfd;
            it.marker = true;
            it.longLength = false;
            it.padding = 0;
            items.insert(items.begin() + at, it);
            break;
        }
        case 4:   // new values
            for (Item &it : items)
                if (!it.marker && Next() % 3 == 0) {
                    int n = (int)(Next() % 5);
                    it.data.clear();
                    for (int i = 0; i < n; i++)
                        it.data.push_back((uint8_t)Next());
                }
            break;
        case 5:   // stale values: a scalar tag with more than four bytes
            items.insert(items.begin() + at, Short(RandomTag(), 5 + (int)(Next() % 6)));
            break;
        case 6: { // the blob tags
            static const uint8_t blobs[] = { 0x98, 0x99, 0x9a, 0x9b, 0xa4, 0xa5 };
            items.insert(items.begin() + at, Short(blobs[Next() % 6], (int)(Next() % 41)));
            break;
        }
        case 7: { // azimuth offsets, user data, channels
            static const uint8_t tags[] = { 0x9c, 0x9d, 0x9e, 0x9f, 0xa6, 0xa7, 0x14, 0x82, 0x82, 0x8c, 0x88, 0x89 };
            uint8_t tag = tags[Next() % sizeof(tags)];
            items.insert(items.begin() + at, Short(tag, tag == 0x14 ? (int)(Next() % 13) : 1 + (int)(Next() % 4)));
            break;
        }
        default:  // random items anywhere
            for (int i = 0, n = 1 + (int)(Next() % 6); i < n; i++)
                items.insert(items.begin() + (items.empty() ? 0 : Next() % (items.size() + 1)), RandomItem());
            break;
        }
    }
    MakeDefined(&items);
    return items;
}

std::vector<Item> Synthetic() {
    std::vector<Item> items;
    int timbres = 1 + (int)(Next() % 3);
    for (int t = 0; t < timbres; t++) {
        if (t > 0) {
            Item it;
            it.tag = 0xfe;
            it.marker = true;
            it.longLength = false;
            it.padding = (int)(Next() % 2);
            items.push_back(it);
        }
        for (int i = 0, n = 1 + (int)(Next() % 20); i < n; i++) {
            if (i > 0 && Next() % 15 == 0) {
                Item it;
                it.tag = 0xfd;
                it.marker = true;
                it.longLength = false;
                it.padding = 0;
                items.push_back(it);
            }
            items.push_back(RandomItem());
        }
    }
    MakeDefined(&items);
    return items;
}

// ---- the tag tests

void TestGettag(const Input &in) {
    SND::TagReader o, p;
    memset(&o, 0, sizeof(o));
    o.cursor = in.pt + 4;
    o.value = 0x13572468;
    p = o;
    for (int step = 0; step < 4096; step++) {
        Call co = { kGettag, true, { (intptr_t)&o } }, cp = { kGettag, false, { (intptr_t)&p } };
        bool okO = Side(&co, true), okP = Side(&cp, false);
        if (!BothRan("SNDI_gettag", in.name.c_str(), okO, okP))
            return;
        if (!SameInt("SNDI_gettag return", in.name.c_str(), co.result, cp.result) ||
            !Same("SNDI_gettag reader", in.name.c_str(), &o, &p, sizeof(o)))
            return;
        if (co.result == 0)
            return;
    }
}

void TestParse(const Input &in) {
    uint8_t *co = in.pt + 4, *cp = in.pt + 4;
    for (int timbre = 0; timbre < 32; timbre++) {
        uint8_t ho[sizeof(SND::PatchHeader) + 16], hp[sizeof(ho)];
        memset(ho, 0xa5, sizeof(ho));
        memset(hp, 0xa5, sizeof(hp));
        Call o = { kParse, true, { (intptr_t)&co, (intptr_t)ho } }, p = { kParse, false, { (intptr_t)&cp, (intptr_t)hp } };
        bool okO = Side(&o, true), okP = Side(&p, false);
        g_cases++;
        if (!BothRan("SNDI_parsetimbre", in.name.c_str(), okO, okP))
            return;
        if (!SameInt("SNDI_parsetimbre return", in.name.c_str(), o.result, p.result) ||
            !SameInt("SNDI_parsetimbre cursor", in.name.c_str(), (intptr_t)co, (intptr_t)cp) ||
            !Same("SNDI_parsetimbre header", in.name.c_str(), ho, hp, sizeof(ho)))
            return;
        if (o.result == 0)
            return;
    }
}

const size_t kHeap = 0x4000;
uint8_t *g_heap;
std::vector<uint8_t> g_heapCopy;

void TestPatch(const Input &in, const Walk &w) {
    int base = (Next() & 1) ? 0 : 0x01230000;
    struct Out {
        SND::StreamFormat format;
        uint8_t guard0[8];
        SND::Attributes attributes;
        uint8_t guard1[8];
        SND::StreamLayout layout;
        uint8_t guard2[8];
    } oo, op;
    memset(&oo, 0x5a, sizeof(oo));
    memset(&op, 0x5a, sizeof(op));
    // the original's side
    memset(g_heap, 0, kHeap);
    ((void (*)(void *, int))0x0013f710)(g_heap, (int)kHeap);   // SNDMEMI_init
    Call o = { kPatch, true, { base, (intptr_t)in.pt, (intptr_t)&oo.format, (intptr_t)&oo.attributes, (intptr_t)&oo.layout } };
    bool okO = Side(&o, true);
    g_heapCopy.assign(g_heap, g_heap + kHeap);
    // ours
    memset(g_heap, 0, kHeap);
    ((void (*)(void *, int))0x0013f710)(g_heap, (int)kHeap);
    Call p = { kPatch, false, { base, (intptr_t)in.pt, (intptr_t)&op.format, (intptr_t)&op.attributes, (intptr_t)&op.layout } };
    bool okP = Side(&p, false);
    g_cases++;
    if (!BothRan("SNDI_patchtohdr", in.name.c_str(), okO, okP))
        return;
    int channels = oo.format.channels;
    if (channels >= 2 && !(OutputMode == 2 && channels == 2) && !w.has9d) {
        oo.attributes.azimuth[1] = 0;
        op.attributes.azimuth[1] = 0;
        g_masked++;
    }
    if (Same("SNDI_patchtohdr outputs", in.name.c_str(), &oo, &op, sizeof(oo)))
        Same("SNDI_patchtohdr sound heap", in.name.c_str(), g_heapCopy.data(), g_heap, kHeap);
}

void TestGetb() {
    for (int i = 0; i < 20000; i++) {
        uint8_t bytes[16];
        for (int k = 0; k < 16; k++)
            bytes[k] = (uint8_t)Next();
        int length = (int)(Next() % 9);
        Call o = { kGetb, true, { (intptr_t)bytes, length } }, p = { kGetb, false, { (intptr_t)bytes, length } };
        bool okO = Side(&o, true), okP = Side(&p, false);
        g_cases++;
        char where[64];
        snprintf(where, sizeof(where), "length %d %02x%02x%02x%02x", length, bytes[0], bytes[1], bytes[2], bytes[3]);
        if (BothRan("SNDI_getb", where, okO, okP))
            SameInt("SNDI_getb", where, o.result, p.result);
    }
}

void TestTags() {
    std::vector<Input> generated;
    // perturbed copies of a sample of the disc's headers, and random streams
    size_t discCount = g_inputs.size();
    for (size_t i = 0; i < discCount; i++) {
        if (Next() % 3 != 0)
            continue;
        Walk w = WalkTags(g_inputs[i].pt, g_inputs[i].end);
        if (!w.ok)
            continue;
        for (int k = 0; k < 2; k++) {
            std::vector<uint8_t> bytes = Encode(Mutate(w.items));
            size_t n = bytes.size();
            uint8_t *b = Keep(std::move(bytes));
            Input in;
            in.name = g_inputs[i].name + " (perturbed)";
            in.pt = b;
            in.end = b + n;
            generated.push_back(in);
            g_perturbed++;
        }
    }
    for (int i = 0; i < 3000; i++) {
        std::vector<uint8_t> bytes = Encode(Synthetic());
        size_t n = bytes.size();
        uint8_t *b = Keep(std::move(bytes));
        Input in;
        char name[64];
        snprintf(name, sizeof(name), "random stream %d", i);
        in.name = name;
        in.pt = b;
        in.end = b + n;
        generated.push_back(in);
        g_synthetic++;
    }
    std::vector<Input> all = g_inputs;
    all.insert(all.end(), generated.begin(), generated.end());

    uint8_t savedMode = OutputMode;
    SavedRange channelAz(kChannelAz, kChannelAzEnd);
    for (uint32_t a = kChannelAz; a < kChannelAzEnd; a += 2)   // default azimuths a wrong index would show
        *(uint16_t *)(uintptr_t)a = (uint16_t)(0x1000 + (a - kChannelAz) * 0x135);
    void *savedHeap = SndHeap;
    for (const Input &in : all) {
        Walk w = WalkTags(in.pt, in.end);
        if (!w.ok || w.undefinedValue) {
            g_skipped++;
            continue;
        }
        TestGettag(in);
        if (w.maxChannels > 6 || w.userData > 4) {
            g_skipped++;
            continue;
        }
        for (int mode = 1; mode <= 2; mode++) {
            OutputMode = (uint8_t)mode;
            TestParse(in);
            TestPatch(in, w);
        }
    }
    SndHeap = savedHeap;
    OutputMode = savedMode;
    channelAz.Restore();
}

// ---- the arithmetic

const int kVoices = 224;
uint8_t g_voiceBuffer[(kVoices + 2) * 0x88];
uint8_t *g_voices = g_voiceBuffer + 0x88;
int8_t g_table256[256];
int8_t g_table512[512];   // indexed by a signed byte from its middle

void RandomRecord(uint8_t *v) {
    for (int i = 0; i < 0x88; i++)
        v[i] = (uint8_t)Next();
}

// Runs one call on voice 'index' from the same record twice; compares the record and its neighbours.
void VoicePair(Which which, int a0, int a1, int index, const char *what) {
    uint8_t *record = g_voices + index * 0x88;
    uint8_t before[3 * 0x88], afterO[3 * 0x88];
    memcpy(before, record - 0x88, sizeof(before));
    Call o = { which, true, { a0, a1 } }, p = { which, false, { a0, a1 } };
    bool okO = Side(&o, true);
    memcpy(afterO, record - 0x88, sizeof(afterO));
    memcpy(record - 0x88, before, sizeof(before));
    bool okP = Side(&p, false);
    g_cases++;
    char where[80];
    snprintf(where, sizeof(where), "voice %d, record %08x", index, Hash(before, sizeof(before)));
    if (BothRan(what, where, okO, okP))
        Same(what, where, afterO, record - 0x88, sizeof(afterO));
}

void TestVoices() {
    uint8_t *savedArray = VoiceArray;
    int16_t savedCount = NumVoicesG;
    uint8_t savedMaster = MasterVolume, savedKey = NextKey;
    SavedRange fxbus(0x00244f44, 0x00244f6c);
    VoiceArray = g_voices;
    NumVoicesG = kVoices;
    for (int i = 0; i < 256; i++)
        g_table256[i] = (int8_t)Next();
    for (int i = 0; i < 512; i++)
        g_table512[i] = (int8_t)Next();

    for (int i = 0; i < 20000; i++) {   // iSNDcalcvol
        int index = (int)(Next() % kVoices);
        SND::Voice *v = (SND::Voice *)(g_voices + index * 0x88);
        RandomRecord((uint8_t *)v);
        if (Next() % 4 == 0)
            v->fade = (int32_t)((Next() % 0x80) << 16);
        if (Next() % 4 == 0)
            v->env = (int32_t)((Next() % 0x80) << 16);
        v->volLfo = Next() % 2 ? g_table256 : NULL;
        v->volTable = Next() % 2 ? g_table512 + 256 : NULL;
        MasterVolume = (uint8_t)(Next() % 3 == 0 ? 0x7f : Next());
        VoicePair(kCalcVol, index, 0, index, "iSNDcalcvol");
    }
    for (int i = 0; i < 20000; i++) {   // iSNDcalcpitch, iSNDdetunetolinear through it
        int index = (int)(Next() % kVoices);
        SND::Voice *v = (SND::Voice *)(g_voices + index * 0x88);
        RandomRecord((uint8_t *)v);
        if (Next() % 5 != 0)
            v->detuneLinear = 0;
        if (Next() % 4 == 0)
            v->bendRange = 0;
        v->bendTable = Next() % 2 ? g_table512 + 256 : NULL;
        v->pitchLfo = Next() % 2 ? g_table256 : NULL;
        VoicePair(kCalcPitch, index, 0, index, "iSNDcalcpitch");
    }
    for (int i = 0; i < 20000; i++) {   // SNDI_calcfxlevel
        static const uint16_t modes[] = { 4, 0x24, 0x400, 0x420, 0x4e0, 0x8c4 };   // all with a bus
        int index = (int)(Next() % kVoices);
        SND::Voice *v = (SND::Voice *)(g_voices + index * 0x88);
        RandomRecord((uint8_t *)v);
        v->renderMode = modes[Next() % (sizeof(modes) / 2)];
        for (uint32_t a = 0x00244f44; a < 0x00244f6c; a++)
            *(uint8_t *)(uintptr_t)a = (uint8_t)Next();
        int bus = (int)(Next() % 2);
        VoicePair(kCalcFx, bus, index, index, "SNDI_calcfxlevel");
    }
    for (int i = 0; i < 2000; i++) {   // SNDBANKI_findfreekey
        for (int k = 0; k < kVoices; k++) {
            SND::Voice *v = (SND::Voice *)(g_voices + k * 0x88);
            v->inUse = (uint8_t)(Next() % 3 == 0 ? 0 : 1);
            v->key = (uint8_t)(Next() % 8 == 0 ? Next() : Next() % 4);
        }
        uint8_t start = (uint8_t)(Next() % 4 == 0 ? 0xfe + Next() % 2 : Next());
        NextKey = start;
        Call o = { kFindKey, true, {} }, p = { kFindKey, false, {} };
        bool okO = Side(&o, true);
        uint8_t keyO = NextKey;
        NextKey = start;
        bool okP = Side(&p, false);
        g_cases++;
        char where[40];
        snprintf(where, sizeof(where), "counter %02x", start);
        if (BothRan("SNDBANKI_findfreekey", where, okO, okP) &&
            SameInt("SNDBANKI_findfreekey", where, o.result, p.result))
            SameInt("SNDBANKI_findfreekey counter", where, keyO, NextKey);
    }

    VoiceArray = savedArray;
    NumVoicesG = savedCount;
    MasterVolume = savedMaster;
    NextKey = savedKey;
    fxbus.Restore();
}

void TestDetune() {
    for (int i = 0; i < 30000; i++) {
        int cents;
        uint32_t r = Next() % 100;
        if (r < 50)
            cents = (int)(Next() % 10001) - 5000;
        else if (r < 70)
            cents = ((int)(Next() % 41) - 20) * 1200 + (int)(Next() % 3) - 1;
        else if (r < 90)
            cents = (int)(Next() % 100001) - 50000;
        else if (r < 97)
            cents = (int)(Next() >> (Next() % 32)) * ((Next() & 1) ? 1 : -1);
        else
            cents = (Next() & 1) ? 0x7fffffff - (int)(Next() % 3) : (int)0x80000000u + (int)(Next() % 3);
        Call o = { kDetune, true, { cents } }, p = { kDetune, false, { cents } };
        bool okO = Side(&o, true), okP = Side(&p, false);
        g_cases++;
        char where[40];
        snprintf(where, sizeof(where), "cents %d", cents);
        if (BothRan("iSNDdetunetolinear", where, okO, okP))
            SameInt("iSNDdetunetolinear", where, o.result, p.result);
    }
}

void TestSpeakers() {
    uint8_t savedMode = OutputMode;
    SavedRange angles(kAngles, kAnglesEnd), table(kGainTable, kGainTableEnd);
    // SNDI_precalcaztospkrvol: modes 1..6, the console's angles and random ones
    for (int mode = 1; mode <= 6; mode++) {
        for (int set = 0; set < 40; set++) {
            uint16_t *a = (uint16_t *)(uintptr_t)(0x00244d34u + (uint32_t)mode * 12);
            if (set == 0) {
                static const uint16_t console[7][6] = {
                    {}, { 0 }, { 0x4000, 0xc000 }, { 0x2000, 0x6000, 0xe000 }, { 0x2000, 0x6000, 0xa000, 0xe000 },
                    { 0x0000, 0x2000, 0x6000, 0xa000, 0xe000 }, { 0x0000, 0x2000, 0x6000, 0xa000, 0xc000, 0xe000 } };
                memcpy(a, console[mode], 12);
            } else {
                std::set<uint16_t> picked;
                while ((int)picked.size() < mode)
                    picked.insert((uint16_t)(set < 20 ? (Next() & 0xffff) : (Next() % 64) * 0x400));
                int k = 0;
                for (uint16_t v : picked)
                    a[k++] = v;
            }
            OutputMode = (uint8_t)mode;
            memset((void *)(uintptr_t)kGainTable, 0x33, kGainTableEnd - kGainTable);
            Call o = { kPrecalc, true, {} }, p = { kPrecalc, false, {} };
            bool okO = Side(&o, true);
            std::vector<uint8_t> tableO((uint8_t *)(uintptr_t)kGainTable, (uint8_t *)(uintptr_t)kGainTableEnd);
            memset((void *)(uintptr_t)kGainTable, 0x33, kGainTableEnd - kGainTable);
            bool okP = Side(&p, false);
            g_cases++;
            char where[80];
            snprintf(where, sizeof(where), "mode %d, angles %04x %04x %04x %04x %04x %04x", mode, a[0], a[1], a[2],
                     a[3], a[4], a[5]);
            if (BothRan("SNDI_precalcaztospkrvol", where, okO, okP))
                Same("SNDI_precalcaztospkrvol table", where, tableO.data(), (void *)(uintptr_t)kGainTable,
                     tableO.size());
        }
    }
    // SNDI_aztospkrvol: random tables, every row and random azimuths
    for (int mode = 0; mode <= 6; mode++) {
        OutputMode = (uint8_t)mode;
        for (uint32_t at = kGainTable; at < kGainTableEnd; at++)
            *(uint8_t *)(uintptr_t)at = (uint8_t)Next();
        for (int i = 0; i < 1024; i++) {
            int azimuth = i < 256 ? (i << 8) | (int)(Next() & 0xff) : (int)Next();
            int16_t go[8], gp[8];
            memset(go, 0x77, sizeof(go));
            memset(gp, 0x77, sizeof(gp));
            Call o = { kAzToSpkr, true, { azimuth, (intptr_t)go } }, p = { kAzToSpkr, false, { azimuth, (intptr_t)gp } };
            bool okO = Side(&o, true), okP = Side(&p, false);
            g_cases++;
            char where[40];
            snprintf(where, sizeof(where), "mode %d, azimuth %x", mode, azimuth);
            if (BothRan("SNDI_aztospkrvol", where, okO, okP))
                Same("SNDI_aztospkrvol", where, go, gp, sizeof(go));
        }
    }
    OutputMode = savedMode;
    angles.Restore();
    table.Restore();

    for (int pan = 0; pan < 256; pan++) {   // SNDI_pantoazimuth
        Call o = { kPan, true, { pan } }, p = { kPan, false, { pan } };
        bool okO = Side(&o, true), okP = Side(&p, false);
        g_cases++;
        char where[32];
        snprintf(where, sizeof(where), "pan %d", pan);
        if (BothRan("SNDI_pantoazimuth", where, okO, okP))
            SameInt("SNDI_pantoazimuth", where, o.result, p.result);
    }
}

void TestSmall() {
    // SNDI_validrendermode: random mode tables and patches
    uint8_t savedCount = ModeCount;
    SavedRange modes(0x00244d18, 0x00244d28);
    static const uint16_t interesting[] = { 0x420, 0x24, 0x400, 0x4, 0x10, 0x20, 0x40, 0x80, 0x480, 0x84, 0x4a0,
                                            0x1c, 0x71c, 0xe0, 0x0, 0x8, 0x100, 0x200, 0x30, 0xffff };
    const int nInteresting = sizeof(interesting) / 2;
    for (int i = 0; i < 20000; i++) {
        int count = (int)(Next() % 5);
        ModeCount = (uint8_t)count;
        for (int k = 0; k < 8; k++)
            *(uint16_t *)(uintptr_t)(0x00244d18u + k * 2) =
                (uint16_t)(Next() % 3 == 0 ? Next() : interesting[Next() % nInteresting]);
        SND::PatchHeader header;
        memset(&header, 0, sizeof(header));
        header.renderMode = (uint16_t)(Next() % 3 == 0 ? Next() : interesting[Next() % nInteresting]);
        header.channels = (int8_t)(Next() % 3 == 0 ? Next() : 1 + Next() % 2);
        int start = (int)(Next() % 6) - 1;
        int io = start, ip = start;
        Call o = { kValidMode, true, { (intptr_t)&io, (intptr_t)&header } },
             p = { kValidMode, false, { (intptr_t)&ip, (intptr_t)&header } };
        bool okO = Side(&o, true), okP = Side(&p, false);
        g_cases++;
        char where[80];
        snprintf(where, sizeof(where), "count %d start %d patch mode %x channels %d", count, start, header.renderMode,
                 header.channels);
        if (BothRan("SNDI_validrendermode", where, okO, okP) &&
            SameInt("SNDI_validrendermode", where, o.result, p.result))
            SameInt("SNDI_validrendermode index", where, io, ip);
    }
    ModeCount = savedCount;
    modes.Restore();

    for (int i = 0; i < 100; i++) {   // SND_attrsetdef
        uint8_t ao[0x70], ap[0x70];
        for (int k = 0; k < 0x70; k++)
            ao[k] = (uint8_t)Next();
        memcpy(ap, ao, sizeof(ap));
        Call o = { kAttrDef, true, { (intptr_t)ao } }, p = { kAttrDef, false, { (intptr_t)ap } };
        bool okO = Side(&o, true), okP = Side(&p, false);
        g_cases++;
        if (BothRan("SND_attrsetdef", "random", okO, okP) && SameInt("SND_attrsetdef", "return", o.result, p.result))
            Same("SND_attrsetdef", "random", ao, ap, sizeof(ao));
    }

    for (int i = 0; i < 5000; i++) {   // SNDCTRLI_getfxbus
        int bus = (int)(Next() % 7) - 2;
        int mode = (int)(Next() % 2 ? Next() : interesting[Next() % nInteresting]);
        Call o = { kGetFxBus, true, { bus, mode } }, p = { kGetFxBus, false, { bus, mode } };
        bool okO = Side(&o, true), okP = Side(&p, false);
        g_cases++;
        char where[40];
        snprintf(where, sizeof(where), "bus %d mode %x", bus, mode);
        if (BothRan("SNDCTRLI_getfxbus", where, okO, okP))
            SameInt("SNDCTRLI_getfxbus", where, o.result, p.result);
    }

    for (uint8_t *bank : g_banks) {   // SNDBANKI_getppatch
        int count = *(uint16_t *)(bank + 6);
        for (int patch = -1; patch < count + 2; patch++) {   // -1 reads the bank's size field as an offset
            Call o = { kGetPPatch, true, { (intptr_t)bank, patch } }, p = { kGetPPatch, false, { (intptr_t)bank, patch } };
            bool okO = Side(&o, true), okP = Side(&p, false);
            g_cases++;
            char where[40];
            snprintf(where, sizeof(where), "patch %d of %d", patch, count);
            if (BothRan("SNDBANKI_getppatch", where, okO, okP))
                SameInt("SNDBANKI_getppatch", where, o.result, p.result);
        }
    }
}

}  // namespace

void SndTagShadow_Run(void) {
    const char *env = getenv("NIGHTFIRE_SNDTAGSHADOW");
    if (env == NULL || atoi(env) == 0)
        return;
    char folder[MAX_PATH], pattern[MAX_PATH], path[MAX_PATH];
    if (!Xbox_ResolvePath("D:\\driving", folder, sizeof(folder)))
        return;
    std::set<uint32_t> seen;
    static const char *const kinds[] = { "*.viv", "*.mus", "*.spe" };
    for (const char *kind : kinds) {
        snprintf(pattern, sizeof(pattern), "%s\\%s", folder, kind);
        WIN32_FIND_DATAA found;
        HANDLE search = FindFirstFileA(pattern, &found);
        if (search == INVALID_HANDLE_VALUE)
            continue;
        do {
            snprintf(path, sizeof(path), "%s\\%s", folder, found.cFileName);
            ReadArchive(path, found.cFileName, &seen);
        } while (FindNextFileA(search, &found));
        FindClose(search);
    }
    g_heap = (uint8_t *)VirtualAlloc(NULL, kHeap, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (g_heap == NULL)
        return;

    {
        Originals scope(true);
        TestGetb();
        TestTags();
        TestVoices();
        TestDetune();
        TestSpeakers();
        TestSmall();
    }

    VirtualFree(g_heap, 0, MEM_RELEASE);
    g_buffers.clear();
    printf("[sndtagshadow] %u banks (%d patches), %d stream headers, %d perturbed, %d random streams (%d left out as "
           "undefined), %d faults on both sides, %d masked: %d cases, %d checks, %d differ\n",
           (unsigned)g_banks.size(), g_bankPatches, g_streams, g_perturbed, g_synthetic, g_skipped, g_faults, g_masked,
           g_cases, g_checks, g_differ);
    fflush(stdout);
}
