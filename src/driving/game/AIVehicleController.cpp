#include "AIVehicleController.h"

#include <string.h>

#include "AIElements.h"                 // fgAIElementController
#include "AIHelicopter.h"
#include "BondCar.h"                    // CarClass
#include "Helicopter.h"                 // PHelicopter
#include "Simulation.h"                 // fgSimulation
#include "Vehicle.h"                    // PVehicle
#include "../../common/xbeOverload.h"   // XbeAddress, XbeVirtual
#include "../../helpers.h"
#include "../data/AttributeSet.h"
#include "../engine/CoreContainers.h"   // GameVector
#include "../engine/CoreFoundation.h"   // AssertMessage
#include "../engine/SimRandom.h"
#include "../engine/UMemory.hpp"
#include "../physics/PhysicsObject.h"
#include "../platform/RealMath.h"
#include "../render/PathEngine.h"       // RPathEngine
#include "../render/RSceneObj.hpp"      // RSceneObj::PreLoad
#include "../world/RoadNav.h"
#include "../world/RoadNetwork.h"       // fgRoadNetworkData
#include "../world/World.h"             // fgWorld
#include "../platform/CRuntimeStd.h"

// XBE_GLOBAL(0x001de914, 0x2)
int16_t MaxTraffic;
// XBE_GLOBAL(0x001de918, 0x2)
int16_t TrafficSpawnCount;

// ---------------------------------------------------------------------------------------------------------------
// AIVehicleController (0x00035c00-0x00036a00, without AIGroundVehicle's and AIHelicopter's vector deleting
// destructors at 0x00035d90 and 0x00035df0), ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's code not ported yet

// ---- globals

// XBE_GLOBAL(0x001de8f0, 4)
uint32_t GreenLightCycle;
// XBE_GLOBAL(0x001de8f4, 4)
int32_t NextGreenLightStep;
// XBE_GLOBAL(0x001de8f8, 0x10)
int16_t TrafficCarTypes[8];
// XBE_GLOBAL(0x001de908, 4)
AIHelicopter *AIHelicopters;
// XBE_GLOBAL(0x001de90c, 4)
AIGroundVehicle *AgentGroundVehicles;
// XBE_GLOBAL(0x001de910, 4)
AIVehicleController *AIVehicleControllerInstance;
// XBE_GLOBAL(0x001de920, 2)
int16_t TrafficCarTypesChosen;
// XBE_GLOBAL(0x001de940, 0x4e0)
alignas(16) AIGroundVehicle PlayerGroundVehicles[kPlayerGroundVehicles];
// XBE_GLOBAL(0x001dee20, 0x30c0)
alignas(16) AIGroundVehicle TrafficGroundVehicles[kTrafficVehicles];
// XBE_GLOBAL(0x001e1ee0, 4)
int32_t AgentUpdateTurn;

namespace {

// new[] keeps the element count in the first word of the 16 bytes before the array
constexpr int kArrayCookieWords = 4;
constexpr uint32_t kArrayCookieSize = 0x10;

constexpr int32_t kGreenLightSteps = 300;   // a traffic light cycle
constexpr int32_t kAmberSteps = 150;        // the lights are red for everyone in a cycle's last steps
constexpr float kPathClearHeight = 5.0f;
constexpr float kPathClearDistanceSquared = 2500.0f;

// AIGroundVehicle's modes and laneSpan as the Register functions set them (neutral names)
constexpr uint8_t kGroundE8Car = 3;
constexpr uint8_t kGroundE8Big = 4;         // the traffic types "bus", "truck", "tnkr" and "flatb"

constexpr int32_t kDefaultMaxTraffic = 20;

void SetAIGroundVehicle(PVehicle *car, AIGroundVehicle *vehicle) {
    car->SetAIGroundVehicle(vehicle);
}

AIGroundVehicle *GetAIGroundVehiclePtr(PVehicle *car) {
    return car->GetAIGroundVehiclePtr();
}

} // namespace

// ---- the controller and its vehicles

// FUNC_AT(0x00035c00)
AIVehicleController* AIVehicleController::Get() {
    return AIVehicleControllerInstance;
}

