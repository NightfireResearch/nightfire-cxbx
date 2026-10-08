#include "Voice.h"

#include "Bank.h"                       // ABank::GetPatchName
#include "Mix.h"                        // AMix, AFX
#include "Stream.h"                     // AVoiceMapMax

#include <bit>

#include "../../helpers.h"
#include "../data/Tree.h"               // TreeThrow
#include "../engine/UMemory.hpp"
#include "../platform/RealSystem.h"     // TIMER_gettick
#include "../platform/X87.h"
#include "../sound/snd/Banks.h"
#include "../sound/snd/System.h"
#include "../sound/snd/Voices.h"

#pragma fp_contract(off)

// ---------------------------------------------------------------------------------------------------------------
// AVoice, its views, the active views' list and the debug map (0x00123820..0x00124790), ported from the listings.
// See Voice.h.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's code called by address
// std::list<T *>'s _Buynode, shared by every list of pointers
#define ViewList_BuyNode ((PointerListNode *(__fastcall *)(PointerList *, int, PointerListNode *next, PointerListNode *prev, void *const *value))0x000130e0)
// The tree helpers every map with 0x20-byte nodes shares (identical code folded by the linker), on this file's node
// type: 0x00053580, 0x000527e0 and 0x00052a20 are the attribute system's ported copies, 0x000bf460 the collision
// manager's, of their own node types.
#define VoiceMap_BuyHead ((AVoiceMapNode *(*)())0x00053580)
#define VoiceMap_Rrotate ((void (__fastcall *)(AVoiceMap *, int, AVoiceMapNode *node))0x000527e0)
#define VoiceMap_Increment ((void (__fastcall *)(AVoiceMapNode **it, int))0x00052a20)
#define VoiceMap_Min ((AVoiceMapNode *(*)(AVoiceMapNode *node))0x000bf460)

// ---- globals
#define LastPlayTick I32_AT(0x00243a98)             // TIMER_gettick at the last PlayVoices
#define VoiceInfoMap (*(AVoiceMap *)0x00243a9c)
#define ActiveViews (*(AVoiceViewList *)0x00243aa8)
#define FxMode I32_AT(0x001d8100)                   // AFX's current mode; -1: off
#define TimerTicksPerSecond I32_AT(0x00242424)

namespace {

constexpr uint16_t kUnitPitch = 0x1000;             // 1.0 as SNDpitchmult's 4.12 multiplier
constexpr int8_t kLoudestFx = 0x7f;
constexpr int kLongestFrame = 8;                     // timer ticks; a longer gap counts as one tick

constexpr float kLevelScale = 127.0f;
constexpr float kPitchScale = 4096.0f;
constexpr float kAzimuthScale = 65536.0f;
constexpr float kHighestPitch = 4.0f;
constexpr float kLongestDelay = 32767.0f;            // timer ticks
constexpr float kQuietest = 1.0f / 127.0f;           // AVoice::Play: a view no louder is silent
static_assert(std::bit_cast<uint32_t>(kQuietest) == 0x3c010204, "the original's 1/127");

void RemoveFromActive(AVoice::View *view) {
    void *value = view;
    ActiveViews.Remove(value);
}

// The map's ++iterator, inlined in its erase(first, last); end() stays where it is.
AVoiceMapNode *Next(AVoiceMapNode *node) {
    if (node->isNil)
        return node;
    if (!node->right->isNil) {
        node = node->right;
        while (!node->left->isNil)
            node = node->left;
        return node;
    }
    AVoiceMapNode *parent = node->parent;
    while (!parent->isNil && node == parent->right) {
        node = parent;
        parent = parent->parent;
    }
    return parent;
}

}  // namespace

// =============================================================================================================
// AVoice::View
// =============================================================================================================

// FUNC_AT(0x00123820)
AVoice::View* AVoice::View::Construct() {
    patch = -1;
    bank = -1;
    loop = 1;
    handle = -1;
    pitch = kUnitPitch;
    azimuth = 0;
    delay = -1;
    volume = 0;
    listedVolume = 0;
    finished = 0;
    return this;
}

// FUNC_AT(0x00123c50)
void AVoice::View::Destruct() {
    if (listedVolume != 0)
        RemoveFromActive(this);
    if (handle != -1) {
        SNDSYS_entercritical();
        SNDstop(handle);
        SNDSYS_leavecritical();
    }
}

