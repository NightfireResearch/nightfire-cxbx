# AIGroundVehicle

FastAlloc/constructed sizes under its tag: None
Xbox vtable 0x0018b8d4 stored by constructor 0x2bd40 (not in vtables.json; slot count unknown)
PS2 sheet virtual table row: ['AIGroundVehicle virtual table']
constructor 0x2bd40 first calls: ['AIVehicle::AIVehicle', 'UMemory::FastAlloc', 'WRoadNav::WRoadNav']

Xbox methods (43):
  0x29d60 undefined Reset(void)
  0x2a0d0 undefined SetNavigateMode(undefined4 param_1, undefined4 param_2)
  0x2a2c0 undefined SetTargetMode(undefined4 param_1, undefined4 param_2)
  0x2a330 undefined SetAttackMode(undefined4 param_1)
  0x2a380 undefined SetSecondaryTarget(undefined4 param_1)
  0x2a3c0 undefined GetTargetPos(void)
  0x2a3e0 undefined UpdateNavPos(undefined4 param_1, undefined4 param_2)
  0x2a690 undefined EngageAutoDrive(void)
  0x2a760 undefined DisengageAutoDrive(void)
  0x2a7c0 undefined DoPlayerLogic(void)
  0x2a800 undefined EngageSplinePath(void)
  0x2a850 undefined DisengageSplinePath(void)
  0x2a8b0 undefined DoSplinePathUpdate(void)
  0x2a9e0 undefined DrivableVec(undefined4 param_1, undefined4 param_2)
  0x2aaf0 undefined BarriersInPath(undefined1 param_1)
  0x2af50 undefined UpdateStuck(void)
  0x2b0c0 undefined GetHeliCollNav(undefined4 param_1)
  0x2b210 undefined GetCollNav(undefined4 param_1, undefined1 param_2, undefined1 param_3)
  0x2b5e0 undefined GetBestWeightedLane(void)
  0x2b660 undefined GetRoadNetworkLaneWeights(void)
  0x2b7a0 undefined GetDirectLaneWeights(undefined4 param_1)
  0x2bd40 undefined AIGroundVehicle(void)
  0x2be70 undefined ~AIGroundVehicle(void)
  0x2bf40 undefined DoAttackMode(void)
  0x2c6e0 undefined TurnTurret(void)
  0x2c9d0 undefined DoAutoDriveUpdate(void)
  0x2cd80 undefined DoAgentStateUpdate(void)
  0x2dac0 undefined UnSpawn(void)
  0x2dc90 undefined GetLookAhead(void)
  0x2dd80 undefined DriveToPointTraffic(undefined4 param_1, undefined4 param_2)
  0x2e0a0 undefined DriveToPointSub(undefined4 param_1, undefined4 param_2)
  0x2e910 undefined FireBullets(void)
  0x2eb40 undefined FireRockets(void)
  0x2ee00 undefined FireMissiles(void)
  0x2f0e0 undefined DoWeaponUpdate(void)
  0x2f190 undefined Spawn(undefined2 param_1)
  0x2f4c0 undefined DoTrafficStateUpdate(void)
  0x2f610 undefined DriveToPoint(undefined4 param_1, undefined4 param_2)
  0x2fde0 undefined CheckAgentRoadNetworkCollision(void)
  0x306b0 undefined CheckAgentDirectCollision(undefined4 param_1)
  0x30b80 undefined CheckTrafficCollision(undefined4 param_1)
  0x30fb0 undefined DoMoveMode(void)
  0x31750 undefined Update(void)

