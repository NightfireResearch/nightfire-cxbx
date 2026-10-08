#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "AudioVoiceShadow.h"

#include "../audio/Bank.h"
#include "../audio/Mix.h"
#include "../audio/Voice.h"
#include "../data/SymbolTable.h"        // CarpGroupMap: trees for the shared helpers
#include "../engine/UMemory.hpp"
#include "../engine/URefCounter.h"
#include "../platform/RealSystem.h"     // TIMER_gettick
#include "../sound/snd/Banks.h"
#include "../sound/snd/System.h"
#include "../sound/snd/Voices.h"
#include "../world/SoundMap.h"          // RefCounterMapBuyHead
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

#include <windows.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <bit>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_AUDIOVOICESHADOW=1, from the first simulation tick (the banks are loaded then).
//
// AVoice and ABank (0x00123820..0x00124790, 0x00125db0..0x00126d10), original against port, the originals of both
// ranges swapped in for the original's runs. The SND calls (SNDstop, SNDover, SNDvol, SNDpitchmult, SND3dpos,
// SNDfxlevel, SNDBANK_play, SNDSYS_enter/leavecritical) and TIMER_gettick are replaced by recording fakes for the
// stateful cases, with the real sound lock held throughout so the 100 Hz server cannot run meanwhile; the active
// list is swapped for a private one. Compared: the objects byte for byte, the private list's contents, the call log.
//   - views: Play (random volumes incl. zero, negative and NaN, pitches past +-4, azimuths, delays past the 32767
//     ticks limit, effects levels outside 0..1, on views with and without a pending delay, listed or not, with and
//     without a patch), Stop, Destruct (with and without a voice, listed or not), Construct;
//   - voices: Construct, Set (same and other patches, -1, with and without voices), Play through a fake mix;
//   - PlayVoices over private lists of random views (no voice, a voice that is or is not over, looping or not,
//     volumes and pitches of zero, delays running out), random elapsed ticks (past the 8-tick clamp), effects
//     mode off, a mode for all voices and one not;
//   - the debug map alone: random insert_unique, erase(where), erase(first, last), -- on two maps built alike,
//     the trees compared (shape, colours, values) after every step; the destructors;
//   - URefCounter<ABank>'s tree alone, the same, names in mixed case (_stricmp);
//   - the 0x18-node helpers on two name trees built alike: find, --, _Max, the right rotation; both _Erase copies
//     run (not compared);
//   - live: GetPatchName for every loaded bank's handle and patches -1..129, IsPatchPresent, Get by name (and in
//     another case, and a miss), begin/end, BuildMap over a private list of views on the loaded banks (compared
//     by name content: the port's "" is its own literal), the empty bank's constructor and the named one with no
//     sound system and no index, and their destructors.
// Mutations it catches: View::Play truncating instead of rounding (the pitch), PlayVoices' 8-tick clamp moved
// (the views' delays), insert_unique's `<` turned to `<=`.
// ---------------------------------------------------------------------------------------------------------------