// FUNC_AT(0x00123be0)
void AVoice::View::Stop() {
    int listed = listedVolume;
    volume = 0;
    if (listed != 0)
        RemoveFromActive(this);
    if (handle != -1) {
        SNDSYS_entercritical();
        SNDstop(handle);
        SNDSYS_leavecritical();
    }
    if (!loop)
        finished = 1;
    listedVolume = volume;
}

// FUNC_AT(0x00124490)
void AVoice::View::Push() {
    AVoiceViewList &list = ActiveViews;
    PointerListNode *end = list.head;
    void *value = this;
    PointerListNode *node = ViewList_BuyNode(&list, 0, end, end->prev, &value);
    list.IncreaseSize(1);
    end->prev = node;
    node->prev->next = node;
}

// FUNC_AT(0x001244d0)
void AVoice::View::Play(float volume, float pitch, float azimuth, float delay, float fxLevel) {
    if (!(volume > 0.0f)) {
        Stop();
        return;
    }
    if (patch < 0 || bank < 0)
        return;

    this->volume = int8_t(RoundToInt(volume * kLevelScale));
    if (listedVolume == 0) {
        listedVolume = this->volume;
        Push();
    }

    if (pitch < 0.0f)
        pitch = -pitch;
    if (pitch > kHighestPitch)
        pitch = kHighestPitch;
    this->pitch = uint16_t(RoundToInt(pitch * kPitchScale));
    this->azimuth = uint16_t(RoundToInt(azimuth * kAzimuthScale));

    if (this->delay < 0) {
        if (delay < 0.0f)
            delay = 0.0f;
        double ticksPerSecond = TimerTicksPerSecond;
        double longest = 1.0 / ticksPerSecond * kLongestDelay;
        if (delay > longest)
            delay = float(longest);
        this->delay = int16_t(RoundToInt(float(ticksPerSecond * delay)));
    }

    if (fxLevel < 0.0f)
        fxLevel = 0.0f;
    else if (fxLevel > 1.0f)
        fxLevel = 1.0f;
    this->fxLevel = int8_t(RoundToInt(fxLevel * kLevelScale));
}

// =============================================================================================================
// AVoice
// =============================================================================================================

// FUNC_AT(0x00123ca0)
AVoice* AVoice::Construct(AMix *mix, int bank, int patch) {
    this->mix = mix;
    for (View &view : views)
        view.Construct();
    Set(bank, patch);
    return this;
}

// FUNC_AT(0x00123870)
void AVoice::Set(int bank, int patch) {
    for (View &view : views) {
        if (patch == -1) {
            view.finished = 1;
        } else if (view.bank != bank || view.patch != patch) {
            view.bank = bank;
            view.patch = patch;
            if (view.handle != -1) {
                SNDSYS_entercritical();
                SNDstop(view.handle);
                SNDSYS_leavecritical();
                view.handle = -1;
            }
        }
    }
}

// FUNC_AT(0x00124690)
void AVoice::Play(int view, float volume, float pitch, float azimuth, float delay, float fxLevel) {
    if (volume > kQuietest) {
        if (volume > 1.0f)
            volume = 1.0f;
        mix->unknown24++;
        mix->unknown28 += volume;
        volume = mix->unknown2c * volume;
    } else {
        volume = 0.0f;
    }
    views[view].Play(volume, pitch, azimuth, delay, fxLevel);
}

