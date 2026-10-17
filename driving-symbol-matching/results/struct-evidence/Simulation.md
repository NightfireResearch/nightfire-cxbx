# Simulation

FastAlloc/constructed sizes under its tag: None
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0xb4fe0 first calls: ['UMemory::FastAlloc', 'WSoundMap::WSoundMap', 'printf']

Xbox methods (45):
  0x1c180 undefined __thiscall GetRigidBody(Simulation * this)
  0xb25e0 void __thiscall PauseSimState(Simulation * this)
  0xb2600 undefined UnPauseSimState(void)
  0xb2620 undefined AdvanceStep(void)
  0xb2630 uint __thiscall AssignRigidBodySlot(Simulation * this, void * param_1, bool param_2)
  0xb2700 int __thiscall GetRigidBody(Simulation * this, undefined4 rigidbodyID)
  0xb2730 int __thiscall GetSimpleRigidBody(Simulation * this, int param_1)
  0xb2760 undefined GetRigidBodyInfo(undefined4 param_1)
  0xb2780 undefined FindSignature(undefined4 param_1, undefined4 param_2)
  0xb27d0 undefined FindPhysicsObjectSignature(undefined4 param_1)
  0xb2820 undefined GetScratchPadFreeZone(void)
  0xb2830 undefined DetectCollisionsSRB(void)
  0xb28d0 undefined GetOrderedBody(undefined4 param_1)
  0xb2900 undefined GetTrackedInstanceList(void)
  0xb2980 undefined ReleaseRigidBodySlot(undefined4 param_1, undefined1 param_2)
  0xb2b50 undefined SetStartConditions(undefined4 param_1, undefined4 param_2)
  0xb2c10 undefined DetonateRemoteMines(void)
  0xb2c40 undefined CanSpawnRigidBody(undefined4 param_1, undefined4 param_2)
  0xb2d30 undefined4 __fastcall GetPlayerObject(int this)
  0xb3000 undefined UntrackInstance(undefined4 param_1)
  0xb30c0 undefined UntrackAllInstances(void)
  0xb3fc0 undefined DeleteDeadObjects(void)
  0xb4250 undefined __thiscall DeletePhysicsObject(Simulation * this, undefined4 param_1)
  0xb4740 undefined __thiscall DeletePhysicsObject(Simulation * this, undefined4 param_1)
  0xb4750 void __thiscall CleanUpObjects(Simulation * this)
  0xb47a0 undefined CanSpawnSimpleRigidBody(undefined param_1, undefined1 param_2)
  0xb4810 undefined TrackInstance(undefined4 param_1, undefined4 param_2)
  0xb4870 undefined UpdateTrackedInstanceVisibility(void)
  0xb49f0 void __thiscall Reset(Simulation * this)
  0xb4a60 undefined SimulateGame(void)
  0xb4cd0 undefined SpawnExplosion(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, undef
  0xb4e10 void __thiscall SpawnPhysicsObject(Simulation * this, PhysicsObject * physicsObject)
  0xb4fe0 undefined Simulation(void)
  0xb5310 undefined ~Simulation(void)
  0xb5700 undefined4 __thiscall SpawnCarObject(Simulation * this, undefined4 param_1, undefined4 param_2, undefined4 par
  0xb57b0 undefined SpawnSmackableObject(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0xb5890 undefined SpawnHumanObject(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0xb5950 undefined SpawnMissileObject(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, u
  0xb5a70 undefined SpawnMineObject(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, unde
  0xb5b80 undefined SpawnGrenadeObject(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, u
  0xb5c90 undefined SpawnShellObject(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, und
  0xb5d50 undefined SpawnExplosionObject(undefined4 param_1, undefined param_2, undefined4 param_3, undefined4 param_4, 
  0xb5e20 undefined SpawnNewtonObject(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, un
  0xb5ee0 undefined SpawnHelicopterObject(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4
  0xb5fb0 ulong __thiscall SpawnSentryObject(Simulation * this, undefined4 param_1, float param_2, float param_3, float 

PS2 methods (64):
  0x1fe1b0 Simulation::Simulation
  0x1feae0 Simulation::~Simulation
  0x1feda0 Simulation::Reset
  0x1fee28 Simulation::PauseSimState
  0x1fee48 Simulation::UnPauseSimState
  0x1fee68 Simulation::SetStartConditions
  0x1fef20 Simulation::AdvanceStep
  0x1fef48 Simulation::AssignRigidBodySlot
  0x1ff088 Simulation::GetRigidBody
  0x1ff0b0 Simulation::GetSimpleRigidBody
  0x1ff0d8 Simulation::GetRigidBodyInfo
  0x1ff0f0 Simulation::ReleaseRigidBodySlot
  0x1ff1d0 Simulation::FindRigidBodySignature
  0x1ff200 Simulation::FindSimpleRigidBodySignature
  0x1ff230 Simulation::FindSignature
  0x1ff280 Simulation::FindPhysicsObjectSignature
  0x1ff2e8 Simulation::CopyRigidBodiesToScratchPad
  0x1ff358 Simulation::CopyRigidBodiesFromScratchPad
  0x1ff3c0 Simulation::ReleaseScratchPad
  0x1ff3c8 Simulation::GetScratchPadFreeZone
  0x1ff3d8 Simulation::SimulateGame
  0x1ff768 Simulation::CleanUpObjects
  0x1ff850 Simulation::SpawnCarObject
  0x1ff908 Simulation::ActivateCarObject
  0x1ff9d0 Simulation::DeactivateCarObject
  0x1ffa90 Simulation::SpawnSmackableObject
  0x1ffb90 Simulation::SpawnHumanObject
  0x1ffc40 Simulation::SpawnMissileObject
  0x1ffd70 Simulation::SpawnMineObject
  0x1ffe70 Simulation::DetonateRemoteMines
  0x1ffea8 Simulation::SpawnGrenadeObject
  0x1fffa8 Simulation::SpawnShellObject
  0x200088 Simulation::SpawnExplosion
  0x200200 Simulation::SpawnExplosionObject
  0x2002e0 Simulation::SpawnNewtonObject
  0x2003c8 Simulation::SpawnHelicopterObject
  0x2004a0 Simulation::SpawnSentryObject
  0x200600 Simulation::DeleteDeadObjects
  0x200ca8 Simulation::DeleteInProgress
  0x200cb8 Simulation::SpawnPhysicsObject
  0x200f18 Simulation::DeletePhysicsObject
  0x2012d0 Simulation::CanSpawnRigidBody
  0x201408 Simulation::CanSpawnSimpleRigidBody
  0x2014b8 Simulation::DetectCollisionsSRB
  0x201590 Simulation::GetOrderedBody
  0x2015d8 Simulation::TrackInstance
  0x201690 Simulation::UntrackInstance
  0x201730 Simulation::UntrackAllInstances
  0x2017b8 Simulation::UpdateTrackedInstanceVisibility
  0x201978 Simulation::GetTrackedInstanceList
  0x201980 Simulation::DeleteCarObject
  0x2019a0 Simulation::DeleteSmackableObject
  0x2019c0 Simulation::DeleteHumanObject
  0x2019e0 Simulation::DeleteMissileObject
  0x201a00 Simulation::DeleteMineObject
  0x201a20 Simulation::DeleteGrenadeObject
  0x201a40 Simulation::DeleteShellObject
  0x201a60 Simulation::DeleteExplosionObject
  0x201a80 Simulation::DeleteNewtonObject
  0x201aa0 Simulation::DeleteHelicopterObject
  0x201ac0 Simulation::DeleteSentryObject
  0x201ae0 Simulation::GetPlayerObject
  0x201af0 Simulation::GetReflectiveObjectPosition
  0x201b68 Simulation::SetReflectiveObject

Sheet rows:
  Simulation::Simulation(void)
  Simulation::~Simulation(void)
  Simulation::Reset(void)
  Simulation::PauseSimState(void)
  Simulation::UnPauseSimState(void)
  Simulation::SetStartConditions(COORD4 &, COORD4 &)
  Simulation::AdvanceStep(void)
  Simulation::AssignRigidBodySlot(PhysicsObject *, bool)
  Simulation::GetRigidBody(int)
  Simulation::GetSimpleRigidBody(int)
  Simulation::GetRigidBodyInfo(int)
  Simulation::ReleaseRigidBodySlot(int, bool)
  Simulation::FindRigidBodySignature(SimObjSig)
  Simulation::FindSimpleRigidBodySignature(SimObjSig)
  Simulation::FindSignature(SimObjSig, bool &)
  Simulation::FindPhysicsObjectSignature(SimObjSig)
  Simulation::CopyRigidBodiesToScratchPad(void)
  Simulation::CopyRigidBodiesFromScratchPad(void)
  Simulation::ReleaseScratchPad(void)
  Simulation::GetScratchPadFreeZone(void)
  Simulation::SimulateGame(void)
  Simulation::CleanUpObjects(void)
  Simulation::SpawnCarObject(int, char *, unsigned int, COORD3 &,
  Simulation::ActivateCarObject(PVehicle *)
  Simulation::DeactivateCarObject(PVehicle *)
  Simulation::SpawnSmackableObject(RSceneObj *, WTrigger *, COORD
  Simulation::SpawnHumanObject(int, COORD3 &, COORD3 &, SimObjSig
  Simulation::SpawnMissileObject(WeaponType, float, COORD3 &, COO
  Simulation::SpawnMineObject(WeaponType, COORD3 &, COORD3 &, flo
  Simulation::DetonateRemoteMines(void)
  Simulation::SpawnGrenadeObject(WeaponType, COORD3 &, COORD3 &,
  Simulation::SpawnShellObject(WeaponType, COORD3 &, COORD3 &, Si
  Simulation::SpawnExplosion(int, int, COORD3 &, COORD3 &, float,
  Simulation::SpawnExplosionObject(int, WeaponType, COORD3 &, COO
  Simulation::SpawnNewtonObject(COORD3 &, COORD3 &, COORD3 &, COO
  Simulation::SpawnHelicopterObject(char *, COORD3 &, COORD3 &, f
  Simulation::SpawnSentryObject(CARP::Instance *, int, float, flo
  Simulation::DeleteDeadObjects(void)
  Simulation::DeleteInProgress(void) const
  Simulation::SpawnPhysicsObject(PhysicsObject *)
  Simulation::DeletePhysicsObject(PhysicsObject *)
  Simulation::CanSpawnRigidBody(COORD3 &, bool)
  Simulation::CanSpawnSimpleRigidBody(COORD3 &, bool)
  Simulation::DetectCollisionsSRB(void)
  Simulation::GetOrderedBody(int)
  Simulation::TrackInstance(CARP::Instance *, SimTrackedInstanceT
  Simulation::UntrackInstance(CARP::Instance *)
  Simulation::UntrackAllInstances(void)
  Simulation::UpdateTrackedInstanceVisibility(void)
  Simulation::GetTrackedInstanceList(void)
  Simulation::DeleteCarObject(PVehicle *)
  Simulation::DeleteSmackableObject(Smackable *)
  Simulation::DeleteHumanObject(Human *)
  Simulation::DeleteMissileObject(Missile *)
  Simulation::DeleteMineObject(Mine *)
  Simulation::DeleteGrenadeObject(Grenade *)
  Simulation::DeleteShellObject(Shell *)
  Simulation::DeleteExplosionObject(Explosion *)
  Simulation::DeleteNewtonObject(Newton *)
  Simulation::DeleteHelicopterObject(PHelicopter *)
  Simulation::DeleteSentryObject(Sentry *)
  Simulation::GetPlayerObject(void)
  Simulation::GetReflectiveObjectPosition(void)
  Simulation::SetReflectiveObject(SimObjSig)

Xbox methods treated as members (44 of 45; untyped ones count when ECX is read before it is written): AdvanceStep, AssignRigidBodySlot, CanSpawnRigidBody, CanSpawnSimpleRigidBody, CleanUpObjects, DeleteDeadObjects, DeletePhysicsObject, DetectCollisionsSRB, DetonateRemoteMines, FindPhysicsObjectSignature, FindSignature, GetOrderedBody, GetPlayerObject, GetRigidBody, GetRigidBodyInfo, GetScratchPadFreeZone, GetSimpleRigidBody, GetTrackedInstanceList, PauseSimState, ReleaseRigidBodySlot, Reset, SetStartConditions, SimulateGame, Simulation, SpawnCarObject, SpawnExplosionObject, SpawnGrenadeObject, SpawnHelicopterObject, SpawnHumanObject, SpawnMineObject, SpawnMissileObject, SpawnNewtonObject, SpawnPhysicsObject, SpawnSentryObject, SpawnShellObject, SpawnSmackableObject, TrackInstance, UnPauseSimState, UntrackAllInstances, UntrackInstance, UpdateTrackedInstanceVisibility, ~Simulation

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W -> dummyNullFunction [3: Reset@b49f0, Simulation@b4fe0, ~Simulation@b5310]
  +0x004  w[1] R/W [7: DetectCollisionsSRB@b2830, GetRigidBody@b2700, GetSimpleRigidBody@b2730, ReleaseRigidBodySlot@b2980, Reset@b49f0, SetStartConditions@b2b50…]
  +0x008  w[4] R/RW/W [3: AssignRigidBodySlot@b2630, CanSpawnRigidBody@b2c40, Reset@b49f0]
  +0x00c  w[4] R/RW/W [4: AssignRigidBodySlot@b2630, CanSpawnSimpleRigidBody@b47a0, ReleaseRigidBodySlot@b2980, Reset@b49f0]
  +0x010  w[4] R/W [1: Simulation@b4fe0]
  +0x014  w[4] W [1: Simulation@b4fe0]
  +0x018  w[4] R/W [2: Simulation@b4fe0, ~Simulation@b5310]
  +0x01c  w[4] R/W [4: GetRigidBody@b2700, SetStartConditions@b2b50, SimulateGame@b4a60, Simulation@b4fe0]
  +0x020  w[4] R/W [4: DetectCollisionsSRB@b2830, GetSimpleRigidBody@b2730, ReleaseRigidBodySlot@b2980, Simulation@b4fe0]
  +0x024  w[4] R/W [2: GetRigidBodyInfo@b2760, Simulation@b4fe0]
  +0x028  w[4] R/W [4: GetRigidBody@b2700, SetStartConditions@b2b50, SimulateGame@b4a60, Simulation@b4fe0]
  +0x02c  w[4] R/W [4: DetectCollisionsSRB@b2830, GetSimpleRigidBody@b2730, ReleaseRigidBodySlot@b2980, Simulation@b4fe0]
  +0x030  w- LEA addr-taken [1: Simulation@b4fe0]
  +0x04a  w[2] R [1: GetRigidBody@1c180]
  +0x130  w- LEA addr-taken [1: Simulation@b4fe0]
  +0x2b0  w- LEA addr-taken [2: AssignRigidBodySlot@b2630, SimulateGame@b4a60]
  +0x3b0  w- LEA addr-taken [2: AssignRigidBodySlot@b2630, DetectCollisionsSRB@b2830]
  +0x530  w- LEA addr-taken [2: DetectCollisionsSRB@b2830, Simulation@b4fe0]
  +0xe30  w[4] R/W [3: PauseSimState@b25e0, Reset@b49f0, UnPauseSimState@b2600]
  +0xe34  w[4] R/W [5: AdvanceStep@b2620, PauseSimState@b25e0, Reset@b49f0, SimulateGame@b4a60, UnPauseSimState@b2600]
  +0xe3c  w[4] W [2: Reset@b49f0, Simulation@b4fe0]
  +0xe40  w[4] W float [2: Reset@b49f0, Simulation@b4fe0]
  +0xe44  w[4] RW/W [2: AdvanceStep@b2620, Reset@b49f0]
  +0xe48  w[4] W [1: Simulation@b4fe0]
  +0xe4c  w- LEA addr-taken [1: Simulation@b4fe0]
  +0xe50  w[4] R/W [3: GetPlayerObject@b2d30, SetStartConditions@b2b50, ~Simulation@b5310]
  +0xe54  w[4] W [1: ~Simulation@b5310]
  +0xe58  w[4] R/W [1: ~Simulation@b5310]
  +0xe5c  w- LEA addr-taken -> FUN_000b4080 [1: Simulation@b4fe0]
  +0xe60  w[4] R/W [2: Simulation@b4fe0, ~Simulation@b5310]
  +0xe64  w[4] W [2: Simulation@b4fe0, ~Simulation@b5310]
  +0xe68  w[4] R/W [2: Simulation@b4fe0, ~Simulation@b5310]
  +0xe6c  w- LEA addr-taken -> FUN_000b4080 [1: Simulation@b4fe0]
  +0xe70  w[4] R/W [3: CanSpawnRigidBody@b2c40, Simulation@b4fe0, ~Simulation@b5310]
  +0xe74  w[4] R/W [3: CanSpawnRigidBody@b2c40, Simulation@b4fe0, ~Simulation@b5310]
  +0xe78  w[4] R/W [2: Simulation@b4fe0, ~Simulation@b5310]
  +0xe7c  w- LEA addr-taken -> FUN_000b4080 [1: Simulation@b4fe0]
  +0xe80  w[4] R/W [2: Simulation@b4fe0, ~Simulation@b5310]
  +0xe84  w[4] W [2: Simulation@b4fe0, ~Simulation@b5310]
  +0xe88  w[4] R/W [2: Simulation@b4fe0, ~Simulation@b5310]
  +0xe8c  w- LEA addr-taken -> FUN_000b4080 [1: Simulation@b4fe0]
  +0xe90  w[4] R/W [2: Simulation@b4fe0, ~Simulation@b5310]
  +0xe94  w[4] W [2: Simulation@b4fe0, ~Simulation@b5310]
  +0xe98  w[4] R/W [2: Simulation@b4fe0, ~Simulation@b5310]
  +0xe9c  w- LEA addr-taken -> FUN_000b4080 [1: Simulation@b4fe0]
  +0xea0  w[4] R/W [3: DetonateRemoteMines@b2c10, Simulation@b4fe0, ~Simulation@b5310]
  +0xea4  w[4] R/W [3: DetonateRemoteMines@b2c10, Simulation@b4fe0, ~Simulation@b5310]
  +0xea8  w[4] R/W [2: Simulation@b4fe0, ~Simulation@b5310]
  +0xeb0  w[4] R/W [2: Simulation@b4fe0, ~Simulation@b5310]
  +0xeb4  w[4] W [2: Simulation@b4fe0, ~Simulation@b5310]
  +0xeb8  w[4] R/W [2: Simulation@b4fe0, ~Simulation@b5310]
  +0xebc  w- LEA addr-taken -> FUN_000b4080 [1: Simulation@b4fe0]
  +0xec0  w[4] R/W [2: Simulation@b4fe0, ~Simulation@b5310]
  +0xec4  w[4] W [2: Simulation@b4fe0, ~Simulation@b5310]
  +0xec8  w[4] R/W [2: Simulation@b4fe0, ~Simulation@b5310]
  +0xecc  w- LEA addr-taken -> FUN_000b2fc0, FUN_000b4080, std::vector<>::push_back [3: DeletePhysicsObject@b4250, Simulation@b4fe0, SpawnPhysicsObject@b4e10]
  +0xed0  w[4] R/W [3: DeletePhysicsObject@b4250, Simulation@b4fe0, ~Simulation@b5310]
  +0xed4  w[4] R/W [3: DeletePhysicsObject@b4250, Simulation@b4fe0, ~Simulation@b5310]
  +0xed8  w[4] R/W [2: Simulation@b4fe0, ~Simulation@b5310]
  +0xedc  w- LEA addr-taken -> FUN_000b4080 [1: Simulation@b4fe0]
  +0xee0  w[4] R/W [3: CanSpawnSimpleRigidBody@b47a0, Simulation@b4fe0, ~Simulation@b5310]
  +0xee4  w[4] R/W [3: CanSpawnSimpleRigidBody@b47a0, Simulation@b4fe0, ~Simulation@b5310]
  +0xee8  w[4] R/W [2: Simulation@b4fe0, ~Simulation@b5310]
  +0xeec  w- LEA addr-taken -> FUN_000b4080 [1: Simulation@b4fe0]
  +0xef0  w[4] R/W [2: Simulation@b4fe0, ~Simulation@b5310]
  +0xef4  w[4] W [2: Simulation@b4fe0, ~Simulation@b5310]
  +0xef8  w[4] R/W [2: Simulation@b4fe0, ~Simulation@b5310]
  +0xefc  w- LEA addr-taken -> FUN_000b4080 [1: Simulation@b4fe0]
  +0xf00  w[4] R/W [2: Simulation@b4fe0, ~Simulation@b5310]
  +0xf04  w[4] W [2: Simulation@b4fe0, ~Simulation@b5310]
  +0xf08  w[4] R/W [2: Simulation@b4fe0, ~Simulation@b5310]
  +0xf0c  w- LEA addr-taken [3: GetTrackedInstanceList@b2900, TrackInstance@b4810, UpdateTrackedInstanceVisibility@b4870]
  +0xf10  w[4] R/W [5: Simulation@b4fe0, TrackInstance@b4810, UntrackAllInstances@b30c0, UntrackInstance@b3000, ~Simulation@b5310]
  +0xf14  w[4] R/W [5: Simulation@b4fe0, UntrackAllInstances@b30c0, UntrackInstance@b3000, UpdateTrackedInstanceVisibility@b4870, ~Simulation@b5310]
  +0xf18  w[4] R/W [3: Simulation@b4fe0, UntrackAllInstances@b30c0, ~Simulation@b5310]
  +0xf1c  w[4] R/W -> FUN_000b3660, std::__tree<>::insert_multi [6: CleanUpObjects@b4750, DeleteDeadObjects@b3fc0, DeletePhysicsObject@b4250, Simulation@b4fe0, SpawnPhysicsObject@b4e10, ~Simulation@b5310]
  +0xf20  w[4] R/W -> FUN_000b2dc0 [4: DeleteDeadObjects@b3fc0, DeletePhysicsObject@b4250, Simulation@b4fe0, ~Simulation@b5310]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] R/W -> SimRandom::~SimRandom [2: Simulation@1fe1b0, ~Simulation@1feae0]
  +0x004  w[4] R/W [4: CopyRigidBodiesFromScratchPad@1ff358, GetRigidBody@1ff088, GetSimpleRigidBody@1ff0b0, ReleaseRigidBodySlot@1ff0f0]
  +0x008  w[4] R/W [3: AssignRigidBodySlot@1fef48, CanSpawnRigidBody@2012d0, ReleaseRigidBodySlot@1ff0f0]
  +0x00c  w[4] R/W [3: AssignRigidBodySlot@1fef48, CanSpawnSimpleRigidBody@201408, ReleaseRigidBodySlot@1ff0f0]
  +0x010  w[4] R/W [1: Simulation@1fe1b0]
  +0x014  w[4] R/W [3: CopyRigidBodiesFromScratchPad@1ff358, GetScratchPadFreeZone@1ff3c8, Simulation@1fe1b0]
  +0x018  w[4] R/W [2: Simulation@1fe1b0, ~Simulation@1feae0]
  +0x01c  w[4] R/W [4: CopyRigidBodiesFromScratchPad@1ff358, GetRigidBody@1ff088, ReleaseRigidBodySlot@1ff0f0, Simulation@1fe1b0]
  +0x020  w[4] R/W [3: GetSimpleRigidBody@1ff0b0, ReleaseRigidBodySlot@1ff0f0, Simulation@1fe1b0]
  +0x024  w[4] R/W [2: GetRigidBodyInfo@1ff0d8, Simulation@1fe1b0]
  +0x028  w[4] R/W -> DMA_FromScratchPad [4: CopyRigidBodiesFromScratchPad@1ff358, GetRigidBody@1ff088, ReleaseRigidBodySlot@1ff0f0, Simulation@1fe1b0]
  +0x02c  w[4] R/W [3: GetSimpleRigidBody@1ff0b0, ReleaseRigidBodySlot@1ff0f0, Simulation@1fe1b0]
  +0x030  w[4] LEA/W addr-taken [2: AssignRigidBodySlot@1fef48, Simulation@1fe1b0]
  +0x0f0  w[4] LEA/W addr-taken [2: AssignRigidBodySlot@1fef48, Simulation@1fe1b0]
  +0x270  w- LEA addr-taken [2: AssignRigidBodySlot@1fef48, SimulateGame@1ff3d8]
  +0x330  w- LEA addr-taken [3: AssignRigidBodySlot@1fef48, DetectCollisionsSRB@2014b8, SimulateGame@1ff3d8]
  +0x4b0  w- LEA addr-taken [1: Simulation@1fe1b0]
  +0xdb0  w[4] R/W [2: PauseSimState@1fee28, UnPauseSimState@1fee48]
  +0xdb4  w[4] R/W [4: AdvanceStep@1fef20, PauseSimState@1fee28, SimulateGame@1ff3d8, UnPauseSimState@1fee48]
  +0xdbc  w[4] W [1: Simulation@1fe1b0]
  +0xdc0  w[4] W [1: Simulation@1fe1b0]
  +0xdc4  w[4] R/W [1: AdvanceStep@1fef20]
  +0xdc8  w[4] R/W [3: GetReflectiveObjectPosition@201af0, SetReflectiveObject@201b68, Simulation@1fe1b0]
  +0xdcc  w[4] LEA/R/W addr-taken -> remove<PVehicle_**,_PVehicle_*> [9: ActivateCarObject@1ff908, DeactivateCarObject@1ff9d0, DeletePhysicsObject@200f18, GetPlayerObject@201ae0, GetReflectiveObjectPosition@201af0, SetStartConditions@1fee68…]
  +0xdd4  w[4] R [1: ~Simulation@1feae0]
  +0xdd8  w[4] LEA/R/W addr-taken -> remove<PVehicle_**,_PVehicle_*> [6: ActivateCarObject@1ff908, DeactivateCarObject@1ff9d0, DeletePhysicsObject@200f18, Simulation@1fe1b0, SpawnPhysicsObject@200cb8, ~Simulation@1feae0]
  +0xde0  w[4] R [1: ~Simulation@1feae0]
  +0xde4  w[4] LEA/R/W addr-taken -> remove<Smackable_**,_Smackable_*> [5: CanSpawnRigidBody@2012d0, DeletePhysicsObject@200f18, Simulation@1fe1b0, SpawnPhysicsObject@200cb8, ~Simulation@1feae0]
  +0xdec  w[4] R [1: ~Simulation@1feae0]
  +0xdf0  w[4] LEA/R/W addr-taken -> remove<Human_**,_Human_*> [4: DeletePhysicsObject@200f18, Simulation@1fe1b0, SpawnPhysicsObject@200cb8, ~Simulation@1feae0]
  +0xdf8  w[4] R [1: ~Simulation@1feae0]
  +0xdfc  w[4] LEA/R/W addr-taken -> remove<Missile_**,_Missile_*> [4: DeletePhysicsObject@200f18, Simulation@1fe1b0, SpawnPhysicsObject@200cb8, ~Simulation@1feae0]
  +0xe04  w[4] R [1: ~Simulation@1feae0]
  +0xe08  w[4] LEA/R/W addr-taken -> remove<Mine_**,_Mine_*> [5: DeletePhysicsObject@200f18, DetonateRemoteMines@1ffe70, Simulation@1fe1b0, SpawnPhysicsObject@200cb8, ~Simulation@1feae0]
  +0xe0c  w[4] R [1: DetonateRemoteMines@1ffe70]
  +0xe10  w[4] R [1: ~Simulation@1feae0]
  +0xe14  w[4] LEA/R/W addr-taken -> remove<Grenade_**,_Grenade_*> [4: DeletePhysicsObject@200f18, Simulation@1fe1b0, SpawnPhysicsObject@200cb8, ~Simulation@1feae0]
  +0xe1c  w[4] R [1: ~Simulation@1feae0]
  +0xe20  w[4] LEA/R/W addr-taken -> remove<Shell_**,_Shell_*> [4: DeletePhysicsObject@200f18, Simulation@1fe1b0, SpawnPhysicsObject@200cb8, ~Simulation@1feae0]
  +0xe28  w[4] R [1: ~Simulation@1feae0]
  +0xe2c  w[4] LEA/R/W addr-taken -> remove<Explosion_**,_Explosion_*> [4: DeletePhysicsObject@200f18, Simulation@1fe1b0, SpawnPhysicsObject@200cb8, ~Simulation@1feae0]
  +0xe34  w[4] R [1: ~Simulation@1feae0]
  +0xe38  w[4] LEA/R/W addr-taken -> remove<Newton_**,_Newton_*> [5: CanSpawnSimpleRigidBody@201408, DeletePhysicsObject@200f18, Simulation@1fe1b0, SpawnPhysicsObject@200cb8, ~Simulation@1feae0]
  +0xe40  w[4] R [1: ~Simulation@1feae0]
  +0xe44  w[4] LEA/R/W addr-taken -> remove<PHelicopter_**,_PHelicopter_*> [4: DeletePhysicsObject@200f18, Simulation@1fe1b0, SpawnPhysicsObject@200cb8, ~Simulation@1feae0]
  +0xe4c  w[4] R [1: ~Simulation@1feae0]
  +0xe50  w[4] LEA/R/W addr-taken -> remove<Sentry_**,_Sentry_*> [4: DeletePhysicsObject@200f18, Simulation@1fe1b0, SpawnPhysicsObject@200cb8, ~Simulation@1feae0]
  +0xe58  w[4] R [1: ~Simulation@1feae0]
  +0xe5c  w[4] LEA/R/W addr-taken [7: GetTrackedInstanceList@201978, Simulation@1fe1b0, TrackInstance@2015d8, UntrackAllInstances@201730, UntrackInstance@201690, UpdateTrackedInstanceVisibility@2017b8…]
  +0xe64  w[4] R [1: ~Simulation@1feae0]
  +0xe68  w[4] R/W -> FUN_00203360 [6: DeleteDeadObjects@200600, DeletePhysicsObject@200f18, SimulateGame@1ff3d8, Simulation@1fe1b0, SpawnPhysicsObject@200cb8, ~Simulation@1feae0]
  +0xe6c  w[4] R/W -> FUN_00203460 [4: DeleteDeadObjects@200600, DeletePhysicsObject@200f18, Simulation@1fe1b0, ~Simulation@1feae0]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  GetRigidBody: R +0x4a w2
  GetRigidBodyInfo: R +0x24 w4
  GetTrackedInstanceList: LEA +0xf0c w0
  GetPlayerObject: R +0xe50 w4
