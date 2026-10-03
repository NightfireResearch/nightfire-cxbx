#include "AnimShadow.h"

#include "../eagl/EaglGlobals.h"
#include "../eagl/Loader.h"
#include "../eagl/anim/AnimObjects.h"
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
#include <set>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_ANIMSHADOW=1, at injection time: EAGLAnim (eagl/anim/) against the originals.
//
// Every anim bank on the disc (data\actors\anims\*.dat, once per distinct file) is run twice on the same addresses:
// with every replaced function in EAGLAnim's ranges swapped back to the original, then with ours. Each side loads
// the bank (the loader is ours on both sides), starts EAGLAnim's pool in an arena that starts over for each side,
// runs the AnimationBank constructor, builds every anim through NewFnAnim and logs the object's bytes, then
// sweeps its virtuals - GetLength, the checksum, attributes and phase channel, and at times around its length
// Eval, EvalSQT (no mask, all bones, every other bone), EvalVel2D, EvalWeights, EvalPhase, EvalState and
// EvalEvent (handlers that record each event) - with pattern-filled outputs hashed into the log; then UseFPS and
// again; then deletes every anim. The logs (every allocation too), the arena and the bank's bytes are compared.
// Eval's output doubles as a handler table for event channels in the original, so each call runs under SEH and a
// fault is logged (it must happen on both sides).
// ---------------------------------------------------------------------------------------------------------------

namespace {   // this file's own types

int g_checks, g_failures;
std::string *g_log;

void Report(const char *what, const char *detail) {
    if (g_failures++ < 30)
        printf("[animshadow] %s: %s\n", what, detail);
}

void Logf(const char *format, ...) {
    if (g_log == NULL)
        return;
    char line[512];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    g_log->append(line);
    g_log->push_back('\n');
}

uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

uint32_t Hash(const void *data, size_t size) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < size; i++)
        h = (h ^ ((const uint8_t *)data)[i]) * 16777619u;
    return h;
}

// ---- the arena and the hooks

const size_t kArena = 64u << 20;
uint8_t *g_arena;
size_t g_arenaUsed;

void ArenaReset() {
    memset(g_arena, 0xcd, g_arenaUsed > 0 ? g_arenaUsed : kArena);
    g_arenaUsed = 0;
}

void *ShadowMalloc(uint32_t size, const char *name) {
    size_t at = (g_arenaUsed + 15) & ~(size_t)15;
    if (at + size > kArena) {
        Report("arena", "out of space");
        return NULL;
    }
    g_arenaUsed = at + size;
    Logf("malloc %u \"%s\" -> +%x", size, name != NULL ? name : "(null)", (unsigned)at);
    return g_arena + at;
}

void ShadowFree(void *data, uint32_t size) {
    uint8_t *p = (uint8_t *)data;
    if (p >= g_arena && p < g_arena + kArena)
        Logf("free +%x %u", (unsigned)(p - g_arena), size);
    else
        Logf("free outside the arena %p %u", data, size);
}

int ShadowPrint(const char *format, va_list args) {
    char line[512];
    vsnprintf(line, sizeof(line), format, args);
    Logf("message %s", line);
    return 0;
}

#define PrintHook       (*(void **)0x00240268)
#define LoadedTables    (*(void **)0x0023fb88)
#define BlockSizes      ((const uint16_t *)0x001ceab0)

// EAGLAnim's ranges: the objects and evaluation, and its functions linked among the engine's.
struct Originals {
    bool on;
    explicit Originals(bool original) : on(original) {
        if (on) {
            XbeOriginal_RestoreRange(0x000f70b0, 0x001073a0, true);
            XbeOriginal_RestoreRange(0x000141d0, 0x00014240, true);
        }
    }
    ~Originals() {
        if (on) {
            XbeOriginal_RestoreRange(0x000f70b0, 0x001073a0, false);
            XbeOriginal_RestoreRange(0x000141d0, 0x00014240, false);
        }
    }
};

// ---- event handlers: slot 0 is called with (time, event record, data)

