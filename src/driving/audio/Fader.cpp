#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // (the build defines it; for a file compiled alone)
#endif

#include "Fader.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <bit>

#include "SoundManager.h"               // FramesSinceAudioUpdate, ASoundManager_fgIsPaused, ASoundManager_fgMissionOver
#include "Stream.h"
#include "../../helpers.h"
#include "../engine/InputConfig.h"      // BuildFileName
#include "../engine/UMemory.hpp"

#pragma fp_contract(off)

// ---------------------------------------------------------------------------------------------------------------
// AFader and URefCounter<AFader>'s tree (0x00124c40..0x00125db0), ported from the listings. See Fader.h.
// ---------------------------------------------------------------------------------------------------------------

// ---- originals called by address

// Moves the sound to the mix `mixName` (Remove, then Add).
#define ABaseSound_SetMix ((void (__fastcall *)(ABaseSound *, int, const char *mixName))0x000d3690)
#define CRT_stricmp ((int (*)(const char *, const char *))0x00134537)

// ---- globals

#define ScreenFade I16_AT(0x00243a74)                      // GHud's screen fade (name ours)

namespace {

constexpr float kFadeRate = 1.0f / 60.0f;       // per frame, doubled: a fade takes half a second
constexpr float kFader0Rate = 0.25f;
constexpr float kSecondaryStartFade = 0.001f;
static_assert(std::bit_cast<uint32_t>(kFadeRate) == 0x3c888889, "1/60 as the original's .rdata has it");
static_assert(std::bit_cast<uint32_t>(kSecondaryStartFade) == 0x3a83126f, "0.001f as the original stores it");

// The warning beside a provisional port (code no shipped data reaches), once.
void AudioUntested(const char *what) {
    static bool warned;
    if (warned)
        return;
    warned = true;
    printf("[audio] WARNING: %s ran - a provisional port that no shipped data reaches, UNTESTED. Check what it "
           "computes against the original.\n", what);
    fflush(stdout);
}

}  // namespace

// =============================================================================================================
// AFader::Priv
// =============================================================================================================

// FUNC_AT(0x00124c40)
AFader::Priv* AFader::Priv::Construct(AStream *sharedStream, AMix *scaledMix) {
    fade = 0.0f;
    stream = sharedStream;
    mix = scaledMix;
    fadeEffects = AMix::Get("0:Fade Effects");
    fadeMusic = AMix::Get("0:Fade Music");
    unknown28 = 1;
    unknown2c = 1;
    unknown29 = 0;
    fadingOut = 0;
    unknown2b = 0;
    secondaryOn = 0;
    unknown2e = 0;
    secondary[0] = '\0';
    filter = 1.0f;
    return this;
}

// FUNC_AT(0x00124ca0)
void AFader::Priv::Event(const char *name, float fadeTime, bool effect, bool held) {
    if (AMix::Get("Speech")->GetVolume() == 0.0 || AMix::Get("NIS")->GetVolume() == 0.0)
        return;
    if (effect && !stream->IsOver() && !stream->IsLoop() && !stream->IsEffect())
        return;

    if (!stream->IsOver() && !(fadeTime >= 0.0f)) {
        fadingOut = 1;
    } else {
        stream->mix->Remove();
        if (effect) {
            stream->mix = AMix::Add("NIS");
            stream->volume = 1.0f;
            stream->SetFilter(1.0f);
        } else {
            stream->mix = AMix::Add("Speech");
            stream->volume = 1.0f;
            stream->SetFilter(1.0f);
            mix->SetVolume(float(fadeEffects->GetVolume()));
            float music = float(fadeMusic->GetVolume());
            AStream::Get("Music")->volume = music;
        }
        fade = 0.0f;
    }
    char path[0x40];
    stream->Event(BuildFileName(path, 0, "", name, ""), fadeTime, false, effect, held);
    unknown28 = 1;
    unknown29 = 0;
}

// FUNC_AT(0x00124e30)
void AFader::Priv::SetSecondary(const char *name) {
    if (!unknown2c || strcmp(secondary, name) == 0)
        return;
    secondaryOn = strcmp(name, "Off") != 0;
    strcpy(secondary, name);
    if (!unknown28) {
        unknown29 = 1;
        fadingOut = 1;
    }
    unknown28 = 1;
}