namespace {   // this file's own types: another test's of the same name must not merge with them

#define Shadow_ActiveViews (*(AVoiceViewList *)0x00243aa8)
#define Shadow_LastPlayTick I32_AT(0x00243a98)
#define Shadow_FxMode I32_AT(0x001d8100)
#define Shadow_fgSystem PTR_AT(0x00243b34)
#define Shadow_EmptyBank ((ABank *)0x00243b18)

// ---- the originals
typedef AVoice::View *(__fastcall *ViewConstructFn)(AVoice::View *, int);
typedef void (__fastcall *ViewVoidFn)(AVoice::View *, int);
typedef void (__fastcall *ViewPlayFn)(AVoice::View *, int, float, float, float, float, float);
typedef AVoice *(__fastcall *VoiceConstructFn)(AVoice *, int, AMix *, int, int);
typedef void (__fastcall *VoiceSetFn)(AVoice *, int, int, int);
typedef void (__fastcall *VoicePlayFn)(AVoice *, int, int, float, float, float, float, float);
typedef void (*PlayVoicesFn)();
typedef AVoiceMap *(*BuildMapFn)();
typedef AVoiceMap *(__fastcall *VoiceMapConstructFn)(AVoiceMap *, int);
typedef void (__fastcall *VoiceMapDestructFn)(AVoiceMap *, int);
typedef AVoiceMapInsert *(__fastcall *VoiceMapInsertFn)(AVoiceMap *, int, AVoiceMapInsert *, const AVoiceMapValue *);
typedef AVoiceMapNode **(__fastcall *VoiceMapEraseFn)(AVoiceMap *, int, AVoiceMapNode **, AVoiceMapNode *);
typedef AVoiceMapNode **(__fastcall *VoiceMapEraseRangeFn)(AVoiceMap *, int, AVoiceMapNode **, AVoiceMapNode *,
                                                           AVoiceMapNode *);
typedef void (__fastcall *VoiceMapDecFn)(AVoiceMap::Iterator *, int);
typedef ABank *(__fastcall *BankConstructFn)(ABank *, int);
typedef ABank *(__fastcall *BankConstructNamedFn)(ABank *, int, const char *, bool);
typedef void (__fastcall *BankDestructFn)(ABank *, int);
typedef bool (__fastcall *BankPatchFn)(ABank *, int, int);
typedef ABank *(*BankGetFn)(const char *);
typedef const char *(*PatchNameFn)(int, int);
typedef RefCounterNode **(*BankIteratorFn)(RefCounterNode **);
typedef RefCounterInsertResult *(__fastcall *RefInsertFn)(BankRefTree *, int, RefCounterInsertResult *,
                                                          const RefCounterValue *);
typedef RefCounterNode **(__fastcall *RefEraseFn)(BankRefTree *, int, RefCounterNode **, RefCounterNode *);
typedef RefCounterNode **(__fastcall *RefEraseRangeFn)(BankRefTree *, int, RefCounterNode **,
                                                       RefCounterNode *, RefCounterNode *);
typedef void (__fastcall *RefDestroyFn)(BankRefTree *, int);
typedef TreeNode *(*TreeMaxFn)(TreeNode *);
typedef void (__fastcall *TreeRotateFn)(SharedTree *, int, TreeNode *);
typedef void (__fastcall *TreeDecFn)(SharedTreeIterator *, int);
typedef TreeNode **(__fastcall *TreeFindFn)(SharedTree *, int, TreeNode **, const char *const *);
typedef void (__fastcall *TreeEraseFn)(SharedTree *, int, TreeNode *);

const ViewConstructFn Orig_ViewConstruct = (ViewConstructFn)0x00123820;
const ViewVoidFn Orig_ViewDestruct = (ViewVoidFn)0x00123c50;
const ViewVoidFn Orig_ViewStop = (ViewVoidFn)0x00123be0;
const ViewPlayFn Orig_ViewPlay = (ViewPlayFn)0x001244d0;
const VoiceConstructFn Orig_VoiceConstruct = (VoiceConstructFn)0x00123ca0;
const VoiceSetFn Orig_VoiceSet = (VoiceSetFn)0x00123870;
const VoicePlayFn Orig_VoicePlay = (VoicePlayFn)0x00124690;
const PlayVoicesFn Orig_PlayVoices = (PlayVoicesFn)0x00123a30;
const BuildMapFn Orig_BuildMap = (BuildMapFn)0x001243c0;
const VoiceMapConstructFn Orig_VoiceMapConstruct = (VoiceMapConstructFn)0x00124710;
const VoiceMapDestructFn Orig_VoiceMapDestruct = (VoiceMapDestructFn)0x00124750;
const VoiceMapInsertFn Orig_VoiceMapInsert = (VoiceMapInsertFn)0x00124240;
const VoiceMapEraseFn Orig_VoiceMapErase = (VoiceMapEraseFn)0x00123f70;
const VoiceMapEraseRangeFn Orig_VoiceMapEraseRange = (VoiceMapEraseRangeFn)0x00124300;
const VoiceMapDecFn Orig_VoiceMapDec = (VoiceMapDecFn)0x00123930;
const BankConstructFn Orig_BankConstruct = (BankConstructFn)0x00125db0;
const BankConstructNamedFn Orig_BankConstructNamed = (BankConstructNamedFn)0x00125dd0;
const BankDestructFn Orig_BankDestruct = (BankDestructFn)0x00125ef0;
const BankPatchFn Orig_IsPatchPresent = (BankPatchFn)0x00125ec0;
const BankGetFn Orig_BankGet = (BankGetFn)0x001269d0;
const PatchNameFn Orig_GetPatchName = (PatchNameFn)0x00126a30;
const BankIteratorFn Orig_BankBegin = (BankIteratorFn)0x00126910;
const BankIteratorFn Orig_BankEnd = (BankIteratorFn)0x00126930;
const RefInsertFn Orig_RefInsert = (RefInsertFn)0x001265f0;
const RefEraseFn Orig_RefErase = (RefEraseFn)0x00125fa0;
const RefEraseRangeFn Orig_RefEraseRange = (RefEraseRangeFn)0x00126500;
const RefDestroyFn Orig_RefDestroy = (RefDestroyFn)0x001267b0;
const TreeMaxFn Orig_TreeMax = (TreeMaxFn)0x00126b50;
const TreeRotateFn Orig_TreeRrotate = (TreeRotateFn)0x00126b70;
const TreeDecFn Orig_TreeDec = (TreeDecFn)0x00126bd0;
const TreeFindFn Orig_TreeFind = (TreeFindFn)0x00126cb0;
const TreeEraseFn Orig_TreeEraseId = (TreeEraseFn)0x00126c30;
const TreeEraseFn Orig_TreeEraseName = (TreeEraseFn)0x00126c70;

// ---- results

int g_cases, g_checks, g_differ, g_faults;

void Report(const char *format, ...) {
    if (g_differ > 10)
        return;
    va_list arguments;
    va_start(arguments, format);
    printf("[audiovoiceshadow]   ");
    vprintf(format, arguments);
    printf("\n");
    va_end(arguments);
    fflush(stdout);
}

void Check(bool same, const char *what, int index) {
    g_checks++;
    if (!same) {
        g_differ++;
        Report("%s differs, case %d", what, index);
    }
}

template <class T>
void CheckBytes(const T &original, const T &port, const char *what, int index) {
    Check(memcmp(&original, &port, sizeof(T)) == 0, what, index);
}

uint32_t g_random = 0x2545f491;
uint32_t Random(uint32_t below) {
    g_random = g_random * 1664525u + 1013904223u;
    return below == 0 ? 0 : (g_random >> 8) % below;
}
float Uniform(float lo, float hi) {
    return lo + (hi - lo) * float(Random(1u << 24)) * (1.0f / 16777216.0f);
}
bool Chance(uint32_t oneIn) {
    return Random(oneIn) == 0;
}

template <class F>
bool Guarded(F &&run) {
#ifdef _MSC_VER
    __try {
        run();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_faults++;
        return false;
    }
#else
    run();
    return true;
#endif
}

// The originals of both ranges, for as long as the scope lives
struct Originals {
    Originals() {
        XbeOriginal_RestoreRange(0x00123820, 0x00124790, true);
        XbeOriginal_RestoreRange(0x00125db0, 0x00126d10, true);
    }
    ~Originals() {
        XbeOriginal_RestoreRange(0x00123820, 0x00124790, false);
        XbeOriginal_RestoreRange(0x00125db0, 0x00126d10, false);
    }
};

// ---- hooks: a jump written over an entry (and over the port a patched entry jumps to)

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};
Hook g_hooks[32];
int g_hookCount;

void HookOne(uint32_t at, const void *to) {
    for (int i = 0; i < g_hookCount; i++) {
        if (g_hooks[i].at == at)
            return;
    }
    if (g_hookCount == int(sizeof(g_hooks) / sizeof(g_hooks[0])))
        return;
    Hook &hook = g_hooks[g_hookCount++];
    hook.at = at;
    hook.on = false;
    DWORD old;
    if (!VirtualProtect((void *)(uintptr_t)at, 5, PAGE_EXECUTE_READWRITE, &old))
        return;
    memcpy(hook.saved, (void *)(uintptr_t)at, 5);
    uint8_t jump[5];
    jump[0] = 0xe9;
    int32_t rel = (int32_t)((uint32_t)(uintptr_t)to - (at + 5));
    memcpy(jump + 1, &rel, 4);
    memcpy((void *)(uintptr_t)at, jump, 5);
    VirtualProtect((void *)(uintptr_t)at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)at, 5);
    hook.on = true;
}

void HookInstall(uint32_t at, const void *to) {
    const uint8_t *entry = (const uint8_t *)(uintptr_t)at;
    uint32_t port = 0;
    if (entry[0] == 0xe9) {
        int32_t rel;
        memcpy(&rel, entry + 1, 4);
        port = at + 5 + uint32_t(rel);
    }
    HookOne(at, to);
    if (port != 0 && port != (uint32_t)(uintptr_t)to)
        HookOne(port, to);
}