void __fastcall OnEvent(void *self, int, float time, uint32_t *event, void *data) {
    Logf("  event handler %u t %08x [%08x %08x %08x %08x] data %p", (unsigned)(((uintptr_t)self >> 3) & 3),
         Bits(time), event[0], event[1], event[2], event[3], data);
}

void *g_handlerVtable[1] = { (void *)OnEvent };
struct Handler {
    void **vtable;
    void *next;
} g_handlers[4];
void *g_handlerTable[1024];

// ---- one virtual call, under SEH (no C++ objects in here)

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
    thunk(context);   // no SEH: a fault ends the test (none happen with the disc's data)
    return true;
#endif
}

struct Call {
    FnAnim *anim;
    float a, b;
    void *p, *q;
    uint32_t result;
};

void CallEval(void *c) {
    Call *k = (Call *)c;
    AnimVCall<void>(k->anim, kSlotEval, k->a, k->b, (float *)k->p);
}
void CallEvalSQT(void *c) {
    Call *k = (Call *)c;
    k->result = AnimVCall<bool>(k->anim, kSlotEvalSQT, k->b, (float *)k->p, k->q);
}
void CallEvalVel2D(void *c) {
    Call *k = (Call *)c;
    k->result = AnimVCall<bool>(k->anim, kSlotEvalVel2D, k->b, (float *)k->p);
}
void CallEvalWeights(void *c) {
    Call *k = (Call *)c;
    k->result = AnimVCall<bool>(k->anim, kSlotEvalWeights, k->b, (float *)k->p);
}
void CallEvalPhase(void *c) {
    Call *k = (Call *)c;
    k->result = AnimVCall<bool>(k->anim, kSlotEvalPhase, k->b, k->p);
}
void CallEvalState(void *c) {
    Call *k = (Call *)c;
    k->result = AnimVCall<bool>(k->anim, kSlotEvalState, k->b, k->p);
}
void CallEvalEvent(void *c) {
    Call *k = (Call *)c;
    k->result = AnimVCall<bool>(k->anim, kSlotEvalEvent, k->a, k->b, (void **)k->p, k->q);
}
void CallGetLength(void *c) {
    Call *k = (Call *)c;
    k->result = AnimVCall<bool>(k->anim, kSlotGetLength, (float *)k->p);
}
void CallUseFPS(void *c) {
    Call *k = (Call *)c;
    AnimVCall<void>(k->anim, kSlotUseFPS, true);
}

const size_t kOut = 8192;
float g_out[kOut];
uint8_t g_maskAll[64], g_maskHalf[64];

void FillPattern() {
    for (size_t i = 0; i < kOut; i++) {
        uint32_t u = 0x7fc0de00 | (uint32_t)(i & 0xff);
        memcpy(&g_out[i], &u, 4);
    }
}

void FillHandlers() {   // Eval's output, for event channels that take it as a handler table
    for (size_t i = 0; i < kOut; i++) {
        void *h = &g_handlers[i & 3];
        memcpy(&g_out[i], &h, 4);
    }
}

void Run(const char *what, Thunk thunk, Call *call, size_t outBytes) {
    call->result = 0xcccccccc;
    bool ok = Guarded(thunk, call);
    Logf("  %s(%08x, %08x) -> %s %x out %08x", what, Bits(call->a), Bits(call->b), ok ? "ok" : "FAULT",
         call->result & 0xff, Hash(g_out, outBytes));
}