// FUNC_AT(0x00123a30)
void AVoice::PlayVoices() {
    int now = TIMER_gettick();
    int elapsed = now - LastPlayTick;
    LastPlayTick = now;
    if (elapsed > kLongestFrame)
        elapsed = 1;
    if (ActiveViews.size == 0)
        return;

    int fxMode = FxMode;
    bool allVoices = AFX::IsAllVoices();
    SNDSYS_entercritical();
    PointerListNode *head = ActiveViews.head;
    for (PointerListNode *node = head != NULL ? head->next : NULL; node != ActiveViews.head;) {
        View *view = static_cast<View *>(node->value);
        node = node->next;
        uint16_t pitch = view->pitch;
        uint16_t azimuth = view->azimuth;
        int8_t volume = view->volume;
        int8_t fxLevel;
        if (fxMode == -1)
            fxLevel = 0;
        else if (allVoices)
            fxLevel = kLoudestFx;
        else
            fxLevel = view->fxLevel;

        if (view->handle == -1) {
            if (volume > 0) {
                view->delay -= elapsed;
                if (view->delay <= 0) {
                    SND::PlayOpts opts;
                    SNDplaysetdef(&opts);
                    opts.pitchMult = pitch;
                    opts.azimuth = azimuth;
                    opts.elevation = 0;
                    opts.fxLevel = fxLevel;
                    opts.vol = pitch != 0 ? volume : 0;
                    view->handle = SNDBANK_play(view->bank, view->patch, &opts);
                }
            }
        } else if (volume > 0) {
            if (SNDover(view->handle) == 0) {
                SNDvol(view->handle, pitch != 0 ? volume : 0);
                SNDpitchmult(view->handle, pitch);
                SND3dpos(view->handle, azimuth, 0);
                SNDfxlevel(view->handle, 0, fxLevel);
            } else if (view->loop) {
                SNDstop(view->handle);
                view->handle = -1;
            } else {
                view->finished = 1;
            }
        }
    }
    SNDSYS_leavecritical();
}

// FUNC_AT(0x001243c0)
AVoiceMap* AVoice::BuildMap() {
    AVoiceMap &map = VoiceInfoMap;
    map.EraseSubtree(map.head->parent);     // clear()
    map.head->parent = map.head;
    map.size = 0;
    map.head->left = map.head;
    map.head->right = map.head;

    PointerListNode *head = ActiveViews.head;
    for (PointerListNode *node = head != NULL ? head->next : NULL; node != ActiveViews.head; node = node->next) {
        View *view = static_cast<View *>(node->value);
        AVoiceMapValue value = {ABank::GetPatchName(view->bank, view->patch), {0, 0, 0, {0, 0, 0}}};
        AVoiceMapInsert slot;
        AVoiceMapInfo &info = map.InsertUnique(&slot, &value)->node->value.info;
        info.volume += view->volume;
        if (view->fxLevel != 0)
            info.fx = 1;
        info.count++;
    }
    return &map;
}

// =============================================================================================================
// The active views' list
// =============================================================================================================

// FUNC_AT(0x00123ec0)
void AVoiceViewList::IncreaseSize(uint32_t count) {
    PointerList::IncreaseSize(count);
}

// =============================================================================================================
// The debug map
// =============================================================================================================

// FUNC_AT(0x00124710)
AVoiceMap* AVoiceMap::Construct() {
    allocator = 0;
    head = VoiceMap_BuyHead();
    head->isNil = 1;
    head->parent = head;
    head->left = head;
    head->right = head;
    size = 0;
    return this;
}

// FUNC_AT(0x00124750)
void AVoiceMap::Destruct() {
    AVoiceMapNode *ignored;
    EraseRange(&ignored, head->left, head);
    if (head != NULL)
        UMemory::FastFree(head, sizeof(AVoiceMapNode));
    head = NULL;
    size = 0;
}

// FUNC_AT(0x00123930)
void AVoiceMap::Iterator::Dec() {
    if (node->isNil) {
        node = node->right;
    } else if (!node->left->isNil) {
        AVoiceMapNode *max = node->left;
        while (!max->right->isNil)
            max = max->right;
        node = max;
    } else {
        AVoiceMapNode *parent;
        while (!(parent = node->parent)->isNil && node == parent->left)
            node = parent;
        if (!parent->isNil)
            node = parent;
    }
}

// FUNC_AT(0x001238d0)
void AVoiceMap::Lrotate(AVoiceMapNode *node) {
    AVoiceMapNode *pivot = node->right;
    node->right = pivot->left;
    if (!pivot->left->isNil)
        pivot->left->parent = node;
    pivot->parent = node->parent;
    if (node == head->parent)
        head->parent = pivot;
    else if (node == node->parent->left)
        node->parent->left = pivot;
    else
        node->parent->right = pivot;
    pivot->left = node;
    node->parent = pivot;
}

// FUNC_AT(0x00123990)
void AVoiceMap::EraseSubtree(AVoiceMapNode *node) {
    while (!node->isNil) {
        EraseSubtree(node->right);
        AVoiceMapNode *left = node->left;
        if (node != NULL)
            UMemory::FastFree(node, sizeof(AVoiceMapNode));
        node = left;
    }
}

