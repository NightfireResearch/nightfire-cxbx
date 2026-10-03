#ifndef DRIVING_SOUND_SND_SNDGLOBALS_H_
#define DRIVING_SOUND_SND_SNDGLOBALS_H_

// The sound library's globals that more than one module reads (docs/driving/sound.md 2.6), named once. They stay
// at the original's addresses: the library's code that is not ours yet, the game and the shadow tests read them
// there. A module's own globals are named at the top of its .cpp.
//
// Include this from .cpp files only: the names are macros.

#include "System.h"
#include "Voices.h"
#include "Banks.h"
#include "Platform.h"
#include "../../../helpers.h"

#include <stdint.h>

// ---- the options (SNDSYS_getopts/setops, SNDPLATFORM_outputcaps/outputset)
#define SndOptions (*(SND::SysOpts *)0x00244cc8)
#define SndSavedSet (*(SND::SysSet *)0x00244de8)        // the settable part as last set, put back once inited
#define NumBanks (SndOptions.set.maxBanks)               // 0x00244cf0
#define PlatformRate (SndOptions.set.outputRate)         // 0x00244cf2, 48000
#define NumStreams (SndOptions.set.maxStreams)           // 0x00244d0f
#define OutputMode (SndOptions.set.outputMode)           // 0x00244d10, the speakers
#define RenderModeCount (SndOptions.set.numRenderModes)  // 0x00244d15
#define RenderModes (SndOptions.set.renderModes)         // 0x00244d18, [RenderModeCount]

// ---- the system's state
#define SystemInited U8_AT(0x00244ed0)                   // SNDSYS_is_inited
#define MasterVolume I8_AT(0x00244ed1)                   // 0x7f
#define NumUserDataClients I8_AT(0x00244ed6)             // never registered
#define MixQuality U8_AT(0x00244ed7)                     // handed to the resampler and the unpackers (2)
#define NumVoices I16_AT(0x00244ed8)                     // NUM_VOICES (224)
#define SndTick U32_AT(0x00244edc)                       // the 100 Hz server's tick counter
#define UserDataClients ((SND::UserDataClient *)0x00244f10)   // [4]
#define BankExitHook (*(int (**)(int))0x00244f2c)       // SNDbank_on_exit_func: SNDbankremove, called with -1
#define StreamExitHook (*(SndRestoreHook *)0x00244f38)  // SNDSTRM_on_exit_func
#define SndHeap (*(SND::MemHeap **)0x00244f6c)          // pSndHeap
#define SoundMutex ((void *)0x00244fc0)                  // the CRITICAL_SECTION

// ---- the voices, banks and buses
#define VoiceArray (*(SND::Voice **)0x00244f3c)         // sndvoicei_buffer: [NumVoices]
#define BankArray (*(SND::BankSlot **)0x00244f40)       // sndbanki_buffer: [NumBanks]
#define FxBusMainCpu ((SND::FxBus *)0x00244f44)         // FXBUS: [bus] - the two overlap from bus 1 on
#define FxBusHardware ((SND::FxBus *)0x00244f58)
#define ActiveBufferLists ((SND::BufferList *)0x00244c48)   // Llist_MaybeActiveDsndBuffers[2]

// The default azimuth of each channel of a sound of n channels: ChannelAzimuth[n][channel], 65536ths of a turn
// (SNDSYSI_init fills rows 2..6; row 0 is never read and overlaps the globals before)
#define ChannelAzimuth ((uint16_t (*)[6])0x00244f64)

#endif // DRIVING_SOUND_SND_SNDGLOBALS_H_
