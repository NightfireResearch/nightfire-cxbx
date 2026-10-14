// driving_standalone's answers to the lockstep and dump harness's hooks (devtools/Teleport.cpp, SndLockstep.cpp,
// SndCallTrace.cpp), which driving_standalone_test links instead: each as the harness behaves with its setting off.

#include "../devtools/SndCallTrace.h"
#include "../devtools/SndLockstep.h"
#include "../devtools/Teleport.h"

bool SndLockstep_Enabled(void) {
    return false;
}

void SndLockstep_Tick(void) {
}

uint32_t SndLockstep_DriverThread(void) {
    return 0;
}

bool SndLockstep_DefersService(void) {
    return false;
}

bool SndLockstep_FileCallback(unsigned, FsCallback) {
    return false;
}

int SndLockstep_TimerTick(int realTick) {
    return realTick;
}

void SndLockstep_BufferCreated(const IDirectSoundBuffer *, uint32_t, uint16_t, uint16_t, uint16_t, uint16_t,
                               uint32_t) {
}

void SndLockstep_BufferData(const IDirectSoundBuffer *, uint32_t) {
}

void SndLockstep_BufferPlay(const IDirectSoundBuffer *, bool) {
}

void SndLockstep_BufferStop(const IDirectSoundBuffer *) {
}

void SndLockstep_BufferPosition(const IDirectSoundBuffer *, uint32_t) {
}

void SndLockstep_BufferLoopRegion(const IDirectSoundBuffer *, uint32_t, uint32_t) {
}

void SndLockstep_BufferFrequency(const IDirectSoundBuffer *, uint32_t) {
}

void SndLockstep_BufferReleased(const IDirectSoundBuffer *) {
}

bool SndLockstep_BufferStatus(const IDirectSoundBuffer *, uint32_t *) {
    return false;
}

bool SndLockstep_BufferCursors(const IDirectSoundBuffer *, uint32_t *, uint32_t *) {
    return false;
}

void Teleport_Tick(void) {
}

void Teleport_BeforeRun(Scheduler *) {
}

bool Teleport_LockstepLocalTime(struct _SYSTEMTIME *) {
    return false;
}

void SndCallTrace_Install(void) {
}

void SndCallTrace_Tick(void) {
}
