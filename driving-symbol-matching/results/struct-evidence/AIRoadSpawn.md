# AIRoadSpawn

FastAlloc/constructed sizes under its tag: None
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none

Xbox methods (13):
  0x347e0 undefined Get(void)
  0x347f0 undefined Init(void)
  0x34830 undefined Restart(void)
  0x34860 undefined GetTrafficSpawnPointOnSegment(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4
  0x34960 undefined GetPedSpawnPointOnSegment(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0x349e0 undefined __stdcall Shutdown(void)
  0x34a00 undefined TrafficRespawnPosCheck(undefined4 param_1, undefined4 param_2)
  0x34a70 undefined TrafficRespawnAvailable(undefined4 param_1)
  0x34bb0 undefined PedRespawnPosCheck(undefined4 param_1)
  0x34d30 undefined PedRespawnAvailable(undefined4 param_1)
  0x34ed0 undefined RefreshSpawnData(undefined1 param_1, undefined4 param_2, undefined4 param_3)
  0x35130 undefined GetRandomTrafficSpawn(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4
  0x35470 undefined GetRandomPedSpawn(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined1 param_4)

PS2 methods (14):
  0x13c4c8 AIRoadSpawn::Get
  0x13c4d8 AIRoadSpawn::Init
  0x13c558 AIRoadSpawn::Restart
  0x13c5b0 AIRoadSpawn::Shutdown
  0x13c5e8 AIRoadSpawn::GetTrafficSpawnPointOnSegment
  0x13c798 AIRoadSpawn::TrafficRespawnPosCheck
  0x13c860 AIRoadSpawn::TrafficRespawnAvailable
  0x13ca60 AIRoadSpawn::GetRandomTrafficSpawn
  0x13cf08 AIRoadSpawn::GetPedSpawnPointOnSegment
  0x13cfd8 AIRoadSpawn::PedRespawnPosCheck
  0x13d228 AIRoadSpawn::PedRespawnAvailable
  0x13d3f0 AIRoadSpawn::GetRandomPedSpawn
  0x13d618 AIRoadSpawn::RefreshSpawnData
  0x13da60 AIRoadSpawn::fgRoadSpawn_global_ctors

Sheet rows:
  AIRoadSpawn::Get(void)
  AIRoadSpawn::Init(void)
  AIRoadSpawn::Restart(void)
  AIRoadSpawn::Shutdown(void)
  AIRoadSpawn::GetTrafficSpawnPointOnSegment(short &, char &, flo
  AIRoadSpawn::TrafficRespawnPosCheck(COORD3 &, float)
  AIRoadSpawn::TrafficRespawnAvailable(COORD3 &, float)
  AIRoadSpawn::GetRandomTrafficSpawn(short &, char &, float &, bo
  AIRoadSpawn::GetPedSpawnPointOnSegment(short &, char &, float &
  AIRoadSpawn::PedRespawnPosCheck(COORD3 &)
  AIRoadSpawn::PedRespawnAvailable(COORD3 &)
  AIRoadSpawn::GetRandomPedSpawn(short &, char &, float &, bool)
  AIRoadSpawn::RefreshSpawnData(bool, float, float)
  AIRoadSpawn::fgRoadSpawn
  AIRoadSpawn::fNumSpawnSegments
  AIRoadSpawn::fInitialUpdate
  AIRoadSpawn::fLastTrafficLaneSpawn
  AIRoadSpawn::fMinDistSpawn
  AIRoadSpawn::fMaxDistSpawn
  AIRoadSpawn::fForceRespawn
  AIRoadSpawn::fLastRefresh
  AIRoadSpawn::fSpawnSegment

Xbox methods treated as members (3 of 13; untyped ones count when ECX is read before it is written): Get, GetRandomPedSpawn, GetRandomTrafficSpawn

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):

PS2 this-relative accesses (PS2 offsets):

Short single-field methods (accessor candidates; check they touch this, not a pointee):
