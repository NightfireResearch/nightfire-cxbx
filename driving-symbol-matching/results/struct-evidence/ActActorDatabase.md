# ActActorDatabase

FastAlloc/constructed sizes under its tag: None
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none

Xbox methods (11):
  0x12580 undefined __stdcall PrepareActorsForCulling(void)
  0x12f40 undefined GetNextActorCullInfo(void)
  0x12fd0 undefined DrawAll(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0x13060 undefined UpdateAll(void)
  0x130a0 undefined SetupFOVConversions(undefined4 param_1)
  0x13270 undefined KillActorByHandle(undefined4 param_1)
  0x13540 void __fastcall ~ActActorDatabase(int this)
  0x13790 undefined StartUp(void)
  0x13810 undefined __stdcall ShutDown(void)
  0x139c0 undefined GetNewActorHandle(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, un
  0x8c8d0 void __stdcall SetActorCull(int param_1, bool param_2)

PS2 methods (15):
  0x10b228 ActActorDatabase::StartUp
  0x10b260 ActActorDatabase::ShutDown
  0x10b298 ActActorDatabase::GetNewActorHandle
  0x10b3b8 ActActorDatabase::GetNewActorHandle
  0x10b4e0 ActActorDatabase::KillActorByHandle
  0x10b540 ActActorDatabase::ActActorDatabase
  0x10b588 ActActorDatabase::~ActActorDatabase
  0x10b5d8 ActActorDatabase::PrepareActorsForCulling
  0x10b5e8 ActActorDatabase::GetNextActorCullInfo
  0x10b6e0 ActActorDatabase::DrawAll
  0x10b778 ActActorDatabase::DrawAllWeapons
  0x10b800 ActActorDatabase::UpdateAll
  0x10b870 ActActorDatabase::SetupFOVConversions
  0x10bf70 ActActorDatabase::fActorDatabase_global_ctors
  0x1c6298 ActActorDatabase::SetActorCull

Sheet rows:
  ActActorDatabase::StartUp(void)
  ActActorDatabase::ShutDown(void)
  ActActorDatabase::GetNewActorHandle(int, char *, char *, void (
  ActActorDatabase::GetNewActorHandle(int, char *, char *, char *
  ActActorDatabase::KillActorByHandle(_List_iterator<ActActor *,
  ActActorDatabase::ActActorDatabase(void)
  ActActorDatabase::~ActActorDatabase(void)
  ActActorDatabase::PrepareActorsForCulling(void)
  ActActorDatabase::GetNextActorCullInfo(COORD4 &, float &, bool
  ActActorDatabase::DrawAll(RViewCamera *, bool, bool)
  ActActorDatabase::DrawAllWeapons(RViewCamera *, bool)
  ActActorDatabase::UpdateAll(void)
  ActActorDatabase::SetupFOVConversions(RViewCamera *)
  ActActorDatabase::SetActorCull(int, bool, float)
  ActActorDatabase::fActorDatabase
  ActActorDatabase::fgFirstCullCall
  ActActorDatabase::fgCullIterator

Xbox methods treated as members (7 of 11; untyped ones count when ECX is read before it is written): DrawAll, GetNewActorHandle, PrepareActorsForCulling, SetupFOVConversions, StartUp, UpdateAll, ~ActActorDatabase

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x004  w[4] R/W [4: DrawAll@12fd0, SetupFOVConversions@130a0, UpdateAll@13060, ~ActActorDatabase@13540]
  +0x008  w[4] W [1: ~ActActorDatabase@13540]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] R/W -> ActActor::Draw, ActActor::DrawWeapons, ActActor::SetupFOVConversion, UMemory::FastFree [9: ActActorDatabase@10b540, DrawAll@10b6e0, DrawAllWeapons@10b778, GetNewActorHandle@10b298, GetNewActorHandle@10b3b8, KillActorByHandle@10b4e0…]
  +0x00c  w[4] W float [1: GetNextActorCullInfo@10b5e8]
  +0x054  w[4] W [1: SetActorCull@1c6298]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
