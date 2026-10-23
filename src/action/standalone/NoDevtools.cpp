// The devtools hooks the game's code calls, answered for the standalone build, which has none of the devtools
// (docs/action-standalone.md): each does what its devtool does when nothing asks it to.

#include "../devtools/Demo.h"
#include "../devtools/FileDump.h"
#include "../devtools/MenuProbe.h"
#include "../devtools/SndTrace.h"
#include "../engine/psiInput.h"
#include "../ui/ui.h"

#ifndef NIGHTFIRE_STANDALONE_TEST   // which has the time demos' player
// No time demo: the game's own clock, the host's pads, mouse and sound device
void Demo_Init(void) {}

bool Demo_Active(void) {
    return false;
}

bool Demo_Playing(void) {
    return false;
}

void Demo_FrameStart(void) {}
void Demo_FrameEnd(void) {}

double Demo_TimestampMs(void) {
    return 0.0;
}

int64_t Demo_Counter(void) {
    return 0;
}

int64_t Demo_CounterFrequency(void) {
    return 1;
}

void Demo_Pad(int, DemoPad *) {}
void Demo_Mouse(bool *, int32_t *, int32_t *) {}
void Demo_SoundAnswer(int, DemoSoundQuery, uint32_t *) {}
void Demo_LeavingForDriving(void) {}
void Demo_HeapReset(void) {}
void Demo_ObjectListsReset(void) {}

int Demo_PresentEvery(void) {
    return 0;
}
#endif

// No menu script: the pads polled and each message handled as the game does
void MenuProbe_PollDevices(void) {
    psiInput_PollDevices();
}

bool MenuProbe_HandleMessage(uchar managerNum, M_CONTROL *control, uint message, intptr_t arg1, intptr_t arg2) {
    return Handler_HandleMessage(managerNum, control, message, arg1, arg2);
}

// No teleport keys, frame dumps or per-frame shadow tests
void Devtools_Tick(void) {}

// No sound call trace, and no copies of the files read
void SndTrace(const char *, ...) {}
void FileDump_Save(const char *, const void *, size_t) {}
