#include "Teleport.h"
#include "ShadowTicks.h"
#include "../engine/GameLoop.h"            // Launch
#include "../game/Simulation.h"
#include "../Scheduler.hpp"
#include "../game/MissionManager.h"
#include "../EventManager.hpp"
#include "../sound/snd/SndGlobals.h"
#include "SndCallTrace.h"
#include "HeapPad.h"
#include "X87Count.h"

#include "Win32Harness.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "../platform/HostInput.h"
#include "../platform/RealSystem.h"
#include "../../common/gfx/d3d9Backend.h"

// ---------------------------------------------------------------------------------------------------------------
// A debug teleport for the player's car, so that a graphics test can look at one particular place in a level
// without someone driving there first.
//
// Two halves, for the two halves of the job:
//
//  - finding the place, by hand. Drive there, point the car where the camera should look, and press F8: the
//    car's position and heading are printed as a "[teleport] Teleport=..." line and appended to
//    teleports.txt beside the executable, ready to paste. F9 puts the car back at the last place F8 recorded
//    (or at the configured one), which is how to check that a recorded place shows what it should.
//  - going there, unattended. Teleport=x,y,z,dx,dy,dz under [Settings] in settings.ini - or the environment
//    variable NIGHTFIRE_TELEPORT, which wins, so tools/drive_game.ps1 -Teleport can pass one without
//    editing the file - puts the car there TeleportDelayMs after it first exists (default 3000), and dumps
//    the frame TeleportDumpMs after that (default 2000; 0 dumps nothing) through the backend's DumpEvery
//    machinery. "[teleport] dumping frame N" in the log says which d3d9_dump_frame_N.bmp it is.
//
// The teleport itself is the game's own, not a write to the car's matrix: EResetPlayerCarPos is the event
// scripts send to put the player somewhere, its destructor (0x0004d4c0; events act in their destructors) does
// the work, and this does what that does. It calls
// Simulation::0x000b2b50 on the simulation (0x00233ff0), which finds the ground under the position and places
// the player's rigid body on it facing the direction, and then PBondCar::ResetCar(pos, dir) (0x000627c0,
// vtable slot 0xa8), which resets the car's own state - suspension, wheels, tyre tracks, the EMP effect - to
// match. The event then sets a velocity along the direction; this sets it to zero, so the car stays put.
// The height in a recorded place is therefore only a fallback: where there is ground under it, the game
// decides the height itself.
//
// Reading the place back: the car is **(0x00234e40) (the event's "playerPhysicsObject"), its rigid body is
// number *(short *)(car + 0x4a) in the simulation's array of 0x80-byte bodies, position at +0x10, and +0x5c
// points at its world matrix. That matrix is built the same way as the one 0x000bce90 makes from a
// direction for the teleport - right, up, forward in rows 0 to 2 - so row 2 is the heading to record.
//
// Run from Scheduler::Run, just before the game processes its own events, which is where
// EResetPlayerCarPos would have run: on the game thread, between simulation steps.
// ---------------------------------------------------------------------------------------------------------------

typedef void (__thiscall *ResetCarFunc)(void *car, float *position, float *direction);

struct Place {
    float x, y, z;
    float dx, dy, dz;
};

static uint8_t *PlayerCar(void) {
    PVehicle **cars = fgSimulation.cars.first;
    return cars != NULL ? reinterpret_cast<uint8_t *>(*cars) : NULL;
}

static uint8_t *PlayerRigidBody(uint8_t *car) {
    return reinterpret_cast<uint8_t *>(fgSimulation.GetRigidBody(*(int16_t *)(car + 0x4a)));
}

static bool ParsePlace(const char *text, Place *place) {
    return sscanf(text, " %f , %f , %f , %f , %f , %f", &place->x, &place->y, &place->z,
                  &place->dx, &place->dy, &place->dz) == 6;
}

static void ReadPlace(uint8_t *car, Place *place) {
    uint8_t *body = PlayerRigidBody(car);
    const float *position = (const float *)(body + 0x10);
    const float *matrix = *(const float **)(body + 0x5c);
    place->x = position[0]; place->y = position[1]; place->z = position[2];
    place->dx = matrix[8]; place->dy = matrix[9]; place->dz = matrix[10];
}