PS2 methods (115):
  0x12dd80 AIGroundVehicle::AIGroundVehicle
  0x12de30 AIGroundVehicle::~AIGroundVehicle
  0x12ded8 AIGroundVehicle::Reset
  0x12e350 AIGroundVehicle::Update
  0x12e468 AIGroundVehicle::SetNavigateMode
  0x12e670 AIGroundVehicle::SetTargetMode
  0x12e6f8 AIGroundVehicle::SetAttackMode
  0x12e770 AIGroundVehicle::SetSecondaryTarget
  0x12e7d0 AIGroundVehicle::GetTargetPos
  0x12e808 AIGroundVehicle::DoAttackMode
  0x12f170 AIGroundVehicle::TurnTurret
  0x12f730 AIGroundVehicle::FireBullets
  0x12fa28 AIGroundVehicle::FireRockets
  0x12fd58 AIGroundVehicle::FireMissiles
  0x130050 AIGroundVehicle::DoWeaponUpdate
  0x130140 AIGroundVehicle::UpdateNavPos
  0x130508 AIGroundVehicle::DoMoveMode
  0x130e88 AIGroundVehicle::EngageAutoDrive
  0x130f98 AIGroundVehicle::DisengageAutoDrive
  0x131018 AIGroundVehicle::DoPlayerLogic
  0x131080 AIGroundVehicle::DoAutoDriveUpdate
  0x131508 AIGroundVehicle::EngageSplinePath
  0x131590 AIGroundVehicle::DisengageSplinePath
  0x131620 AIGroundVehicle::DoSplinePathUpdate
  0x131770 AIGroundVehicle::DoAgentStateUpdate
  0x132808 AIGroundVehicle::Spawn
  0x132ba8 AIGroundVehicle::UnSpawn
  0x132d80 AIGroundVehicle::DoTrafficStateUpdate
  0x132f80 AIGroundVehicle::GetLookAhead
  0x1330d0 AIGroundVehicle::DrivableVec
  0x133238 AIGroundVehicle::BarriersInPath
  0x133850 AIGroundVehicle::UpdateStuck
  0x133ab0 AIGroundVehicle::DriveToPointTraffic
  0x133f60 AIGroundVehicle::DriveToPoint
  0x1349c0 AIGroundVehicle::DriveToPointSub
  0x135018 AIGroundVehicle::GetHeliCollNav
  0x1351d0 AIGroundVehicle::GetCollNav
  0x135cf0 AIGroundVehicle::GetBestWeightedLane
  0x135d88 AIGroundVehicle::GetRoadNetworkLaneWeights
  0x135f70 AIGroundVehicle::CheckAgentRoadNetworkCollision
  0x1369a0 AIGroundVehicle::GetDirectLaneWeights
  0x137070 AIGroundVehicle::WeightDirectLanesPosRad
  0x137310 AIGroundVehicle::CheckAgentDirectCollision
  0x1377c8 AIGroundVehicle::CheckTrafficCollision
  0x137f58 AIGroundVehicle::GetType
  0x137f60 AIGroundVehicle::SetType
  0x137f68 AIGroundVehicle::GetUpdateState
  0x137f70 AIGroundVehicle::SetUpdateState
  0x137f78 AIGroundVehicle::GetNavigateMode
  0x137f80 AIGroundVehicle::GetTargetMode
  0x137f88 AIGroundVehicle::GetAttackMode
  0x137f90 AIGroundVehicle::GetAICommand
  0x137f98 AIGroundVehicle::SetAICommand
  0x137fa0 AIGroundVehicle::GetLaneWeightWidth
  0x137fa8 AIGroundVehicle::SetLaneWeightWidth
  0x137fb0 AIGroundVehicle::GetMinDistToTarget
  0x137fb8 AIGroundVehicle::SetMinDistToTarget
  0x137fc0 AIGroundVehicle::SetSpeedLimit
  0x137fc8 AIGroundVehicle::SetSpeedScaler
  0x137fd0 AIGroundVehicle::SetRamFreq
  0x137fd8 AIGroundVehicle::SetLeadScaler
  0x137fe0 AIGroundVehicle::SetRateOfFire
  0x137fe8 AIGroundVehicle::SetCollAvoidance
  0x137ff8 AIGroundVehicle::SetRecordedGlue
  0x138008 AIGroundVehicle::ResetTrackState
  0x138020 AIGroundVehicle::DisableSteering
  0x138030 AIGroundVehicle::EnableSteering
  0x138038 AIGroundVehicle::DisabledSteering
  0x138040 AIGroundVehicle::GetBeenInSmoke
  0x138048 AIGroundVehicle::GetBeenInOil
  0x138050 AIGroundVehicle::SetDisabled
  0x138058 AIGroundVehicle::ForceFullUpdate
  0x138068 AIGroundVehicle::SetCanSpawn
  0x138070 AIGroundVehicle::GetSpawnPos
  0x138078 AIGroundVehicle::SetSpawnTimer
  0x138080 AIGroundVehicle::SetIsSub
  0x138088 AIGroundVehicle::GetIsSub
  0x138090 AIGroundVehicle::SetUseCeilingFloor
  0x138098 AIGroundVehicle::SetCeiling
  0x1380a0 AIGroundVehicle::SetFloor
  0x1380a8 AIGroundVehicle::SetWeaponsActive
  0x1380b0 AIGroundVehicle::SetHasTurret
  0x1380b8 AIGroundVehicle::GetHasTurret
  0x1380c0 AIGroundVehicle::SetHasMachineGuns
  0x1380c8 AIGroundVehicle::GetHasMachineGuns
  0x1380d0 AIGroundVehicle::SetHasRockets
  0x1380d8 AIGroundVehicle::GetHasRockets
  0x1380e0 AIGroundVehicle::SetHasMissiles
  0x1380e8 AIGroundVehicle::GetHasMissiles
  0x1380f0 AIGroundVehicle::SetDamage
  0x1380f8 AIGroundVehicle::GetDamage
  0x138100 AIGroundVehicle::SetAccuracy
  0x138108 AIGroundVehicle::GetAccuracy
  0x138110 AIGroundVehicle::GetSimCar
  0x138130 AIGroundVehicle::SetSimCar
  0x138150 AIGroundVehicle::GetRecSpeedOffset
  0x138158 AIGroundVehicle::SetRecSpeedOffset
  0x138160 AIGroundVehicle::SetPrimaryTarget
  0x138168 AIGroundVehicle::GetPrimaryTarget
  0x138170 AIGroundVehicle::SetDefaultTarget
  0x138178 AIGroundVehicle::GetDefaultTarget
  0x138180 AIGroundVehicle::GetBulletDir
  0x138188 AIGroundVehicle::GetRocketDir
  0x138190 AIGroundVehicle::GetTurretAngle
  0x138198 AIGroundVehicle::AddCollisionInfo
  0x1381a8 AIGroundVehicle::ClearCollisionInfo
  0x1381b0 AIGroundVehicle::GetReversing
  0x1381b8 AIGroundVehicle::SetDirOverrideTimer
  0x1381c0 AIGroundVehicle::SetReverseOverride
  0x1381d0 AIGroundVehicle::SetMinSimUpdate
  0x1381d8 AIGroundVehicle::GetMinSimUpdate
  0x1381e0 AIGroundVehicle::GetMinSimPos
  0x1381e8 AIGroundVehicle::GetMinSimDir
  0x1381f0 AIGroundVehicle::CancelStuck
  0x138200 AIGroundVehicle::AIGroundVehicle_global_ctors

