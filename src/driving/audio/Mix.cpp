#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // (the build defines it; for a file compiled alone)
#endif

#include "Mix.h"
#include "SoundManager.h"               // ASystem_fgSystem, FramesSinceAudioUpdate

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <bit>

#include "../../helpers.h"
#include "../engine/UFileLoader.h"
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"       // VU0_MATRIX4_vect4mult
#include "../platform/RealMemory.h"
#include "../platform/X87.h"
#include "../sound/snd/Voices.h"        // SNDfxmasterlevel, SNDfxinitbus

#pragma fp_contract(off)

// ---------------------------------------------------------------------------------------------------------------
// AMix (0x0011c980..0x0011dad0), URefCounter<AMix>'s tree, AFX and AListener (0x00124790..0x00124c40), ported from
// the listings. See Mix.h.
// ---------------------------------------------------------------------------------------------------------------

// ---- originals called by address


// std::list<T *>'s node and head makers.
#define PointerList_BuyHead ((PointerListNode *(__fastcall *)(PointerList *, int))0x000b8490)

// The C runtime's (the names' order and the volumes' parsing are its).
#define CRT_sscanf ((int (*)(const char *text, const char *format, ...))0x00133234)

// ---- globals

#define Mixes (*(MixList *)0x00243954)

#define Fx (*(FxState *)0x001d80f0)
#define FxModes ((const FxMode *)0x001d8190)
#define FxBusModes ((const int32_t *)0x001d8118)   // the sound library's reverb for each FxMode::reverb
#define FxBus I32_AT(0x00243ab4)
#define FxMixes ((AMix **)0x00243ab8)
#define FxReady BOOL8_AT(0x00243aec)
#define FxLevel FLOAT_AT(0x00243af0)
#define FxPaused BOOL8_AT(0x00243af4)

namespace {

constexpr float kFxFadeOutRate = 0.04f;    // the level lost per frame before the bus changes mode
constexpr float kFxFadeInRate = 0.3f;      // the level gained per frame towards the mode's mix
static_assert(std::bit_cast<uint32_t>(kFxFadeOutRate) == 0x3d23d70a, "0.04f as the original's .rdata has it");
static_assert(std::bit_cast<uint32_t>(kFxFadeInRate) == 0x3e99999a, "0.3f as the original's .rdata has it");

constexpr float kListenerUnknown70 = -100000.0f;

// new AMix
AMix *NewMix() {
    AMix *mix = static_cast<AMix *>(UMemory::FastAlloc(sizeof(AMix), "AMix"));
    if (mix != NULL) {
        mix->master = NULL;
        mix->volume = 0.0f;
        mix->previous = 0.0f;
        mix->unknown0c = -1.0f;
        mix->presetVolume = -1.0f;
        mix->target = 0.0f;
        mix->step = 0.0f;
        mix->transitioning = false;
        mix->unknown24 = 0;
        mix->unknown28 = 0.0f;
        mix->unknown2c = 1.0f;
    }
    return mix;
}

// delete mix: the destructor gives up the master.
void DeleteMix(AMix *mix) {
    if (mix != NULL) {
        mix->ClearMaster();
        UMemory::FastFree(mix, sizeof(AMix));
    }
}

}  // namespace

// =============================================================================================================
// AMix
// =============================================================================================================

// FUNC_AT(0x0011c980)
void AMix::SetTransition(float to, int steps) {
    if (to < 0.0f)
        to = 0.0f;
    else if (to > 1.0f)
        to = 1.0f;
    if (steps < 1)
        steps = 1;
    target = to;
    previous = volume;
    step = float((double(to) - volume) / steps);
    transitioning = true;
}

// FUNC_AT(0x0011c9e0)
void AMix::Revert(int steps) {
    if (steps < 1)
        steps = 1;
    transitioning = true;
    target = previous;
    step = float((double(previous) - volume) / steps);
}

// FUNC_AT(0x0011ca10)
double AMix::GetVolume() {
    if (transitioning) {
        // The sum is compared with the target before it is rounded to a float.
        double next = double(volume) + step;
        volume = float(next);
        if ((step < 0.0f && next <= target) || (step > 0.0f && next >= target)) {
            volume = target;
            transitioning = false;
        }
    }
    float scale = volume;
    if (master != NULL)
        return master->GetVolume() * scale;
    return scale;
}

// FUNC_AT(0x0011ca90)
float AMix::GetPresetVolume() {
    return presetVolume;
}

// FUNC_AT(0x0011cb80)
void AMix::Reset(int steps) {
    if (steps < 1)
        steps = 1;
    for (PointerListNode *node = Mixes.head != NULL ? Mixes.head->next : NULL; node != Mixes.head; node = node->next) {
        AMix *mix = static_cast<AMix *>(node->value);
        mix->SetTransition(mix->presetVolume, steps);
    }
}

// FUNC_AT(0x0011d6a0)
AMix* AMix::Add(const char *name) {
    AMix *mix = MixRefCounter::Get()->GetReference(name);
    if (mix == NULL)
        mix = NewMix();
    MixRefCounter::Get()->AddReference(name, mix);
    return mix;
}