static void Teleport(uint8_t *car, const Place *place) {
    // The event keeps both vectors 16-byte aligned with w = 1, and so does this: the callees are the PS2
    // port's VU0 helpers, which may assume both.
    alignas(16) float position[4] = { place->x, place->y, place->z, 1.0f };
    alignas(16) float direction[4] = { place->dx, place->dy, place->dz, 1.0f };
    fgSimulation.SetStartConditions(reinterpret_cast<const Coord3 *>(position),
                                    reinterpret_cast<const Coord3 *>(direction));
    ResetCarFunc resetCar = *(ResetCarFunc *)(*(uint8_t **)car + 0xa8);
    resetCar(car, position, direction);
    float *velocity = (float *)(PlayerRigidBody(car) + 0x40);
    velocity[0] = velocity[1] = velocity[2] = 0.0f;
}

// A key that went down since the last tick, with the game in front.
static bool KeyPressed(HostKey key, bool *wasDown) {
    bool down = HostInput_KeyHeld(key);
    bool pressed = down && !*wasDown;
    *wasDown = down;
    return pressed && HostInput_WindowHasFocus();
}

static void FormatPlace(const Place *p, char *out, size_t size) {
    snprintf(out, size, "%.3f,%.3f,%.3f,%.4f,%.4f,%.4f", p->x, p->y, p->z, p->dx, p->dy, p->dz);
}

