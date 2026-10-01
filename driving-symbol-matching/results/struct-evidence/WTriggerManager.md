# WTriggerManager

FastAlloc/constructed sizes under its tag: None
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none

Xbox methods (12):
  0xcf520 undefined CheckCollide(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0xcfae0 undefined CheckCollide(undefined4 param_1, undefined4 param_2)
  0xcfdf0 undefined CheckCollide(undefined4 param_1, undefined4 param_2)
  0xcfeb0 undefined CheckCollide(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0xcff20 undefined CheckCollide(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, undefin
  0xcff90 undefined __stdcall Restart(void)
  0xd03b0 undefined __cdecl Init(long param_1)
  0xd04a0 void __thiscall Process(WTriggerManager * this, undefined4 param_1, RigidBody * param_2)
  0xd0630 undefined Process(undefined4 param_1)
  0xd0810 undefined Process(undefined4 param_1, undefined4 param_2)
  0xd0a30 undefined Process(undefined4 param_1)
  0xd0c90 void __thiscall Update(WTriggerManager * this)

PS2 methods (17):
  0x22cba0 WTriggerManager::WTriggerManager
  0x22cbb8 WTriggerManager::~WTriggerManager
  0x22cbe0 WTriggerManager::Init
  0x22cd30 WTriggerManager::Restart
  0x22d240 WTriggerManager::Process
  0x22d5a8 WTriggerManager::Process
  0x22dae8 WTriggerManager::Process
  0x22def0 WTriggerManager::Process
  0x22e3a0 WTriggerManager::CheckCollide
  0x22eb28 WTriggerManager::CheckCollide
  0x22eec0 WTriggerManager::CheckCollide
  0x22efb0 WTriggerManager::CheckCollide
  0x22f030 WTriggerManager::CheckCollide
  0x22f0b0 WTriggerManager::GetIntersectingTriggers
  0x22f0b8 WTriggerManager::FireTriggers
  0x22f0c0 WTriggerManager::Update
  0x22f3c0 WTriggerManager::fgTriggerManager_global_ctors

Sheet rows:
  WTriggerManager::WTriggerManager(CARP::Trigger *, unsigned int)
  WTriggerManager::~WTriggerManager(void)
  WTriggerManager::Init(UData *)
  WTriggerManager::Restart(void)
  WTriggerManager::Process(unsigned int, RigidBody &) const
  WTriggerManager::Process(CARP::Instance &) const
  WTriggerManager::Process(unsigned int, SimpleRigidBody &) const
  WTriggerManager::Process(int) const
  WTriggerManager::CheckCollide(COORD4 *, float, WTrigger *) cons
  WTriggerManager::CheckCollide(RigidBody &, WTrigger *) const
  WTriggerManager::CheckCollide(SimpleRigidBody &, WTrigger *) co
  WTriggerManager::CheckCollide(COORD3 &, float, WTrigger *) cons
  WTriggerManager::CheckCollide(COORD3 &, float, float, float, WT
  WTriggerManager::GetIntersectingTriggers(COORD3 &, float, WTrig
  WTriggerManager::FireTriggers(WTriggerList *) const
  WTriggerManager::Update(void) const
  WTriggerManager::fgTriggerManager
  WTriggerManager::fTriggerDataSize
  WTriggerManager::fOriginalTriggerData
  WTriggerManager::fSavedTriggerData
  WTriggerManager::fgIterCount

Xbox methods treated as members (5 of 12; untyped ones count when ECX is read before it is written): Process, Update

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x004  w[4] R [4: Process@d04a0, Process@d0630, Process@d0810, Process@d0a30]
  +0x008  w[1] R [4: Process@d04a0, Process@d0630, Process@d0810, Process@d0a30]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] W [1: WTriggerManager@22cba0]
  +0x004  w[4] R/W [6: Init@22cbe0, Process@22d240, Process@22d5a8, Process@22dae8, Process@22def0, WTriggerManager@22cba0]
  +0x008  w[4] R/W [6: Init@22cbe0, Process@22d240, Process@22d5a8, Process@22dae8, Process@22def0, WTriggerManager@22cba0]
  +0x00c  w[4] R [1: Init@22cbe0]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
