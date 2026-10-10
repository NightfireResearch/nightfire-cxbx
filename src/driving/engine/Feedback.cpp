#include "Feedback.h"

#include <math.h>

#include "GameLoop.h"
#include "../physics/RigidBodyResolve.h"   // CollisionImpact
#include "../platform/Pad.hpp"
#include "../platform/RealSystem.h"
#include "../platform/X87.h"
#include "../../helpers.h"

#pragma fp_contract(off)

// ---------------------------------------------------------------------------------------------------------------
// IFeedback (0x0004f8b0..0x0004ff50), ported from the listing. Update runs on the feedback thread; the rest on
// the main thread, with nothing between them but the records.
// ---------------------------------------------------------------------------------------------------------------

// XAPI's XINPUT_FEEDBACK: a header (status, an event to signal on completion, the driver's bytes), then the two
// motors' speeds
struct PadFeedback {
    uint32_t status;            // +0x00
    void *event;                // +0x04
    uint8_t reserved[58];       // +0x08
    uint16_t leftMotorSpeed;    // +0x42
    uint16_t rightMotorSpeed;   // +0x44
};
static_assert(offsetof(PadFeedback, leftMotorSpeed) == 0x42 && offsetof(PadFeedback, rightMotorSpeed) == 0x44,
              "XINPUT_FEEDBACK layout");

#define Launch (*(LaunchPage *)0x00243b90)
#define gSuperEasy BOOL8_AT(0x001e476c)
#define gPads ((PadData *)0x00241c50)
// Cleared by the end-of-mission, exit, letterbox and pause events, set again by resume and restart (name ours)
#define gFeedbackAllowed BOOL8_AT(0x001b6f50)

#define gFeedbackThread (*(RealThread *)0x001e2410)
#define gFeedbackThreadStop BOOL8_AT(0x001e241c)
#define gFeedbackSignal (*(RealSignal *)0x001e2420)
#define gFeedbackThreadStack ((uint8_t *)0x001e2430)   // 0x2000 bytes, the PS2's thread stack
#define gFeedbackUsers I32_AT(0x001e4430)              // ports in use
#define gPadFeedback (*(PadFeedback *)0x001e4438)
#define gFeedbackPorts ((FeedbackPort *)0x001e4488)
#define gFeedbackTick U32_AT(0x001e4558)

#define XInputSetState ((uint32_t (__stdcall *)(void *, PadFeedback *))0x00184b59)

static const int kPorts = 4;
static const int kMaxLevel = 255;
static const uint16_t kPulseSpeed = 0xb34c;   // both motors while a pulse is on
static const unsigned kThreadStackBytes = 0x2000;
static const int kThreadPriority = 3;

// The dither: 0..255 with their bits reversed (the table at 0x0018d790)
static uint8_t Dither(uint32_t tick) {
    uint8_t value = 0;
    for (int bit = 0; bit < 8; bit++)
        if (tick & (1u << bit))
            value |= 0x80 >> bit;
    return value;
}

static int Clamp255(int level) {
    return level > kMaxLevel ? kMaxLevel : level;
}

// Both motors at `speed`, if a pad is open on the port
static void SetMotors(int port, uint16_t speed) {
    void *handle = gPads[port].handle;
    if (handle == nullptr)
        return;
    gPadFeedback.event = nullptr;
    gPadFeedback.leftMotorSpeed = speed;
    gPadFeedback.rightMotorSpeed = speed;
    XInputSetState(handle, &gPadFeedback);
}

// FUNC_AT(0x0004f8b0)
void IFeedback_Timer_Callback() {
    SIGNAL_post(&gFeedbackSignal);
}

// FUNC_AT(0x0004fe10)
void IFeedback_Thread() {
    do {
        SIGNAL_wait(&gFeedbackSignal);
        IFeedback::Update();
    } while (!gFeedbackThreadStop);
    THREAD_exit();
}

// FUNC_AT(0x0004fe30)
IFeedback* IFeedback::Construct(int port) {
    record = &gFeedbackPorts[port];
    if (Launch.vibration != 0 && !gSuperEasy) {
        if (record->users == 0) {
            record->Reset(port);
            if (gFeedbackUsers == 0) {
                gFeedbackThreadStop = false;
                SIGNAL_create(&gFeedbackSignal);
                THREAD_create(&gFeedbackThread, IFeedback_Thread, int(uintptr_t(gFeedbackThreadStack)),
                              kThreadStackBytes, kThreadPriority);
                TIMER_addtask(IFeedback_Timer_Callback);
            }
            gFeedbackUsers++;
        }
        record->users++;
    }
    return this;
}

