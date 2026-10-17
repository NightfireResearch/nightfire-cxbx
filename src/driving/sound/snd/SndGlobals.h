#ifndef DRIVING_SOUND_SND_SNDGLOBALS_H_
#define DRIVING_SOUND_SND_SNDGLOBALS_H_

// The sound library's globals that more than one module reads (docs/driving/sound.md 2.6), defined in System.cpp
// (the buffer lists in Platform.cpp). A module's own globals are defined at the top of its .cpp.

#include "System.h"
#include "Voices.h"
#include "Banks.h"
#include "Platform.h"

#include <stdint.h>

// ---- the options (SNDSYS_getopts/setops, SNDPLATFORM_outputcaps/outputset)
extern SND::SysOpts SndOptions;
extern SND::SysSet SndSavedSet;                  // the settable part as last set, put back once inited
#define NumBanks (SndOptions.set.maxBanks)               // 0x00244cf0
#define PlatformRate (SndOptions.set.outputRate)         // 0x00244cf2, 48000
#define NumStreams (SndOptions.set.maxStreams)           // 0x00244d0f
#define OutputMode (SndOptions.set.outputMode)           // 0x00244d10, the speakers
#define RenderModeCount (SndOptions.set.numRenderModes)  // 0x00244d15
#define RenderModes (SndOptions.set.renderModes)         // 0x00244d18, [RenderModeCount]

// ---- the system's state
extern uint8_t SystemInited;                     // SNDSYS_is_inited
extern int8_t MasterVolume;                      // 0x7f
extern int8_t NumUserDataClients;                // never registered
extern uint8_t MixQuality;                       // handed to the resampler and the unpackers (2)
extern int16_t NumVoices;                        // NUM_VOICES (224)
extern uint32_t SndTick;                         // the 100 Hz server's tick counter
extern SND::UserDataClient UserDataClients[4];
extern int (*BankExitHook)(int);                 // SNDbank_on_exit_func: SNDbankremove, called with -1
extern SndRestoreHook StreamExitHook;            // SNDSTRM_on_exit_func
extern SND::MemHeap *SndHeap;                    // pSndHeap

// ---- the voices, banks and buses
extern SND::Voice *VoiceArray;                   // sndvoicei_buffer: [NumVoices]
extern SND::BankSlot *BankArray;                 // sndbanki_buffer: [NumBanks]
// FXBUS: [bus], the main CPU's path's from the first entry and the hardware path's from the second - the two
// overlap from bus 1 on (only bus 0 is ever used)
extern SND::FxBus FxBuses[2];
#define FxBusMainCpu FxBuses
#define FxBusHardware (FxBuses + 1)
// The hardware buffer nodes' lists by pool: Llist_MaybeActiveDsndBuffers[2], then LList_maybeFreeDsndBuffers[2]
// - one block, as FUN_0013d550 can pop from the list before the free lists
extern SND::BufferList BufferLists[4];
#define ActiveBufferLists BufferLists
#define FreeBufferLists (BufferLists + 2)

// The default azimuth of each channel of a sound of n channels: ChannelAzimuth[n - 1][channel], 65536ths of a turn
// (SNDSYSI_init fills the rows for 2..6 channels; a single channel's stays 0)
extern uint16_t ChannelAzimuth[6][6];

#endif // DRIVING_SOUND_SND_SNDGLOBALS_H_