// FUNC_AT(0x001239d0)
AVoiceMapNode* AVoiceMap::Buynode(AVoiceMapNode *left, AVoiceMapNode *parent, AVoiceMapNode *right,
                                  const AVoiceMapValue *value, uint8_t color) {
    AVoiceMapNode *node = static_cast<AVoiceMapNode *>(UMemory::FastAlloc(sizeof(AVoiceMapNode), "STL"));
    if (node != NULL) {
        node->left = left;
        node->parent = parent;
        node->right = right;
        node->value = *value;
        node->color = color;
        node->isNil = 0;
    }
    return node;
}

// FUNC_AT(0x00123ce0)
AVoiceMapNode** AVoiceMap::InsertAt(AVoiceMapNode **result, bool addLeft, AVoiceMapNode *where,
                                    const AVoiceMapValue *value) {
    if (size >= 0x0ffffffe) {
        AUDIO_VOICE_UNTESTED("the voice map's insert past max_size");
        TreeThrow("map/set<T> too long", kLengthErrorVtable, kLengthErrorThrowInfo);
    }
    AVoiceMapNode *node = Buynode(head, where, head, value, kTreeRed);
    size++;
    if (where == head) {
        head->parent = node;
        head->left = node;
        head->right = node;
    } else if (addLeft) {
        where->left = node;
        if (where == head->left)
            head->left = node;
    } else {
        where->right = node;
        if (where == head->right)
            head->right = node;
    }

    for (AVoiceMapNode *x = node; x->parent->color == kTreeRed;) {
        AVoiceMapNode *parent = x->parent;
        AVoiceMapNode *grandparent = parent->parent;
        if (parent == grandparent->left) {
            AVoiceMapNode *uncle = grandparent->right;
            if (uncle->color == kTreeRed) {
                parent->color = kTreeBlack;
                uncle->color = kTreeBlack;
                x->parent->parent->color = kTreeRed;
                x = x->parent->parent;
            } else {
                if (x == parent->right) {
                    x = parent;
                    Lrotate(x);
                }
                x->parent->color = kTreeBlack;
                x->parent->parent->color = kTreeRed;
                VoiceMap_Rrotate(this, 0, x->parent->parent);
            }
        } else {
            AVoiceMapNode *uncle = grandparent->left;
            if (uncle->color == kTreeRed) {
                parent->color = kTreeBlack;
                uncle->color = kTreeBlack;
                x->parent->parent->color = kTreeRed;
                x = x->parent->parent;
            } else {
                if (x == parent->left) {
                    x = parent;
                    VoiceMap_Rrotate(this, 0, x);
                }
                x->parent->color = kTreeBlack;
                x->parent->parent->color = kTreeRed;
                Lrotate(x->parent->parent);
            }
        }
    }
    head->parent->color = kTreeBlack;
    *result = node;
    return result;
}

