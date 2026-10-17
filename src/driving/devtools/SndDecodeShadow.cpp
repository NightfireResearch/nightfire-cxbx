#include "SndDecodeShadow.h"

#include "../sound/snd/Decode.h"
#include "../sound/snd/DecodeUnused.h"
#include "../../common/xbeOriginal.h"
#include "../../common/xboxPath.h"

#include <windows.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_SNDDECSHADOW=1, at injection time: the decoders (sound/snd/Decode.cpp and DecodeUnused.cpp, the range
// 0x00146bb0..0x0014a260) against the originals. Each test runs every case with the whole range swapped back to
// the original, then with ours, on the same addresses (one decoder object, one output buffer, one state), and
// compares the results bit for bit; "mixed" runs swap back only some of the originals, so an original calls our
// functions through their patched entries (decodexac under the original object, readsamples and the two
// register-argument adaptors under the original decodemut).
//
// - EA-XA from the disc (sound.md 9.3 step 2): every SCDl chunk of every stream in every .mus and .spe (all four
//   languages), each channel's packet decoded as SFILTER_unpackxapf does it - SetState from its two leading
//   shorts, Feed, then Decode in random request sizes (1..2000, biased towards the partial-block path) until it
//   returns 0 - comparing each call's return, the output (and 32 floats past it), the object's bytes and
//   GetState. Feed's refusals are checked on the way. One job in 16 also runs mixed. With N > 1 in the variable,
//   only every Nth chunk of each file. All of it is about 830 MB of stream data, 1.5 billion samples per side:
//   expect around half a minute (the time is printed).
// - EA-XA, random: packets of random bytes (every predictor and shift nibble, including predictors above 3 that
//   read past the coefficient tables), random raw history bits, and decodexac alone on random argument blocks
//   (frame counts from -60 to 300).
// - PCM16 (data-dead): decode16x87 on random int16 data, counts 0..40 and some to 2000, into separate and
//   overlapping buffers with guard zones (a count of 0 writes before the output).
// - MicroTalk (data-dead; no encoder, so random bit streams - any stream decodes to something): initmut and six
//   decodemut frames per stream, the whole state compared after each; readsamples alone in both modes;
//   FUN_00146e00 and FUN_00146f00 alone through their register conventions (and the registers they must keep).
// ---------------------------------------------------------------------------------------------------------------

namespace {   // this file's own types

const unsigned kRangeLo = 0x00146bb0, kRangeHi = 0x0014a260;
const unsigned kObjectLo = 0x00149e50, kObjectHi = 0x0014a1e0;   // the CEAXABLKDecf methods, not decodexac

int g_cases, g_checks, g_differ, g_details;
int g_files, g_packets, g_calls, g_mixedJobs, g_randomJobs, g_xacCases, g_pcmCases, g_mutFrames, g_mutMixed,
    g_mutDirect, g_otherCodecs, g_faults;

void Detail(const char *format, ...) {
    if (g_details++ >= 10)
        return;
    char line[600];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    printf("[snddecshadow] %s\n", line);
}

void Check(bool same) {
    g_checks++;
    if (!same)
        g_differ++;
}

uint32_t Hash(const void *data, size_t bytes) {
    const uint8_t *p = (const uint8_t *)data;
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i + 4 <= bytes; i += 4) {
        uint32_t w;
        memcpy(&w, p + i, 4);
        h = (h ^ w) * 1099511628211ull;
    }
    return (uint32_t)(h ^ (h >> 32));
}