Sheet rows:
  AIGroundVehicle::AIGroundVehicle(void)
  AIGroundVehicle::~AIGroundVehicle(void)
  AIGroundVehicle::Reset(void)
  AIGroundVehicle::Update(void)
  AIGroundVehicle::SetNavigateMode(AICommandType::ENavigate, CARP
  AIGroundVehicle::SetTargetMode(AICommandType::ETarget, COORD3 &
  AIGroundVehicle::SetAttackMode(AICommandType::EAttackMode)
  AIGroundVehicle::SetSecondaryTarget(CARP::AIElement *)
  AIGroundVehicle::GetTargetPos(void)
  AIGroundVehicle::DoAttackMode(void)
  AIGroundVehicle::TurnTurret(void)
  AIGroundVehicle::FireBullets(void)
  AIGroundVehicle::FireRockets(void)
  AIGroundVehicle::FireMissiles(void)
  AIGroundVehicle::DoWeaponUpdate(void)
  AIGroundVehicle::UpdateNavPos(COORD3 &, float)
  AIGroundVehicle::DoMoveMode(void)
  AIGroundVehicle::EngageAutoDrive(void)
  AIGroundVehicle::DisengageAutoDrive(void)
  AIGroundVehicle::DoPlayerLogic(void)
  AIGroundVehicle::DoAutoDriveUpdate(void)
  AIGroundVehicle::EngageSplinePath(CARP::AISpline *)
  AIGroundVehicle::DisengageSplinePath(void)
  AIGroundVehicle::DoSplinePathUpdate(void)
  AIGroundVehicle::DoAgentStateUpdate(void)
  AIGroundVehicle::Spawn(short)
  AIGroundVehicle::UnSpawn(void)
  AIGroundVehicle::DoTrafficStateUpdate(void)
  AIGroundVehicle::GetLookAhead(float)
  AIGroundVehicle::DrivableVec(COORD3 &, COORD3 &)
  AIGroundVehicle::BarriersInPath(bool)
  AIGroundVehicle::UpdateStuck(void)
  AIGroundVehicle::DriveToPointTraffic(COORD3 *, float)
  AIGroundVehicle::DriveToPoint(COORD3 *, float)
  AIGroundVehicle::DriveToPointSub(COORD3 *, float)
  AIGroundVehicle::GetHeliCollNav(COORD3 &)
  AIGroundVehicle::GetCollNav(COORD3 &, bool, char)
  AIGroundVehicle::GetBestWeightedLane(short)
  AIGroundVehicle::GetRoadNetworkLaneWeights(void)
  AIGroundVehicle::CheckAgentRoadNetworkCollision(COORD3 *)
  AIGroundVehicle::GetDirectLaneWeights(COORD3 *)
  AIGroundVehicle::WeightDirectLanesPosRad(COORD3 &, COORD3 &, CO
  AIGroundVehicle::CheckAgentDirectCollision(COORD3 *)
  AIGroundVehicle::CheckTrafficCollision(COORD3 *)
  AIGroundVehicle type_info function
  AIGroundVehicle::GetType(void)
  AIGroundVehicle::SetType(AIGroundVehicle::EType)
  AIGroundVehicle::GetUpdateState(void)
  AIGroundVehicle::SetUpdateState(AIGroundVehicle::EUpdateState)
  AIGroundVehicle::GetNavigateMode(void)
  AIGroundVehicle::GetTargetMode(void)
  AIGroundVehicle::GetAttackMode(void)
  AIGroundVehicle::GetAICommand(void)
  AIGroundVehicle::SetAICommand(CARP::AICommand *)
  AIGroundVehicle::GetLaneWeightWidth(void)
  AIGroundVehicle::SetLaneWeightWidth(unsigned char)
  AIGroundVehicle::GetMinDistToTarget(void)
  AIGroundVehicle::SetMinDistToTarget(float)
  AIGroundVehicle::SetSpeedLimit(float)
  AIGroundVehicle::SetSpeedScaler(float)
  AIGroundVehicle::SetRamFreq(float)
  AIGroundVehicle::SetLeadScaler(float)
  AIGroundVehicle::SetRateOfFire(float)
  AIGroundVehicle::SetCollAvoidance(char)
  AIGroundVehicle::SetRecordedGlue(char)
  AIGroundVehicle::ResetTrackState(void)
  AIGroundVehicle::DisableSteering(void)
  AIGroundVehicle::EnableSteering(void)
  AIGroundVehicle::DisabledSteering(void)
  AIGroundVehicle::GetBeenInSmoke(void)
  AIGroundVehicle::GetBeenInOil(void)
  AIGroundVehicle::SetDisabled(char)
  AIGroundVehicle::ForceFullUpdate(void)
  AIGroundVehicle::SetCanSpawn(bool)
  AIGroundVehicle::GetSpawnPos(void) const
  AIGroundVehicle::SetSpawnTimer(int)
  AIGroundVehicle::SetIsSub(char)
  AIGroundVehicle::GetIsSub(void)
  AIGroundVehicle::SetUseCeilingFloor(char)
  AIGroundVehicle::SetCeiling(float)

Xbox methods treated as members (42 of 43; untyped ones count when ECX is read before it is written): AIGroundVehicle, BarriersInPath, CheckAgentDirectCollision, CheckAgentRoadNetworkCollision, CheckTrafficCollision, DisengageAutoDrive, DisengageSplinePath, DoAgentStateUpdate, DoAttackMode, DoAutoDriveUpdate, DoMoveMode, DoPlayerLogic, DoSplinePathUpdate, DoTrafficStateUpdate, DoWeaponUpdate, DriveToPoint, DriveToPointSub, DriveToPointTraffic, EngageAutoDrive, EngageSplinePath, FireBullets, FireMissiles, FireRockets, GetBestWeightedLane, GetCollNav, GetDirectLaneWeights, GetHeliCollNav, GetLookAhead, GetRoadNetworkLaneWeights, GetTargetPos, Reset, SetAttackMode, SetNavigateMode, SetSecondaryTarget, SetTargetMode, Spawn, TurnTurret, UnSpawn, Update, UpdateNavPos, UpdateStuck, ~AIGroundVehicle

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: AIGroundVehicle@2bd40, ~AIGroundVehicle@2be70]
  +0x064  w[4] R -> FUN_00029cd0, WRoadNav::IncNavPosition, WRoadNav::InitAtPoint, WRoadNav::InitAtSegment, WRoadNav::Reset, WRoadNav::Rever [17: CheckAgentRoadNetworkCollision@2fde0, CheckTrafficCollision@30b80, DisengageAutoDrive@2a760, DisengageSplinePath@2a850, DoAgentStateUpdate@2cd80, DoAttackMode@2bf40…]
  +0x068  w[1] R/W [4: EngageSplinePath@2a800, Reset@29d60, Spawn@2f190, Update@31750]
  +0x069  w[1] R/W [3: EngageSplinePath@2a800, Reset@29d60, Update@31750]
  +0x06c  w[4] W [1: Reset@29d60]
  +0x080  w[4] R/W [11: AIGroundVehicle@2bd40, DisengageAutoDrive@2a760, DisengageSplinePath@2a850, DoAttackMode@2bf40, DoMoveMode@30fb0, EngageAutoDrive@2a690…]
  +0x084  w[4] R/W [2: DoAgentStateUpdate@2cd80, Reset@29d60]
  +0x088  w[4] R/W [8: DisengageAutoDrive@2a760, DisengageSplinePath@2a850, DoAgentStateUpdate@2cd80, DoAttackMode@2bf40, DoMoveMode@30fb0, DriveToPoint@2f610…]
  +0x08c  w[4] W [1: DoAgentStateUpdate@2cd80]
  +0x090  w[4] R/W [4: DoAgentStateUpdate@2cd80, GetTargetPos@2a3c0, SetTargetMode@2a2c0, TurnTurret@2c6e0]
  +0x094  w[4] R/W [12: DisengageAutoDrive@2a760, DisengageSplinePath@2a850, DoAgentStateUpdate@2cd80, DoAttackMode@2bf40, DoTrafficStateUpdate@2f4c0, DoWeaponUpdate@2f0e0…]
  +0x098  w[4] W [1: Reset@29d60]
  +0x09c  w[4] R/W float [3: DoAttackMode@2bf40, DoMoveMode@30fb0, Reset@29d60]
  +0x0a0  w[2] R/W [3: DoTrafficStateUpdate@2f4c0, Reset@29d60, UnSpawn@2dac0]
  +0x0a4  w[4] R/W [10: CheckAgentDirectCollision@306b0, CheckAgentRoadNetworkCollision@2fde0, DoAgentStateUpdate@2cd80, DoAutoDriveUpdate@2c9d0, DoMoveMode@30fb0, DoTrafficStateUpdate@2f4c0…]
  +0x0a8  w- LEA addr-taken [1: Reset@29d60]
  +0x0e8  w[1] R/W [2: GetBestWeightedLane@2b5e0, Reset@29d60]
  +0x0e9  w[1] R/W [2: BarriersInPath@2aaf0, Reset@29d60]
  +0x0ea  w[1] R/W [2: BarriersInPath@2aaf0, Reset@29d60]
  +0x0eb  w[1] R/W [4: DoMoveMode@30fb0, DoTrafficStateUpdate@2f4c0, Reset@29d60, Spawn@2f190]
  +0x0ec  w[4] R/W [2: GetCollNav@2b210, Reset@29d60]
  +0x0f0  w[4] R/W -> WRoadNav::InitAtPoint, WRoadNav::ReverseNavDirection, WRoadNav::~WRoadNav [4: AIGroundVehicle@2bd40, GetCollNav@2b210, Reset@29d60, ~AIGroundVehicle@2be70]
  +0x0f4  w[4] R/W [2: GetCollNav@2b210, Reset@29d60]
  +0x0f8  w[4] R/W -> WRoadNav::InitAtPoint, WRoadNav::Reset, WRoadNav::ReverseNavDirection, WRoadNav::~WRoadNav [4: AIGroundVehicle@2bd40, GetCollNav@2b210, Reset@29d60, ~AIGroundVehicle@2be70]
  +0x0fc  w[4] R/W [2: GetHeliCollNav@2b0c0, Reset@29d60]
  +0x100  w[4] R/W -> WRoadNav::InitAtPoint, WRoadNav::Reset, WRoadNav::ReverseNavDirection, WRoadNav::~WRoadNav [4: AIGroundVehicle@2bd40, GetHeliCollNav@2b0c0, Reset@29d60, ~AIGroundVehicle@2be70]
  +0x104  w[4] R/W -> WRoadNav::InitAtPoint, WRoadNav::Reset, WRoadNav::~WRoadNav [4: AIGroundVehicle@2bd40, DoAgentStateUpdate@2cd80, Reset@29d60, ~AIGroundVehicle@2be70]
  +0x108  w[1] R/W [4: DoAgentStateUpdate@2cd80, DriveToPoint@2f610, Reset@29d60, UpdateNavPos@2a3e0]
  +0x109  w[1] R/W [4: DriveToPoint@2f610, DriveToPointSub@2e0a0, Reset@29d60, UpdateNavPos@2a3e0]
  +0x10c  w[4] W float [2: DriveToPoint@2f610, Reset@29d60]
  +0x110  w[4] W float [2: DriveToPoint@2f610, Reset@29d60]
  +0x114  w[4] R/W float [3: DriveToPoint@2f610, Reset@29d60, SetNavigateMode@2a0d0]
  +0x118  w[4] W float [3: DriveToPoint@2f610, Reset@29d60, SetNavigateMode@2a0d0]
  +0x11c  w[1] R/W [2: DoWeaponUpdate@2f0e0, Reset@29d60]
  +0x11d  w[1] R/W [2: DoWeaponUpdate@2f0e0, Reset@29d60]
  +0x11e  w[1] R/W [2: DoWeaponUpdate@2f0e0, Reset@29d60]
  +0x11f  w[1] R/W [2: DoWeaponUpdate@2f0e0, Reset@29d60]
  +0x120  w[1] W [1: Reset@29d60]
  +0x121  w[1] W [1: Reset@29d60]
  +0x124  w[4] W float [2: DoAttackMode@2bf40, Reset@29d60]
  +0x128  w[4] R/W float -> FUN_0001c600 [2: DoAttackMode@2bf40, Reset@29d60]
  +0x12c  w[4] R/W float -> FUN_0001c600 [2: DoAttackMode@2bf40, Reset@29d60]
  +0x130  w[1] R/W [3: DriveToPoint@2f610, DriveToPointSub@2e0a0, Reset@29d60]
  +0x134  w[4] R/W [2: DoAgentStateUpdate@2cd80, Reset@29d60]
  +0x138  w[4] R/W [3: DoAgentStateUpdate@2cd80, DoAttackMode@2bf40, Reset@29d60]
  +0x13c  w[4] W float [2: DoAgentStateUpdate@2cd80, Reset@29d60]
  +0x140  w[4] R/W [2: DoAgentStateUpdate@2cd80, Reset@29d60]
  +0x144  w[1] R/W [6: CheckAgentRoadNetworkCollision@2fde0, DoAgentStateUpdate@2cd80, DoAutoDriveUpdate@2c9d0, DoMoveMode@30fb0, Reset@29d60, SetNavigateMode@2a0d0]
  +0x145  w[1] R/W [2: DoAttackMode@2bf40, Reset@29d60]
  +0x148  w[4] R/RW/W [4: DoAgentStateUpdate@2cd80, DoAutoDriveUpdate@2c9d0, DriveToPoint@2f610, Reset@29d60]
  +0x14c  w[1] R/W [2: DriveToPoint@2f610, Reset@29d60]
  +0x14e  w[2] R/W [5: DoAgentStateUpdate@2cd80, DoAttackMode@2bf40, DoPlayerLogic@2a7c0, DriveToPoint@2f610, Reset@29d60]
  +0x150  w[1] W [2: DoAgentStateUpdate@2cd80, Reset@29d60]
  +0x151  w[1] W [1: Reset@29d60]
  +0x152  w[1] R/W [4: DoAttackMode@2bf40, DoMoveMode@30fb0, DoWeaponUpdate@2f0e0, Reset@29d60]
  +0x154  w[4] R/W [3: DoAgentStateUpdate@2cd80, DoAutoDriveUpdate@2c9d0, Reset@29d60]
  +0x158  w[1] R/W [5: DoMoveMode@30fb0, DriveToPointTraffic@2dd80, Reset@29d60, Spawn@2f190, UpdateNavPos@2a3e0]
  +0x15c  w- LEA addr-taken [2: Reset@29d60, UpdateNavPos@2a3e0]
  +0x168  w- LEA addr-taken [2: Reset@29d60, UpdateNavPos@2a3e0]
  +0x174  w[1] R/W [3: CheckTrafficCollision@30b80, Reset@29d60, UpdateNavPos@2a3e0]
  +0x175  w[1] R/W [3: CheckTrafficCollision@30b80, DoMoveMode@30fb0, Reset@29d60]
  +0x176  w[1] R/W [3: CheckTrafficCollision@30b80, DriveToPointTraffic@2dd80, Reset@29d60]
  +0x177  w[1] R/W [2: Reset@29d60, Update@31750]
  +0x178  w[4] R/W -> AIVehicle::GetPhysicsObject, AIVehicle::GetPosition [12: DisengageAutoDrive@2a760, DisengageSplinePath@2a850, DoAgentStateUpdate@2cd80, DoAttackMode@2bf40, DoWeaponUpdate@2f0e0, DriveToPoint@2f610…]
  +0x17c  w- LEA addr-taken [3: DoAgentStateUpdate@2cd80, Reset@29d60, SetTargetMode@2a2c0]
  +0x190  w- LEA addr-taken [3: DoAgentStateUpdate@2cd80, DoAttackMode@2bf40, Reset@29d60]
  +0x19c  w[1] R/W [2: DoAgentStateUpdate@2cd80, Reset@29d60]
  +0x1a0  w[4] W float [3: DoAgentStateUpdate@2cd80, DoAttackMode@2bf40, Reset@29d60]
  +0x1b0  w- LEA addr-taken [6: DoAgentStateUpdate@2cd80, DoAttackMode@2bf40, DoMoveMode@30fb0, DriveToPoint@2f610, Reset@29d60, UpdateNavPos@2a3e0]
  +0x1c0  w- LEA addr-taken -> AIGroundVehicle::UpdateNavPos [2: DoMoveMode@30fb0, Reset@29d60]
  +0x1cc  w[2] R/W [3: DoAgentStateUpdate@2cd80, Reset@29d60, UpdateNavPos@2a3e0]
  +0x1d0  w[4] W [2: Reset@29d60, SetTargetMode@2a2c0]
  +0x1d4  w[4] R/W [4: DoAgentStateUpdate@2cd80, DoAttackMode@2bf40, Reset@29d60, SetSecondaryTarget@2a380]
  +0x1d8  w[4] R/W [5: DoAgentStateUpdate@2cd80, DoAttackMode@2bf40, Reset@29d60, SetTargetMode@2a2c0, TurnTurret@2c6e0]
  +0x1dc  w[1] W [2: DoAgentStateUpdate@2cd80, Reset@29d60]
  +0x1e0  w- LEA addr-taken -> v3dotprod [4: DoAgentStateUpdate@2cd80, DoAttackMode@2bf40, DoAutoDriveUpdate@2c9d0, Reset@29d60]
  +0x1ec  w[1] R/W [3: DoAgentStateUpdate@2cd80, DoAutoDriveUpdate@2c9d0, Reset@29d60]
  +0x1f0  w[4] R/W float [3: DoAgentStateUpdate@2cd80, DoAttackMode@2bf40, Reset@29d60]
  +0x1f4  w[1] R/W [2: DoWeaponUpdate@2f0e0, Reset@29d60]
  +0x1f6  w[2] W [1: Reset@29d60]
  +0x1f8  w[4] W [1: Reset@29d60]
  +0x1fc  w[2] W [1: Reset@29d60]
  +0x1fe  w[1] W [1: Reset@29d60]
  +0x200  w[2] W [1: Reset@29d60]
  +0x202  w[1] W [1: Reset@29d60]
  +0x204  w[4] W [1: Reset@29d60]
  +0x208  w[1] W [1: Reset@29d60]
  +0x210  w- LEA addr-taken [1: Reset@29d60]
  +0x220  w- LEA addr-taken [1: Reset@29d60]
  +0x22c  w[2] R/W [4: DoAgentStateUpdate@2cd80, DriveToPoint@2f610, DriveToPointSub@2e0a0, Reset@29d60]
  +0x22e  w[1] R/W [4: DoAgentStateUpdate@2cd80, DriveToPoint@2f610, DriveToPointSub@2e0a0, Reset@29d60]
  +0x230  w[2] R/RW/W [3: DoAutoDriveUpdate@2c9d0, Reset@29d60, UpdateStuck@2af50]
  +0x232  w[1] R/W [4: DoAgentStateUpdate@2cd80, DoAutoDriveUpdate@2c9d0, Reset@29d60, UpdateStuck@2af50]
  +0x240  w- LEA addr-taken [2: Reset@29d60, UpdateStuck@2af50]
  +0x24c  w[1] R/W [3: DoAgentStateUpdate@2cd80, DoAutoDriveUpdate@2c9d0, Reset@29d60]
  +0x250  w- LEA addr-taken [2: Reset@29d60, Spawn@2f190]
  +0x25c  w[1] R/W [2: Reset@29d60, Spawn@2f190]
  +0x260  w[4] R/W [3: Reset@29d60, Spawn@2f190, UnSpawn@2dac0]

PS2 this-relative accesses (PS2 offsets):
  +0x054  w[4] R -> AIVehicle::SetSplinePath, WRoadNav::ChangeLanes, WRoadNav::IncNavPosition, WRoadNav::InitAtPoint, WRoadNav::InitAtSegmen [14: CheckAgentRoadNetworkCollision@135f70, CheckTrafficCollision@1377c8, DoAgentStateUpdate@131770, DoAttackMode@12e808, DoAutoDriveUpdate@131080, DoMoveMode@130508…]
  +0x058  w[4] R/W [4: EngageSplinePath@131508, Reset@12ded8, Spawn@132808, Update@12e350]
  +0x05c  w[4] R/W [3: EngageSplinePath@131508, Reset@12ded8, Update@12e350]
  +0x060  w[4] W [1: Reset@12ded8]
  +0x06c  w[4] W [2: AIGroundVehicle@12dd80, ~AIGroundVehicle@12de30]
  +0x070  w[4] R/W [9: AIGroundVehicle@12dd80, DoAttackMode@12e808, DoMoveMode@130508, EngageAutoDrive@130e88, EngageSplinePath@131508, GetType@137f58…]
  +0x074  w[4] R/W -> AIVehicle::GetPhysicsObject [2: DoAgentStateUpdate@131770, Reset@12ded8]
  +0x078  w[4] R/W [6: DoAgentStateUpdate@131770, DoAttackMode@12e808, DoMoveMode@130508, DriveToPoint@133f60, GetNavigateMode@137f78, SetNavigateMode@12e468]
  +0x07c  w[4] W [1: DoAgentStateUpdate@131770]
  +0x080  w[4] R/W [5: DoAgentStateUpdate@131770, GetTargetMode@137f80, GetTargetPos@12e7d0, SetTargetMode@12e670, TurnTurret@12f170]
  +0x084  w[4] R/W [8: DoAgentStateUpdate@131770, DoAttackMode@12e808, DoTrafficStateUpdate@132d80, DriveToPoint@133f60, GetAttackMode@137f88, SetAttackMode@12e6f8…]
  +0x088  w[4] R/W [3: GetAICommand@137f90, Reset@12ded8, SetAICommand@137f98]
  +0x08c  w[4] R/W float [3: DoAttackMode@12e808, DoMoveMode@130508, Reset@12ded8]
  +0x090  w[2] R/W [3: DoTrafficStateUpdate@132d80, Reset@12ded8, UnSpawn@132ba8]
  +0x094  w[4] R/W [11: CheckAgentDirectCollision@137310, CheckAgentRoadNetworkCollision@135f70, DoAgentStateUpdate@131770, DoAutoDriveUpdate@131080, DoMoveMode@130508, DoTrafficStateUpdate@132d80…]
  +0x098  w- LEA addr-taken [3: GetBestWeightedLane@135cf0, GetDirectLaneWeights@1369a0, GetRoadNetworkLaneWeights@135d88]
  +0x0b7  w[1] W [2: CheckAgentRoadNetworkCollision@135f70, GetDirectLaneWeights@1369a0]
  +0x0b8  w[1] W [2: CheckAgentRoadNetworkCollision@135f70, GetDirectLaneWeights@1369a0]
  +0x0b9  w[1] W [2: CheckAgentRoadNetworkCollision@135f70, GetDirectLaneWeights@1369a0]
  +0x0d7  w- LEA addr-taken [2: GetDirectLaneWeights@1369a0, Reset@12ded8]
  +0x0d8  w[1] R/W [6: CheckAgentDirectCollision@137310, CheckAgentRoadNetworkCollision@135f70, GetBestWeightedLane@135cf0, GetLaneWeightWidth@137fa0, Reset@12ded8, SetLaneWeightWidth@137fa8]
  +0x0dc  w[4] R/W [4: AddCollisionInfo@138198, BarriersInPath@133238, ClearCollisionInfo@1381a8, Reset@12ded8]
  +0x0e0  w[1] R/W [3: AddCollisionInfo@138198, BarriersInPath@133238, Reset@12ded8]
  +0x0e4  w[4] R/W [4: DoMoveMode@130508, DoTrafficStateUpdate@132d80, Reset@12ded8, Spawn@132808]
  +0x0e8  w[8] R/W [2: GetCollNav@1351d0, Reset@12ded8]
  +0x0f0  w[4] R/W -> AIVehicle::GetPhysicsObject, WRoadNav::~WRoadNav [4: AIGroundVehicle@12dd80, GetCollNav@1351d0, Reset@12ded8, ~AIGroundVehicle@12de30]
  +0x0f8  w[8] R/W [2: GetCollNav@1351d0, Reset@12ded8]
  +0x100  w[4] R/W -> AIVehicle::GetPhysicsObject, WRoadNav::Reset, WRoadNav::~WRoadNav [4: AIGroundVehicle@12dd80, GetCollNav@1351d0, Reset@12ded8, ~AIGroundVehicle@12de30]
  +0x108  w[8] R/W [2: GetHeliCollNav@135018, Reset@12ded8]
  +0x110  w[4] R/W -> AIVehicle::GetPhysicsObject, WRoadNav::Reset, WRoadNav::~WRoadNav [4: AIGroundVehicle@12dd80, GetHeliCollNav@135018, Reset@12ded8, ~AIGroundVehicle@12de30]
  +0x114  w[4] R/W -> WRoadNav::InitAtPoint, WRoadNav::~WRoadNav [5: AIGroundVehicle@12dd80, CheckAgentRoadNetworkCollision@135f70, DoAgentStateUpdate@131770, Reset@12ded8, ~AIGroundVehicle@12de30]
  +0x118  w[1] R/W [7: CheckAgentRoadNetworkCollision@135f70, DoAgentStateUpdate@131770, DriveToPoint@133f60, GetIsSub@138088, Reset@12ded8, SetIsSub@138080…]
  +0x119  w[1] R/W [5: DriveToPoint@133f60, DriveToPointSub@1349c0, Reset@12ded8, SetUseCeilingFloor@138090, UpdateNavPos@130140]
  +0x11c  w[4] R/W float [3: DriveToPoint@133f60, Reset@12ded8, SetCeiling@138098]
  +0x120  w[4] R/W float [3: DriveToPoint@133f60, Reset@12ded8, SetFloor@1380a0]
  +0x124  w[4] R/W float [3: DriveToPoint@133f60, Reset@12ded8, SetNavigateMode@12e468]
  +0x128  w[4] R/W float [3: DriveToPoint@133f60, Reset@12ded8, SetNavigateMode@12e468]
  +0x12c  w[1] R/W [5: FireBullets@12f730, FireRockets@12fa28, GetHasTurret@1380b8, Reset@12ded8, SetHasTurret@1380b0]
  +0x12d  w[1] R/W [3: GetHasMachineGuns@1380c8, Reset@12ded8, SetHasMachineGuns@1380c0]
  +0x12e  w[1] R/W [3: GetHasRockets@1380d8, Reset@12ded8, SetHasRockets@1380d0]
  +0x12f  w[1] R/W [3: GetHasMissiles@1380e8, Reset@12ded8, SetHasMissiles@1380e0]
  +0x130  w[1] R/W [3: GetAccuracy@138108, Reset@12ded8, SetAccuracy@138100]
  +0x131  w[1] R/W [3: GetDamage@1380f8, Reset@12ded8, SetDamage@1380f0]
  +0x134  w[4] R/W float [4: DoAttackMode@12e808, GetRecSpeedOffset@138150, Reset@12ded8, SetRecSpeedOffset@138158]
  +0x138  w[4] R/W float [3: DoAttackMode@12e808, Reset@12ded8, SetSpeedLimit@137fc0]
  +0x13c  w[4] R/W float [3: DoAttackMode@12e808, Reset@12ded8, SetSpeedScaler@137fc8]
  +0x140  w[4] R/W [4: DriveToPoint@133f60, DriveToPointSub@1349c0, GetReversing@1381b0, Reset@12ded8]
  +0x144  w[4] R/W [2: DoAgentStateUpdate@131770, Reset@12ded8]
  +0x148  w[4] R/W [4: CheckAgentRoadNetworkCollision@135f70, DoAgentStateUpdate@131770, DoAttackMode@12e808, Reset@12ded8]
  +0x14c  w[4] R/W float [3: DoAgentStateUpdate@131770, Reset@12ded8, SetRamFreq@137fd0]
  +0x150  w[4] R/W float [3: DoAgentStateUpdate@131770, Reset@12ded8, SetLeadScaler@137fd8]
  +0x154  w[4] R/W [7: CheckAgentRoadNetworkCollision@135f70, DoAgentStateUpdate@131770, DoAutoDriveUpdate@131080, DoMoveMode@130508, Reset@12ded8, SetCollAvoidance@137fe8…]
  +0x158  w[4] R/W [3: DoAttackMode@12e808, Reset@12ded8, SetRecordedGlue@137ff8]
  +0x15c  w[4] R/W [4: DoAgentStateUpdate@131770, DoAutoDriveUpdate@131080, DriveToPoint@133f60, Reset@12ded8]
  +0x160  w[4] R/W [5: DisableSteering@138020, DisabledSteering@138038, DriveToPoint@133f60, EnableSteering@138030, Reset@12ded8]
  +0x164  w[2] R/W [4: DoAgentStateUpdate@131770, DoAttackMode@12e808, DriveToPoint@133f60, Reset@12ded8]
  +0x168  w[4] R/W [3: DoAgentStateUpdate@131770, GetBeenInSmoke@138040, Reset@12ded8]
  +0x16c  w[4] R/W [2: GetBeenInOil@138048, Reset@12ded8]
  +0x170  w[1] R/W [4: DoAttackMode@12e808, DoMoveMode@130508, Reset@12ded8, SetDisabled@138050]
  +0x174  w[4] R/W [4: DoAgentStateUpdate@131770, DoAutoDriveUpdate@131080, Reset@12ded8, ResetTrackState@138008]
  +0x178  w[4] R/W [7: DoMoveMode@130508, DriveToPointTraffic@133ab0, GetMinSimUpdate@1381d8, Reset@12ded8, SetMinSimUpdate@1381d0, Spawn@132808…]
  +0x17c  w[8] LEA/W addr-taken [3: GetMinSimPos@1381e0, Reset@12ded8, UpdateNavPos@130140]
  +0x184  w[4] W [2: Reset@12ded8, UpdateNavPos@130140]
  +0x188  w[8] LEA/W addr-taken [3: GetMinSimDir@1381e8, Reset@12ded8, UpdateNavPos@130140]
  +0x190  w[4] W [2: Reset@12ded8, UpdateNavPos@130140]
  +0x194  w[4] R/W [3: CheckTrafficCollision@1377c8, Reset@12ded8, UpdateNavPos@130140]
  +0x198  w[4] R/W [3: CheckTrafficCollision@1377c8, DoMoveMode@130508, Reset@12ded8]
  +0x19c  w[4] R/W [3: CheckTrafficCollision@1377c8, DriveToPointTraffic@133ab0, Reset@12ded8]
  +0x1a0  w[4] W [2: ForceFullUpdate@138058, Reset@12ded8]
  +0x1a4  w[4] R/W -> AIVehicle::GetPhysicsObject, AIVehicle::GetPosition, Simulation::GetRigidBody, VU0_Sin [16: CheckAgentDirectCollision@137310, CheckAgentRoadNetworkCollision@135f70, DoAgentStateUpdate@131770, DoAttackMode@12e808, DriveToPoint@133f60, EngageAutoDrive@130e88…]
  +0x1a8  w[8] R/W [3: DoAgentStateUpdate@131770, Reset@12ded8, SetTargetMode@12e670]
  +0x1b0  w[4] R/W [3: DoAgentStateUpdate@131770, Reset@12ded8, SetTargetMode@12e670]
  +0x1c0  w[8] R/W [3: DoAgentStateUpdate@131770, DoAttackMode@12e808, Reset@12ded8]
  +0x1c8  w[4] R/W [3: DoAgentStateUpdate@131770, DoAttackMode@12e808, Reset@12ded8]
  +0x1cc  w[4] R/W [6: DoAgentStateUpdate@131770, FireBullets@12f730, FireMissiles@12fd58, FireRockets@12fa28, Reset@12ded8, ResetTrackState@138008]
  +0x1d0  w[4] R/W float [5: DoAgentStateUpdate@131770, DoAttackMode@12e808, FireBullets@12f730, FireRockets@12fa28, Reset@12ded8]
  +0x1e0  w[4, 8] LEA/R/W float addr-taken [3: CheckAgentDirectCollision@137310, Reset@12ded8, UpdateNavPos@130140]
  +0x1e4  w[4] R float [1: CheckAgentDirectCollision@137310]
  +0x1e8  w[4] R/W float [3: CheckAgentDirectCollision@137310, Reset@12ded8, UpdateNavPos@130140]
  +0x1f0  w[8] LEA/W addr-taken [2: DoMoveMode@130508, Reset@12ded8]
  +0x1f8  w[4] W [2: DoMoveMode@130508, Reset@12ded8]
  +0x1fc  w[2] R/W [3: DoAgentStateUpdate@131770, Reset@12ded8, UpdateNavPos@130140]
  +0x200  w[4] R/W [4: GetDefaultTarget@138178, Reset@12ded8, SetDefaultTarget@138170, SetTargetMode@12e670]
  +0x204  w[4] R/W [5: CheckAgentRoadNetworkCollision@135f70, DoAgentStateUpdate@131770, DoAttackMode@12e808, Reset@12ded8, SetSecondaryTarget@12e770]
  +0x208  w[4] R/W [10: CheckAgentRoadNetworkCollision@135f70, DoAgentStateUpdate@131770, DoAttackMode@12e808, FireBullets@12f730, FireMissiles@12fd58, FireRockets@12fa28…]
  +0x20c  w[4] W [2: DoAgentStateUpdate@131770, Reset@12ded8]
  +0x210  w[8] W [1: Reset@12ded8]
  +0x218  w[4] W [1: Reset@12ded8]
  +0x21c  w[4] R/W [4: DoAgentStateUpdate@131770, DoAutoDriveUpdate@131080, Reset@12ded8, ResetTrackState@138008]
  +0x220  w[4] R/W float [5: DoAgentStateUpdate@131770, DoAttackMode@12e808, GetMinDistToTarget@137fb0, Reset@12ded8, SetMinDistToTarget@137fb8]
  +0x224  w[1] W [2: Reset@12ded8, SetWeaponsActive@1380a8]
  +0x226  w[2] R/W [2: FireBullets@12f730, Reset@12ded8]
  +0x228  w[4] R/W float [4: FireBullets@12f730, FireRockets@12fa28, Reset@12ded8, SetRateOfFire@137fe0]
  +0x22c  w[2] R/W [2: FireRockets@12fa28, Reset@12ded8]
  +0x22e  w[1] R/W [2: FireRockets@12fa28, Reset@12ded8]
  +0x230  w[2] W [1: Reset@12ded8]
  +0x232  w[1] W [1: Reset@12ded8]
  +0x234  w[4] R/W float [3: GetTurretAngle@138190, Reset@12ded8, TurnTurret@12f170]
  +0x238  w[1] R/W [4: FireBullets@12f730, FireRockets@12fa28, Reset@12ded8, TurnTurret@12f170]
  +0x240  w[8] LEA/W addr-taken [2: GetBulletDir@138180, Reset@12ded8]
  +0x248  w[4] W [1: Reset@12ded8]
  +0x250  w[8] LEA/W addr-taken [2: GetRocketDir@138188, Reset@12ded8]
  +0x258  w[4] W [1: Reset@12ded8]
  +0x25c  w[2] R/W [5: DoAgentStateUpdate@131770, DriveToPoint@133f60, DriveToPointSub@1349c0, Reset@12ded8, SetDirOverrideTimer@1381b8]
  +0x260  w[4] R/W [5: DoAgentStateUpdate@131770, DriveToPoint@133f60, DriveToPointSub@1349c0, Reset@12ded8, SetReverseOverride@1381c0]
  +0x264  w[2] R/W [4: CancelStuck@1381f0, DoAutoDriveUpdate@131080, Reset@12ded8, UpdateStuck@133850]
  +0x268  w[4] R/W [5: CancelStuck@1381f0, DoAgentStateUpdate@131770, DoAutoDriveUpdate@131080, Reset@12ded8, UpdateStuck@133850]
  +0x270  w[8] W [2: Reset@12ded8, UpdateStuck@133850]
  +0x278  w[4] W [2: Reset@12ded8, UpdateStuck@133850]
  +0x27c  w[4] R/W [3: DoAgentStateUpdate@131770, DoAutoDriveUpdate@131080, Reset@12ded8]
  +0x280  w[8] LEA/W addr-taken [3: GetSpawnPos@138070, Reset@12ded8, Spawn@132808]
  +0x288  w[4] W [2: Reset@12ded8, Spawn@132808]
  +0x28c  w[4] R/W [3: Reset@12ded8, SetCanSpawn@138068, Spawn@132808]
  +0x290  w[4] R/W [4: Reset@12ded8, SetSpawnTimer@138078, Spawn@132808, UnSpawn@132ba8]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