void HooksRemove() {
    for (int i = g_hookCount; i-- > 0;) {
        Hook &hook = g_hooks[i];
        if (!hook.on)
            continue;
        DWORD old;
        VirtualProtect((void *)(uintptr_t)hook.at, 5, PAGE_EXECUTE_READWRITE, &old);
        memcpy((void *)(uintptr_t)hook.at, hook.saved, 5);
        VirtualProtect((void *)(uintptr_t)hook.at, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)hook.at, 5);
        hook.on = false;
    }
    g_hookCount = 0;
}

// ---- the call log and the fakes

std::vector<uint32_t> g_log;
int g_tick;

void Log(uint32_t type, std::initializer_list<uint32_t> words) {
    g_log.push_back(type);
    g_log.push_back(uint32_t(words.size()));
    g_log.insert(g_log.end(), words.begin(), words.end());
}

int FakeStop(int handle) {
    Log(1, {uint32_t(handle)});
    return 0;
}
void FakeEnter(void) {
    Log(2, {});
}
void FakeLeave(void) {
    Log(3, {});
}
int FakeOver(int handle) {
    Log(4, {uint32_t(handle)});
    return (handle >> 4) & 1;
}
int FakeVol(int handle, int volume) {
    Log(5, {uint32_t(handle), uint32_t(volume)});
    return 0;
}
int FakePitchMult(int handle, int multiplier) {
    Log(6, {uint32_t(handle), uint32_t(multiplier)});
    return 0;
}
int Fake3dPos(int handle, int azimuth, int elevation) {
    Log(7, {uint32_t(handle), uint32_t(azimuth), uint32_t(elevation)});
    return 0;
}
int FakeFxLevel(int handle, int bus, int level) {
    Log(8, {uint32_t(handle), uint32_t(bus), uint32_t(level)});
    return 0;
}
int FakeBankPlay(int bank, int patch, SND::PlayOpts *opts) {
    Log(9, {uint32_t(bank), uint32_t(patch), uint32_t(opts->vol), uint32_t(opts->bend), uint32_t(opts->key),
            uint32_t(opts->velocity), opts->progVol, uint32_t(opts->fxLevel), opts->azimuth, opts->elevation,
            opts->pitchMult, opts->timeMult, opts->tempoMult, opts->distort, opts->lowpass, opts->highpass});
    return 0x5000 + ((bank * 131 + patch) & 0xfff);
}
int FakeTick() {
    return g_tick;
}

void InstallFakes() {
    HookInstall(0x0013c900, (const void *)&FakeStop);
    HookInstall(uint32_t(uintptr_t(&SNDstop)), (const void *)&FakeStop);
    HookInstall(0x0013b950, (const void *)&FakeEnter);
    HookInstall(uint32_t(uintptr_t(&SNDSYS_entercritical)), (const void *)&FakeEnter);
    HookInstall(0x0013b970, (const void *)&FakeLeave);
    HookInstall(uint32_t(uintptr_t(&SNDSYS_leavecritical)), (const void *)&FakeLeave);
    HookInstall(0x0013cc00, (const void *)&FakeOver);
    HookInstall(uint32_t(uintptr_t(&SNDover)), (const void *)&FakeOver);
    HookInstall(0x0013cb40, (const void *)&FakeVol);
    HookInstall(uint32_t(uintptr_t(&SNDvol)), (const void *)&FakeVol);
    HookInstall(0x0013caa0, (const void *)&FakePitchMult);
    HookInstall(uint32_t(uintptr_t(&SNDpitchmult)), (const void *)&FakePitchMult);
    HookInstall(0x0013c9f0, (const void *)&Fake3dPos);
    HookInstall(uint32_t(uintptr_t(&SND3dpos)), (const void *)&Fake3dPos);
    HookInstall(0x0013c960, (const void *)&FakeFxLevel);
    HookInstall(uint32_t(uintptr_t(&SNDfxlevel)), (const void *)&FakeFxLevel);
    HookInstall(0x0013cc20, (const void *)&FakeBankPlay);
    HookInstall(uint32_t(uintptr_t(&SNDBANK_play)), (const void *)&FakeBankPlay);
    HookInstall(0x0010a6c0, (const void *)&FakeTick);
    HookInstall(uint32_t(uintptr_t(&TIMER_gettick)), (const void *)&FakeTick);
}

// ---- a private active list (the real one is put back afterwards)

PointerListNode g_listHead;
uint8_t g_savedList[sizeof(AVoiceViewList)];

void ListInstall() {
    memcpy(g_savedList, &Shadow_ActiveViews, sizeof(g_savedList));
    g_listHead.next = &g_listHead;
    g_listHead.prev = &g_listHead;
    g_listHead.value = NULL;
    Shadow_ActiveViews.head = &g_listHead;
    Shadow_ActiveViews.size = 0;
}

void ListRestore() {
    memcpy(&Shadow_ActiveViews, g_savedList, sizeof(g_savedList));
}

// Every node freed (all came from the fast allocator), the list emptied.
void ListClear() {
    PointerListNode *node = g_listHead.next;
    while (node != &g_listHead) {
        PointerListNode *next = node->next;
        UMemory::FastFree(node, sizeof(PointerListNode));
        node = next;
    }
    g_listHead.next = &g_listHead;
    g_listHead.prev = &g_listHead;
    Shadow_ActiveViews.size = 0;
}

void ListAppend(void *value) {
    PointerListNode *node = static_cast<PointerListNode *>(UMemory::FastAlloc(sizeof(PointerListNode), "STL"));
    node->value = value;
    node->next = &g_listHead;
    node->prev = g_listHead.prev;
    g_listHead.prev->next = node;
    g_listHead.prev = node;
    Shadow_ActiveViews.size++;
}

// The list as words, `self` written as a marker; the size first.
std::vector<uint32_t> ListContents(const void *self) {
    std::vector<uint32_t> words;
    words.push_back(Shadow_ActiveViews.size);
    int n = 0;
    for (PointerListNode *node = g_listHead.next; node != &g_listHead && n < 64; node = node->next, n++)
        words.push_back(node->value == self ? 0xc0ffee00u : uint32_t(uintptr_t(node->value)));
    return words;
}

// ---- random inputs