// FUNC_AT(0x0011d720)
AMix* AMix::Get(const char *name) {
    AMix *mix = MixRefCounter::Get()->GetReference(name);
    if (mix == NULL)
        mix = Add(name);
    return mix;
}

// FUNC_AT(0x0011d750)
void AMix::Remove() {
    if (MixRefCounter::Get()->RemoveReference(this))
        DeleteMix(this);
}

// FUNC_AT(0x0011d790)
void AMix::Load(const char *path) {
    char *text = static_cast<char *>(UFileLoader::FileLoadz(path, 0x100));
    if (text == NULL) {
        text = static_cast<char *>(UFileLoader::FileLoadz("data/audio/default.ini", 0x100));
        if (text == NULL)
            return;
    }
    int size = MEM_size(text);
    const char *at = text;
    while (at - text < size) {
        // A line of up to 127 characters, up to the "\r\n" that ends it
        char line[0x80];
        int length = 0;
        while (at - text < size && length < 0x7f && *at != '\r')
            line[length++] = *at++;
        line[length] = '\0';
        while (at - text < size && *at != '\r')
            at++;
        at += 2;

        char name[0x80];
        float volume;
        int number;
        if (CRT_sscanf(line, "%[^=]=%f, %d", name, &volume, &number) != 3)
            continue;
        for (size_t end = strlen(name); end != 0 && name[end - 1] == ' '; end = strlen(name))
            name[end - 1] = '\0';

        AMix *mix = Add(name);
        // Mixes.push_back(mix)
        void *value = mix;
        PointerListNode *node = Mixes.BuyNode(Mixes.head, Mixes.head->prev, &value);
        Mixes.IncreaseSize(1);
        Mixes.head->prev = node;
        node->prev->next = node;

        mix->volume = volume;
        mix->previous = volume;
        mix->unknown0c = volume;
        mix->unknown20 = number;
        mix->presetVolume = volume;
        strcpy(mix->name, name);
    }
    MEM_free(text);
}

// FUNC_AT(0x0011d980)
void AMix::Clear() {
    while (MixRefCounter::Get()->Begin() != MixRefCounter::Get()->End()) {
        AMix *mix = static_cast<AMix *>(MixRefCounter::Get()->Begin()->value.entry.object);
        mix->Remove();
    }
    PointerListNode *after;
    Mixes.Erase(&after, Mixes.head != NULL ? Mixes.head->next : NULL, Mixes.head);
}

// FUNC_AT(0x0011da20)
void AMix::ClearMaster() {
    if (master != NULL) {
        master->Remove();
        master = NULL;
    }
}

// FUNC_AT(0x0011da60)
void AMix::SetMaster(const char *name) {
    ClearMaster();
    master = Add(name);
}

// ---- the list

// FUNC_AT(0x0011cc10)
MixList* MixList::Construct() {
    head = PointerList_BuyHead(this, 0);
    size = 0;
    return this;
}

// FUNC_AT(0x0011cc80)
void MixList::IncreaseSize(uint32_t count) {
    PointerList::IncreaseSize(count);
}

// =============================================================================================================
// The URefCounters' trees
// =============================================================================================================

// ---- URefCounter<AMix>'s

// FUNC_AT(0x0011cc30)
void MixRefTree::EraseSubtree(RefCounterNode *node) {
    RefCounterTree::EraseSubtree(node);
}

// FUNC_AT(0x0011cd30)
RefCounterNode** MixRefTree::EraseAt(RefCounterNode **result, RefCounterNode *where) {
    return RefCounterTree::EraseAt(result, where);
}

// FUNC_AT(0x0011d0a0)
RefCounterNode** MixRefTree::InsertAt(RefCounterNode **result, bool addLeft, RefCounterNode *where, const RefCounterValue *value) {
    return RefCounterTree::InsertAt(result, addLeft, where, value);
}

// FUNC_AT(0x0011d290)
RefCounterNode** MixRefTree::EraseRange(RefCounterNode **result, RefCounterNode *first, RefCounterNode *last) {
    return RefCounterTree::EraseRange(result, first, last);
}

// FUNC_AT(0x0011d380)
RefCounterInsertResult* MixRefTree::InsertUnique(RefCounterInsertResult *result, const RefCounterValue *value) {
    return RefCounterTree::InsertUnique(result, value);
}

// FUNC_AT(0x0011d580)
void MixRefTree::Destruct() {
    Destroy();
}

// =============================================================================================================
// AFX
// =============================================================================================================

// FUNC_AT(0x00124790)
void AFX::Init() {
    for (int i = 0; i < kModeCount; i++)
        FxMixes[i] = AMix::Add(FxModes[i].name);
    FxReady = 1;
    FxBus = 0x100;
}

// FUNC_AT(0x001247d0)
void AFX::SetMode(int mode, int slot, int priority) {
    FxRequest &request = Fx.requests[slot];
    if (priority < request.priority)
        return;
    if (priority == request.priority && mode < request.mode)
        return;
    request.priority = priority;
    request.mode = mode;
}

