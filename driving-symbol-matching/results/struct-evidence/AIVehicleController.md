# AIVehicleController

FastAlloc/constructed sizes under its tag: None
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none

Xbox methods (24):
  0x35c00 undefined4 __stdcall Get(void)
  0x35c10 undefined Init(void)
  0x35e50 undefined GetTrafficVehicle(undefined4 param_1)
  0x35e70 undefined GetPlayerGroundVehiclePtr(undefined4 param_1)
  0x35e90 undefined FindAgentGroundVehiclePtr(undefined4 param_1)
  0x35ef0 undefined GetHelicopter(undefined4 param_1)
  0x35f10 undefined UpdateAgentGroundVehicles(void)
  0x35f70 undefined UpdateTrafficVehicles(void)
  0x35fb0 undefined UpdateHelicopters(void)
  0x35ff0 undefined __stdcall UpdateVehicles(void)
  0x36060 undefined IntersectionGreenLight(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0x36140 undefined ApproveAdditionalAgentGroundVehicleSpawn(void)
  0x36180 undefined ApproveAdditionalHelicopterSpawn(void)
  0x361c0 undefined RegisterPlayerGroundVehicle(undefined4 param_1)
  0x36270 undefined RegisterAgentGroundVehicle(undefined4 param_1)
  0x362e0 undefined RegisterTrafficVehicle(undefined4 param_1)
  0x363f0 void default Reset(void)
  0x364d0 undefined __stdcall Shutdown(void)
  0x365a0 undefined RegisterSimCar(undefined4 param_1)
  0x365f0 undefined DeRegisterSimCar(undefined4 param_1)
  0x36660 undefined RegisterHelicopter(undefined4 param_1)
  0x366a0 undefined DeRegisterHelicopter(undefined4 param_1)
  0x366e0 void __thiscall CreateSimTrafficCars(void * this, char param_1)
  0x36990 undefined RegisterSimCarList(void)

PS2 methods (41):
  0x13e678 AIVehicleController::Get
  0x13e688 AIVehicleController::Init
  0x13e840 AIVehicleController::Restart
  0x13ea08 AIVehicleController::Shutdown
  0x13eb78 AIVehicleController::GetPlayerGroundVehicle
  0x13eb90 AIVehicleController::GetAgentGroundVehicle
  0x13eba8 AIVehicleController::GetTrafficVehicle
  0x13ebc0 AIVehicleController::GetPlayerGroundVehiclePtr
  0x13ebd8 AIVehicleController::GetAgentGroundVehiclePtr
  0x13ebf0 AIVehicleController::GetTrafficVehiclePtr
  0x13ec08 AIVehicleController::FindAgentGroundVehiclePtr
  0x13ecb0 AIVehicleController::GetHelicopter
  0x13ecc8 AIVehicleController::GetHelicopterPtr
  0x13ece0 AIVehicleController::UpdatePlayerGroundVheicles
  0x13ed38 AIVehicleController::UpdateAgentGroundVehicles
  0x13ee30 AIVehicleController::UpdateTrafficVehicles
  0x13eec0 AIVehicleController::UpdateHelicopters
  0x13ef40 AIVehicleController::UpdateVehicles
  0x13efc8 AIVehicleController::IntersectionGreenLight
  0x13f118 AIVehicleController::RegisterSimCar
  0x13f1c8 AIVehicleController::RegisterSimCarList
  0x13f238 AIVehicleController::DeRegisterSimCar
  0x13f2e8 AIVehicleController::RegisterSimHeli
  0x13f308 AIVehicleController::DeRegisterSimHeli
  0x13f328 AIVehicleController::ApproveAdditionalPlayerGroundVehicleSpawn
  0x13f390 AIVehicleController::ApproveAdditionalAgentGroundVehicleSpawn
  0x13f420 AIVehicleController::ApproveAdditionalTrafficVehicleSpawn
  0x13f488 AIVehicleController::ApproveAdditionalHelicopterSpawn
  0x13f518 AIVehicleController::GetAgentSlotsAvailable
  0x13f598 AIVehicleController::GetHelicopterSlotsAvailable
  0x13f618 AIVehicleController::RegisterPlayerGroundVehicle
  0x13f6d0 AIVehicleController::DeRegisterPlayerGroundVehicle
  0x13f748 AIVehicleController::RegisterAgentGroundVehicle
  0x13f7f0 AIVehicleController::DeRegisterAgentGroundVehicle
  0x13f868 AIVehicleController::RegisterTrafficVehicle
  0x13f9b0 AIVehicleController::DeRegisterTrafficVehicle
  0x13fa28 AIVehicleController::RegisterHelicopter
  0x13fa90 AIVehicleController::DeRegisterHelicopter
  0x13fae0 AIVehicleController::CreateSimTrafficCars
  0x1400c0 AIVehicleController::fgController_global_ctors
  0x1400e0 AIVehicleController::fgController_global_dtors

Sheet rows:
  AIVehicleController::Get(void)
  AIVehicleController::Init(void)
  AIVehicleController::Restart(void)
  AIVehicleController::Shutdown(void)
  AIVehicleController::GetPlayerGroundVehicle(int)
  AIVehicleController::GetAgentGroundVehicle(int)
  AIVehicleController::GetTrafficVehicle(int)
  AIVehicleController::GetPlayerGroundVehiclePtr(int)
  AIVehicleController::GetAgentGroundVehiclePtr(int)
  AIVehicleController::GetTrafficVehiclePtr(int)
  AIVehicleController::FindAgentGroundVehiclePtr(PVehicle *)
  AIVehicleController::GetHelicopter(int)
  AIVehicleController::GetHelicopterPtr(int)
  AIVehicleController::UpdatePlayerGroundVehicles(void)
  AIVehicleController::UpdateAgentGroundVehicles(void)
  AIVehicleController::UpdateTrafficVehicles(void)
  AIVehicleController::UpdateHelicopters(void)
  AIVehicleController::UpdateVehicles(void)
  AIVehicleController::IntersectionGreenLight(COORD3 &, int, int)
  AIVehicleController::RegisterSimCar(PVehicle *)
  AIVehicleController::RegisterSimCarList(void)
  AIVehicleController::DeRegisterSimCar(PVehicle *)
  AIVehicleController::RegisterSimHeli(PHelicopter *)
  AIVehicleController::DeRegisterSimHeli(PHelicopter *)
  AIVehicleController::ApproveAdditionalPlayerGroundVehicleSpawn(
  AIVehicleController::ApproveAdditionalAgentGroundVehicleSpawn(v
  AIVehicleController::ApproveAdditionalTrafficVehicleSpawn(void)
  AIVehicleController::ApproveAdditionalHelicopterSpawn(void)
  AIVehicleController::GetAgentSlotsAvailable(void)
  AIVehicleController::GetHelicopterSlotsAvailable(void)
  AIVehicleController::RegisterPlayerGroundVehicle(PVehicle *)
  AIVehicleController::DeRegisterPlayerGroundVehicle(PVehicle *)
  AIVehicleController::RegisterAgentGroundVehicle(PVehicle *)
  AIVehicleController::DeRegisterAgentGroundVehicle(PVehicle *)
  AIVehicleController::RegisterTrafficVehicle(PVehicle *)
  AIVehicleController::DeRegisterTrafficVehicle(PVehicle *)
  AIVehicleController::RegisterHelicopter(PHelicopter *)
  AIVehicleController::DeRegisterHelicopter(PHelicopter *)
  AIVehicleController::CreateSimTrafficCars(bool)
  AIVehicleController::fgController
  AIVehicleController::fPlayerGroundVehicleList
  AIVehicleController::fTrafficVehicleList
  AIVehicleController::fMaxActiveTraffic
  AIVehicleController::fNumActiveTraffic
  AIVehicleController::fTrafficCarCullIndex
  AIVehicleController::fPreselectedUsedTrafficTypes
  AIVehicleController::fAgentGroundVehicleList
  AIVehicleController::fHelicopterList
  AIVehicleController::fCurrentLightCounter
  AIVehicleController::fNextLightChangeTick
  AIVehicleController::fUsedTrafficTypes

Xbox methods treated as members (7 of 24; untyped ones count when ECX is read before it is written): CreateSimTrafficCars, Get, GetPlayerGroundVehiclePtr, GetTrafficVehicle, Init, RegisterSimCar, RegisterSimCarList

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):

PS2 this-relative accesses (PS2 offsets):

Short single-field methods (accessor candidates; check they touch this, not a pointee):