// FUNC_AT(0x00035c10)
void AIVehicleController::Init() {
    if (AIVehicleControllerInstance == NULL)
        AIVehicleControllerInstance = static_cast<AIVehicleController *>(OperatorNew(sizeof(AIVehicleController)));

    int count = fgAIElementController->carSlots;
    int32_t *block = static_cast<int32_t *>(OperatorNewArray(count * sizeof(AIGroundVehicle) + kArrayCookieSize));
    AIGroundVehicle *agents = NULL;
    if (block != NULL) {
        block[0] = count;
        agents = reinterpret_cast<AIGroundVehicle *>(block + kArrayCookieWords);
        Crt::EhVectorConstructor(agents, sizeof(AIGroundVehicle), count,
                                 reinterpret_cast<Crt::ElementFn>(XbeAddress(&AIGroundVehicle::Construct)),
                                 reinterpret_cast<Crt::ElementFn>(XbeAddress(&AIGroundVehicle::Destruct)));
    }
    AgentGroundVehicles = agents;

    count = fgAIElementController->helicopterSlots;
    block = static_cast<int32_t *>(OperatorNewArray(count * sizeof(AIHelicopter) + kArrayCookieSize));
    AIHelicopter *helicopters = NULL;
    if (block != NULL) {
        block[0] = count;
        helicopters = reinterpret_cast<AIHelicopter *>(block + kArrayCookieWords);
        Crt::EhVectorConstructor(helicopters, sizeof(AIHelicopter), count,
                                 reinterpret_cast<Crt::ElementFn>(XbeAddress(&AIHelicopter::Construct)),
                                 reinterpret_cast<Crt::ElementFn>(XbeAddress(&AIHelicopter::Destruct)));
    }
    AIHelicopters = helicopters;

    NextGreenLightStep = fgSimulation.stepCount + kGreenLightSteps;
    GreenLightCycle = 0;
    for (int i = 0; i < kPlayerGroundVehicles; i++)
        PlayerGroundVehicles[i].index = i;
    for (int i = 0; i < fgAIElementController->carSlots; i++)
        AgentGroundVehicles[i].index = i;
    for (int i = 0; i < fgAIElementController->helicopterSlots; i++)
        AIHelicopters[i].index = i;
}

// FUNC_AT(0x00035e50)
AIGroundVehicle* AIVehicleController::GetTrafficVehicle(int index) {
    return &TrafficGroundVehicles[index];
}

// FUNC_AT(0x00035e70)
AIGroundVehicle* AIVehicleController::GetPlayerGroundVehiclePtr(int player) {
    return &PlayerGroundVehicles[player];
}

// FUNC_AT(0x00035e90)
AIGroundVehicle* AIVehicleController::FindAgentGroundVehiclePtr(PhysicsObject *vehicle) {
    for (int i = 0; i < fgAIElementController->carSlots; i++) {
        AIGroundVehicle *agent = &AgentGroundVehicles[i];
        if (agent != NULL && agent->GetPhysicsObject() == vehicle)
            return agent;
    }
    return NULL;
}

// FUNC_AT(0x00035ef0)
AIHelicopter* AIVehicleController::GetHelicopter(int index) {
    return &AIHelicopters[index];
}

// ---- the updates

// FUNC_AT(0x00035f10)
void AIVehicleController::UpdateAgentGroundVehicles() {
    // A division by zero without agent cars, as in the original
    AgentUpdateTurn = (AgentUpdateTurn + 1) % fgAIElementController->carSlots;
    for (int i = 0; i < fgAIElementController->carSlots; i++) {
        AgentGroundVehicles[i].skipFullUpdate = AgentUpdateTurn != i;
        AgentGroundVehicles[i].CallUpdate();
    }
}

// FUNC_AT(0x00035f70)
void AIVehicleController::UpdateTrafficVehicles() {
    for (int i = 0; i < kTrafficVehicles; i++) {
        if ((fgSimulation.stepCount & 7) == (i & 7)) {
            TrafficGroundVehicles[i].skipFullUpdate = 0;
            TrafficGroundVehicles[i].CallUpdate();
        }
    }
}