// FUNC_AT(0x00124810)
const char* AFX::GetCurrentModeName() {
    if (Fx.currentMode == -1)
        return "Off";
    return FxModes[Fx.currentMode].name + 2;
}

// FUNC_AT(0x00124830)
bool AFX::IsAllVoices() {
    if (Fx.currentMode == -1)
        return false;
    return FxModes[Fx.currentMode].allVoices;
}

// FUNC_AT(0x00124850)
void AFX::Pause() {
    if (ASystem_fgSystem != NULL)
        SNDfxmasterlevel(0x40, 0);      // a bus of its own, not FxBus
    FxPaused = 1;
}

// FUNC_AT(0x00124870)
void AFX::Resume() {
    FxPaused = 0;
}

// FUNC_AT(0x00124880)
void AFX::Shutdown() {
    for (int i = 0; i < kModeCount; i++)
        FxMixes[i]->Remove();
    FxReady = 0;
}

// FUNC_AT(0x001248b0)
void AFX::Update() {
    if (ASystem_fgSystem == NULL || FxPaused)
        return;
    int mode;
    if (Fx.requests[1].priority > Fx.requests[0].priority) {
        mode = Fx.requests[1].mode;
        Fx.requests[1].priority = -1;
    } else {
        mode = Fx.requests[0].mode;
        Fx.requests[0].priority = -1;
    }

    if (Fx.currentMode != mode) {
        const FxMode &next = FxModes[mode];
        if (Fx.busReverb == next.reverb) {
            Fx.currentMode = mode;
            return;
        }
        if (Fx.currentMode != -1) {
            Fx.currentMode = -1;
            return;
        }
        if (FxLevel > 0.0f) {
            // The level is compared with zero before it is rounded to a float.
            double level = FxLevel - FramesSinceAudioUpdate * double(kFxFadeOutRate);
            FxLevel = float(level);
            if (level <= 0.0)
                FxLevel = 0.0f;
            SNDfxmasterlevel(FxBus, RoundToInt(FxLevel * 127.0f));
            return;
        }
        Fx.currentMode = mode;
        SNDfxinitbus(FxBus, RoundToInt(FxLevel * 127.0f), FxBusModes[next.reverb], next.delay, next.feedback);
        Fx.busReverb = next.reverb;
        Fx.busDelay = next.delay;
        Fx.busFeedback = next.feedback;
        return;
    }

    // Each GetVolume advances the mix's transition: the calls are the original's, one by one.
    AMix *mix = FxMixes[mode];
    if (mix->GetVolume() > FxLevel) {
        FxLevel = float(FramesSinceAudioUpdate * double(kFxFadeInRate) + FxLevel);
        if (mix->GetVolume() <= FxLevel)
            FxLevel = float(mix->GetVolume());
        SNDfxmasterlevel(FxBus, RoundToInt(FxLevel * 127.0f));
    } else if (mix->GetVolume() < FxLevel) {
        double volume = mix->GetVolume();
        FxLevel = float(volume);
        SNDfxmasterlevel(FxBus, RoundToInt(float(volume * 127.0f)));
    }
}

// =============================================================================================================
// AListener
// =============================================================================================================

// FUNC_AT(0x00124ae0)
AListener* AListener::Construct(int unknown) {
    for (int row = 0; row < 4; row++) {
        for (int column = 0; column < 4; column++)
            matrix.mtx[row][column] = row == column ? 1.0f : 0.0f;
    }
    position.x = 0.0f;
    position.y = 0.0f;
    position.z = 0.0f;
    position.w = 1.0f;
    velocity.x = 0.0f;
    velocity.y = 0.0f;
    velocity.z = 0.0f;
    unknown5c = unknown;
    unknown70 = kListenerUnknown70;
    unknown74 = 0;
    return this;
}

// FUNC_AT(0x00124b50)
AListener* AListener::Construct(const MATRIX4 *camera, const Coord3 *cameraVelocity, int unknown) {
    position.x = camera->mtx[3][0];
    position.y = camera->mtx[3][1];
    position.z = camera->mtx[3][2];
    position.w = 1.0f;
    Coord4 back = { -position.x, -position.y, -position.z, 0.0f };
    // The rotation transposed, then the translation that undoes the camera's
    for (int row = 0; row < 3; row++) {
        for (int column = 0; column < 3; column++)
            matrix.mtx[row][column] = camera->mtx[column][row];
        matrix.mtx[row][3] = 0.0f;
    }
    matrix.mtx[3][0] = 0.0f;
    matrix.mtx[3][1] = 0.0f;
    matrix.mtx[3][2] = 0.0f;
    matrix.mtx[3][3] = 1.0f;
    VU0_MATRIX4_vect4mult(&back, &matrix, matrix.mtx[3]);
    matrix.mtx[3][3] = 1.0f;
    velocity = *cameraVelocity;
    unknown74 = 0;
    unknown5c = unknown;
    unknown70 = kListenerUnknown70;
    return this;
}
