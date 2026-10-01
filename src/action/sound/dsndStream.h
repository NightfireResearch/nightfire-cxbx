#ifndef DSNDSTREAM_H_
#define DSNDSTREAM_H_

#include <stdint.h>

// ---------------------------------------------------------------------------------------------------------------
// The DirectSound stream path, natively.
//
// This is the FMV audio path, and the one part of DSOUND the audio seam deliberately left alone. The XMV
// decoder does not go through any game function the seam replaces: it calls DSOUND's own exports directly, so
// under CXBX those calls landed in CXBX's HLE and CXBX played the streams. docs/audio-inventory.md records
// that arrangement, and predicted that streams "become the backend's problem only once those entry points are
// hooked at their own addresses" - which is what this does, now that CXBX is gone.
//
// Standalone there is nothing behind those exports but the XBE's real DirectSound, which drives the MCPX
// audio hardware: it programs the mixer registers at 0xfe80xxxx and spins on them. That is unbacked memory
// here, so the first FMV with an audio track faulted. Both of the crashes reported against the standalone
// loader - starting a mission, and opening the codename screen - were this, because both play a movie.
//
// So the exports the decoder uses are patched to land here instead, and a stream becomes an XAudio2 source
// voice. Doing it for real rather than stubbing it silent is worth the extra work for one reason beyond the
// audio: the decoder paces video against audio progress (see maybeSFXStreamCallback, which sets the
// decoder's sync offset from when a packet completed), so packets have to complete when the audio is
// actually consumed. Completing them immediately would run every movie at whatever speed the disc could
// feed it.
// ---------------------------------------------------------------------------------------------------------------

// Installs the hooks. Called from Inject(), and only when the game is not running under CXBX - under CXBX the
// XBE's DSOUND is already replaced by CXBX's own patches and must be left exactly as it is.
void DSoundStream_InstallHooks(void);

// True when nothing else is hosting this process - that is, when the standalone loader is running the game
// and the XBE's own libraries are the only implementation there is.
bool DSoundStream_RunningStandalone(void);

// Completes any packets whose audio has finished playing, on the thread that calls it. The seam's
// DirectSoundDoWork calls this, which is exactly where the decoder expects progress to happen: it calls
// DirectSoundDoWork and then checks whether its packets are still pending.
void DSoundStream_DoWork(void);

#endif // DSNDSTREAM_H_
