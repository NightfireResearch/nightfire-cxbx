#ifndef DRIVING_GAME_AIVEHICLECONTROLLER_H_
#define DRIVING_GAME_AIVEHICLECONTROLLER_H_

// ---------------------------------------------------------------------------------------------------------------
// AIVehicleController (a one-byte singleton): the AI's vehicles. It holds the AI of the two player cars and the
// twenty traffic cars (static arrays the static initialisers construct), and those of the agent cars and
// helicopters (arrays Init makes, as many as the AI elements declare). The Simulation's cars register with it by
// car class (RegisterSimCar), the AI elements' helicopters as they wake; UpdateVehicles runs them all each step -
// the player cars and helicopters every step, one agent car in full a step, a traffic car every eighth step. It
// also makes the track's traffic cars and times the traffic lights. See AIVehicleController.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "AIVehicle.h"
#include "../../helpers.h"

class AIHelicopter;
struct PHelicopter;
struct PVehicle;

constexpr int kPlayerGroundVehicles = 2;
constexpr int kTrafficVehicles = 20;
constexpr int kTrafficCarTypes = 7;

class AIVehicleController {
public:
    uint8_t unused;

    static AIVehicleController* Get();                                                          // 0x00035c00
    // The controller, and the agent cars' and helicopters' arrays
    static void Init();                                                                         // 0x00035c10
    static void Reset();            // every vehicle's Reset, and inactive                      // 0x000363f0
    static void Shutdown();                                                                     // 0x000364d0

    AIGroundVehicle* GetTrafficVehicle(int index);                                              // 0x00035e50
    AIGroundVehicle* GetPlayerGroundVehiclePtr(int player);                                     // 0x00035e70
    // The agent car driving the physics object, NULL if none
    AIGroundVehicle* FindAgentGroundVehiclePtr(PhysicsObject *vehicle);                         // 0x00035e90
    AIHelicopter* GetHelicopter(int index);                                                     // 0x00035ef0

    void UpdateAgentGroundVehicles();                                                           // 0x00035f10
    void UpdateTrafficVehicles();                                                               // 0x00035f70
    void UpdateHelicopters();                                                                   // 0x00035fb0
    void UpdateVehicles();                                                                      // 0x00035ff0
    // Whether the lights at the position are green for `phase` of `phases`: never with a moving path near it, nor
    // in the last 150 steps of a cycle
    bool IntersectionGreenLight(const Coord3 *position, uint32_t phase, uint32_t phases);       // 0x00036060
    // The first inactive agent car or helicopter (with none, the count as a pointer: see the .cpp)
    AIGroundVehicle* ApproveAdditionalAgentGroundVehicleSpawn();                                // 0x00036140
    AIHelicopter* ApproveAdditionalHelicopterSpawn();                                           // 0x00036180

    // The car under the AI of a free vehicle of its kind
    void RegisterPlayerGroundVehicle(PVehicle *car);                                            // 0x000361c0
    void RegisterAgentGroundVehicle(PVehicle *car);                                             // 0x00036270
    void RegisterTrafficVehicle(PVehicle *car);                                                 // 0x000362e0
    // ... by its car class
    void RegisterSimCar(PVehicle *car);                                                         // 0x000365a0
    void DeRegisterSimCar(PVehicle *car);                                                       // 0x000365f0
    void RegisterHelicopter(PHelicopter *helicopter);                                           // 0x00036660
    void DeRegisterHelicopter(PHelicopter *helicopter);                                         // 0x000366a0
    // The track's traffic: MAX_TRAFFIC cars of NUM_TRAFFIC_TYPES_XBOX types (their models loaded first if asked)
    void CreateSimTrafficCars(bool preload);                                                    // 0x000366e0
    // RegisterSimCar for each of the Simulation's cars
    void RegisterSimCarList();                                                                  // 0x00036990
};
static_assert(sizeof(AIVehicleController) == 1, "Init news one byte for the controller");

// The controller's statics (the names are ours)
extern AIVehicleController *AIVehicleControllerInstance;
extern AIGroundVehicle *AgentGroundVehicles;    // new[]'d by Init, one per agent car of the AI elements
extern AIHelicopter *AIHelicopters;             // ... per helicopter
extern uint32_t GreenLightCycle;                // counts up every 300 steps
extern int32_t NextGreenLightStep;
extern int32_t AgentUpdateTurn;                 // the agent car updated in full this step
extern int16_t TrafficCarTypes[8];              // each traffic car type's name (CreateSimTrafficCars), chosen once
extern int16_t TrafficCarTypesChosen;

#define PlayerGroundVehicles ((AIGroundVehicle *)0x001de940)   // [kPlayerGroundVehicles]
#define TrafficGroundVehicles ((AIGroundVehicle *)0x001dee20)  // [kTrafficVehicles]

// The traffic: the cars allowed (the launch page's, then EChangeMaxTraffic's) and the cars spawned (ESpawnTraffic's
// count; AIGroundVehicle::UnSpawn takes one off, reaching it through Get()). The names are ours.
extern int16_t MaxTraffic;
extern int16_t TrafficSpawnCount;

#endif // DRIVING_GAME_AIVEHICLECONTROLLER_H_