// FUNC_AT(0x00035fb0)
void AIVehicleController::UpdateHelicopters() {
    for (int i = 0; i < fgAIElementController->helicopterSlots; i++)
        AIHelicopters[i].CallUpdate();
}

// FUNC_AT(0x00035ff0)
void AIVehicleController::UpdateVehicles() {
    if (NextGreenLightStep < fgSimulation.stepCount) {
        NextGreenLightStep = fgSimulation.stepCount + kGreenLightSteps;
        GreenLightCycle++;
    }
    for (int i = 0; i < kPlayerGroundVehicles; i++)
        PlayerGroundVehicles[i].CallUpdate();
    AIVehicleControllerInstance->UpdateAgentGroundVehicles();
    AIVehicleControllerInstance->UpdateTrafficVehicles();
    AIVehicleControllerInstance->UpdateHelicopters();
}

// FUNC_AT(0x00036060)
bool AIVehicleController::IntersectionGreenLight(const Coord3 *position, uint32_t phase, uint32_t phases) {
    Coord4 at;
    *Xyz(&at) = *position;
    for (RPathHandle *path = RPathEngine::GetFirstPathHandle(); path != NULL;
         path = RPathEngine::GetNextPathHandle()) {
        Coord4 pathAt;
        *Xyz(&pathAt) = *path->GetPosition();
        // The original's fabs takes the comparison, not the height difference
        if (double(at.y) - pathAt.y < kPathClearHeight && VU0_v3distancesquare(&at, &pathAt) < kPathClearDistanceSquared)
            return false;
    }
    return fgSimulation.stepCount <= NextGreenLightStep - kAmberSteps && GreenLightCycle % phases == phase;
}

// ---- registering the cars

// FUNC_AT(0x00036140)
AIGroundVehicle* AIVehicleController::ApproveAdditionalAgentGroundVehicleSpawn() {
    int count = fgAIElementController->carSlots;
    AIGroundVehicle *agents = AgentGroundVehicles;
    int i;
    for (i = 0; i < count; i++) {
        if (!agents[i].active)
            return &agents[i];
    }
    // The original's source returns nothing here, leaving the count in EAX
    return reinterpret_cast<AIGroundVehicle *>(uintptr_t(i));
}

// FUNC_AT(0x00036180)
AIHelicopter* AIVehicleController::ApproveAdditionalHelicopterSpawn() {
    int count = fgAIElementController->helicopterSlots;
    AIHelicopter *helicopters = AIHelicopters;
    int i;
    for (i = 0; i < count; i++) {
        if (!helicopters[i].active)
            return &helicopters[i];
    }
    // As above: the count, as a pointer
    return reinterpret_cast<AIHelicopter *>(uintptr_t(i));
}

// FUNC_AT(0x000361c0)
void AIVehicleController::RegisterPlayerGroundVehicle(PVehicle *car) {
    int i;
    for (i = 0; i < kPlayerGroundVehicles; i++) {
        if (!PlayerGroundVehicles[i].active)
            break;
    }
    // With no free one the original takes the car itself for its AI vehicle
    AIGroundVehicle *vehicle =
        i < kPlayerGroundVehicles ? &PlayerGroundVehicles[i] : reinterpret_cast<AIGroundVehicle *>(car);
    vehicle->SetPhysicsObject(car);
    SetAIGroundVehicle(car, vehicle);
    vehicle->vehicleType = kAIGroundVehicle;
    vehicle->type = kAIGroundPlayer;
    vehicle->active = true;
    vehicle->commanded = 1;
    vehicle->SetAttackMode(kGroundAttackMove);
    vehicle->SetNavigateMode(kGroundNavigateNone, NULL);
    vehicle->target = NULL;
    vehicle->laneSpan = kGroundE8Car;
    vehicle->driveToNav->mode = 0;
    vehicle->isSub = uint8_t(car->GetPhysics()->isSub);
}

