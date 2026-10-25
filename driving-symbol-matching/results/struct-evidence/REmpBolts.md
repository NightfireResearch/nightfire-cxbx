# REmpBolts

FastAlloc/constructed sizes under its tag: None
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0x9dc90 first calls: ['FUN_0009c420', 'REmpBolts::FindAnchors']

Xbox methods (9):
  0x9bd50 undefined __stdcall LoadAttributes(void)
  0x9c0e0 undefined CreateVehicleBolt(undefined4 param_1, undefined4 param_2)
  0x9c260 undefined CreateWorldBolt(undefined4 param_1, undefined4 param_2)
  0x9d2d0 undefined GetRandomAnchor(undefined1 param_1)
  0x9d320 undefined TrySpawnBolt(void)
  0x9d6b0 undefined Update(void)
  0x9d7c0 undefined Draw(void)
  0x9d9a0 undefined FindAnchors(void)
  0x9dc90 undefined REmpBolts(undefined4 param_1)

PS2 methods (12):
  0x1dbcb8 REmpBolts::REmpBolts
  0x1dbd68 REmpBolts::~REmpBolts
  0x1dbe18 REmpBolts::Update
  0x1dbf90 REmpBolts::Draw
  0x1dc268 REmpBolts::LoadAttributes
  0x1dc750 REmpBolts::FindAnchors
  0x1dccb0 REmpBolts::GetRandomAnchor
  0x1dcd00 REmpBolts::GetRandomAnchor
  0x1dcea8 REmpBolts::TrySpawnBolt
  0x1dd248 REmpBolts::CreateVehicleBolt
  0x1dd4a0 REmpBolts::CreateWorldBolt
  0x1de640 REmpBolts::kSnapToBoundingBox_global_ctors

Sheet rows:
  REmpBolts::REmpBolts(RVehicle *)
  REmpBolts::~REmpBolts(void)
  REmpBolts::Update(void)
  REmpBolts::Draw(void)
  REmpBolts::LoadAttributes(void)
  REmpBolts::FindAnchors(void)
  REmpBolts::GetRandomAnchor(void)
  REmpBolts::GetRandomAnchor(unsigned int)
  REmpBolts::TrySpawnBolt(void)
  REmpBolts::CreateVehicleBolt(REmpBolts::Anchor *, REmpBolts::An
  REmpBolts::CreateWorldBolt(REmpBolts::Anchor *, COORD3 &)
  REmpBolts::kSnapToBoundingBox
  REmpBolts::kAllowVehicleBolts
  REmpBolts::kAllowWorldBolts
  REmpBolts::kTargetBoltsAlive
  REmpBolts::kScaleLifetime
  REmpBolts::kHitWorldPct
  REmpBolts::kHitWorldRange
  REmpBolts::kWorldPerturb
  REmpBolts::kWorldFlashSize
  REmpBolts::kWorldArcFactor
  REmpBolts::kVehicleArcFactor
  REmpBolts::kVehicleDriftMin
  REmpBolts::kVehicleDriftMax
  REmpBolts::kVehicleDriftVariance
  REmpBolts::kVehiclePerturb
  REmpBolts::kVehicleSparkRate
  REmpBolts::kVehicleJoltForce
  REmpBolts::kVehicleJoltTorque
  REmpBolts::kVehicleSegments
  REmpBolts::kVehicleLevels
  REmpBolts::kCenterPullPct
  REmpBolts::kGlowEnable
  REmpBolts::kGlowStrength
  REmpBolts::kGlowAmplitude
  REmpBolts::kGlowFrequency
  REmpBolts::kGlowNoiseStrength
  REmpBolts::kGlowNoiseRange
  REmpBolts::kGlowScatter
  REmpBolts::kGlowColour

Xbox methods treated as members (8 of 9; untyped ones count when ECX is read before it is written): CreateVehicleBolt, CreateWorldBolt, Draw, FindAnchors, GetRandomAnchor, REmpBolts, TrySpawnBolt, Update

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W -> RSceneObj::GetPosition, RSceneObj::LocateFX [6: CreateVehicleBolt@9c0e0, CreateWorldBolt@9c260, Draw@9d7c0, FindAnchors@9d9a0, REmpBolts@9dc90, TrySpawnBolt@9d320]
  +0x004  w[4] R/W [2: REmpBolts@9dc90, Update@9d6b0]
  +0x008  w[4] W [1: REmpBolts@9dc90]
  +0x00c  w[4] W [1: REmpBolts@9dc90]
  +0x010  w[4] R/W [3: FindAnchors@9d9a0, REmpBolts@9dc90, TrySpawnBolt@9d320]
  +0x014  w[4] R/W [3: FindAnchors@9d9a0, REmpBolts@9dc90, TrySpawnBolt@9d320]
  +0x018  w- LEA addr-taken [1: REmpBolts@9dc90]
  +0x224  w[4] R/RW/W [2: REmpBolts@9dc90, Update@9d6b0]
  +0x228  w[4] R/W [2: REmpBolts@9dc90, Update@9d6b0]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] R/W -> RSceneObj::GetPosition, RSceneObj::LocateFX [7: CreateVehicleBolt@1dd248, CreateWorldBolt@1dd4a0, Draw@1dbf90, FindAnchors@1dc750, REmpBolts@1dbcb8, TrySpawnBolt@1dcea8…]
  +0x004  w[4] R/W [2: REmpBolts@1dbcb8, Update@1dbe18]
  +0x008  w[4] W [1: REmpBolts@1dbcb8]
  +0x00c  w[4] W float [1: REmpBolts@1dbcb8]
  +0x010  w[4] LEA/R/W addr-taken -> __builtin_vec_delete [4: FindAnchors@1dc750, GetRandomAnchor@1dccb0, REmpBolts@1dbcb8, ~REmpBolts@1dbd68]
  +0x014  w[4] R/W [2: GetRandomAnchor@1dccb0, REmpBolts@1dbcb8]
  +0x018  w[4] LEA/R/W addr-taken -> UMemory::FastFree [4: FindAnchors@1dc750, GetRandomAnchor@1dcd00, REmpBolts@1dbcb8, ~REmpBolts@1dbd68]
  +0x024  w- LEA addr-taken [1: Update@1dbe18]
  +0x224  w[4] R/W [2: REmpBolts@1dbcb8, Update@1dbe18]
  +0x228  w[4] R/W [2: REmpBolts@1dbcb8, Update@1dbe18]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
