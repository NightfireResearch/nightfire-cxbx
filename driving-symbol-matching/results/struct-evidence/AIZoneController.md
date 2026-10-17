# AIZoneController

FastAlloc/constructed sizes under its tag: None
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none

Xbox methods (6):
  0x36a10 undefined4 __stdcall Get(void)
  0x36a20 undefined Init(void)
  0x36ac0 undefined __stdcall Update(void)
  0x36b00 undefined __stdcall Shutdown(void)
  0x36b20 undefined InZone(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0x36bb0 undefined RegisterZone(undefined4 param_1, undefined4 param_2)

PS2 methods (8):
  0x140100 AIZoneController::Get
  0x140110 AIZoneController::Init
  0x1401e0 AIZoneController::Shutdown
  0x140218 AIZoneController::InZone
  0x1402f8 AIZoneController::RegisterZone
  0x1404c8 AIZoneController::Update
  0x1406c0 AIZoneController::fgController_global_ctors
  0x1406e0 AIZoneController::fgController_global_dtors

Sheet rows:
  AIZoneController::Get(void)
  AIZoneController::Init(void)
  AIZoneController::Shutdown(void)
  AIZoneController::InZone(int, COORD3 &, float)
  AIZoneController::RegisterZone(AIZone::EType, COORD3 &)
  AIZoneController::Update(void)
  AIZoneController::fgController
  AIZoneController::fZone
  AIZoneController::fActiveZones
  AIZoneController::fZoneLife
  AIZoneController::fZoneHalfLife
  AIZoneController::fZoneRadius

Xbox methods treated as members (2 of 6; untyped ones count when ECX is read before it is written): Get, Update

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):

PS2 this-relative accesses (PS2 offsets):

Short single-field methods (accessor candidates; check they touch this, not a pointee):