// FUNC_AT(0x00036270)
void AIVehicleController::RegisterAgentGroundVehicle(PVehicle *car) {
    AIGroundVehicle *vehicle = ApproveAdditionalAgentGroundVehicleSpawn();
    vehicle->SetPhysicsObject(car);
    SetAIGroundVehicle(car, vehicle);
    vehicle->vehicleType = kAIGroundVehicle;
    vehicle->type = kAIGroundAgent;
    vehicle->active = true;
    vehicle->commanded = 0;
    vehicle->laneSpan = kGroundE8Car;
    vehicle->hasTurret = car->attributes.LookupBool("HAS_TURRET", NULL);
    vehicle->isSub = uint8_t(car->GetPhysics()->isSub);
}

// FUNC_AT(0x000362e0)
void AIVehicleController::RegisterTrafficVehicle(PVehicle *car) {
    int i;
    for (i = 0; i < kTrafficVehicles; i++) {
        if (!TrafficGroundVehicles[i].active)
            break;
    }
    // As RegisterPlayerGroundVehicle: the car itself when none is free
    AIGroundVehicle *vehicle =
        i < kTrafficVehicles ? &TrafficGroundVehicles[i] : reinterpret_cast<AIGroundVehicle *>(car);
    vehicle->SetPhysicsObject(car);
    SetAIGroundVehicle(car, vehicle);
    if (!fgRoadNetworkData.loaded)
        return;
    vehicle->vehicleType = kAIGroundVehicle;
    vehicle->type = kAIGroundTraffic;
    vehicle->active = true;
    vehicle->commanded = 1;
    vehicle->target = NULL;
    if (_stricmp(car->GetCarType(), "bus") == 0 || _stricmp(car->GetCarType(), "truck") == 0 ||
        _stricmp(car->GetCarType(), "tnkr") == 0 || _stricmp(car->GetCarType(), "flatb") == 0)
        vehicle->laneSpan = kGroundE8Big;
    else
        vehicle->laneSpan = kGroundE8Car;
    vehicle->UnSpawn();
}

// FUNC_AT(0x000363f0)
void AIVehicleController::Reset() {
    NextGreenLightStep = fgSimulation.stepCount + kGreenLightSteps;
    GreenLightCycle = 0;
    for (int i = 0; i < kPlayerGroundVehicles; i++) {
        PlayerGroundVehicles[i].CallReset();
        PlayerGroundVehicles[i].active = false;
    }
    for (int i = 0; i < fgAIElementController->carSlots; i++) {
        AgentGroundVehicles[i].CallReset();
        AgentGroundVehicles[i].active = false;
    }
    for (int i = 0; i < kTrafficVehicles; i++) {
        TrafficGroundVehicles[i].CallReset();
        TrafficGroundVehicles[i].active = false;
    }
    for (int i = 0; i < fgAIElementController->helicopterSlots; i++) {
        AIHelicopters[i].CallReset();
        AIHelicopters[i].active = false;
    }
}

// The agent cars' and helicopters' arrays are destroyed element by element and never freed, as in the original.
// FUNC_AT(0x000364d0)
void AIVehicleController::Shutdown() {
    for (int i = 0; i < kPlayerGroundVehicles; i++)
        PlayerGroundVehicles[i].Destruct();
    for (int i = 0; i < fgAIElementController->carSlots; i++)
        AgentGroundVehicles[i].CallDelete(0);
    for (int i = 0; i < kTrafficVehicles; i++)
        TrafficGroundVehicles[i].Destruct();
    for (int i = 0; i < fgAIElementController->helicopterSlots; i++)
        AIHelicopters[i].CallDelete(0);
    if (AIVehicleControllerInstance != NULL)
        OperatorDelete(AIVehicleControllerInstance);
    AIVehicleControllerInstance = NULL;
}

// FUNC_AT(0x000365a0)
void AIVehicleController::RegisterSimCar(PVehicle *car) {
    switch (car->GetCarClass()) {
    case kPlayerCar:
        RegisterPlayerGroundVehicle(car);
        break;
    case kCarClass2:
        RegisterAgentGroundVehicle(car);
        break;
    case kCarClass3:
        RegisterTrafficVehicle(car);
        break;
    }
}

// FUNC_AT(0x000365f0)
void AIVehicleController::DeRegisterSimCar(PVehicle *car) {
    switch (car->GetCarClass()) {
    case kPlayerCar:
    case kCarClass2:
    case kCarClass3:
        GetAIGroundVehiclePtr(car)->active = false;
        GetAIGroundVehiclePtr(car)->commanded = 0;
        GetAIGroundVehiclePtr(car)->CallReset();
        break;
    }
}

