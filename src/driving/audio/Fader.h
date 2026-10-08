#ifndef DRIVING_AUDIO_FADER_H_
#define DRIVING_AUDIO_FADER_H_

// ---------------------------------------------------------------------------------------------------------------
// AFader: one stream shared between a primary sound and a secondary one. Each primary event (QueuePrimary) moves
// the stream to the "Speech" or "NIS" mix and queues the event, fading the stream out first if something is still
// playing; when the primary is over, the stream plays the secondary file as a loop in the "Ambience" mix, fading
// in. The fade also scales the mix the fader was made with against the "0:Fade Effects" mix. Faders are shared by
// name through URefCounter<AFader>. See Fader.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "Mix.h"                        // AMix, RefCounterTree

class AStream;

class AFader {
public:
    // The fader's state (0x30 bytes, from UMemory::FastAlloc "AFader::Priv").
    class Priv {
    public:
        float fade;                 // +0x00 the stream's volume while fading, 0..1
        char secondary[0x10];       // +0x04 the secondary's file ("Off": none)
        AStream *stream;            // +0x14
        AMix *mix;                  // +0x18 scaled against fadeEffects by the fade
        AMix *fadeEffects;          // +0x1c "0:Fade Effects"
        AMix *fadeMusic;            // +0x20 "0:Fade Music"
        float filter;               // +0x24 the stream's filter while it fades
        uint8_t unknown28;          // +0x28 set by a primary event (and by a new secondary, Call911); cleared, while
                                    //       unknown2c is set, when the stream is over
        uint8_t unknown29;          // +0x29
        uint8_t fadingOut;          // +0x2a
        uint8_t unknown2b;          // +0x2b set by Call911 only
        uint8_t unknown2c;          // +0x2c one from the constructor
        uint8_t secondaryOn;        // +0x2d the secondary is not "Off"
        uint8_t unknown2e;          // +0x2e zero from the constructor
        uint8_t unknown2f;

        Priv* Construct(AStream *stream, AMix *mix);                            // 0x00124c40
        // A primary event: the file `name`, `fadeTime` as AStream::Event takes it; `effect` plays it in the
        // "NIS" mix (and only once the stream is over, looping or an effect), `held` is passed on.
        void Event(const char *name, float fadeTime, bool effect, bool held);   // 0x00124ca0
        // A new secondary file (the name is ours; Ghidra: FUN_00124e30).
        void SetSecondary(const char *name);                                    // 0x00124e30
        // Once per audio frame.
        void Update();                                                          // 0x00124ed0
    };

    Priv *priv;                     // +0x00

    AFader* Construct(AStream *stream, AMix *mix);                              // 0x00125180
    void QueuePrimary(const char *name, float fadeTime, bool effect, bool held); // 0x001251f0
    void SetSecondary(const char *name);                                        // 0x00125200
    void SetSecondary(bool on);                                                 // 0x00125210
    void SetFilter(float filter);                                               // 0x00125220
    // With the secondary "city": as if a primary event had come.
    void Call911();                                                             // 0x00125230
    void Update();                                                              // 0x00125260

    // A fader named `name` over the stream, scaling `mix`, with one more reference.
    static AFader* Create(const char *name, AStream *stream, AMix *mix);        // 0x00125cb0
    static AFader* Get(const char *name);                                       // 0x00125d40
    // One reference fewer to the fader `name`; the fader is freed whatever the count.
    static void Remove(const char *name);                                       // 0x00125d60
};
static_assert(offsetof(AFader::Priv, stream) == 0x14, "AFader::Priv::stream is at +0x14");
static_assert(offsetof(AFader::Priv, filter) == 0x24, "AFader::Priv::filter is at +0x24");
static_assert(sizeof(AFader::Priv) == 0x30, "AFader::Priv is 0x30 bytes");
static_assert(sizeof(AFader) == 4, "AFader is 4 bytes");

// URefCounter<AFader>'s tree (FaderRefCounter's map, at 0x00243af8): its compiled copies of RefCounterTree's code.
class FaderRefTree : public RefCounterTree {
public:
    void EraseSubtree(RefCounterNode *node);                                    // 0x001252f0
    RefCounterNode** EraseAt(RefCounterNode **result, RefCounterNode *where);   // 0x00125340
    RefCounterNode** InsertAt(RefCounterNode **result, bool addLeft, RefCounterNode *where,
                              const RefCounterValue *value);                    // 0x001256b0
    RefCounterNode** EraseRange(RefCounterNode **result, RefCounterNode *first,
                                RefCounterNode *last);                          // 0x001258a0
    RefCounterInsertResult* InsertUnique(RefCounterInsertResult *result,
                                         const RefCounterValue *value);         // 0x00125990
    // The destructor's erase and free, without its subtree erase (only an exception unwind calls it).
    void DestroyRange();                                                        // 0x00125b50
    void Destruct();                                                            // 0x00125b90
};

#endif // DRIVING_AUDIO_FADER_H_
