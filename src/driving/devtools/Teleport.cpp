#include "Teleport.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "../../common/renderWindow.h"
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

#define PLAYER_CAR_HANDLE   0x00234e40   // -> -> the player's PBondCar
#define SIMULATION          0x00233ff0   // Sim
#define MISSION_NUMBER      0x00244504   // from the launch data (1 paris, 2 uw_mis11, ... - see inject_driving.cpp)

typedef uint8_t *(__thiscall *GetRigidBodyFunc)(void *simulation, int rigidBodyId);
typedef void (__thiscall *PlaceRigidBodyFunc)(void *simulation, float *position, float *direction);
typedef void (__thiscall *ResetCarFunc)(void *car, float *position, float *direction);

static const GetRigidBodyFunc Simulation_GetRigidBody = (GetRigidBodyFunc)0x000b2700;
static const PlaceRigidBodyFunc Simulation_PlacePlayer = (PlaceRigidBodyFunc)0x000b2b50;

#define VK_F8_  0x77
#define VK_F9_  0x78
#define GA_ROOT_ 2

struct Place {
    float x, y, z;
    float dx, dy, dz;
};

static uint8_t *PlayerCar(void) {
    uint8_t **handle = *(uint8_t ***)PLAYER_CAR_HANDLE;
    return handle != NULL ? *handle : NULL;
}

static uint8_t *PlayerRigidBody(uint8_t *car) {
    return Simulation_GetRigidBody((void *)SIMULATION, *(int16_t *)(car + 0x4a));
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
    Simulation_PlacePlayer((void *)SIMULATION, position, direction);
    ResetCarFunc resetCar = *(ResetCarFunc *)(*(uint8_t **)car + 0xa8);
    resetCar(car, position, direction);
    float *velocity = (float *)(PlayerRigidBody(car) + 0x40);
    velocity[0] = velocity[1] = velocity[2] = 0.0f;
}

static bool GameWindowHasFocus(void) {
    HWND render = FindWindowA(NIGHTFIRE_RENDER_WINDOW_CLASS, NULL);
    if (render == NULL)
        render = FindWindowA("CxbxRender", NULL);
    if (render == NULL)
        return true;
    HWND foreground = GetForegroundWindow();
    return foreground != NULL && (foreground == render || foreground == GetAncestor(render, GA_ROOT_));
}

// A key that went down since the last tick, with the game in front.
static bool KeyPressed(int virtualKey, bool *wasDown) {
    bool down = (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
    bool pressed = down && !*wasDown;
    *wasDown = down;
    return pressed && GameWindowHasFocus();
}

static void FormatPlace(const Place *p, char *out, size_t size) {
    snprintf(out, size, "%.3f,%.3f,%.3f,%.4f,%.4f,%.4f", p->x, p->y, p->z, p->dx, p->dy, p->dz);
}

void Teleport_Tick(void) {
    static bool loaded = false;
    static Place configured, recorded;
    static bool haveConfigured = false, haveRecorded = false;
    static DWORD delayMs = 3000, dumpMs = 2000;
    static DWORD carSeenAt = 0, teleportedAt = 0;
    static bool autoDone = false, dumpDone = false;
    static bool f8Down = false, f9Down = false;

    if (!loaded) {
        loaded = true;
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
            printf("[teleport] will teleport to %s %lu ms after the car appears\n", formatted, delayMs);
        }
    }

    uint8_t *car = PlayerCar();
    if (car == NULL) {
        carSeenAt = 0;
        return;
    }
    DWORD now = GetTickCount();
    if (carSeenAt == 0)
        carSeenAt = now;

    if (KeyPressed(VK_F8_, &f8Down)) {
        ReadPlace(car, &recorded);
        haveRecorded = true;
        char formatted[128];
        FormatPlace(&recorded, formatted, sizeof(formatted));
        printf("[teleport] Teleport=%s   (mission %u)\n", formatted, *(uint32_t *)MISSION_NUMBER);
        FILE *f = fopen("teleports.txt", "a");
        if (f != NULL) {
            SYSTEMTIME t;
            GetLocalTime(&t);
            fprintf(f, "Teleport=%s   ; mission %u, recorded %04u-%02u-%02u %02u:%02u:%02u\n", formatted,
                    *(uint32_t *)MISSION_NUMBER, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
            fclose(f);
        }
    }

    if (KeyPressed(VK_F9_, &f9Down)) {
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
