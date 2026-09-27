# AIHelicopter

FastAlloc/constructed sizes under its tag: None
deleting destructor 0x35df0 frees/deletes with size 0x140 (call to ??_M@YGXPAXIHP6EX0@Z@Z)
deleting destructor 0x35df0 frees/deletes with size 0x140 (call to UMemory::FastFree)
Xbox vtable 0x0018b950 (5 slots) stored by its constructor
PS2 sheet virtual table row: ['AIHelicopter virtual table']
constructor 0x32a60 first calls: ['AIVehicle::AIVehicle', 'UMemory::FastAlloc', 'WRoadNav::WRoadNav']

Xbox methods (19):
  0x31850 undefined EnableTargetBeacon(void)
  0x318d0 undefined DisableTargetBeacon(void)
  0x31910 undefined GetDirectionToTarget(undefined4 param_1)
  0x31990 undefined GetPlayerLaneOffsets(void)
  0x31a50 undefined FireRockets(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0x31c80 undefined EngageSplinePath(void)
  0x31ca0 undefined DisengageSplinePath(void)
  0x31cd0 undefined ManeuverToPos(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined1 param_4)
  0x321d0 undefined ~AIHelicopter(void)
  0x32280 undefined Reset(void)
  0x324d0 undefined FireBullets(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, undefine
  0x32700 undefined NewState(undefined4 param_1)
  0x32a20 undefined GetLookAhead(undefined4 param_1)
  0x32a60 undefined AIHelicopter(void)
  0x32af0 undefined ControlMoveState(undefined4 param_1, undefined1 param_2)
  0x331c0 undefined DoAgentLogic(void)
  0x34600 undefined DoProximityDestruct(void)
  0x34760 undefined Update(void)
  0x35df0 undefined scalar_deleting_destructor(undefined1 param_1)

PS2 methods (38):
  0x138220 AIHelicopter::AIHelicopter
  0x138280 AIHelicopter::~AIHelicopter
  0x1382f0 AIHelicopter::Reset
  0x138528 AIHelicopter::EnableTargetBeacon
  0x138588 AIHelicopter::DisableTargetBeacon
  0x1385d8 AIHelicopter::GetDirectionToTarget
  0x1386b8 AIHelicopter::GetPlayerLaneOffsets
  0x1387a0 AIHelicopter::FireRockets
  0x138b88 AIHelicopter::FireBullets
  0x138ef0 AIHelicopter::Update
  0x138fa8 AIHelicopter::NewState
  0x139380 AIHelicopter::ControlMoveState
  0x139be8 AIHelicopter::DoAgentLogic
  0x13b2a8 AIHelicopter::DoProximityDestruct
  0x13b468 AIHelicopter::EngageSplinePath
  0x13b498 AIHelicopter::DisengageSplinePath
  0x13b4c8 AIHelicopter::GetLookAhead
  0x13b4f8 AIHelicopter::ManeuverToPos
  0x13bd80 AIHelicopter::operator_new
  0x13bda0 AIHelicopter::operator_delete
  0x13bdc0 AIHelicopter::GetType
  0x13bdc8 AIHelicopter::SetType
  0x13bdd0 AIHelicopter::GetNavigateMode
  0x13bdd8 AIHelicopter::SetNavigateMode
  0x13bde0 AIHelicopter::GetTargetMode
  0x13bde8 AIHelicopter::SetTargetMode
  0x13bdf0 AIHelicopter::GetAttackMode
  0x13bdf8 AIHelicopter::SetAttackMode
  0x13be00 AIHelicopter::GetSimHeli
  0x13be20 AIHelicopter::SetSimHeli
  0x13be40 AIHelicopter::SetAttackedPosition
  0x13be60 AIHelicopter::SetTargetPosition
  0x13be80 AIHelicopter::GetHeliTargetPosition
  0x13be88 AIHelicopter::GetHeliBulletDir
  0x13be90 AIHelicopter::GetTargetBeacon
  0x13be98 AIHelicopter::SetPrimaryTarget
  0x13bea0 AIHelicopter::GetPrimaryTarget
  0x13bea8 AIHelicopter::AIHelicopter_global_ctors

Sheet rows:
  AIHelicopter::AIHelicopter(void)
  AIHelicopter::~AIHelicopter(void)
  AIHelicopter::Reset(void)
  AIHelicopter::EnableTargetBeacon(bool)
  AIHelicopter::DisableTargetBeacon(void)
  AIHelicopter::GetDirectionToTarget(COORD3 &)
  AIHelicopter::GetPlayerLaneOffsets(void)
  AIHelicopter::FireBullets(float, COORD3 &, COORD3 &, COORD3 &)
  AIHelicopter::FireRockets(float, float, COORD3 &, COORD3 &, COO
  AIHelicopter::Update(void)
  AIHelicopter::NewState(CARP::AICommand *)
  AIHelicopter::ControlMoveState(bool, bool)
  AIHelicopter::DoAgentLogic(void)
  AIHelicopter::DoProximityDestruct(void)
  AIHelicopter::EngageSplinePath(CARP::AISpline *)
  AIHelicopter::DisengageSplinePath(void)
  AIHelicopter::GetLookAhead(float, float)
  AIHelicopter::ManeuverToPos(COORD3 *, float, float, bool)
  AIHelicopter type_info function
  AIHelicopter::operator new(unsigned int)
  AIHelicopter::operator delete(void *, unsigned int)
  AIHelicopter::GetType(void)
  AIHelicopter::SetType(AIHelicopter::EType)
  AIHelicopter::GetNavigateMode(void)
  AIHelicopter::SetNavigateMode(AICommandType::ENavigate)
  AIHelicopter::GetTargetMode(void)
  AIHelicopter::SetTargetMode(AICommandType::ETarget)
  AIHelicopter::GetAttackMode(void)
  AIHelicopter::SetAttackMode(AICommandType::EAttackMode)
  AIHelicopter::GetSimHeli(void)
  AIHelicopter::SetSimHeli(PHelicopter *)
  AIHelicopter::SetAttackedPosition(COORD3 &)
  AIHelicopter::SetTargetPosition(COORD3 &)
  AIHelicopter::GetHeliTargetPosition(void)
  AIHelicopter::GetHeliBulletDir(void)
  AIHelicopter::GetTargetBeacon(void)
  AIHelicopter::SetPrimaryTarget(AIGroundVehicle *)
  AIHelicopter::GetPrimaryTarget(void)
  AIHelicopter virtual table
  AIHelicopter type_info node

Xbox methods treated as members (19 of 19; untyped ones count when ECX is read before it is written): AIHelicopter, ControlMoveState, DisableTargetBeacon, DisengageSplinePath, DoAgentLogic, DoProximityDestruct, EnableTargetBeacon, EngageSplinePath, FireBullets, FireRockets, GetDirectionToTarget, GetLookAhead, GetPlayerLaneOffsets, ManeuverToPos, NewState, Reset, Update, scalar_deleting_destructor, ~AIHelicopter

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: AIHelicopter@32a60, ~AIHelicopter@321d0]
  +0x064  w[4] R -> WRoadNav::Reset [3: GetPlayerLaneOffsets@31990, NewState@32700, Reset@32280]
  +0x068  w[1] R/W [3: EngageSplinePath@31c80, Reset@32280, Update@34760]
  +0x069  w[1] R/W [3: EngageSplinePath@31c80, Reset@32280, Update@34760]
  +0x06c  w[4] W [1: Reset@32280]
  +0x084  w[4] R/W [6: ControlMoveState@32af0, DisengageSplinePath@31ca0, EngageSplinePath@31c80, ManeuverToPos@31cd0, NewState@32700, Reset@32280]
  +0x088  w[4] R/W [3: NewState@32700, Reset@32280, Update@34760]
  +0x08c  w[4] R/W [8: ControlMoveState@32af0, DisengageSplinePath@31ca0, DoAgentLogic@331c0, FireBullets@324d0, ManeuverToPos@31cd0, NewState@32700…]
  +0x090  w[4] W [2: NewState@32700, Reset@32280]
  +0x094  w[4] W [2: NewState@32700, Reset@32280]
  +0x098  w[4] R/W -> WTargetable::RemoveReference [5: AIHelicopter@32a60, DisableTargetBeacon@318d0, EnableTargetBeacon@31850, Reset@32280, ~AIHelicopter@321d0]
  +0x09c  w[4] R/W -> WRoadNav::InitAtPoint, WRoadNav::Reset, WRoadNav::~WRoadNav [4: AIHelicopter@32a60, ManeuverToPos@31cd0, Reset@32280, ~AIHelicopter@321d0]
  +0x0a0  w- LEA addr-taken [2: NewState@32700, Reset@32280]
  +0x0ac  w[4] R/W -> AIVehicle::GetPhysicsObject [5: DisengageSplinePath@31ca0, GetPlayerLaneOffsets@31990, NewState@32700, Reset@32280, Update@34760]
  +0x0b0  w- LEA addr-taken [4: DoAgentLogic@331c0, NewState@32700, Reset@32280, Update@34760]
  +0x0bc  w[4] W [1: Reset@32280]
  +0x0c0  w- LEA addr-taken [2: GetDirectionToTarget@31910, Reset@32280]
  +0x0cc  w[4] W float [2: NewState@32700, Reset@32280]
  +0x0d0  w- LEA addr-taken [2: NewState@32700, Reset@32280]
  +0x0dc  w[4] W [1: Reset@32280]
  +0x0e0  w[1] W [1: Reset@32280]
  +0x0e1  w[1] W [1: Reset@32280]
  +0x0e2  w[1] W [1: Reset@32280]
  +0x0e3  w[1] W [1: Reset@32280]
  +0x0e4  w[2] W [1: Reset@32280]
  +0x0e6  w[1] R/W [3: ManeuverToPos@31cd0, NewState@32700, Reset@32280]
  +0x0e7  w[1] W [1: Reset@32280]
  +0x0e8  w[4] W [2: NewState@32700, Reset@32280]
  +0x0ec  w[1] R/W [3: FireBullets@324d0, NewState@32700, Reset@32280]
  +0x0ed  w[1] W [2: NewState@32700, Reset@32280]
  +0x0ee  w[1] R/W [2: DoProximityDestruct@34600, Reset@32280]
  +0x100  w- LEA addr-taken [1: Reset@32280]
  +0x10c  w[1] W [1: Reset@32280]
  +0x10d  w[1] W [2: DoAgentLogic@331c0, Reset@32280]
  +0x10e  w[1] W [1: Reset@32280]
  +0x110  w[2] R/W [3: FireBullets@324d0, NewState@32700, Reset@32280]
  +0x112  w[2] R/W [3: FireRockets@31a50, NewState@32700, Reset@32280]
  +0x120  w- LEA addr-taken [1: Reset@32280]
  +0x12c  w[4] W float [2: NewState@32700, Reset@32280]
  +0x130  w[4] W float [4: FireBullets@324d0, FireRockets@31a50, NewState@32700, Reset@32280]

PS2 this-relative accesses (PS2 offsets):
  +0x054  w[4] R -> WRoadNav::ChangeLanes, WRoadNav::IncNavPosition, WRoadNav::InitAtPoint [5: ControlMoveState@139380, DoAgentLogic@139be8, GetPlayerLaneOffsets@1386b8, NewState@138fa8, Reset@1382f0]
  +0x058  w[4] R/W [2: Reset@1382f0, Update@138ef0]
  +0x05c  w[4] R/W [3: EngageSplinePath@13b468, Reset@1382f0, Update@138ef0]
  +0x060  w[4] W [1: Reset@1382f0]
  +0x06c  w[4] W [1: ~AIHelicopter@138280]
  +0x070  w[4] R/W [2: GetType@13bdc0, SetType@13bdc8]
  +0x074  w[4] R/W [9: ControlMoveState@139380, DisengageSplinePath@13b498, DoAgentLogic@139be8, EngageSplinePath@13b468, GetNavigateMode@13bdd0, ManeuverToPos@13b4f8…]
  +0x078  w[4] R/W [6: ControlMoveState@139380, DoAgentLogic@139be8, GetTargetMode@13bde0, NewState@138fa8, Reset@1382f0, SetTargetMode@13bde8]
  +0x07c  w[4] R/W [9: ControlMoveState@139380, DisengageSplinePath@13b498, DoAgentLogic@139be8, FireBullets@138b88, GetAttackMode@13bdf0, ManeuverToPos@13b4f8…]
  +0x080  w[4] R/W float [3: ControlMoveState@139380, NewState@138fa8, Reset@1382f0]
  +0x084  w[4] R/W float [3: ControlMoveState@139380, NewState@138fa8, Reset@1382f0]
  +0x088  w[4] R/W [3: DisableTargetBeacon@138588, EnableTargetBeacon@138528, GetTargetBeacon@13be90]
  +0x08c  w[4] R -> WRoadNav::InitAtPoint, WRoadNav::~WRoadNav [3: ManeuverToPos@13b4f8, Reset@1382f0, ~AIHelicopter@138280]
  +0x090  w[8] LEA/W addr-taken [4: DoAgentLogic@139be8, NewState@138fa8, Reset@1382f0, SetAttackedPosition@13be40]
  +0x098  w[4] W [3: NewState@138fa8, Reset@1382f0, SetAttackedPosition@13be40]
  +0x09c  w[4] R/W -> AIVehicle::GetPhysicsObject, Simulation::GetRigidBody [7: ControlMoveState@139380, DoAgentLogic@139be8, GetPlayerLaneOffsets@1386b8, GetPrimaryTarget@13bea0, NewState@138fa8, Reset@1382f0…]
  +0x0a0  w[8] LEA/R/W addr-taken [5: ControlMoveState@139380, DoAgentLogic@139be8, NewState@138fa8, Reset@1382f0, SetTargetPosition@13be60]
  +0x0a8  w[4] R/W [5: ControlMoveState@139380, DoAgentLogic@139be8, NewState@138fa8, Reset@1382f0, SetTargetPosition@13be60]
  +0x0ac  w[4] R/W float [3: ControlMoveState@139380, DoAgentLogic@139be8, Reset@1382f0]
  +0x0b0  w[8] R/W [2: DoAgentLogic@139be8, Reset@1382f0]
  +0x0b8  w[4] R/W [2: DoAgentLogic@139be8, Reset@1382f0]
  +0x0bc  w[4] R/W float [4: ControlMoveState@139380, DoAgentLogic@139be8, NewState@138fa8, Reset@1382f0]
  +0x0c0  w[8] LEA/W addr-taken [4: ControlMoveState@139380, DoAgentLogic@139be8, NewState@138fa8, Reset@1382f0]
  +0x0c4  w[4] W [1: DoAgentLogic@139be8]
  +0x0c8  w[4] W [1: Reset@1382f0]
  +0x0cc  w[4] W [1: Reset@1382f0]
  +0x0d0  w[1] W [1: Reset@1382f0]
  +0x0d1  w[1] W [1: Reset@1382f0]
  +0x0d2  w[1] W [1: Reset@1382f0]
  +0x0d3  w[1] W [1: Reset@1382f0]
  +0x0d4  w[2] R/W [3: ControlMoveState@139380, DoAgentLogic@139be8, Reset@1382f0]
  +0x0d6  w[1] R/W [4: DoAgentLogic@139be8, ManeuverToPos@13b4f8, NewState@138fa8, Reset@1382f0]
  +0x0d7  w[1] W [1: Reset@1382f0]
  +0x0d8  w[4] W [2: NewState@138fa8, Reset@1382f0]
  +0x0dc  w[1] R/W [4: DoAgentLogic@139be8, FireBullets@138b88, NewState@138fa8, Reset@1382f0]
  +0x0dd  w[1] R/W [3: DoAgentLogic@139be8, NewState@138fa8, Reset@1382f0]
  +0x0de  w[1] R/W [3: DoAgentLogic@139be8, DoProximityDestruct@13b2a8, Reset@1382f0]
  +0x0f0  w[4, 8] LEA/R/W float addr-taken [3: FireRockets@1387a0, GetHeliBulletDir@13be88, Reset@1382f0]
  +0x0f4  w[4] R/W float [1: FireRockets@1387a0]
  +0x0f8  w[4] R/W float [2: FireRockets@1387a0, Reset@1382f0]
  +0x0fc  w[1] W [1: Reset@1382f0]
  +0x0fd  w[1] W [2: DoAgentLogic@139be8, Reset@1382f0]
  +0x0fe  w[1] R/W [2: FireBullets@138b88, Reset@1382f0]
  +0x100  w[2] R/W [3: FireBullets@138b88, NewState@138fa8, Reset@1382f0]
  +0x102  w[2] R/W [3: FireRockets@1387a0, NewState@138fa8, Reset@1382f0]
  +0x110  w[8] LEA/W addr-taken [3: FireBullets@138b88, GetHeliTargetPosition@13be80, Reset@1382f0]
  +0x118  w[4] W [2: FireBullets@138b88, Reset@1382f0]
  +0x11c  w[4] R/W float [4: ControlMoveState@139380, DoAgentLogic@139be8, NewState@138fa8, Reset@1382f0]
  +0x120  w[4] R/W float [4: FireBullets@138b88, FireRockets@1387a0, NewState@138fa8, Reset@1382f0]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