// FUNC_AT(0x00036660)
void AIVehicleController::RegisterHelicopter(PHelicopter *helicopter) {
    AIHelicopter *vehicle = ApproveAdditionalHelicopterSpawn();
    vehicle->SetPhysicsObject(helicopter);
    helicopter->ai = vehicle;
    vehicle->vehicleType = kAIHelicopter;
    vehicle->active = true;
    if (!helicopter->proximityDestructEnabled)
        vehicle->EnableTargetBeacon(0);
}

// FUNC_AT(0x000366a0)
void AIVehicleController::DeRegisterHelicopter(PHelicopter *helicopter) {
    AIHelicopter *vehicle = helicopter->ai;
    vehicle->DisableTargetBeacon();
    vehicle->active = false;
    vehicle->commanded = 0;
    vehicle->CallReset();
}

// ---- the track's traffic

// FUNC_AT(0x000366e0)
void AIVehicleController::CreateSimTrafficCars(bool preload) {
    const char *names[kTrafficCarTypes] = {
        "coupe", "sedan2", "hatchback", "sedan", "station_wagon", "suv1", "taxi",
    };
    bool typesFound = false;
    bool maxFound = false;
#if NIGHTFIRE_DRIVING_DATA_GC
    int typeCount = fgWorld->attributes.LookupInt("NUM_TRAFFIC_TYPES_GC", &typesFound);   // the GameCube's key
#else
    int typeCount = fgWorld->attributes.LookupInt("NUM_TRAFFIC_TYPES_XBOX", &typesFound);
#endif
    int maxTraffic = fgWorld->attributes.LookupInt("MAX_TRAFFIC", &maxFound);
    if (!maxFound)
        maxTraffic = kDefaultMaxTraffic;
    if (!typesFound)
        typeCount = uint32_t(maxTraffic) < kTrafficCarTypes ? maxTraffic : kTrafficCarTypes;

    if (TrafficCarTypesChosen == 0) {
        for (int i = 0; i < typeCount; i++) {
            if (uint32_t(typeCount) < kTrafficCarTypes)
                TrafficCarTypes[i] = int16_t(int(fgSimulation.random->Generate()) * kTrafficCarTypes >> 16);
            else
                TrafficCarTypes[i] = int16_t(i);
        }
        TrafficCarTypesChosen = 1;
    }

    if (preload) {
        for (int i = 0; i < typeCount; i++) {
            AttributeSet attributes;
            const char *model =
                PVehicle::RenderNameAttrib(PVehicle::GetNamedAttribs(&attributes, names[TrafficCarTypes[i]]));
            attributes.Destruct();
            if (model == NULL)
                AssertMessage("Could not find attribute rendername for %s", names[TrafficCarTypes[i]]);
            RSceneObj::PreLoad("data\\car\\model\\", model, names[TrafficCarTypes[i]]);
        }
    }

    if (!fgRoadNetworkData.loaded)
        return;
    for (int i = 0; i < maxTraffic; i++) {
        const char *name = names[TrafficCarTypes[i % typeCount]];
        AttributeSet attributes;
        uint32_t colours = PVehicle::NumColoursAttrib(PVehicle::GetNamedAttribs(&attributes, name));
        uint32_t variations = colours < 1 ? 1 : colours;
        attributes.Destruct();
        uint32_t colour = int(fgSimulation.random->Generate()) * int(variations) >> 16;
        Coord3 direction = {0.0f, 0.0f, 0.0f};
        Coord3 position = {0.0f, 0.0f, 0.0f};
        fgSimulation.SpawnCarObject(kCarClass3, name, colour, &direction, &position);
    }
}

// FUNC_AT(0x00036990)
void AIVehicleController::RegisterSimCarList() {
    if (fgSimulation.cars.first == NULL || fgSimulation.cars.last - fgSimulation.cars.first == 0)
        return;
    for (PVehicle **car = fgSimulation.cars.first; car != fgSimulation.cars.last; car++)
        RegisterSimCar(*car);
}
