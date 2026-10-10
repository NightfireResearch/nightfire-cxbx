#ifndef DRIVING_ENGINE_FEEDBACK_H_
#define DRIVING_ENGINE_FEEDBACK_H_

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------------------------------------------
// Force feedback (Ghidra: IFeedback): the pads' motors. An IFeedback is one word, a pointer to its port's record;
// any number of them share a port (the game's main function holds one for the player's controller, PBondCar's
// constructor makes another). While the launch page has vibration on and super-easy mode is off, the first
// IFeedback starts a thread and a timer task: each timer tick posts the thread's signal, and the thread runs Update,
// which turns each port's record into motor speeds for XInputSetState. The last one's destructor stops them again.
//
// What a record mixes, each tick (sums clamped to 255):
//   - two inputs, floats nothing here writes: one (x100) is a pulse level, which switches both motors to 0xb34c
//     on the ticks where it beats the tick count with its bits reversed (an ordered dither of the duty cycle); the
//     other (x125) is a steady speed, x255 for the motors;
//   - a vibration (SetVibrate), added to the pulse level for its duration;
//   - a rumble (SetRumble), added to the steady speed for its duration;
//   - a jolt (ApplyJolt, AddCollisionInfo): its level added to both at first, then, for its second half, fading
//     linearly on the pulse side only, dithered at half the rate.
// See docs/driving/input.md.
// ---------------------------------------------------------------------------------------------------------------

struct CollisionImpact;

// A port's state (0x34 bytes, four of them at 0x001e4488; the name is ours). Durations count timer ticks.
struct FeedbackPort {
    int32_t users;          // +0x00 IFeedback objects on the port
    int32_t port;           // +0x04 its number
    int32_t pulseLevel;     // +0x08 this tick's, from pulseInput and the vibration
    int32_t steadyLevel;    // +0x0c this tick's, from steadyInput
    int32_t joltLevel;      // +0x10
    int32_t joltTicks;      // +0x14 left to run
    int32_t joltFadeTicks;  // +0x18 half the jolt's length: the fade starts when joltTicks reaches it
    int32_t rumbleLevel;    // +0x1c
    int32_t rumbleTicks;    // +0x20
    int32_t vibrateLevel;   // +0x24
    int32_t vibrateTicks;   // +0x28
    float pulseInput;       // +0x2c x100 for pulseLevel (name ours: nothing here writes it but Reset)
    float steadyInput;      // +0x30 x125 for steadyLevel (name ours, as pulseInput)

    // Everything but the user count zeroed, the port number set (inlined at each use in the original)
    void Reset(int number) {
        port = number;
        pulseLevel = 0;
        steadyLevel = 0;
        joltLevel = 0;
        joltTicks = 0;
        joltFadeTicks = 0;
        rumbleLevel = 0;
        rumbleTicks = 0;
        vibrateLevel = 0;
        vibrateTicks = 0;
        pulseInput = 0.0f;
        steadyInput = 0.0f;
    }
};
static_assert(sizeof(FeedbackPort) == 0x34, "a feedback port is 0x34 bytes");
static_assert(offsetof(FeedbackPort, joltLevel) == 0x10 && offsetof(FeedbackPort, rumbleLevel) == 0x1c &&
                  offsetof(FeedbackPort, vibrateLevel) == 0x24 && offsetof(FeedbackPort, pulseInput) == 0x2c,
              "feedback port layout");

struct IFeedback {
    FeedbackPort *record;

    // The constructor (0x0004fe30): the port's record, and a user of it while vibration is allowed.
    IFeedback* Construct(int port);
    // The destructor (0x0004fb10): the last user of the port resets it; the last user of all stops the thread.
    void Destruct();

    // A jolt of `strength`: up to 0.25, a level of strength / 0.25 x 255 for 8 ticks; above, the full level for
    // strength x 32 ticks (at most 24). It replaces the running jolt only if it is stronger or longer.
    void ApplyJolt(float strength);
    // A rumble of strength x 255 for duration x 60 ticks (0x0004f960).
    void SetRumble(float strength, float duration);
    // A vibration of strength x 255 for duration x 60 ticks (0x0004f9b0).
    void SetVibrate(float strength, float duration);
    // A jolt from a collision: its strength scaled by 0.2 when either side's word is 14, 0.5 when the second
    // side's kind is 4, else 1.5 (0x0004fee0).
    void AddCollisionInfo(const CollisionImpact *impact);

    // Every port's record reset; the user counts are kept (0x0004fa00).
    static void Pause();
    // One tick of every port's motors (0x0004fbc0).
    static void Update();
};
static_assert(sizeof(IFeedback) == 4, "an IFeedback is one pointer");

// The timer task: posts the thread's signal (0x0004f8b0).
void IFeedback_Timer_Callback();
// The thread: an Update for every signal until the destructor asks it to stop (0x0004fe10; the name is ours).
void IFeedback_Thread();

#endif // DRIVING_ENGINE_FEEDBACK_H_