// FUNC_AT(0x00123f70)
AVoiceMapNode** AVoiceMap::EraseAt(AVoiceMapNode **result, AVoiceMapNode *where) {
    if (where->isNil) {
        AUDIO_VOICE_UNTESTED("the voice map's erase of end()");
        TreeThrow("invalid map/set<T> iterator", kOutOfRangeVtable, kOutOfRangeThrowInfo);
    }
    AVoiceMapNode *erased = where;
    AVoiceMapNode *next = where;
    VoiceMap_Increment(&next, 0);

    // unlink it: the node that takes its place (pnode), the subtree that moves up (fixnode) and its new parent
    AVoiceMapNode *pnode = erased;
    AVoiceMapNode *fixnode;
    AVoiceMapNode *fixparent;
    if (erased->left->isNil) {
        fixnode = erased->right;
    } else if (erased->right->isNil) {
        fixnode = erased->left;
    } else {
        pnode = next;
        fixnode = pnode->right;
    }
    if (pnode == erased) {
        fixparent = erased->parent;
        if (!fixnode->isNil)
            fixnode->parent = fixparent;
        if (head->parent == erased)
            head->parent = fixnode;
        else if (fixparent->left == erased)
            fixparent->left = fixnode;
        else
            fixparent->right = fixnode;
        if (head->left == erased)
            head->left = fixnode->isNil ? fixparent : VoiceMap_Min(fixnode);
        if (head->right == erased)
            head->right = fixnode->isNil ? fixparent : AVoiceMapMax(fixnode);
    } else {
        erased->left->parent = pnode;
        pnode->left = erased->left;
        if (pnode == erased->right) {
            fixparent = pnode;
        } else {
            fixparent = pnode->parent;
            if (!fixnode->isNil)
                fixnode->parent = fixparent;
            fixparent->left = fixnode;
            pnode->right = erased->right;
            erased->right->parent = pnode;
        }
        if (head->parent == erased)
            head->parent = pnode;
        else if (erased->parent->left == erased)
            erased->parent->left = pnode;
        else
            erased->parent->right = pnode;
        pnode->parent = erased->parent;
        uint8_t color = pnode->color;
        pnode->color = erased->color;
        erased->color = color;
    }

    // rebalance
    if (erased->color == kTreeBlack) {
        for (; fixnode != head->parent && fixnode->color == kTreeBlack;
             fixnode = fixparent, fixparent = fixparent->parent) {
            if (fixnode == fixparent->left) {
                pnode = fixparent->right;
                if (pnode->color == kTreeRed) {
                    pnode->color = kTreeBlack;
                    fixparent->color = kTreeRed;
                    Lrotate(fixparent);
                    pnode = fixparent->right;
                }
                if (pnode->isNil)
                    continue;
                if (pnode->left->color == kTreeBlack && pnode->right->color == kTreeBlack) {
                    pnode->color = kTreeRed;
                    continue;
                }
                if (pnode->right->color == kTreeBlack) {
                    pnode->left->color = kTreeBlack;
                    pnode->color = kTreeRed;
                    VoiceMap_Rrotate(this, 0, pnode);
                    pnode = fixparent->right;
                }
                pnode->color = fixparent->color;
                fixparent->color = kTreeBlack;
                pnode->right->color = kTreeBlack;
                Lrotate(fixparent);
                break;
            } else {
                pnode = fixparent->left;
                if (pnode->color == kTreeRed) {
                    pnode->color = kTreeBlack;
                    fixparent->color = kTreeRed;
                    VoiceMap_Rrotate(this, 0, fixparent);
                    pnode = fixparent->left;
                }
                if (pnode->isNil)
                    continue;
                if (pnode->right->color == kTreeBlack && pnode->left->color == kTreeBlack) {
                    pnode->color = kTreeRed;
                    continue;
                }
                if (pnode->left->color == kTreeBlack) {
                    pnode->right->color = kTreeBlack;
                    pnode->color = kTreeRed;
                    Lrotate(pnode);
                    pnode = fixparent->left;
                }
                pnode->color = fixparent->color;
                fixparent->color = kTreeBlack;
                pnode->left->color = kTreeBlack;
                VoiceMap_Rrotate(this, 0, fixparent);
                break;
            }
        }
        fixnode->color = kTreeBlack;
    }

    UMemory::FastFree(erased, sizeof(AVoiceMapNode));
    if (size > 0)
        size--;
    *result = next;
    return result;
}

// FUNC_AT(0x00124240)
AVoiceMapInsert* AVoiceMap::InsertUnique(AVoiceMapInsert *result, const AVoiceMapValue *value) {
    AVoiceMapNode *where = head;
    bool addLeft = true;
    for (AVoiceMapNode *x = head->parent; !x->isNil; x = addLeft ? x->left : x->right) {
        where = x;
        addLeft = uintptr_t(value->name) < uintptr_t(x->value.name);
    }
    Iterator at = {where};
    if (addLeft) {
        if (where == head->left) {
            InsertAt(&at.node, true, where, value);
            result->node = at.node;
            result->inserted = true;
            return result;
        }
        at.Dec();
    }
    if (uintptr_t(at.node->value.name) < uintptr_t(value->name)) {
        InsertAt(&at.node, addLeft, where, value);
        result->node = at.node;
        result->inserted = true;
        return result;
    }
    result->node = at.node;
    result->inserted = false;
    return result;
}

// FUNC_AT(0x00124300)
AVoiceMapNode** AVoiceMap::EraseRange(AVoiceMapNode **result, AVoiceMapNode *first, AVoiceMapNode *last) {
    if (first == head->left && last == head) {
        EraseSubtree(head->parent);
        head->parent = head;
        size = 0;
        head->left = head;
        head->right = head;
        *result = head->left;
        return result;
    }
    while (first != last) {
        AVoiceMapNode *where = first;
        first = Next(first);
        AVoiceMapNode *ignored;
        EraseAt(&ignored, where);
    }
    *result = first;
    return result;
}
