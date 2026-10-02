#include "Teleport.h"

#include "../actionhelpers.h"
#include "../game.h"
#include "../game/obj/Player.h"
#include "../../common/renderWindow.h"
#include "../../common/gfx/d3d9Backend.h"

#include <windows.h>
#include <stdio.h>

// ---------------------------------------------------------------------------------------------------------------
// A debug teleport and frame capture for the action engine, so that a test can look at one particular place
// in a level without someone walking there first. The driving engine's counterpart is
// src/driving/devtools/Teleport.cpp.
//
//  - F8 records where player 1 is and where they are looking: a "[teleport] level=0x... Teleport=x,y,z,yaw,pitch"
//    line in the log, appended to teleports_action.txt beside the executable, ready to paste into a replay
//    script (MenuProbe's "level" and "teleport" steps) or settings.
//  - F9 goes back to the place F8 last recorded, to check a recorded place shows what it should.
//  - F7 dumps the next frame through the backend's DumpEvery machinery (d3d9_dump_frame_N.bmp and its draw
//    trace d3d9_trace_N.log), "[teleport] dumping frame N" saying which.
//
// The teleport is the game's own: Player_StandAtNewPosition (0x000ac8b0) is what scripts use to put the player
// somewhere. It sets the position and rotation, clears movement and aim, stands the player on the floor under
// the position and relinks them into the right room. The view's pitch is BLData.pitchFromHorizontal, set after.
// ---------------------------------------------------------------------------------------------------------------

#define VK_F7_ 0x76
#define VK_F8_ 0x77
#define VK_F9_ 0x78
#define GA_ROOT_ 2

// AUTOGEN
undefined __cdecl Player_StandAtNewPosition(obj_tag * player, _VECTOR * param_2, _VECTOR * param_3, ushort param_4);

static bool GameWindowHasFocus(void) {
    HWND render = FindWindowA(NIGHTFIRE_RENDER_WINDOW_CLASS, NULL);
    if (render == NULL)
        return true;
    HWND foreground = GetForegroundWindow();
    return foreground != NULL && (foreground == render || foreground == GetAncestor(render, GA_ROOT_));
}

// A key that went down since the last tick, with the game in front
static bool KeyPressed(int virtualKey, bool *wasDown) {
    bool down = (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
    bool pressed = down && !*wasDown;
    *wasDown = down;
    return pressed && GameWindowHasFocus();
}

static obj_tag *Player1(void) {
    obj_tag *player = glb_players[0];
    if (player == NULL || player->extraObjectData == NULL)
        return NULL;
    return player;
}

bool ActionTeleport_Parse(const char *text, ActionPlace *place) {
    return sscanf(text, " %f , %f , %f , %f , %f", &place->x, &place->y, &place->z, &place->yaw, &place->pitch) == 5;
}

void ActionTeleport_To(const ActionPlace *place) {
    obj_tag *player = Player1();
    if (player == NULL) {
        printf("[teleport] no player to move\n");
        return;
    }
    _VECTOR position = {place->x, place->y, place->z};
    _VECTOR rotation = {0.0f, place->yaw, 0.0f};
    Player_StandAtNewPosition(player, &position, &rotation, MovementType_Walk);
    BLData *bl = (BLData *)player->extraObjectData;
    bl->pitchFromHorizontal = place->pitch;
    bl->aimAutoLevelState = 1;   // or the view eases back to level (see Player.cpp's mouse look)
    printf("[teleport] moved to %.3f,%.3f,%.3f facing %.4f, pitch %.4f\n", place->x, place->y, place->z, place->yaw,
           place->pitch);
    fflush(stdout);
}

static ActionPlace ReadPlace(obj_tag *player) {
    BLData *bl = (BLData *)player->extraObjectData;
    ActionPlace place;
    place.level = (unsigned)GameState.CurrentLevelHashcode;
    place.x = player->position.x;
    place.y = player->position.y;
    place.z = player->position.z;
    place.yaw = player->rotation.y;
    place.pitch = bl->pitchFromHorizontal;
    return place;
}

void ActionTeleport_Tick(void) {
    static ActionPlace recorded;
    static bool haveRecorded = false;
    static bool f7Down = false, f8Down = false, f9Down = false;

    if (KeyPressed(VK_F8_, &f8Down)) {
        obj_tag *player = Player1();
        if (player != NULL) {
            recorded = ReadPlace(player);
            haveRecorded = true;
            char line[192];
            snprintf(line, sizeof(line), "level=0x%08x Teleport=%.3f,%.3f,%.3f,%.4f,%.4f", recorded.level, recorded.x,
                     recorded.y, recorded.z, recorded.yaw, recorded.pitch);
            printf("[teleport] %s\n", line);
            FILE *file = fopen("teleports_action.txt", "a");
            if (file != NULL) {
                fprintf(file, "%s\n", line);
                fclose(file);
            }
            fflush(stdout);
        }
    }
    if (KeyPressed(VK_F9_, &f9Down)) {
        if (!haveRecorded)
            printf("[teleport] nothing recorded yet (F8 records)\n");
        else if (recorded.level != (unsigned)GameState.CurrentLevelHashcode)
            printf("[teleport] the recorded place is in level 0x%08x, not this one\n", recorded.level);
        else
            ActionTeleport_To(&recorded);
        fflush(stdout);
    }
    if (KeyPressed(VK_F7_, &f7Down)) {
        printf("[teleport] dumping frame %u\n", D3D9_RequestDump());
        fflush(stdout);
    }
}
