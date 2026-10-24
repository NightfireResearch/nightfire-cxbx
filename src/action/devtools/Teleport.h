#ifndef ACTION_DEVTOOLS_TELEPORT_H_
#define ACTION_DEVTOOLS_TELEPORT_H_

// A debug teleport and frame capture for the action engine's player. See Teleport.cpp.

// A place: the level it is in, the player's position, and where they are looking
struct ActionPlace {
    unsigned level;        // the level's hashcode
    float x, y, z;
    float yaw;             // radians, the player object's rotation.y
    float pitch;           // BLData.pitchFromHorizontal: -1 straight down .. 1 straight up
};

// Called once per Game_Run, on the game thread: F8 records the place, F9 goes back to it, F7 dumps the frame
void ActionTeleport_Tick(void);

// Parses "x,y,z,yaw,pitch" (the level is not part of it); false if it is not that
bool ActionTeleport_Parse(const char *text, ActionPlace *place);

// Puts player 1 at the place (in the current level), as the game's own Player_StandAtNewPosition does
void ActionTeleport_To(const ActionPlace *place);

#endif // ACTION_DEVTOOLS_TELEPORT_H_
