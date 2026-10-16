#ifndef ACTION_DEVTOOLS_DEMO_H_
#define ACTION_DEVTOOLS_DEMO_H_

#include <stdint.h>

// Time demos for the action engine: record a session's input, then play it back and check that the game does the
// same thing frame by frame. See Demo.cpp and docs/action-timedemo.md.
//
//   record:  settings.ini [Settings] DemoRecord=on (to demos\<date>-<time>.nfdemo), or NIGHTFIRE_DEMO_RECORD=<file>
//   play:    NIGHTFIRE_DEMO_PLAY=<file>
//
// Recording and playback both run the game on a clock that follows its own frames, with disc reads finishing as
// they are issued and the movie following that clock. What the sound device says of its progress is recorded and
// given back. So a playback meets the same game the recording did.

// From Inject(), before anything reads settings.ini: picks the mode and, for a playback, moves the working
// directory into a sandbox holding the files the recording started with.
void Demo_Init(void);

// Recording or playing
bool Demo_Active(void);
bool Demo_Playing(void);

// mainloop, either side of GameFlow_Main
void Demo_FrameStart(void);
void Demo_FrameEnd(void);

// The clock the game sees while a demo is active: timestamp()'s milliseconds, and the XAPI performance counter
// (Demo_CounterFrequency ticks a second). Both move on with every frame and a little with every read.
double Demo_TimestampMs(void);
int64_t Demo_Counter(void);
int64_t Demo_CounterFrequency(void);

// One poll of one controller port, as psiInput_PollDevices has it from the host: recorded, or replaced by the
// recording. The packet number is set from the state, so it changes when the state does and not otherwise.
struct DemoPad {
    uint8_t connected;
    uint32_t packet;
    uint16_t buttons;            // the host's XInput button bits
    uint8_t leftTrigger, rightTrigger;
    int16_t thumbLX, thumbLY, thumbRX, thumbRY;
};
void Demo_Pad(int port, DemoPad *pad);

// The mouse once MouseLook_Update has run: whether it holds the pointer, and the counts it has gathered and not yet
// handed to the game. Recorded, or replaced by the recording.
void Demo_Mouse(bool *captured, long *countsX, long *countsY);

// A sound buffer's status or play cursor, as the device answered (xaudio2Backend.cpp): recorded, or replaced by
// the recording. `buffer` is the buffer's index in creation order, -1 for none.
enum DemoSoundQuery { DEMO_SOUND_STATUS, DEMO_SOUND_PLAY_CURSOR };
void Demo_SoundAnswer(int buffer, DemoSoundQuery query, uint32_t *answer);

// psiLaunchDriving: the session leaves for the driving engine. A recording ends here; a playback stops.
void Demo_LeavingForDriving(void);

// GfxHost_PresentEvery for a playback: NIGHTFIRE_DEMO_FAST=N presents every Nth frame, unpaced
int Demo_PresentEvery(void);

#endif // ACTION_DEVTOOLS_DEMO_H_
