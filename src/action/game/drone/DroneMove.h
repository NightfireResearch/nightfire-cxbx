#ifndef DRONEMOVE_H_
#define DRONEMOVE_H_

#include "Drone.h"
#include "../../../common/xbeClass.h"
#include "../../math/X87Ops.h"

// How the drones move (DroneMove.cpp): turning towards where they go while steering round each other and the players,
// the shots' push, floating in zero gravity, which ways are blocked, and running from what scares them.

#pragma pack(push, 1)

// One of the two movers NDrone2_EvaluateObstacle tests (0x30 bytes; our name)
typedef struct SteerBody {
    uint flags;             // 0x00 - 2: standing still (set by NDrone2_EvaluateObstacle; nothing clears it)
    _VECTOR *pos;           // 0x04
    _VECTOR *rot;           // 0x08 - only .y is read
    float speed;            // 0x0c - how far it moves a frame
    float radius;           // 0x10
    float unknown14;
    _VECTOR vel;            // 0x18 - this frame's step
    _VECTOR contact;        // 0x24 - where it is when the two meet
} SteerBody;

// NDrone2_Steer's test of one obstacle, on its stack (0x90 bytes)
typedef struct ObstacleEval_tag {
    SteerBody self;         // 0x00 - the drone that steers
    SteerBody *other;       // 0x30 - the obstacle
    char _unknown34[0x60 - 0x34];
    float radiusSum;        // 0x60
    float share;            // 0x64 - its part of the turn: its speed over both
    float steer;            // 0x68 - the turn it asks for, away from the obstacle
    float weight;           // 0x6c - 1 at contact, less the further off it is
    float t;                // 0x70 - frames to contact
    float limit;            // 0x74 - how fast they close
    float dist;             // 0x78 - apart at contact
    _VECTOR delta;          // 0x7c - from its contact point to the obstacle's (y = 0)
    float unknown88;
    float angle;            // 0x8c - the obstacle's bearing off its heading
} ObstacleEval_tag;

#pragma pack(pop)

LAYOUT_32(sizeof(SteerBody) == 0x30, "SteerBody is 0x30 bytes");
LAYOUT_32(offsetof(SteerBody, vel) == 0x18, "Wrong offset for SteerBody.vel");
LAYOUT_32(sizeof(ObstacleEval_tag) == 0x90, "ObstacleEval_tag is 0x90 bytes");
LAYOUT_32(offsetof(ObstacleEval_tag, radiusSum) == 0x60, "Wrong offset for radiusSum");
LAYOUT_32(offsetof(ObstacleEval_tag, t) == 0x70, "Wrong offset for t");
LAYOUT_32(offsetof(ObstacleEval_tag, delta) == 0x7c, "Wrong offset for delta");
LAYOUT_32(offsetof(ObstacleEval_tag, angle) == 0x8c, "Wrong offset for angle");

// Each frame (NDrone2_ControlSTANDARD)
void NDrone2_Move(DCVars_tag *dcv);
void NDrone2_Steer(DCVars_tag *dcv, float angle);
bool DroneMove_NoBunching(DCVars_tag *dcv);
void DroneMove_SetBoundryFlags(DCVars_tag *dcv);

// The obstacle test
bool NDrone2_EvaluateObstacle(Drone_tag *drone, Drone_tag *other, ObstacleEval_tag *eval);
bool SphereSphereSweep(float radius1, _VECTOR *from1, _VECTOR *to1, float radius2, _VECTOR *from2, _VECTOR *to2,
                       float *first, float *second);
bool SolveQuadratic(float a, float b, float c, float *root1, float *root2);

// Turning and routes
void DroneMove_TurnToObj(DCVars_tag *dcv, obj_tag *obj, float offset, float tolerance);
// Returns in ST0, unrounded
double DroneMove_NextRouteAngle(DCVars_tag *dcv);
Extended DroneMove_RouteTurn(DCVars_tag *dcv);
float DroneMove_DistanceToNextRouteNode(DCVars_tag *dcv);
// Returns in ST0, unrounded
double DroneMove_ObjectMissionPathDistance(DCVars_tag *dcv, obj_tag *obj, float radius);
short NDrone2_NearestNodeDCV(DCVars_tag *dcv);
bool NDrone2_CanReachGoalPosition(DCVars_tag *dcv);

// Running from what scares them
bool DroneMove_FindSafetyFromScaryPosition(DCVars_tag *dcv, _VECTOR *pos, obj_tag *obj, float dist);
bool DroneMove_FindSafetyFromScaryObject(DCVars_tag *dcv);

// Zero gravity (DTYPE_Astronaut)
void DroneMove_AstronautCombat(DCVars_tag *dcv);
void DroneMove_AstronautAnim(DCVars_tag *dcv);
void DroneMove_AstronautHitMove(DCVars_tag *dcv);
void DroneMove_AstronautDeathMove(DCVars_tag *dcv);

#endif // DRONEMOVE_H_