void Sweep(FnAnim *anim) {
    Call call;
    memset(&call, 0, sizeof(call));
    call.anim = anim;
    float length = -1;
    call.p = &length;
    bool ok = Guarded(CallGetLength, &call);
    Logf("  GetLength %s %x %08x", ok ? "ok" : "FAULT", call.result & 0xff, Bits(length));
    Logf("  checksum %04x attributes %p phase %p", AnimVCall<uint32_t>(anim, kSlotTargetCheckSum) & 0xffff,
         AnimVCall<void *>(anim, kSlotGetAttributes), AnimVCall<void *>(anim, kSlotGetPhaseChan));
    if (!(length > 0) || length > 100000)
        length = 30;
    const float times[] = { -1.0f, 0.0f, 0.25f, 0.5f, 1.0f, 1.5f, length / 3, length / 2, length - 1, length - 0.5f,
                            length, length + 2.5f, 1000.0f };
    for (float t : times) {
        call.a = t - 1.7f;
        call.b = t;
        FillHandlers();
        call.p = g_out;
        Run("Eval", CallEval, &call, sizeof(g_out));
        void *masks[3] = { NULL, g_maskAll, g_maskHalf };
        for (void *mask : masks) {
            FillPattern();
            call.q = mask;
            Run("EvalSQT", CallEvalSQT, &call, sizeof(g_out));
        }
        FillPattern();
        Run("EvalVel2D", CallEvalVel2D, &call, 64);
        FillPattern();
        Run("EvalWeights", CallEvalWeights, &call, sizeof(g_out));
        FillPattern();
        Run("EvalPhase", CallEvalPhase, &call, 64);
        FillPattern();
        Run("EvalState", CallEvalState, &call, 1024);
        call.p = g_handlerTable;
        call.q = (void *)0x1234;
        Run("EvalEvent", CallEvalEvent, &call, 0);
        call.q = NULL;
    }
    ok = Guarded(CallUseFPS, &call);
    call.p = &length;
    Guarded(CallGetLength, &call);
    Logf("  UseFPS %s, length %08x", ok ? "ok" : "FAULT", Bits(length));
    call.b = 0.5f;
    call.p = g_out;
    FillPattern();
    Run("EvalSQT fps", CallEvalSQT, &call, sizeof(g_out));
}

// ---- the banks

struct Bank {
    std::string name;
    std::vector<uint8_t> data;   // the file (unpacked), then zeros
    size_t size;
};