void RandomView(AVoice::View *view, bool withVoice) {
    uint8_t *bytes = reinterpret_cast<uint8_t *>(view);
    for (size_t i = 0; i < sizeof(*view); i++)
        bytes[i] = uint8_t(Random(256));
    view->patch = Chance(8) ? -1 - int(Random(3)) : int(Random(128));
    view->bank = Chance(8) ? -1 : int(Random(10));
    view->loop = uint8_t(Random(2));
    view->handle = withVoice && Chance(2) ? int(0x100 + Random(64) * 16) : -1;
    view->delay = Chance(3) ? int16_t(-1 - int(Random(5))) : int16_t(int(Random(2000)) - 100);
    view->listedVolume = Chance(2) ? 0 : int8_t(1 + Random(127));
    if (Chance(6))
        view->pitch = 0;
    view->finished = uint8_t(Random(2));
}

float RandomLevel(float lo, float hi) {
    switch (Random(12)) {
    case 0: return 0.0f;
    case 1: return -0.0f;
    case 2: return 1.0f / 127.0f;
    case 3: return 1.0f;
    case 4: return std::bit_cast<float>(0x7fc00000u);
    default: return Uniform(lo, hi);
    }
}

// ---- trees: shape, colours and values, nodes named by their in-order position

template <class Node>
Node *NextNode(Node *node) {
    if (node->isNil)
        return node;
    if (!node->right->isNil) {
        node = node->right;
        while (!node->left->isNil)
            node = node->left;
        return node;
    }
    Node *parent = node->parent;
    while (!parent->isNil && node == parent->right) {
        node = parent;
        parent = parent->parent;
    }
    return parent;
}

template <class Node>
std::vector<Node *> InOrder(Node *head) {
    std::vector<Node *> order;
    for (Node *node = head->left; node != head && order.size() < 4096; node = NextNode(node))
        order.push_back(node);
    return order;
}

template <class Node>
uint32_t Position(const std::vector<Node *> &order, Node *head, Node *node) {
    if (node == head)
        return 0xffffffffu;
    for (size_t i = 0; i < order.size(); i++) {
        if (order[i] == node)
            return uint32_t(i);
    }
    return 0xfffffffeu;
}

template <class Node, class ValueFn>
std::vector<uint32_t> Shape(Node *head, uint32_t size, ValueFn value) {
    std::vector<Node *> order = InOrder(head);
    std::vector<uint32_t> words;
    words.push_back(size);
    words.push_back(uint32_t(order.size()));
    words.push_back(Position(order, head, head->parent));
    words.push_back(Position(order, head, head->left));
    words.push_back(Position(order, head, head->right));
    words.push_back(head->color | head->isNil << 8);
    for (Node *node : order) {
        value(node, words);
        words.push_back(node->color | node->isNil << 8);
        words.push_back(Position(order, head, node->left));
        words.push_back(Position(order, head, node->right));
        words.push_back(Position(order, head, node->parent));
    }
    return words;
}

uint32_t HashText(const char *text) {
    uint32_t hash = 2166136261u;
    for (; *text != '\0'; text++)
        hash = (hash ^ uint8_t(*text)) * 16777619u;
    return hash;
}

// =============================================================================================================
// Views and voices (the fakes installed, the private list in place)
// =============================================================================================================

void ViewCases() {
    for (int i = 0; i < 4000; i++) {
        AVoice::View start;
        RandomView(&start, true);
        int operation = int(Random(8));     // 0..4 Play, 5 Stop, 6 Destruct, 7 Construct
        float volume = RandomLevel(-0.3f, 1.4f);
        float pitch = Chance(5) ? Uniform(-8.0f, 8.0f) : Uniform(-1.5f, 3.0f);
        float azimuth = Uniform(-1.5f, 1.5f);
        float delay = Chance(6) ? Uniform(1000.0f, 1.0e6f) : Uniform(-2.0f, 20.0f);
        float fxLevel = RandomLevel(-0.5f, 1.5f);
        bool listed = start.listedVolume != 0 && Chance(2);

        AVoice::View original = start, port = start;
        std::vector<uint32_t> listO, listP, logO, logP;

        ListClear();
        if (listed)
            ListAppend(&original);
        g_log.clear();
        {
            Originals scope;
            Guarded([&] {
                switch (operation) {
                case 5: Orig_ViewStop(&original, 0); break;
                case 6: Orig_ViewDestruct(&original, 0); break;
                case 7: Orig_ViewConstruct(&original, 0); break;
                default: Orig_ViewPlay(&original, 0, volume, pitch, azimuth, delay, fxLevel); break;
                }
            });
        }
        listO = ListContents(&original);
        logO = g_log;

        ListClear();
        if (listed)
            ListAppend(&port);
        g_log.clear();
        Guarded([&] {
            switch (operation) {
            case 5: port.Stop(); break;
            case 6: port.Destruct(); break;
            case 7: port.Construct(); break;
            default: port.Play(volume, pitch, azimuth, delay, fxLevel); break;
            }
        });
        listP = ListContents(&port);
        logP = g_log;

        g_cases++;
        CheckBytes(original, port, "view", i);
        Check(listO == listP, "view: active list", i);
        Check(logO == logP, "view: SND calls", i);
    }
    ListClear();
}

void VoiceCases() {
    uint8_t mixO[sizeof(AMix)], mixP[sizeof(AMix)];
    for (int i = 0; i < 3000; i++) {
        int operation = int(Random(3));     // 0 Construct, 1 Set, 2 Play
        AVoice start;
        for (AVoice::View &view : start.views)
            RandomView(&view, true);
        start.mix = reinterpret_cast<AMix *>(mixO);
        int bank = Chance(6) ? -1 : int(Random(10));
        int patch = Chance(5) ? -1 : int(Random(128));
        if (operation == 1 && Chance(3)) {
            bank = start.views[0].bank;
            patch = start.views[0].patch;
        }
        int which = int(Random(3));
        float volume = Chance(4) ? Uniform(0.0f, 0.02f) : RandomLevel(-0.3f, 1.4f);
        float pitch = Uniform(-5.0f, 5.0f);
        float azimuth = Uniform(-1.0f, 1.0f);
        float delay = Uniform(-1.0f, 10.0f);
        float fxLevel = RandomLevel(-0.5f, 1.5f);
        for (size_t k = 0; k < sizeof(mixO); k++)
            mixO[k] = uint8_t(Random(256));
        AMix *fields = reinterpret_cast<AMix *>(mixO);
        fields->unknown24 = int(Random(10));
        fields->unknown28 = Uniform(0.0f, 3.0f);
        fields->unknown2c = Chance(5) ? 1.0f : Uniform(0.0f, 1.2f);
        memcpy(mixP, mixO, sizeof(mixP));

        AVoice original = start, port = start;
        port.mix = reinterpret_cast<AMix *>(mixP);
        if (operation == 0) {
            memset(&original, 0xcd, sizeof(original));
            memset(&port, 0xcd, sizeof(port));
        }
        std::vector<uint32_t> listO, listP, logO, logP;

        ListClear();
        g_log.clear();
        {
            Originals scope;
            Guarded([&] {
                switch (operation) {
                case 0: Orig_VoiceConstruct(&original, 0, reinterpret_cast<AMix *>(0x00123456), bank, patch); break;
                case 1: Orig_VoiceSet(&original, 0, bank, patch); break;
                default: Orig_VoicePlay(&original, 0, which, volume, pitch, azimuth, delay, fxLevel); break;
                }
            });
        }
        listO = ListContents(&original.views[which]);
        logO = g_log;

        ListClear();
        g_log.clear();
        Guarded([&] {
            switch (operation) {
            case 0: port.Construct(reinterpret_cast<AMix *>(0x00123456), bank, patch); break;
            case 1: port.Set(bank, patch); break;
            default: port.Play(which, volume, pitch, azimuth, delay, fxLevel); break;
            }
        });
        listP = ListContents(&port.views[which]);
        logP = g_log;

        if (operation != 0)
            port.mix = original.mix;   // the copies' own mixes
        g_cases++;
        CheckBytes(original, port, "voice", i);
        Check(memcmp(mixO, mixP, sizeof(mixO)) == 0, "voice: mix", i);
        Check(listO == listP, "voice: active list", i);
        Check(logO == logP, "voice: SND calls", i);
    }
    ListClear();
}

