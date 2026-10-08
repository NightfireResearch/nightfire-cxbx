#ifndef DRIVING_DEVTOOLS_SNDLOCKSTEP_H_
#define DRIVING_DEVTOOLS_SNDLOCKSTEP_H_

#include "../platform/FileSys.h"

#include <stdint.h>

// NIGHTFIRE_SNDLOCKSTEP=1, with NIGHTFIRE_LOCKSTEP=1: everything the game thread can see of the sound side follows
// the simulation's ticks instead of real time, so that two lockstep runs give the same sound trace. See
// SndLockstep.cpp. Every hook below does nothing (or answers false) when the mode is off.

bool SndLockstep_Enabled(void);

// From Scheduler.cpp's RunTick, first thing in each game loop that simulates: TIMER_gettick moves on, the sound
// driver runs its 10 ms steps for one timer tick's time, then the main thread's sound service runs.
void SndLockstep_Tick(void);

// From SNDDRV_thread: the driver thread's loop in this mode, running steps only as SndLockstep_Tick hands them over.
uint32_t SndLockstep_DriverThread(void);

// From SNDREAL_systemtask: true when SYNCTASK's call should do nothing, because SndLockstep_Tick runs the service.
bool SndLockstep_DefersService(void);

// From FILESYS_callbackop: for a sound stream's op, waits until it has finished and runs its callback (and the ops
// that starts) on this thread. True when it has, and FILESYS_callbackop has nothing left to do.
bool SndLockstep_FileCallback(unsigned handle, FsCallback callback);

// From TIMER_gettick: the tick count to answer, given the real one.
int SndLockstep_TimerTick(int realTick);

// From the DirectSound seam: what each buffer is told, and its status and cursors from the simulated playback.
struct IDirectSoundBuffer;
void SndLockstep_BufferCreated(const IDirectSoundBuffer *buffer, uint32_t sampleRate, uint16_t formatTag,
                               uint16_t channels, uint16_t blockAlign, uint16_t bitsPerSample, uint32_t bytes);
void SndLockstep_BufferData(const IDirectSoundBuffer *buffer, uint32_t bytes);
void SndLockstep_BufferPlay(const IDirectSoundBuffer *buffer, bool looping);
void SndLockstep_BufferStop(const IDirectSoundBuffer *buffer);
void SndLockstep_BufferPosition(const IDirectSoundBuffer *buffer, uint32_t bytes);
void SndLockstep_BufferLoopRegion(const IDirectSoundBuffer *buffer, uint32_t startBytes, uint32_t lengthBytes);
void SndLockstep_BufferFrequency(const IDirectSoundBuffer *buffer, uint32_t hz);
void SndLockstep_BufferReleased(const IDirectSoundBuffer *buffer);
bool SndLockstep_BufferStatus(const IDirectSoundBuffer *buffer, uint32_t *status);
bool SndLockstep_BufferCursors(const IDirectSoundBuffer *buffer, uint32_t *playCursor, uint32_t *writeCursor);

#endif // DRIVING_DEVTOOLS_SNDLOCKSTEP_H_