void ReadBanks(const char *hostPath, std::vector<Bank> *out, std::set<uint32_t> *seen) {
    HANDLE file = CreateFileA(hostPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return;
    uint8_t header[16];
    DWORD got = 0;
    ReadFile(file, header, 16, &got, NULL);
    int dirSize = got == 16 ? BIG_dirsize(header) : 0;
    std::vector<uint8_t> dir(dirSize > 16 ? dirSize : 16);
    SetFilePointer(file, 0, NULL, FILE_BEGIN);
    ReadFile(file, dir.data(), (DWORD)dirSize, &got, NULL);
    for (int i = 0;; i++) {
        int offset, size;
        const char *name = BIG_find(dir.data(), NULL, i, &offset, &size);
        if (name == NULL)
            break;
        std::string n = name;
        if (n.size() < 4 || _stricmp(n.c_str() + n.size() - 4, ".dat") != 0 || n.find("anims") == std::string::npos)
            continue;
        std::vector<uint8_t> raw(size + 16);
        SetFilePointer(file, offset, NULL, FILE_BEGIN);
        ReadFile(file, raw.data(), (DWORD)size, &got, NULL);
        Bank b;
        b.name = n;
        unsigned unpacked = unpacksizez(raw.data());
        if (unpacked != 0) {
            b.data.assign(unpacked + 64, 0);
            UNPACK_unpack(raw.data(), b.data.data());
            b.size = unpacked;
        } else {
            b.data.assign(raw.begin(), raw.begin() + size);
            b.data.resize(b.data.size() + 64, 0);
            b.size = (size_t)size;
        }
        if (b.size < 0x34 || memcmp(b.data.data(), "\x7f" "ELF", 4) != 0)
            continue;
        if (!seen->insert(Hash(b.data.data(), b.size)).second)
            continue;
        out->push_back(b);
    }
    CloseHandle(file);
}

struct Snapshot {
    std::string log;
    std::vector<uint8_t> arena, image;
};

int g_anims, g_types[kAnimTypeCount], g_subTypes[kAnimTypeCount], g_faults, g_events;

// A compound's sub-channels (built by the sweep), by type.
void CountSubChannels(FnAnim *anim) {
    if (anim->type != kCompound)
        return;
    FnCompoundChannel *compound = static_cast<FnCompoundChannel *>(anim);
    FnAnim **channels = compound->channels;
    if (channels == NULL)
        return;
    for (int i = 0; i < reinterpret_cast<CompoundData *>(compound->anim)->count; i++) {
        uint32_t t = channels[i]->type;
        if (t < kAnimTypeCount)
            g_subTypes[t]++;
        CountSubChannels(channels[i]);
    }
}

int CountLines(const std::string &log, const char *needle) {
    int n = 0;
    for (size_t at = log.find(needle); at != std::string::npos; at = log.find(needle, at + 1))
        n++;
    return n;
}

void BankSide(bool original, const Bank &bank, std::vector<uint8_t> &image, Snapshot *out) {
    g_log = &out->log;
    ArenaReset();
    memcpy(image.data(), bank.data.data(), image.size());
    LoadedTables = NULL;
    memset((void *)0x00241ba0, 0, 0x24);   // the three scratch buffers: each side starts without them
    {
        Originals scope(original);
        ((void (*)(uint32_t))0x000f7d00)(0x400000);   // MemoryPoolManager::Init
        DynamicLoader loader;
        loader.Construct(image.data(), (uint32_t)bank.size, NULL);
        int index = 0;
        uint8_t *symbol = NULL;
        if (!loader.GetNextAddr("AnimationBank", &index, (void **)&symbol) || symbol == NULL) {
            Logf("no AnimationBank");
        } else {
            ((void (*)(uint8_t *, void *))0x000f7170)(symbol, &loader);   // AnimBank::Constructor
            AnimBank *animBank = reinterpret_cast<AnimBank *>(symbol);
            int count = animBank->count;
            uint8_t **anims = animBank->anims;
            const char **names = animBank->names;
            std::vector<FnAnim *> built;
            for (int i = 0; i < count; i++) {
                uint16_t type = reinterpret_cast<AnimData *>(anims[i])->type;
                FnAnim *anim = ((FnAnim *(*)(uint8_t *))0x00014210)(anims[i]);   // NewFnAnim
                uint32_t size = type < kAnimTypeCount ? BlockSizes[type] : 0;
                Logf("anim %d \"%s\" type %u at %p, %08x", i, names != NULL ? names[i] : "?", type, (void *)anim,
                     Hash(anim, size));
                if (!original) {
                    g_anims++;
                    if (type < kAnimTypeCount)
                        g_types[type]++;
                }
                Sweep(anim);
                if (!original)
                    CountSubChannels(anim);
                built.push_back(anim);
            }
            for (size_t i = built.size(); i-- > 0;)
                ((void (*)(FnAnim *))0x000141e0)(built[i]);   // the pool's delete
        }
        ((void (*)())0x00106790)();   // ScratchBuffer::FreeScratchBuffers, so the next side starts the same
        out->arena.assign(g_arena, g_arena + g_arenaUsed);
        out->image = image;
        loader.Destruct();
        ((void (*)())0x000f7d50)();   // MemoryPoolManager::Cleanup
    }
    g_log = NULL;
}

void CompareLogs(const char *what, const std::string &o, const std::string &p) {
    g_checks++;
    if (o == p)
        return;
    size_t i = 0, line = 0, start = 0;
    while (i < o.size() && i < p.size() && o[i] == p[i]) {
        if (o[i] == '\n') {
            line++;
            start = i + 1;
        }
        i++;
    }
    size_t eo = o.find('\n', start), ep = p.find('\n', start);
    std::string lo = o.substr(start, eo == std::string::npos ? std::string::npos : eo - start);
    std::string lp = p.substr(start, ep == std::string::npos ? std::string::npos : ep - start);
    // the anim the difference is in
    size_t a = o.rfind("\nanim ", start);
    std::string anim = a == std::string::npos ? "" : o.substr(a + 1, o.find('\n', a + 1) - a - 1);
    char detail[900];
    snprintf(detail, sizeof(detail), "line %u (%.120s): original \"%.250s\" / ours \"%.250s\"", (unsigned)line,
             anim.c_str(), lo.c_str(), lp.c_str());
    Report(what, detail);
}

void CompareBytes(const char *what, const std::vector<uint8_t> &o, const std::vector<uint8_t> &p) {
    g_checks++;
    if (o == p)
        return;
    char detail[200];
    if (o.size() != p.size()) {
        snprintf(detail, sizeof(detail), "%u bytes / %u", (unsigned)o.size(), (unsigned)p.size());
    } else {
        size_t i = 0;
        while (o[i] == p[i])
            i++;
        snprintf(detail, sizeof(detail), "first difference at +%x: %02x / %02x", (unsigned)i, o[i], p[i]);
    }
    Report(what, detail);
}

}   // namespace