void PlayVoicesCases() {
    const int kMost = 12;
    AVoice::View viewsO[kMost], viewsP[kMost];
    PointerListNode headO, headP, nodesO[kMost], nodesP[kMost];
    int savedFxMode = Shadow_FxMode;
    int savedLastTick = Shadow_LastPlayTick;
    for (int i = 0; i < 1500; i++) {
        int count = Chance(10) ? 0 : int(1 + Random(kMost));
        for (int k = 0; k < count; k++) {
            RandomView(&viewsO[k], true);
            if (Chance(4))
                viewsO[k].volume = int8_t(-int(Random(4)));
            viewsP[k] = viewsO[k];
        }
        // the lists, built alike
        PointerListNode *heads[2] = {&headO, &headP};
        PointerListNode *nodes[2] = {nodesO, nodesP};
        AVoice::View *views[2] = {viewsO, viewsP};
        for (int side = 0; side < 2; side++) {
            PointerListNode *head = heads[side];
            head->next = head;
            head->prev = head;
            for (int k = 0; k < count; k++) {
                PointerListNode *node = &nodes[side][k];
                node->value = &views[side][k];
                node->next = head;
                node->prev = head->prev;
                head->prev->next = node;
                head->prev = node;
            }
        }
        int fxMode = int(Random(3)) - 1;
        int now = int(Random(100000));
        int last = now - (int(Random(16)) - 2);

        std::vector<uint32_t> logO, logP;
        int lastO, lastP;

        Shadow_ActiveViews.head = &headO;
        Shadow_ActiveViews.size = uint32_t(count);
        Shadow_FxMode = fxMode;
        Shadow_LastPlayTick = last;
        g_tick = now;
        g_log.clear();
        {
            Originals scope;
            Guarded([&] { Orig_PlayVoices(); });
        }
        logO = g_log;
        lastO = Shadow_LastPlayTick;

        Shadow_ActiveViews.head = &headP;
        Shadow_ActiveViews.size = uint32_t(count);
        Shadow_FxMode = fxMode;
        Shadow_LastPlayTick = last;
        g_log.clear();
        Guarded([&] { AVoice::PlayVoices(); });
        logP = g_log;
        lastP = Shadow_LastPlayTick;

        g_cases++;
        Check(memcmp(viewsO, viewsP, sizeof(AVoice::View) * count) == 0, "PlayVoices: views", i);
        Check(logO == logP, "PlayVoices: SND calls", i);
        Check(lastO == lastP, "PlayVoices: last tick", i);
    }
    Shadow_ActiveViews.head = &g_listHead;
    Shadow_ActiveViews.size = 0;
    Shadow_FxMode = savedFxMode;
    Shadow_LastPlayTick = savedLastTick;
}

// =============================================================================================================
// The debug map alone
// =============================================================================================================

char g_keys[48];

void VoiceMapValueWords(AVoiceMapNode *node, std::vector<uint32_t> &words) {
    words.push_back(uint32_t(node->value.name - g_keys));
    words.push_back(uint32_t(node->value.info.count));
    words.push_back(uint32_t(node->value.info.volume));
    words.push_back(node->value.info.fx);
}

std::vector<uint32_t> VoiceMapShape(AVoiceMap *map) {
    return Shape(map->head, map->size, VoiceMapValueWords);
}