void Teleport_Tick(void) {
    SndCallTrace_Tick();   // NIGHTFIRE_SNDTRACE=1 only: marks the tick boundary in the sound call trace
    X87Count_Tick();       // NIGHTFIRE_X87COUNT=1 only
    HeapPad_Tick();        // NIGHTFIRE_PARTICLE_ORDER=1 only
    static bool loaded = false;
    static Place configured, recorded;
    static bool haveConfigured = false, haveRecorded = false;
    static DWORD delayMs = 3000, dumpMs = 2000;
    static DWORD carSeenAt = 0, teleportedAt = 0;
    static bool autoDone = false, dumpDone = false;
    static bool f8Down = false, f9Down = false;

    if (!loaded) {
        loaded = true;
        ShadowTicks_FirstTick();   // the shadow tests (devtools/ShadowTicks.cpp)
        char text[256] = "";
        DWORD fromEnv = GetEnvironmentVariableA("NIGHTFIRE_TELEPORT", text, sizeof(text));
        if (fromEnv == 0 || fromEnv >= sizeof(text))
            GetPrivateProfileStringA("Settings", "Teleport", "", text, sizeof(text), ".\\settings.ini");
        if (text[0] != 0) {
            haveConfigured = ParsePlace(text, &configured);
            if (!haveConfigured)
                printf("[teleport] could not read \"%s\" as x,y,z,dx,dy,dz - not teleporting\n", text);
        }
        delayMs = GetPrivateProfileIntA("Settings", "TeleportDelayMs", 3000, ".\\settings.ini");
        dumpMs = GetPrivateProfileIntA("Settings", "TeleportDumpMs", 2000, ".\\settings.ini");
        if (haveConfigured) {
            char formatted[128];
            FormatPlace(&configured, formatted, sizeof(formatted));
            printf("[teleport] will teleport to %s %lu ms after the car appears\n", formatted, (unsigned long)delayMs);
        }
    }
    ShadowTicks_Tick();    // the live vehicle shadows (devtools/ShadowTicks.cpp)

    // NIGHTFIRE_LOCKSTEP=1: exactly one simulation tick per rendered frame (Teleport_BeforeRun), so that a run's Nth
    // frame is the same whatever the host's timing - the frame dumps and traces of two builds can then be compared
    // exactly. With NIGHTFIRE_DUMP_TICKS=N (or a comma-separated list) the frame N ticks after the car appears is
    // dumped, the same frame in every run. NIGHTFIRE_DUMP_EVERY=N with NIGHTFIRE_DUMP_UNTIL=M dumps every Nth tick
    // up to M instead (50 ticks a second).
    static bool dumpsRead = false;
    static int ticksSinceCar = 0;
    static int dumpTicks[16], dumpTickCount = 0;
    static int dumpEvery = 0, dumpUntil = 0;
    if (!dumpsRead) {
        dumpsRead = true;
        char text[32] = "";
        char list[256] = "";
        if (GetEnvironmentVariableA("NIGHTFIRE_DUMP_TICKS", list, sizeof(list)))
            for (char *p = list; *p != 0 && dumpTickCount < 16; p++) {
                dumpTicks[dumpTickCount++] = atoi(p);
                while (*p != 0 && *p != ',')
                    p++;
                if (*p == 0)
                    break;
            }
        if (GetEnvironmentVariableA("NIGHTFIRE_DUMP_EVERY", text, sizeof(text)))
            dumpEvery = atoi(text);
        if (GetEnvironmentVariableA("NIGHTFIRE_DUMP_UNTIL", text, sizeof(text)))
            dumpUntil = atoi(text);
    }

    uint8_t *car = PlayerCar();
    if (car == NULL) {
        carSeenAt = 0;
        return;
    }
    DWORD now = GetTickCount();
    if (carSeenAt == 0)
        carSeenAt = now;
    for (int i = 0; i < dumpTickCount; i++)
        if (ticksSinceCar == dumpTicks[i]) {
            uint32_t frame = D3D9_RequestDump();
            printf("[teleport] dumping frame %u (d3d9_dump_frame_%u.bmp), %d ticks after the car appeared%s\n",
                   frame, frame, dumpTicks[i], i == dumpTickCount - 1 ? " - the last" : "");
        }
    if (dumpEvery > 0 && ticksSinceCar > 0 && ticksSinceCar % dumpEvery == 0 && ticksSinceCar <= dumpUntil) {
        uint32_t frame = D3D9_RequestDump();
        printf("[teleport] dumping frame %u (d3d9_dump_frame_%u.bmp), %d ticks after the car appeared%s\n",
               frame, frame, ticksSinceCar, ticksSinceCar + dumpEvery > dumpUntil ? " - the last" : "");
    }
    ticksSinceCar++;

    // NIGHTFIRE_GODMODE=1: the player's health (the hit points SetHitPointLoc points the player's object at) is put
    // back to what it was when the car appeared, every tick, so that long unattended runs cannot fail the mission.
    static int godMode = -1;
    static float fullHealth = 0.0f;
    if (godMode < 0) {
        char text[8] = "";
        godMode = GetEnvironmentVariableA("NIGHTFIRE_GODMODE", text, sizeof(text)) && text[0] != '0' ? 1 : 0;
    }
    if (godMode && glbMissionManager != NULL) {
        float &health = glbMissionManager->playerHealth;
        if (fullHealth <= 0.0f && health > 0.0f) {
            fullHealth = health;
            printf("[godmode] keeping the player's health at %g\n", fullHealth);
        }
        if (fullHealth > 0.0f && health < fullHealth)
            health = fullHealth;
    }

    if (KeyPressed(kKeyF8, &f8Down)) {
        ReadPlace(car, &recorded);
        haveRecorded = true;
        char formatted[128];
        FormatPlace(&recorded, formatted, sizeof(formatted));
        printf("[teleport] Teleport=%s   (mission %u)\n", formatted, Launch.missionNum);
        FILE *f = fopen("teleports.txt", "a");
        if (f != NULL) {
            SYSTEMTIME t;
            GetLocalTime(&t);
            fprintf(f, "Teleport=%s   ; mission %u, recorded %04u-%02u-%02u %02u:%02u:%02u\n", formatted,
                    Launch.missionNum, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
            fclose(f);
        }
    }

    if (KeyPressed(kKeyF9, &f9Down)) {
        const Place *target = haveRecorded ? &recorded : haveConfigured ? &configured : NULL;
        if (target == NULL) {
            printf("[teleport] F9: nowhere to go - press F8 first, or set Teleport= in settings.ini\n");
        } else {
            char formatted[128];
            FormatPlace(target, formatted, sizeof(formatted));
            printf("[teleport] F9: teleporting to %s\n", formatted);
            Teleport(car, target);
        }
    }

    if (haveConfigured && !autoDone && now - carSeenAt >= delayMs) {
        autoDone = true;
        teleportedAt = now;
        char formatted[128];
        FormatPlace(&configured, formatted, sizeof(formatted));
        printf("[teleport] teleporting to %s\n", formatted);
        Teleport(car, &configured);
        Place arrived;
        ReadPlace(car, &arrived);
        FormatPlace(&arrived, formatted, sizeof(formatted));
        printf("[teleport] arrived at %s\n", formatted);
    }

    // NIGHTFIRE_DUMP_MS=N without a teleport: dump the frame N ms after the car appears, wherever it is - for
    // effects on the car itself (brake lights with NIGHTFIRE_HOLD=brake, say) that need no particular place.
    static int dumpOnlyMs = -2;
    if (dumpOnlyMs == -2) {
        char text[32] = "";
        dumpOnlyMs = GetEnvironmentVariableA("NIGHTFIRE_DUMP_MS", text, sizeof(text)) ? atoi(text) : -1;
    }
    if (!haveConfigured && dumpOnlyMs >= 0 && !dumpDone && now - carSeenAt >= (DWORD)dumpOnlyMs) {
        dumpDone = true;
        uint32_t frame = D3D9_RequestDump();
        printf("[teleport] dumping frame %u (d3d9_dump_frame_%u.bmp)\n", frame, frame);
    }

    if (autoDone && !dumpDone && dumpMs != 0 && now - teleportedAt >= dumpMs) {
        dumpDone = true;
        uint32_t frame = D3D9_RequestDump();
        printf("[teleport] dumping frame %u (d3d9_dump_frame_%u.bmp)\n", frame, frame);
    }

    fflush(stdout);
}

static bool LockstepOn(void) {
    static int lockstep = -1;
    if (lockstep < 0) {
        char text[32] = "";
        lockstep = GetEnvironmentVariableA("NIGHTFIRE_LOCKSTEP", text, sizeof(text)) && text[0] != '0' ? 1 : 0;
        if (lockstep) {
            printf("[lockstep] one simulation tick per frame\n");
            fflush(stdout);
        }
    }
    return lockstep != 0;
}

// The scheduler's own one-tick mode, on before each call decides how many ticks to run. Scheduler::Run otherwise
// simulates as many ticks as the clock moved since its last call, which on the first call after the scheduler is
// made, and the first after an ERestart's Scheduler::Reset, is however long the host took to get there.
//
// Unpaced (Teleport_LockstepPresentEvery), the clock is taken off the 50 Hz timer and moved one tick here instead, so
// that every call simulates a tick without waiting for the timer. Its only other reader, RealClock_Div2Ticks, feeds
// ERenderFrame's LastRenderClock.
void Teleport_BeforeRun(Scheduler *scheduler) {
    if (!LockstepOn())
        return;
    scheduler->oneTickPerRun = 1;
    if (Teleport_LockstepPresentEvery() != 0) {
        TIMER_removetask(RealClock_InterruptHandler);   // again after any RealClock_Init
        RealClock_InterruptHandler();
    }
}

// The dashboard clock reads the host's local time, which no two runs share.
bool Teleport_LockstepLocalTime(SYSTEMTIME *time) {
    if (!LockstepOn())
        return false;
    *time = SYSTEMTIME{ 2002, 11, 2, 26, 12, 0, 0, 0 };   // year, month, weekday, day, 12:00:00.000
    return true;
}

// A lockstep run simulates one tick per frame whatever the time, and its frames are dumped from the back buffer
// before Present, so neither presenting a frame nor waiting for its time (the backend's pacing, the scheduler's 50 Hz
// clock: Teleport_BeforeRun) changes the run - only how long it takes.
int Teleport_LockstepPresentEvery(void) {
    static int every = -1;
    if (every < 0) {
        every = 0;
        if (LockstepOn()) {
            char text[16] = "";
            every = GetEnvironmentVariableA("NIGHTFIRE_PRESENT_EVERY", text, sizeof(text)) ? atoi(text) : 50;
            if (every < 0)
                every = 0;
            printf("[lockstep] %s\n", every == 0 ? "every frame presented and paced"
                                                : "frames not paced; every so many presented (NIGHTFIRE_PRESENT_EVERY)");
            fflush(stdout);
        }
    }
    return every;
}