void AnimShadow_Run(void) {
    if (getenv("NIGHTFIRE_ANIMSHADOW") == NULL)
        return;
    char folder[MAX_PATH], pattern[MAX_PATH], path[MAX_PATH];
    if (!Xbox_ResolvePath("D:\\driving", folder, sizeof(folder)))
        return;
    std::vector<Bank> banks;
    std::set<uint32_t> seen;
    snprintf(pattern, sizeof(pattern), "%s\\*.viv", folder);
    WIN32_FIND_DATAA found;
    HANDLE search = FindFirstFileA(pattern, &found);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            snprintf(path, sizeof(path), "%s\\%s", folder, found.cFileName);
            ReadBanks(path, &banks, &seen);
        } while (FindNextFileA(search, &found));
        FindClose(search);
    }
    g_arena = (uint8_t *)VirtualAlloc(NULL, kArena, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (g_arena == NULL)
        return;
    g_arenaUsed = kArena;
    for (int i = 0; i < 4; i++) {
        g_handlers[i].vtable = g_handlerVtable;
        g_handlers[i].next = NULL;
    }
    for (size_t i = 0; i < 1024; i++)
        g_handlerTable[i] = &g_handlers[i & 3];
    memset(g_maskAll, 0xff, sizeof(g_maskAll));
    memset(g_maskHalf, 0x55, sizeof(g_maskHalf));

    // the game's state, put back at the end: EAGL's hooks and loaded list, EAGLAnim's pool and scratch globals
    EaglMallocFn savedMalloc = EaglMalloc;
    EaglFreeFn savedFree = EaglFree;
    void *savedPrint = PrintHook, *savedLoaded = LoadedTables;
    std::vector<uint8_t> savedPool((uint8_t *)0x002414b0, (uint8_t *)0x00241c00);
    EaglMalloc = ShadowMalloc;
    EaglFree = ShadowFree;
    PrintHook = (void *)ShadowPrint;

    for (const Bank &bank : banks) {
        std::vector<uint8_t> image(bank.data.size(), 0);
        Snapshot o, p;
        BankSide(true, bank, image, &o);
        BankSide(false, bank, image, &p);
        char what[300];
        snprintf(what, sizeof(what), "%s: the log", bank.name.c_str());
        CompareLogs(what, o.log, p.log);
        g_faults += CountLines(o.log, "FAULT");
        g_events += CountLines(o.log, "event handler");
        snprintf(what, sizeof(what), "%s: the arena", bank.name.c_str());
        CompareBytes(what, o.arena, p.arena);
        snprintf(what, sizeof(what), "%s: the bank", bank.name.c_str());
        CompareBytes(what, o.image, p.image);
    }

    EaglMalloc = savedMalloc;
    EaglFree = savedFree;
    PrintHook = savedPrint;
    LoadedTables = savedLoaded;
    memcpy((uint8_t *)0x002414b0, savedPool.data(), savedPool.size());
    VirtualFree(g_arena, 0, MEM_RELEASE);
    char types[400] = "";
    for (int t = 0; t < kAnimTypeCount; t++)
        if (g_types[t] != 0)
            snprintf(types + strlen(types), sizeof(types) - strlen(types), " %d:%d", t, g_types[t]);
    char subTypes[400] = "";
    for (int t = 0; t < kAnimTypeCount; t++)
        if (g_subTypes[t] != 0)
            snprintf(subTypes + strlen(subTypes), sizeof(subTypes) - strlen(subTypes), " %d:%d", t, g_subTypes[t]);
    printf("[animshadow] %u banks, %d anims (by type%s; their channels%s), %d events, %d faults, %d checks: %s\n",
           (unsigned)banks.size(), g_anims, types, subTypes, g_events, g_faults, g_checks,
           g_failures == 0 ? "the same as the original" : "FAILED");
    fflush(stdout);
}