void VoiceMapCases() {
    for (int round = 0; round < 40; round++) {
        AVoiceMap mapO, mapP;
        {
            Originals scope;
            Orig_VoiceMapConstruct(&mapO, 0);
        }
        mapP.Construct();
        int steps = int(20 + Random(200));
        for (int step = 0; step < steps; step++) {
            int index = round * 1000 + step;
            uint32_t sizeNow = mapO.size;
            int operation = int(Random(10));
            if (operation < 6 || sizeNow == 0) {
                AVoiceMapValue value = {&g_keys[Random(sizeof(g_keys))],
                                        {int(Random(5)), int(Random(500)), uint8_t(Random(2)), {0, 0, 0}}};
                AVoiceMapInsert resultO = {}, resultP = {};
                {
                    Originals scope;
                    Guarded([&] { Orig_VoiceMapInsert(&mapO, 0, &resultO, &value); });
                }
                Guarded([&] { mapP.InsertUnique(&resultP, &value); });
                Check(Position(InOrder(mapO.head), mapO.head, resultO.node) ==
                          Position(InOrder(mapP.head), mapP.head, resultP.node) &&
                      resultO.inserted == resultP.inserted, "voice map insert: result", index);
            } else if (operation < 8) {
                uint32_t at = Random(sizeNow);
                AVoiceMapNode *resultO = NULL, *resultP = NULL;
                AVoiceMapNode *whereO = InOrder(mapO.head)[at], *whereP = InOrder(mapP.head)[at];
                {
                    Originals scope;
                    Guarded([&] { Orig_VoiceMapErase(&mapO, 0, &resultO, whereO); });
                }
                Guarded([&] { mapP.EraseAt(&resultP, whereP); });
                Check(Position(InOrder(mapO.head), mapO.head, resultO) ==
                      Position(InOrder(mapP.head), mapP.head, resultP), "voice map erase: result", index);
            } else if (operation == 8) {
                std::vector<AVoiceMapNode *> orderO = InOrder(mapO.head), orderP = InOrder(mapP.head);
                uint32_t first = Chance(4) ? 0 : Random(sizeNow + 1);
                uint32_t last = Chance(4) ? sizeNow : first + Random(sizeNow - first + 1);
                AVoiceMapNode *resultO = NULL, *resultP = NULL;
                AVoiceMapNode *firstO = first < sizeNow ? orderO[first] : mapO.head;
                AVoiceMapNode *lastO = last < sizeNow ? orderO[last] : mapO.head;
                AVoiceMapNode *firstP = first < sizeNow ? orderP[first] : mapP.head;
                AVoiceMapNode *lastP = last < sizeNow ? orderP[last] : mapP.head;
                {
                    Originals scope;
                    Guarded([&] { Orig_VoiceMapEraseRange(&mapO, 0, &resultO, firstO, lastO); });
                }
                Guarded([&] { mapP.EraseRange(&resultP, firstP, lastP); });
                Check(Position(InOrder(mapO.head), mapO.head, resultO) ==
                      Position(InOrder(mapP.head), mapP.head, resultP), "voice map erase range: result", index);
            } else {
                uint32_t at = Random(sizeNow + 1);
                AVoiceMap::Iterator itO = {at < sizeNow ? InOrder(mapO.head)[at] : mapO.head};
                AVoiceMap::Iterator itP = {at < sizeNow ? InOrder(mapP.head)[at] : mapP.head};
                {
                    Originals scope;
                    Guarded([&] { Orig_VoiceMapDec(&itO, 0); });
                }
                Guarded([&] { itP.Dec(); });
                Check(Position(InOrder(mapO.head), mapO.head, itO.node) ==
                      Position(InOrder(mapP.head), mapP.head, itP.node), "voice map --: result", index);
            }
            g_cases++;
            Check(VoiceMapShape(&mapO) == VoiceMapShape(&mapP), "voice map: tree", index);
        }
        {
            Originals scope;
            Guarded([&] { Orig_VoiceMapDestruct(&mapO, 0); });
        }
        Guarded([&] { mapP.Destruct(); });
        Check(mapO.head == NULL && mapO.size == 0 && mapP.head == NULL && mapP.size == 0, "voice map: destructor",
              round);
    }
}

// =============================================================================================================
// URefCounter<ABank>'s tree alone
// =============================================================================================================

void RefValueWords(RefCounterNode *node, std::vector<uint32_t> &words) {
    words.push_back(HashText(node->value.name));
    words.push_back(uint32_t(node->value.entry.references));
    words.push_back(uint32_t(uintptr_t(node->value.entry.object)));
}

void RefTreeInit(BankRefTree *tree) {
    tree->allocator = 0;
    tree->head = RefCounterMapBuyHead();
    tree->head->isNil = 1;
    tree->head->parent = tree->head;
    tree->head->left = tree->head;
    tree->head->right = tree->head;
    tree->size = 0;
}

void RefTreeCases() {
    for (int round = 0; round < 30; round++) {
        BankRefTree treeO, treeP;
        RefTreeInit(&treeO);
        RefTreeInit(&treeP);
        int steps = int(20 + Random(150));
        for (int step = 0; step < steps; step++) {
            int index = round * 1000 + step;
            uint32_t sizeNow = treeO.size;
            int operation = int(Random(10));
            if (operation < 6 || sizeNow == 0) {
                RefCounterValue value;
                memset(&value, 0, sizeof(value));
                sprintf(value.name, "%s\\bank%02u", Chance(2) ? "SFX" : "sfx", Random(40));
                if (Chance(3))
                    value.name[5] = char(toupper(uint8_t(value.name[5])));
                value.entry.references = int(Random(4));
                value.entry.object = reinterpret_cast<void *>(uintptr_t(Random(1000)));
                RefCounterInsertResult resultO = {}, resultP = {};
                {
                    Originals scope;
                    Guarded([&] { Orig_RefInsert(&treeO, 0, &resultO, &value); });
                }
                Guarded([&] { treeP.InsertUnique(&resultP, &value); });
                Check(Position(InOrder(treeO.head), treeO.head, resultO.node) ==
                          Position(InOrder(treeP.head), treeP.head, resultP.node) &&
                      resultO.inserted == resultP.inserted, "bank tree insert: result", index);
            } else if (operation < 8) {
                uint32_t at = Random(sizeNow);
                RefCounterNode *resultO = NULL, *resultP = NULL;
                RefCounterNode *whereO = InOrder(treeO.head)[at], *whereP = InOrder(treeP.head)[at];
                {
                    Originals scope;
                    Guarded([&] { Orig_RefErase(&treeO, 0, &resultO, whereO); });
                }
                Guarded([&] { treeP.EraseAt(&resultP, whereP); });
                Check(Position(InOrder(treeO.head), treeO.head, resultO) ==
                      Position(InOrder(treeP.head), treeP.head, resultP), "bank tree erase: result", index);
            } else {
                std::vector<RefCounterNode *> orderO = InOrder(treeO.head), orderP = InOrder(treeP.head);
                uint32_t first = Chance(4) ? 0 : Random(sizeNow + 1);
                uint32_t last = Chance(4) ? sizeNow : first + Random(sizeNow - first + 1);
                RefCounterNode *resultO = NULL, *resultP = NULL;
                RefCounterNode *firstO = first < sizeNow ? orderO[first] : treeO.head;
                RefCounterNode *lastO = last < sizeNow ? orderO[last] : treeO.head;
                RefCounterNode *firstP = first < sizeNow ? orderP[first] : treeP.head;
                RefCounterNode *lastP = last < sizeNow ? orderP[last] : treeP.head;
                {
                    Originals scope;
                    Guarded([&] { Orig_RefEraseRange(&treeO, 0, &resultO, firstO, lastO); });
                }
                Guarded([&] { treeP.EraseRange(&resultP, firstP, lastP); });
                Check(Position(InOrder(treeO.head), treeO.head, resultO) ==
                      Position(InOrder(treeP.head), treeP.head, resultP), "bank tree erase range: result", index);
            }
            g_cases++;
            Check(Shape(treeO.head, treeO.size, RefValueWords) == Shape(treeP.head, treeP.size, RefValueWords),
                  "bank tree: tree", index);
        }
        {
            Originals scope;
            Guarded([&] { Orig_RefDestroy(&treeO, 0); });
        }
        Guarded([&] { treeP.DestroyRange(); });
        Check(treeO.head == NULL && treeO.size == 0 && treeP.head == NULL && treeP.size == 0,
              "bank tree: destructor", round);
    }
}