// FUNC_AT(0x00124ed0)
void AFader::Priv::Update() {
    float step = float(FramesSinceAudioUpdate * double(kFadeRate));

    if (stream->IsOver()) {
        if (unknown28) {
            stream->volume = 0.0f;
            if (unknown2c)
                unknown28 = 0;
        } else if (secondaryOn) {
            stream->mix->Remove();
            stream->mix = AMix::Add("Ambience");
            char path[0x40];
            stream->Event(BuildFileName(path, 0, "", secondary, ""), 0.0f, true, false, false);
            fade = kSecondaryStartFade;
        }
    }

    // The fades are compared before they are rounded to a float.
    if (!unknown28) {
        double next = double(step) + step + fade;
        fade = float(next);
        if (next > 1.0) {
            fade = 1.0f;
            unknown29 = 0;
        }
        stream->volume = fade;
        stream->SetFilter(filter);
    } else if (fadingOut) {
        double next = fade - (double(step) + step);
        fade = float(next);
        if (next <= 0.0) {
            fade = 0.0f;
            if (stream->IsLoop() || unknown29)
                stream->Next();
            ABaseSound_SetMix(stream, 0, stream->IsEffect() ? "NIS" : "Speech");
            stream->volume = 1.0f;
            stream->SetFilter(1.0f);
            fadingOut = 0;
        } else {
            stream->volume = fade;
            stream->SetFilter(filter);
        }
    } else if (ScreenFade < 0) {
        stream->Stop();
        stream->Next();
    } else if (!stream->IsPaused()) {
        stream->volume = 1.0f;
        stream->SetFilter(1.0f);
    } else if (!ASoundManager_fgIsPaused) {
        stream->Stop();
    }

    if (ASoundManager_fgMissionOver) {
        double effects = fadeEffects->GetVolume();
        mix->SetVolume(float(effects + (1.0 - effects) * fade));
        AMix *fader0 = AMix::Get("0:Fader0");
        double next = fader0->volume - double(step) * kFader0Rate;
        fader0->SetVolume(next > 0.0 ? float(next) : 0.0f);
    } else if (unknown29) {
        mix->SetVolume(1.0f);
        AMix::Get("0:Fader0")->SetVolume(1.0f);
        AStream::Get("Music")->volume = 1.0f;
    } else {
        float effects = float(fadeEffects->GetVolume());
        float music = float(fadeMusic->GetVolume());
        AMix::Get("0:Fader0")->SetVolume(1.0f);
        if (!stream->IsEffect()) {
            mix->SetVolume(float((1.0 - effects) * fade + effects));
            float musicVolume = float((1.0 - music) * fade + music);
            AStream::Get("Music")->volume = musicVolume;
        }
    }
}

// =============================================================================================================
// AFader
// =============================================================================================================

// FUNC_AT(0x00125180)
AFader* AFader::Construct(AStream *stream, AMix *mix) {
    Priv *state = static_cast<Priv *>(UMemory::FastAlloc(sizeof(Priv), "AFader::Priv"));
    priv = state != NULL ? state->Construct(stream, mix) : NULL;
    return this;
}

// FUNC_AT(0x001251f0)
void AFader::QueuePrimary(const char *name, float fadeTime, bool effect, bool held) {
    priv->Event(name, fadeTime, effect, held);
}

// FUNC_AT(0x00125200)
void AFader::SetSecondary(const char *name) {
    priv->SetSecondary(name);
}

// FUNC_AT(0x00125210)
void AFader::SetSecondary(bool on) {
    priv->secondaryOn = on;
}

// FUNC_AT(0x00125220)
void AFader::SetFilter(float filter) {
    priv->filter = filter;
}

// FUNC_AT(0x00125230)
void AFader::Call911() {
    Priv *state = priv;
    if (CRT_stricmp(state->secondary, "city") != 0)
        return;
    if (!state->unknown28) {
        state->unknown29 = 1;
        state->fadingOut = 1;
    }
    state->unknown28 = 1;
    state->unknown2b = 1;
}

// FUNC_AT(0x00125260)
void AFader::Update() {
    priv->Update();
}

// FUNC_AT(0x00125cb0)
AFader* AFader::Create(const char *name, AStream *stream, AMix *mix) {
    FaderRefCounter::Get()->GetReference(name);     // the answer is not used
    AFader *fader = static_cast<AFader *>(UMemory::FastAlloc(sizeof(AFader), "AFader"));
    if (fader != NULL)
        fader = fader->Construct(stream, mix);
    FaderRefCounter::Get()->AddReference(name, fader);
    return fader;
}

// FUNC_AT(0x00125d40)
AFader* AFader::Get(const char *name) {
    return FaderRefCounter::Get()->GetReference(name);
}

// FUNC_AT(0x00125d60)
void AFader::Remove(const char *name) {
    AFader *fader = FaderRefCounter::Get()->GetReference(name);
    FaderRefCounter::Get()->RemoveReference(fader);
    if (fader != NULL) {
        if (fader->priv != NULL)
            UMemory::FastFree(fader->priv, sizeof(Priv));
        UMemory::FastFree(fader, sizeof(AFader));
    }
}

// =============================================================================================================
// URefCounter<AFader>'s tree
// =============================================================================================================

// FUNC_AT(0x001252f0)
void FaderRefTree::EraseSubtree(RefCounterNode *node) {
    RefCounterTree::EraseSubtree(node);
}

// FUNC_AT(0x00125340)
RefCounterNode** FaderRefTree::EraseAt(RefCounterNode **result, RefCounterNode *where) {
    return RefCounterTree::EraseAt(result, where);
}

// FUNC_AT(0x001256b0)
RefCounterNode** FaderRefTree::InsertAt(RefCounterNode **result, bool addLeft, RefCounterNode *where, const RefCounterValue *value) {
    return RefCounterTree::InsertAt(result, addLeft, where, value);
}

// FUNC_AT(0x001258a0)
RefCounterNode** FaderRefTree::EraseRange(RefCounterNode **result, RefCounterNode *first, RefCounterNode *last) {
    return RefCounterTree::EraseRange(result, first, last);
}

// FUNC_AT(0x00125990)
RefCounterInsertResult* FaderRefTree::InsertUnique(RefCounterInsertResult *result, const RefCounterValue *value) {
    return RefCounterTree::InsertUnique(result, value);
}

// FUNC_AT(0x00125b50)
void FaderRefTree::DestroyRange() {
    AudioUntested("URefCounter<AFader>'s tree, destroyed by an exception unwind");
    RefCounterTree::DestroyRange();
}

// FUNC_AT(0x00125b90)
void FaderRefTree::Destruct() {
    Destroy();
}