uint32_t Next(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

uint32_t Seed(uint32_t a, uint32_t b) {
    uint32_t s = (a * 0x9e3779b1u) ^ (b + 0x7f4a7c15u) ^ 0x2545f491u;
    return s != 0 ? s : 1;
}

float FloatBits(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}

uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

// The whole decoder range swapped back for as long as it lives (or only [lo, hi) of it).
struct Originals {
    unsigned lo, hi;
    Originals(unsigned l, unsigned h) : lo(l), hi(h) { XbeOriginal_RestoreRange(lo, hi, true); }
    ~Originals() { XbeOriginal_RestoreRange(lo, hi, false); }
};

typedef void (*Thunk)(void *context);

bool Guarded(Thunk thunk, void *context) {
#ifdef _MSC_VER
    __try {
        thunk(context);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    thunk(context);
    return true;
#endif
}

// ---- EA-XA: the object's calls, by address (the original or, mixed, the original's entry) or ours

enum Side { kOriginal, kPort };

#define OrigCtor      ((void *(__fastcall *)(void *self, int))0x00149e70u)
#define OrigFeed      ((int (__fastcall *)(void *self, int, const void *data, int bytes, int frames))0x00149e90u)
#define OrigDecode    ((int (__fastcall *)(void *self, int, float **out, int frames))0x00149ec0u)
#define OrigGetState  ((void *(__fastcall *)(void *self, int, void *state))0x0014a190u)
#define OrigSetState  ((void (__fastcall *)(void *self, int, const void *state))0x0014a1c0u)
#define OrigDecodeXac ((void (__cdecl *)(SND::DecodeXacParams *params))0x00149d80u)

const int kSlack = 32;
const uint32_t kPattern = 0x7fa5a5a5u;
const int kMaxRecords = 8192;

alignas(16) uint8_t g_object[0xa8];
alignas(16) float g_out[2048 + 64];

struct Record {
    int32_t ret;
    uint32_t out, object, state0, state1;
    int32_t stateReturn;               // GetState's return is its argument
};

struct Job {
    const uint8_t *packet;             // the two history shorts, then the blocks
    int frames;
    uint32_t seed;
    bool rawState;                     // random raw history bits instead of the shorts
    uint32_t raw0, raw1;
    int file, chunk, channel;          // for the report
};

SND::CEAXABLKDecf *Ours() {
    return (SND::CEAXABLKDecf *)g_object;
}

int RequestSize(uint32_t *rng) {
    uint32_t r = Next(rng);
    switch (r & 3) {
    case 0:
        return 1 + (int)((r >> 8) % 2000);
    case 1:
        return 1 + (int)((r >> 8) % 60);
    case 2:
        return 28 * (1 + (int)((r >> 8) % 40)) - (int)((r >> 4) & 1);
    default:
        return 1 + (int)((r >> 8) % 300);
    }
}

struct JobRun {
    Side side;
    const Job *job;
    Record *records;
    int count;
    int captureCall;                   // >= 0: keep that call's output and object
    float *captureOut;
    uint8_t *captureObject;
};

void RecordObject(JobRun *r, int32_t ret) {
    Record &rec = r->records[r->count++];
    rec.ret = ret;
    rec.out = 0;
    rec.object = Hash(g_object, sizeof(g_object));
    rec.state0 = rec.state1 = 0;
    rec.stateReturn = 0;
}

void RunJob(void *context) {
    JobRun *r = (JobRun *)context;
    const Job &job = *r->job;
    bool original = r->side == kOriginal;
    memset(g_object, 0xcd, sizeof(g_object));
    void *self = original ? OrigCtor(g_object, 0) : (void *)Ours()->Construct();
    RecordObject(r, self == g_object ? 1 : 0);
    const uint8_t *data = job.packet + 4;
    int bytes = job.frames * 4;
    RecordObject(r, original ? OrigFeed(g_object, 0, NULL, bytes, job.frames) : Ours()->Feed(NULL, bytes, job.frames));
    uint32_t history[2];
    if (job.rawState) {
        history[0] = job.raw0;
        history[1] = job.raw1;
    } else {
        history[0] = Bits((float)(int16_t)(job.packet[0] | job.packet[1] << 8));
        history[1] = Bits((float)(int16_t)(job.packet[2] | job.packet[3] << 8));
    }
    if (original)
        OrigSetState(g_object, 0, history);
    else
        Ours()->SetState(history);
    RecordObject(r, original ? OrigFeed(g_object, 0, data, bytes, job.frames) : Ours()->Feed(data, bytes, job.frames));
    if (job.frames != 0)
        RecordObject(r, original ? OrigFeed(g_object, 0, data, bytes, job.frames)
                                 : Ours()->Feed(data, bytes, job.frames));
    uint32_t rng = job.seed;
    for (int call = 0; r->count < kMaxRecords; call++) {
        int want = RequestSize(&rng);
        for (int i = 0; i < want + kSlack; i++)
            memcpy(&g_out[i], &kPattern, 4);
        float *to = g_out;
        int got = original ? OrigDecode(g_object, 0, &to, want) : Ours()->Decode(&to, want);
        uint32_t state[2] = { 0xeeeeeeeeu, 0xeeeeeeeeu };
        void *returned = original ? OrigGetState(g_object, 0, state) : Ours()->GetState(state);
        Record &rec = r->records[r->count++];
        rec.ret = got;
        rec.out = Hash(g_out, (size_t)(want + kSlack) * 4);
        rec.object = Hash(g_object, sizeof(g_object));
        rec.state0 = state[0];
        rec.state1 = state[1];
        rec.stateReturn = returned == (void *)state ? 1 : 0;
        if (call == r->captureCall) {
            memcpy(r->captureOut, g_out, (size_t)(want + kSlack) * 4);
            memcpy(r->captureObject, g_object, sizeof(g_object));
        }
        if (got == 0)
            break;
    }
}

Record g_records[kMaxRecords];

// Runs one job; the records go to `out` (a fault is one record with ret 0x0bad0bad).
void Run(Side side, const Job &job, std::vector<Record> *out, int captureCall = -1, float *captureOut = NULL,
         uint8_t *captureObject = NULL) {
    JobRun r = { side, &job, g_records, 0, captureCall, captureOut, captureObject };
    if (!Guarded(RunJob, &r)) {
        g_faults++;
        Record fault = { 0x0bad0bad, 0, 0, 0, 0, 0 };
        g_records[r.count < kMaxRecords ? r.count++ : kMaxRecords - 1] = fault;
    }
    out->insert(out->end(), g_records, g_records + r.count);
}

bool SameRecords(const Record *a, size_t na, const Record *b, size_t nb, size_t *first) {
    size_t n = na < nb ? na : nb;
    for (size_t i = 0; i < n; i++)
        if (memcmp(&a[i], &b[i], sizeof(Record)) != 0) {
            *first = i;
            return false;
        }
    *first = n;
    return na == nb;
}

// Where a job differs: runs both sides again keeping the differing call's output and object.
void Explain(const char *what, const Job &job, const std::vector<std::string> &names, size_t at,
             const Record &o, const Record &p, bool mixed) {
    if (g_details >= 10)
        return;
    const char *field = o.ret != p.ret ? "return" : o.out != p.out ? "output" : o.object != p.object ? "object"
                      : o.stateReturn != p.stateReturn ? "GetState's return" : "GetState";
    int call = (int)at - (job.frames != 0 ? 4 : 3);
    char where[200] = "";
    if (job.file >= 0)
        snprintf(where, sizeof(where), "%s chunk %d channel %d", names[job.file].c_str(), job.chunk, job.channel);
    else
        snprintf(where, sizeof(where), "random job %d", job.chunk);
    if (call < 0) {
        Detail("%s: %s, %s, setup record %u: %s %08x / %08x", what, where, mixed ? "mixed" : "port", (unsigned)at,
               field, (unsigned)o.ret, (unsigned)p.ret);
        return;
    }
    static float outO[2048 + 64], outP[2048 + 64];
    static uint8_t objO[0xa8], objP[0xa8];
    std::vector<Record> scratch;
    {
        Originals swap(kRangeLo, kRangeHi);
        Run(kOriginal, job, &scratch, call, outO, objO);
    }
    if (mixed) {
        Originals swap(kObjectLo, kObjectHi);
        Run(kOriginal, job, &scratch, call, outP, objP);
    } else {
        Run(kPort, job, &scratch, call, outP, objP);
    }
    char diff[200] = "";
    if (o.out != p.out) {
        for (int i = 0; i < 2048 + kSlack; i++)
            if (Bits(outO[i]) != Bits(outP[i])) {
                snprintf(diff, sizeof(diff), " first at frame %d: %08x / %08x", i, Bits(outO[i]), Bits(outP[i]));
                break;
            }
    } else if (o.object != p.object) {
        for (int i = 0; i < 0xa8; i++)
            if (objO[i] != objP[i]) {
                snprintf(diff, sizeof(diff), " first at +%02x: %02x / %02x", i, objO[i], objP[i]);
                break;
            }
    }
    Detail("%s: %s, %s, Decode call %d: %s differs (returns %d / %d)%s", what, where, mixed ? "mixed" : "port",
           call, field, o.ret, p.ret, diff);
}

// Runs a list of jobs: the originals, ours, and (for every 16th, or all with mixedAll) mixed.
void RunJobs(const char *what, const std::vector<Job> &jobs, const std::vector<std::string> &names,
             bool mixedAll) {
    std::vector<Record> original, port, mixed;
    std::vector<size_t> start(jobs.size() + 1), mixedStart;
    {
        Originals swap(kRangeLo, kRangeHi);
        for (size_t j = 0; j < jobs.size(); j++) {
            start[j] = original.size();
            Run(kOriginal, jobs[j], &original);
        }
        start[jobs.size()] = original.size();
    }
    for (size_t j = 0; j < jobs.size(); j++) {
        size_t from = port.size();
        Run(kPort, jobs[j], &port);
        g_cases++;
        size_t first;
        bool same = SameRecords(&original[start[j]], start[j + 1] - start[j], &port[from], port.size() - from,
                                &first);
        Check(same);
        g_calls += (int)(start[j + 1] - start[j]);
        if (!same && first < start[j + 1] - start[j] && first < port.size() - from)
            Explain(what, jobs[j], names, first, original[start[j] + first], port[from + first], false);
        else if (!same)
            Detail("%s: job %u: %u records / %u", what, (unsigned)j, (unsigned)(start[j + 1] - start[j]),
                   (unsigned)(port.size() - from));
    }
    {
        Originals swap(kObjectLo, kObjectHi);
        for (size_t j = 0; j < jobs.size(); j++) {
            if (!mixedAll && j % 16 != 0)
                continue;
            size_t from = mixed.size();
            Run(kOriginal, jobs[j], &mixed);
            mixedStart.push_back(j);
            mixedStart.push_back(from);
            mixedStart.push_back(mixed.size());
        }
    }
    for (size_t m = 0; m < mixedStart.size(); m += 3) {
        size_t j = mixedStart[m], from = mixedStart[m + 1], to = mixedStart[m + 2];
        size_t first;
        g_cases++;
        g_mixedJobs++;
        bool same = SameRecords(&original[start[j]], start[j + 1] - start[j], &mixed[from], to - from, &first);
        Check(same);
        if (!same && first < start[j + 1] - start[j] && first < to - from)
            Explain(what, jobs[j], names, first, original[start[j] + first], mixed[from + first], true);
        else if (!same)
            Detail("%s: mixed job %u: %u records / %u", what, (unsigned)j, (unsigned)(start[j + 1] - start[j]),
                   (unsigned)(to - from));
    }
}

// ---- the disc: BIGF archives of .asf streams (SCHl header with a PT tag stream, SCCl, SCDl data, SCEl end)

uint32_t BigEndian(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

uint32_t LittleEndian(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

// The PT tags SNDI_gettag reads: channels (0x82, default 1) and sample representation (0xa0, default 8).
void ParsePatch(const uint8_t *p, const uint8_t *end, int *channels, int *codec) {
    *channels = 1;
    *codec = 8;
    if (end - p < 4 || p[0] != 'P' || p[1] != 'T')
        return;
    p += 4;
    while (p < end) {
        uint8_t tag = *p++;
        if (tag == 0xff)
            break;
        if (tag == 0xfc || tag == 0xfd || tag == 0xfe)
            continue;
        if (p >= end)
            break;
        uint32_t length = *p++;
        if (length == 0xff) {
            if (end - p < 4)
                break;
            length = BigEndian(p);
            p += 4;
        }
        if ((uint32_t)(end - p) < length)
            break;
        uint32_t value = 0;
        for (uint32_t i = 0; i < length && i < 4; i++)
            value = value << 8 | p[i];
        if (tag == 0x82)
            *channels = (int)value;
        else if (tag == 0xa0)
            *codec = (int)value;
        p += length;
    }
}

void AddStreamJobs(const uint8_t *p, const uint8_t *end, int file, int stride, int *chunkIndex,
                   std::vector<Job> *jobs) {
    int channels = 1, codec = 8;
    while (end - p >= 8) {
        uint32_t size = LittleEndian(p + 4);
        if (size < 8 || size > (uint32_t)(end - p))
            break;
        if (memcmp(p, "SCHl", 4) == 0) {
            ParsePatch(p + 8, p + size, &channels, &codec);
        } else if (memcmp(p, "SCDl", 4) == 0 && size >= 12) {
            int index = (*chunkIndex)++;
            if (codec != 10) {
                g_otherCodecs++;
            } else if (index % stride == 0 && channels >= 1 && channels <= 6 && size >= 16u + 4u * channels) {
                int frames = (int)(LittleEndian(p + 8) & 0x7fffffff);
                for (int c = 0; c < channels; c++) {
                    uint32_t offset = LittleEndian(p + 12 + 4 * c);
                    if (offset > size - 12 - 4 * channels - 4)
                        continue;
                    Job job;
                    job.packet = p + 12 + 4 * channels + offset;
                    job.frames = frames;
                    job.seed = Seed((uint32_t)file * 0x10000u + (uint32_t)index, (uint32_t)c);
                    job.rawState = false;
                    job.raw0 = job.raw1 = 0;
                    job.file = file;
                    job.chunk = index;
                    job.channel = c;
                    jobs->push_back(job);
                    g_packets++;
                }
            }
        }
        p += size;
    }
}

bool ReadWholeFile(const char *path, std::vector<uint8_t> *data) {
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    DWORD size = GetFileSize(f, NULL);
    if (size == 0 || size == INVALID_FILE_SIZE || size > (256u << 20)) {
        CloseHandle(f);
        return false;
    }
    data->assign((size_t)size + 4096, 0);   // the decoders may read a block past a short last packet
    DWORD got = 0;
    bool ok = ::ReadFile(f, data->data(), size, &got, NULL) && got == size;
    CloseHandle(f);
    return ok;
}

void DiscStreams(int stride) {
    char folder[MAX_PATH], pattern[MAX_PATH], path[MAX_PATH];
    if (!Xbox_ResolvePath("D:\\driving", folder, sizeof(folder)))
        return;
    std::vector<std::string> names;
    static const char *const kPatterns[2] = { "*.mus", "*.spe" };
    for (int k = 0; k < 2; k++) {
        snprintf(pattern, sizeof(pattern), "%s\\%s", folder, kPatterns[k]);
        WIN32_FIND_DATAA found;
        HANDLE search = FindFirstFileA(pattern, &found);
        if (search == INVALID_HANDLE_VALUE)
            continue;
        do {
            names.push_back(found.cFileName);
        } while (FindNextFileA(search, &found));
        FindClose(search);
    }
    for (size_t f = 0; f < names.size(); f++) {
        snprintf(path, sizeof(path), "%s\\%s", folder, names[f].c_str());
        std::vector<uint8_t> data;
        if (!ReadWholeFile(path, &data)) {
            Detail("%s: could not read it", path);
            continue;
        }
        g_files++;
        size_t size = data.size() - 4096;
        std::vector<Job> jobs;
        int chunkIndex = 0;
        const uint8_t *base = data.data();
        if (size >= 16 && memcmp(base, "BIGF", 4) == 0) {
            uint32_t count = BigEndian(base + 8);
            const uint8_t *entry = base + 16;
            for (uint32_t e = 0; e < count && entry + 9 <= base + size; e++) {
                uint32_t offset = BigEndian(entry), length = BigEndian(entry + 4);
                entry += 8;
                while (entry < base + size && *entry != 0)
                    entry++;
                entry++;
                if (offset < size && length <= size - offset)
                    AddStreamJobs(base + offset, base + offset + length, (int)f, stride, &chunkIndex, &jobs);
            }
        } else {
            AddStreamJobs(base, base + size, (int)f, stride, &chunkIndex, &jobs);
        }
        RunJobs("disc", jobs, names, false);
    }
}

// ---- EA-XA, random packets and decodexac alone

void RandomPackets() {
    std::vector<uint8_t> data(1 << 16);
    uint32_t rng = 0x5eed1234u;
    for (size_t i = 0; i < data.size(); i++)
        data[i] = (uint8_t)(Next(&rng) >> 24);
    std::vector<Job> jobs;
    std::vector<std::string> names;
    for (int j = 0; j < 2000; j++) {
        Job job;
        job.frames = (int)(Next(&rng) % 4001);
        job.packet = data.data() + Next(&rng) % (data.size() - 4 - (size_t)(job.frames / 28 + 2) * 15);
        job.seed = Seed(0xabcd, (uint32_t)j);
        job.rawState = (j & 1) != 0;
        job.raw0 = Next(&rng);
        job.raw1 = Next(&rng);
        if ((j & 6) == 2) {   // ordinary magnitudes as well as raw bits
            job.raw0 = Bits((float)(int)(Next(&rng) % 65536 - 32768));
            job.raw1 = Bits((float)(int)(Next(&rng) % 65536 - 32768));
        }
        job.file = -1;
        job.chunk = j;
        job.channel = 0;
        jobs.push_back(job);
        g_randomJobs++;
    }
    RunJobs("random EA-XA", jobs, names, true);
}

struct XacCase {
    SND::DecodeXacParams params;
    bool original;
};

void RunXac(void *context) {
    XacCase *c = (XacCase *)context;
    if (c->original)
        OrigDecodeXac(&c->params);
    else
        SND::decodexac(&c->params);
}

void DecodeXacAlone() {
    static uint8_t blocks[400 * 15];
    const int kCases = 5000, kFloats = 340 + 28 + kSlack;
    std::vector<uint32_t> results;
    for (int side = 0; side < 2; side++) {
        Originals *swap = side == 0 ? new Originals(kRangeLo, kRangeHi) : NULL;
        for (int k = 0; k < kCases; k++) {
            uint32_t rng = Seed(0x0dec, (uint32_t)k);
            for (size_t i = 0; i < sizeof(blocks); i++)
                blocks[i] = (uint8_t)(Next(&rng) >> 24);
            XacCase c;
            int frames = (int)(Next(&rng) % 361) - 60;
            c.params.frames = frames;
            c.params.s1 = FloatBits(Next(&rng));
            c.params.s2 = FloatBits(Next(&rng));
            if (k & 1) {
                c.params.s1 = (float)(int)(Next(&rng) % 65536 - 32768);
                c.params.s2 = (float)(int)(Next(&rng) % 65536 - 32768);
            }
            c.params.src = blocks;
            c.params.dst = g_out;
            c.original = side == 0;
            for (int i = 0; i < kFloats; i++)
                memcpy(&g_out[i], &kPattern, 4);
            bool ok = Guarded(RunXac, &c);
            uint32_t r[2] = { ok ? Hash(g_out, kFloats * 4) : 0x0bad0badu, Hash(&c.params, sizeof(c.params)) };
            if (side == 0) {
                results.insert(results.end(), r, r + 2);
            } else {
                g_cases++;
                g_xacCases++;
                bool same = results[2 * k] == r[0] && results[2 * k + 1] == r[1];
                Check(same);
                if (!same)
                    Detail("decodexac alone, case %d (%d frames): %s differs", k, frames,
                           results[2 * k] != r[0] ? "the output" : "the argument block");
            }
        }
        delete swap;
    }
}

// ---- PCM16

#define OrigDecode16 ((void (__cdecl *)(uint32_t count, const int16_t *in, float *out))0x0014a1e0u)

struct PcmCase {
    uint32_t count;
    const int16_t *in;
    float *out;
    bool original;
};

void RunPcm(void *context) {
    PcmCase *c = (PcmCase *)context;
    if (c->original)
        OrigDecode16(c->count, c->in, c->out);
    else
        decode16x87(c->count, c->in, c->out);
}

void Pcm16() {
    const int kCases = 3000, kGuard = 16, kMax = 2000;
    static int16_t input[kMax + 2 * kGuard];
    static float output[kMax + 2 * kGuard];
    std::vector<uint32_t> results;
    for (int side = 0; side < 2; side++) {
        Originals *swap = side == 0 ? new Originals(kRangeLo, kRangeHi) : NULL;
        for (int k = 0; k < kCases; k++) {
            uint32_t rng = Seed(0x16, (uint32_t)k);
            uint32_t count = (k % 10 == 9) ? Next(&rng) % (kMax + 1) : Next(&rng) % 41;
            bool inPlace = (k & 1) != 0;
            for (int i = 0; i < kMax + 2 * kGuard; i++) {
                input[i] = (int16_t)(Next(&rng) >> 16);
                uint32_t fill = Next(&rng);
                memcpy(&output[i], &fill, 4);
            }
            PcmCase c;
            c.count = count;
            c.out = output + kGuard;
            c.in = inPlace ? (const int16_t *)(output + kGuard) : input + kGuard;
            c.original = side == 0;
            bool ok = Guarded(RunPcm, &c);
            uint32_t h = ok ? Hash(output, sizeof(output)) : 0x0bad0badu;
            if (side == 0) {
                results.push_back(h);
            } else {
                g_cases++;
                g_pcmCases++;
                Check(results[k] == h);
                if (results[k] != h)
                    Detail("decode16x87, case %d: %u samples%s differ", k, count, inPlace ? " in place" : "");
            }
        }
        delete swap;
    }
}

// ---- MicroTalk

#define OrigInitmut     ((void (__cdecl *)(const uint8_t *src, SND::MutState *state))0x001493e0u)
#define OrigDecodemut   ((void (__cdecl *)(SND::MutState *state))0x00149500u)
#define OrigReadsamples ((void (__cdecl *)(SND::MutState *state, int mode, float *out))0x00146bb0u)

const int kStreams = 300, kFrames = 6;
alignas(16) uint8_t g_stream[4096];
alignas(16) uint8_t g_mutBytes[sizeof(SND::MutState) + 64];

SND::MutState *MutStateBuffer() {
    return (SND::MutState *)g_mutBytes;
}

struct MutRun {
    int mode;                          // 0 original side or mixed (by address), 1 ours
    int stream;
    std::vector<uint8_t> *states;
};

void FillStream(int stream) {
    uint32_t rng = Seed(0x3a7, (uint32_t)stream);
    for (size_t i = 0; i < sizeof(g_stream); i++)
        g_stream[i] = (uint8_t)(Next(&rng) >> 24);
}

void RunMutStream(void *context) {
    MutRun *r = (MutRun *)context;
    SND::MutState *s = MutStateBuffer();
    memset(g_mutBytes, 0x5c, sizeof(g_mutBytes));
    FillStream(r->stream);
    if (r->mode == 0)
        OrigInitmut(g_stream, s);
    else
        initmut(g_stream, s);
    r->states->insert(r->states->end(), g_mutBytes, g_mutBytes + sizeof(g_mutBytes));
    for (int f = 0; f < kFrames; f++) {
        if (r->mode == 0)
            OrigDecodemut(s);
        else
            decodemut(s);
        r->states->insert(r->states->end(), g_mutBytes, g_mutBytes + sizeof(g_mutBytes));
    }
}

void MutStreams(int mode, unsigned lo1, unsigned hi1, unsigned lo2, unsigned hi2, std::vector<uint8_t> *all,
                std::vector<int> *faults) {
    if (lo1 != 0)
        XbeOriginal_RestoreRange(lo1, hi1, true);
    if (lo2 != 0)
        XbeOriginal_RestoreRange(lo2, hi2, true);
    for (int k = 0; k < kStreams; k++) {
        std::vector<uint8_t> states;
        MutRun r = { mode, k, &states };
        bool ok = Guarded(RunMutStream, &r);
        states.resize((size_t)(kFrames + 1) * sizeof(g_mutBytes), 0);
        all->insert(all->end(), states.begin(), states.end());
        faults->push_back(ok ? 0 : 1);
    }
    if (lo1 != 0)
        XbeOriginal_RestoreRange(lo1, hi1, false);
    if (lo2 != 0)
        XbeOriginal_RestoreRange(lo2, hi2, false);
}

void CompareMut(const char *what, const std::vector<uint8_t> &o, const std::vector<int> &of,
                const std::vector<uint8_t> &p, const std::vector<int> &pf, bool mixed) {
    size_t step = sizeof(g_mutBytes);
    for (int k = 0; k < kStreams; k++) {
        for (int f = 0; f <= kFrames; f++) {
            const uint8_t *a = &o[((size_t)k * (kFrames + 1) + f) * step];
            const uint8_t *b = &p[((size_t)k * (kFrames + 1) + f) * step];
            g_cases++;
            if (mixed)
                g_mutMixed++;
            else
                g_mutFrames++;
            bool same = memcmp(a, b, step) == 0 && of[k] == pf[k];
            Check(same);
            if (!same) {
                size_t i = 0;
                while (i < step && a[i] == b[i])
                    i++;
                Detail("%s, stream %d, %s %d: the state differs at +%03x (%02x / %02x)%s", what, k,
                       f == 0 ? "initmut" : "frame", f, (unsigned)i, i < step ? a[i] : 0, i < step ? b[i] : 0,
                       of[k] != pf[k] ? " (a fault on one side)" : "");
                break;
            }
        }
    }
}

struct ReadCase {
    int mode;
    bool original;
    float out[110 + 16];
};

void RunRead(void *context) {
    ReadCase *c = (ReadCase *)context;
    if (c->original)
        OrigReadsamples(MutStateBuffer(), c->mode, c->out);
    else
        readsamples(MutStateBuffer(), c->mode, c->out);
}

void ReadsamplesAlone() {
    const int kCases = 2000;
    std::vector<uint32_t> results;
    for (int side = 0; side < 2; side++) {
        Originals *swap = side == 0 ? new Originals(kRangeLo, kRangeHi) : NULL;
        for (int k = 0; k < kCases; k++) {
            FillStream(10000 + k);
            uint32_t rng = Seed(0x5a, (uint32_t)k);
            SND::MutState *s = MutStateBuffer();
            s->ptr = g_stream;
            s->count = 8 + (int32_t)(Next(&rng) % 17);
            s->bits = Next(&rng) & (s->count >= 32 ? 0xffffffffu : (1u << s->count) - 1);
            ReadCase c;
            c.mode = (k & 1) ? (int)(Next(&rng) % 3) + 1 : 0;
            c.original = side == 0;
            memset(c.out, 0x77, sizeof(c.out));
            bool ok = Guarded(RunRead, &c);
            uint32_t r[2] = { ok ? Hash(c.out, sizeof(c.out)) : 0x0bad0badu, Hash(s, 12) };
            if (side == 0) {
                results.insert(results.end(), r, r + 2);
            } else {
                g_cases++;
                g_mutDirect++;
                bool same = results[2 * k] == r[0] && results[2 * k + 1] == r[1];
                Check(same);
                if (!same)
                    Detail("readsamples alone, case %d (mode %d): %s differs", k, c.mode,
                           results[2 * k] != r[0] ? "the output" : "the bit reader");
            }
        }
        delete swap;
    }
}

// The register-argument helpers, called at their addresses with the original's conventions: the original inside
// a swap, otherwise our adaptor through the patched entry. The registers each must keep are checked too.
uint32_t g_afterEbx, g_afterEsi, g_afterEdi;

#if defined(_MSC_VER) && defined(_M_IX86)
__declspec(noinline) void Call146e00(float *result, const float *coefs) {
    __asm {
        push ebx
        push esi
        push edi
        mov ebx, result
        mov esi, 0x5e5e5e5e
        mov edi, 0x7d7d7d7d
        push coefs
        mov eax, 0x00146e00
        call eax
        add esp, 4
        mov g_afterEbx, ebx
        mov g_afterEsi, esi
        mov g_afterEdi, edi
        pop edi
        pop esi
        pop ebx
    }
}

__declspec(noinline) void Call146f00(int first, void *state, int groups) {
    __asm {
        push ebx
        push esi
        push edi
        mov eax, first
        mov esi, state
        mov ebx, 0x3b3b3b3b
        mov edi, 0x7d7d7d7d
        push groups
        mov ecx, 0x00146f00
        call ecx
        add esp, 4
        mov g_afterEbx, ebx
        mov g_afterEsi, esi
        mov g_afterEdi, edi
        pop edi
        pop esi
        pop ebx
    }
}
const bool kRegisterCalls = true;
#else
void Call146e00(float *, const float *) {}
void Call146f00(int, void *, int) {}
const bool kRegisterCalls = false;
#endif

struct RegCase {
    bool lpc;
    int first, groups;
    float coefs[12];
    float out[12 + 4];
};

void RunReg(void *context) {
    RegCase *c = (RegCase *)context;
    if (c->lpc)
        Call146e00(c->out, c->coefs);
    else
        Call146f00(c->first, MutStateBuffer(), c->groups);
}

void RegisterHelpersAlone() {
    if (!kRegisterCalls)
        return;
    const int kCases = 3000;
    std::vector<uint32_t> results;
    for (int side = 0; side < 2; side++) {
        Originals *swap = side == 0 ? new Originals(kRangeLo, kRangeHi) : NULL;
        for (int k = 0; k < kCases; k++) {
            uint32_t rng = Seed(0xe0, (uint32_t)k);
            RegCase c;
            c.lpc = (k & 1) == 0;
            SND::MutState *s = MutStateBuffer();
            memset(g_mutBytes, 0x3c, sizeof(g_mutBytes));
            for (int i = 0; i < 12; i++) {
                c.coefs[i] = (float)((double)(int32_t)Next(&rng) / 2147483648.0 * 0.999);
                s->coefs[i] = c.coefs[i];
                s->synth[i] = (float)((double)(int32_t)Next(&rng) / 2147483648.0 * 3000.0);
            }
            for (int i = 0; i < 756; i++)
                s->signal[i] = (float)((double)(int32_t)Next(&rng) / 2147483648.0 * 30000.0);
            c.first = (int)(Next(&rng) % 37);
            c.groups = 1 + (int)(Next(&rng) % ((432 - c.first) / 12));
            memset(c.out, 0x66, sizeof(c.out));
            g_afterEbx = g_afterEsi = g_afterEdi = 0;
            bool ok = Guarded(RunReg, &c);
            uint32_t data = c.lpc ? Hash(c.out, sizeof(c.out)) : Hash(g_mutBytes, sizeof(g_mutBytes));
            bool kept = c.lpc ? g_afterEbx == (uint32_t)(uintptr_t)c.out && g_afterEsi == 0x5e5e5e5eu
                              : g_afterEbx == 0x3b3b3b3bu && g_afterEsi == (uint32_t)(uintptr_t)s;
            kept = kept && g_afterEdi == 0x7d7d7d7du;
            uint32_t r[2] = { ok ? data : 0x0bad0badu, kept ? 1u : 0u };
            if (side == 0) {
                results.insert(results.end(), r, r + 2);
            } else {
                g_cases++;
                g_mutDirect++;
                bool same = results[2 * k] == r[0] && results[2 * k + 1] == r[1] && kept;
                Check(same);
                if (!same)
                    Detail("%s alone, case %d: %s", c.lpc ? "FUN_00146e00" : "FUN_00146f00", k,
                           results[2 * k] != r[0] ? "the output differs" : "a register was not kept");
            }
        }
        delete swap;
    }
}

void MicroTalk() {
    std::vector<uint8_t> o, p, m1, m2;
    std::vector<int> of, pf, m1f, m2f;
    MutStreams(0, kRangeLo, kRangeHi, 0, 0, &o, &of);
    MutStreams(1, 0, 0, 0, 0, &p, &pf);
    CompareMut("MicroTalk", o, of, p, pf, false);
    // the original decodemut over our readsamples and FUN_00146f00 adaptor; then over our FUN_00146e00 adaptor
    MutStreams(0, 0x00149500, 0x00149501, 0, 0, &m1, &m1f);
    CompareMut("MicroTalk mixed (original decodemut)", o, of, m1, m1f, true);
    MutStreams(0, 0x00149500, 0x00149501, 0x00146f00, 0x00146f01, &m2, &m2f);
    CompareMut("MicroTalk mixed (original decodemut and FUN_00146f00)", o, of, m2, m2f, true);
    ReadsamplesAlone();
    RegisterHelpersAlone();
}

}   // namespace

void SndDecodeShadow_Run(void) {
    char on[32];
    DWORD length = GetEnvironmentVariableA("NIGHTFIRE_SNDDECSHADOW", on, sizeof(on));
    if (length == 0 || length >= sizeof(on) || atoi(on) == 0)
        return;
    int stride = atoi(on) > 1 ? atoi(on) : 1;
    DWORD started = GetTickCount();
    DiscStreams(stride);
    RandomPackets();
    DecodeXacAlone();
    Pcm16();
    MicroTalk();
    double seconds = (double)(GetTickCount() - started) / 1000.0;
    printf("[snddecshadow] EA-XA: %d files, %d disc packets (chunks 1 in %d; %d of other codecs), %d Decode/Feed "
           "records, %d mixed jobs, %d random packets, %d decodexac alone; PCM16: %d; MicroTalk: %d states, %d "
           "mixed, %d alone; %d faults, %.1f s: %d cases, %d checks, %d differ\n",
           g_files, g_packets, stride, g_otherCodecs, g_calls, g_mixedJobs, g_randomJobs,
           g_xacCases, g_pcmCases, g_mutFrames, g_mutMixed, g_mutDirect, g_faults, seconds, g_cases, g_checks,
           g_differ);
    fflush(stdout);
}