// =============================================================================================================
// The 0x18-node helpers, on name trees
// =============================================================================================================

const char *const kNames[] = {"alpha", "Bravo", "charlie", "DELTA", "echo", "foxtrot", "Golf", "hotel", "india",
                              "juliet", "kilo", "Lima", "mike", "november", "oscar", "papa", "quebec", "romeo",
                              "sierra", "tango", "uniform", "victor", "whiskey", "xray", "yankee", "zulu"};
const int kNameCount = int(sizeof(kNames) / sizeof(kNames[0]));

void NameValueWords(TreeNode *node, std::vector<uint32_t> &words) {
    words.push_back(HashText(node->value.name));
    words.push_back(uint32_t(uintptr_t(node->value.group)));
}

void BuildNameTree(CarpGroupMap *tree, uint32_t seed) {
    tree->Init();
    uint32_t saved = g_random;
    g_random = seed;
    int count = int(1 + Random(kNameCount));
    for (int k = 0; k < count; k++) {
        TreePair pair;
        pair.name = kNames[Random(kNameCount)];
        pair.group = reinterpret_cast<UGroup *>(uintptr_t(k + 1));
        TreeInsertResult ignored;
        tree->InsertUnique(&ignored, &pair);
    }
    g_random = saved;
}

void FreeNameTree(CarpGroupMap *tree) {
    tree->Destroy();
}

void SharedTreeCases() {
    char lower[16];
    for (int round = 0; round < 200; round++) {
        uint32_t seed = Random(0x7fffffff);
        CarpGroupMap treeO, treeP;
        BuildNameTree(&treeO, seed);
        BuildNameTree(&treeP, seed);
        SharedTree *sharedO = static_cast<SharedTree *>(static_cast<Tree *>(&treeO));
        SharedTree *sharedP = static_cast<SharedTree *>(static_cast<Tree *>(&treeP));
        for (int step = 0; step < 30; step++) {
            int index = round * 100 + step;
            std::vector<TreeNode *> orderO = InOrder(treeO.head), orderP = InOrder(treeP.head);
            uint32_t size = uint32_t(orderO.size());
            int operation = int(Random(4));
            g_cases++;
            if (operation == 0) {
                // find: a name, the same in lower case, or a miss
                const char *key = kNames[Random(kNameCount)];
                if (Chance(3)) {
                    size_t n = strlen(key);
                    for (size_t c = 0; c <= n; c++)
                        lower[c] = char(tolower(uint8_t(key[c])));
                    key = lower;
                } else if (Chance(5)) {
                    key = "zz-missing";
                }
                TreeNode *resultO = NULL, *resultP = NULL;
                {
                    Originals scope;
                    Guarded([&] { Orig_TreeFind(sharedO, 0, &resultO, &key); });
                }
                Guarded([&] { sharedP->FindName(&resultP, &key); });
                Check(Position(orderO, treeO.head, resultO) == Position(orderP, treeP.head, resultP), "find", index);
            } else if (operation == 1) {
                uint32_t at = Random(size + 1);
                SharedTreeIterator itO = {at < size ? orderO[at] : treeO.head};
                SharedTreeIterator itP = {at < size ? orderP[at] : treeP.head};
                {
                    Originals scope;
                    Guarded([&] { Orig_TreeDec(&itO, 0); });
                }
                Guarded([&] { itP.Dec(); });
                Check(Position(orderO, treeO.head, itO.node) == Position(orderP, treeP.head, itP.node), "--", index);
            } else if (operation == 2) {
                uint32_t at = Random(size);
                TreeNode *resultO = NULL, *resultP = NULL;
                {
                    Originals scope;
                    Guarded([&] { resultO = Orig_TreeMax(orderO[at]); });
                }
                Guarded([&] { resultP = SharedTreeMax(orderP[at]); });
                Check(Position(orderO, treeO.head, resultO) == Position(orderP, treeP.head, resultP), "_Max", index);
            } else {
                uint32_t at = Random(size);
                if (orderO[at]->left->isNil)
                    continue;
                {
                    Originals scope;
                    Guarded([&] { Orig_TreeRrotate(sharedO, 0, orderO[at]); });
                }
                Guarded([&] { sharedP->Rrotate(orderP[at]); });
                Check(Shape(treeO.head, treeO.size, NameValueWords) == Shape(treeP.head, treeP.size, NameValueWords),
                      "Rrotate: tree", index);
            }
        }
        // both _Erase copies, original and port, each freeing a whole tree
        bool second = (round & 1) != 0;
        {
            Originals scope;
            Guarded([&] {
                if (second)
                    Orig_TreeEraseName(sharedO, 0, treeO.head->parent);
                else
                    Orig_TreeEraseId(sharedO, 0, treeO.head->parent);
            });
        }
        Guarded([&] {
            if (second)
                sharedP->EraseNameSubtree(treeP.head->parent);
            else
                sharedP->EraseIdSubtree(treeP.head->parent);
        });
        CarpGroupMap *trees[2] = {&treeO, &treeP};
        for (CarpGroupMap *tree : trees) {
            tree->head->parent = tree->head;
            tree->head->left = tree->head;
            tree->head->right = tree->head;
            tree->size = 0;
            FreeNameTree(tree);
        }
    }
}

// =============================================================================================================
// Live banks
// =============================================================================================================