// The thread is asked to stop, and the main thread runs the sync tasks until it has.
// FUNC_AT(0x0004fb10)
void IFeedback::Destruct() {
    if (record->users <= 0)
        return;
    record->users--;
    if (record->users != 0)
        return;
    record->Reset(record->port);
    if (--gFeedbackUsers != 0)
        return;
    TIMER_removetask(IFeedback_Timer_Callback);
    gFeedbackThreadStop = true;
    SIGNAL_post(&gFeedbackSignal);
    while (!THREAD_testexit(&gFeedbackThread))
        SYNCTASK_run(0);
    THREAD_destroy(&gFeedbackThread);
    SIGNAL_destroy(&gFeedbackSignal);
}

// FUNC_AT(0x0004f8c0)
void IFeedback::ApplyJolt(float strength) {
    int level, ticks;
    if (strength > 0.25f) {
        level = kMaxLevel;
        ticks = RoundToInt(8.0f / 0.25f * strength);
        if (ticks > 24)
            ticks = 24;
    } else {
        level = RoundToInt(255.0f / 0.25f * strength);
        ticks = 8;
    }
    if (level > record->joltLevel || ticks > record->joltTicks) {
        record->joltFadeTicks = ticks >> 1;
        record->joltTicks = ticks;
        record->joltLevel = level;
    }
}

// FUNC_AT(0x0004f960)
void IFeedback::SetRumble(float strength, float duration) {
    record->rumbleLevel = RoundToInt(strength * 255.0f);
    record->rumbleTicks = RoundToInt(duration * 60.0f);
}

// FUNC_AT(0x0004f9b0)
void IFeedback::SetVibrate(float strength, float duration) {
    record->vibrateLevel = RoundToInt(strength * 255.0f);
    record->vibrateTicks = RoundToInt(duration * 60.0f);
}

// FUNC_AT(0x0004fee0)
void IFeedback::AddCollisionInfo(const CollisionImpact *impact) {
    float strength = fabsf(impact->strength);
    if (impact->unknown4a == 14 || impact->tag == 14)
        ApplyJolt(strength * 0.2f);
    else if (impact->kindB == 4)
        ApplyJolt(strength * 0.5f);
    else
        ApplyJolt(strength * 1.5f);
}

// FUNC_AT(0x0004fa00)
void IFeedback::Pause() {
    for (int i = 0; i < kPorts; i++)
        gFeedbackPorts[i].Reset(i);
}

// FUNC_AT(0x0004fbc0)
void IFeedback::Update() {
    for (int i = 0; i < kPorts; i++) {
        FeedbackPort *p = &gFeedbackPorts[i];
        if (!gFeedbackAllowed || Launch.vibration == 0 || gSuperEasy) {
            SetMotors(i, 0);
            p->vibrateTicks = 0;
            p->rumbleTicks = 0;
            p->joltTicks = 0;
            continue;
        }
        if (p->users <= 0)
            continue;

        p->pulseLevel = RoundToInt(p->pulseInput * 100.0f);
        p->steadyLevel = RoundToInt(p->steadyInput * 125.0f);
        if (p->vibrateTicks > 0)
            p->vibrateTicks--;
        else
            p->vibrateLevel = 0;
        int pulse = Clamp255(p->pulseLevel + p->vibrateLevel);
        p->pulseLevel = pulse;

        int steady;
        bool pulseOn;
        if (p->joltTicks > p->joltFadeTicks) {
            steady = Clamp255(p->steadyLevel + p->joltLevel);
            pulseOn = Dither(gFeedbackTick) < pulse + p->joltLevel;
            p->joltTicks--;
        } else if (p->joltTicks != 0) {
            steady = p->steadyLevel;
            int fading = p->joltLevel * p->joltTicks / p->joltFadeTicks;
            pulseOn = Dither(gFeedbackTick >> 1) < pulse + fading;
            p->joltTicks--;
        } else {
            p->joltLevel = 0;
            steady = p->steadyLevel;
            pulseOn = Dither(gFeedbackTick) < pulse;
        }

        if (p->rumbleTicks > 0)
            p->rumbleTicks--;
        else
            p->rumbleLevel = 0;
        steady = Clamp255(steady + p->rumbleLevel);

        if (pulseOn)
            SetMotors(i, kPulseSpeed);
        else if (steady != 0)
            SetMotors(i, uint16_t(steady * 255));
        else
            SetMotors(i, 0);
    }
    gFeedbackTick++;
}