void BankLiveCases() {
    BankRefCounter *refs = BankRefCounter::Get();
    std::vector<RefCounterNode *> banks = InOrder(refs->head);
    int index = 0;

    // begin/end
    {
        RefCounterNode *beginO = NULL, *endO = NULL, *beginP = NULL, *endP = NULL;
        {
            Originals scope;
            Orig_BankBegin(&beginO);
            Orig_BankEnd(&endO);
        }
        ABank::Begin(&beginP);
        ABank::End(&endP);
        g_cases++;
        Check(beginO == beginP && endO == endP, "begin/end", 0);
    }

    for (RefCounterNode *node : banks) {
        ABank *bank = static_cast<ABank *>(node->value.entry.object);
        // GetPatchName over the bank's handle
        for (int patch = -1; patch < 130; patch++, index++) {
            const char *nameO = NULL, *nameP = NULL;
            {
                Originals scope;
                Guarded([&] { nameO = Orig_GetPatchName(bank->handle, patch); });
            }
            Guarded([&] { nameP = ABank::GetPatchName(bank->handle, patch); });
            g_cases++;
            bool same = nameO != NULL && nameP != NULL && strcmp(nameO, nameP) == 0 && (*nameO == '\0' || nameO == nameP);
            Check(same, "GetPatchName", index);
        }
        // IsPatchPresent
        for (int patch = -2; patch < 131; patch++, index++) {
            bool presentO = false, presentP = true;
            {
                Originals scope;
                Guarded([&] { presentO = Orig_IsPatchPresent(bank, 0, patch); });
            }
            Guarded([&] { presentP = bank->IsPatchPresent(patch); });
            g_cases++;
            Check(presentO == presentP, "IsPatchPresent", index);
        }
        // Get, by the name, the name in upper case, and a miss
        char upper[0x80];
        strcpy(upper, node->value.name);
        for (char *c = upper; *c != '\0'; c++)
            *c = char(toupper(uint8_t(*c)));
        const char *names[3] = {node->value.name, upper, "no such bank"};
        for (const char *name : names) {
            ABank *bankO = NULL, *bankP = NULL;
            {
                Originals scope;
                Guarded([&] { bankO = Orig_BankGet(name); });
            }
            Guarded([&] { bankP = ABank::Get(name); });
            g_cases++;
            Check(bankO == bankP, "Get", index++);
        }
    }
    // handles no bank has, and the empty bank's patches
    int handles[3] = {-1, 99, 0x7fffffff};
    for (int handle : handles) {
        const char *nameO = NULL, *nameP = NULL;
        {
            Originals scope;
            Guarded([&] { nameO = Orig_GetPatchName(handle, 3); });
        }
        Guarded([&] { nameP = ABank::GetPatchName(handle, 3); });
        g_cases++;
        Check(nameO != NULL && nameP != NULL && strcmp(nameO, nameP) == 0, "GetPatchName (no bank)", index++);
    }
    for (int patch = -1; patch < 3; patch++) {
        bool presentO = true, presentP = false;
        {
            Originals scope;
            Guarded([&] { presentO = Orig_IsPatchPresent(Shadow_EmptyBank, 0, patch); });
        }
        Guarded([&] { presentP = Shadow_EmptyBank->IsPatchPresent(patch); });
        g_cases++;
        Check(presentO == presentP, "IsPatchPresent (empty bank)", index++);
    }

    // the constructors: the empty bank, and a named one with no sound system and no index; the destructors
    for (int named = 0; named < 2; named++) {
        ABank bankO, bankP;
        memset(&bankO, 0xcd, sizeof(bankO));
        memset(&bankP, 0xcd, sizeof(bankP));
        void *system = Shadow_fgSystem;
        Shadow_fgSystem = NULL;
        {
            Originals scope;
            Guarded([&] {
                if (named)
                    Orig_BankConstructNamed(&bankO, 0, "shadowbank", false);
                else
                    Orig_BankConstruct(&bankO, 0);
            });
        }
        Guarded([&] {
            if (named)
                bankP.Construct("shadowbank", false);
            else
                bankP.Construct();
        });
        Shadow_fgSystem = system;
        g_cases++;
        Check(memcmp(&bankO, &bankP, offsetof(ABank, index)) == 0 && bankO.index.names == bankP.index.names &&
              (bankO.index.maps != NULL) == (bankP.index.maps != NULL), "ABank constructor", named);
        {
            Originals scope;
            Guarded([&] { Orig_BankDestruct(&bankO, 0); });
        }
        Guarded([&] { bankP.Destruct(); });
    }
}

// BuildMap over a private list of views on the loaded banks' patches (the fakes need not be installed).
struct MapEntry {
    std::string name;
    int32_t count, volume;
    uint8_t fx;

    bool operator<(const MapEntry &other) const {
        if (name != other.name)
            return name < other.name;
        return count != other.count ? count < other.count : volume < other.volume;
    }
    bool operator==(const MapEntry &other) const {
        return name == other.name && count == other.count && volume == other.volume && fx == other.fx;
    }
};

std::vector<MapEntry> MapEntries(AVoiceMap *map) {
    std::vector<MapEntry> entries;
    for (AVoiceMapNode *node : InOrder(map->head)) {
        MapEntry entry = {node->value.name, node->value.info.count, node->value.info.volume, node->value.info.fx};
        entries.push_back(entry);
    }
    std::sort(entries.begin(), entries.end());
    return entries;
}

void BuildMapCases() {
    std::vector<RefCounterNode *> banks = InOrder(BankRefCounter::Get()->head);
    std::vector<int> handles;
    for (RefCounterNode *node : banks)
        handles.push_back(static_cast<ABank *>(node->value.entry.object)->handle);
    handles.push_back(-1);

    const int kMost = 40;
    static AVoice::View views[kMost];
    for (int round = 0; round < 30; round++) {
        ListClear();
        int count = int(Random(kMost + 1));
        for (int k = 0; k < count; k++) {
            RandomView(&views[k], false);
            views[k].bank = handles[Random(uint32_t(handles.size()))];
            views[k].patch = Chance(6) ? int(Random(300)) - 20 : int(Random(64));
            ListAppend(&views[k]);
        }
        std::vector<MapEntry> entriesO, entriesP;
        {
            Originals scope;
            Guarded([&] { entriesO = MapEntries(Orig_BuildMap()); });
        }
        Guarded([&] { entriesP = MapEntries(AVoice::BuildMap()); });
        g_cases++;
        Check(entriesO == entriesP, "BuildMap", round);
    }
    ListClear();
    // leave the debug map as the active list makes it
    ListRestore();
    AVoice::BuildMap();
}

}  // namespace

void AudioVoiceShadow_Run(void) {
    const char *setting = getenv("NIGHTFIRE_AUDIOVOICESHADOW");
    if (setting == NULL || atoi(setting) == 0)
        return;

    for (size_t k = 0; k < sizeof(g_keys); k++)
        g_keys[k] = char('a' + k % 26);

    VoiceMapCases();
    RefTreeCases();
    SharedTreeCases();
    BankLiveCases();

    // the stateful cases: the real sound lock held (the server waits), the SND calls faked, a private active list
    SNDSYS_entercritical();
    ListInstall();
    InstallFakes();
    ViewCases();
    VoiceCases();
    PlayVoicesCases();
    HooksRemove();
    BuildMapCases();            // puts the real list back
    SNDSYS_leavecritical();

    printf("[audiovoiceshadow] AVoice, ABank: %d cases, %d checks, %d differ (%d faults)\n", g_cases, g_checks,
           g_differ, g_faults);
    fflush(stdout);
}
