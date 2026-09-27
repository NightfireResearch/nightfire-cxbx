# PBondCar

FastAlloc/constructed sizes under its tag: {'allocated': [1056], 'constructed': []}
Xbox vtable 0x0018f580 (84 slots) stored by its constructor
PS2 sheet virtual table row: ['PBondCar virtual table']
constructor 0x6d350 first calls: ['PVehicle::PVehicle', '??_L@YGXPAXIHP6EX0@Z1@Z', '__stricmp']

Xbox methods (102):
  0x61a50 undefined4 __thiscall GetPhysics(PBondCar * this)
  0x61ae0 undefined __stdcall InitializeBondCarGlobals(void)
  0x62120 void __thiscall InitTyreTracks(PBondCar * this)
  0x623b0 void __thiscall InitializeCarControls(PBondCar * this)
  0x62430 void __thiscall FlushCarControls(PBondCar * this)
  0x62440 void __thiscall InitializeCarVariables(PBondCar * this, bool param_1)
  0x62600 void __thiscall ResetDamage(PBondCar * this)
  0x62660 void __thiscall AddDamageByPlayer(PBondCar * this, float dmgAmt)
  0x626e0 undefined GetNumTires(void)
  0x62740 void __thiscall SetInShock(PBondCar * this, float amt)
  0x627c0 undefined ResetCar(undefined4 param_1, undefined4 param_2)
  0x62a40 undefined GetDamageZones(undefined4 param_1)
  0x62a60 undefined CalculateRPM(void)
  0x62b70 undefined FireLaser(void)
  0x62b90 void __thiscall AttackWithEmp(PBondCar * this)
  0x62c40 undefined4 __thiscall GetSplinePath(PBondCar * this)
  0x62c60 void __thiscall EnableTargetBeacon(PBondCar * this, bool param_1)
  0x62cf0 void __thiscall DisableTargetBeacon(PBondCar * this)
  0x62d00 undefined GlareOn(undefined4 param_1)
  0x62d70 undefined GlareOff(undefined4 param_1)
  0x62de0 undefined HandleTurnSignals(void)
  0x62ec0 undefined ClearEMPState(void)
  0x62ee0 undefined EnableRocketBoost(void)
  0x62f50 undefined EnableTwoWheelStunt(void)
  0x62f60 void __thiscall DisableTwoWheelStunt(PBondCar * this)
  0x62fe0 undefined ImproveLanding(void)
  0x632a0 undefined AddSnowmobileForces(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, 
  0x63ad0 undefined ProcessSnowmobilePhysics(void)
  0x644c0 undefined ProcessSubmarinePhysics(void)
  0x65740 undefined ProcessSplinePhysics(void)
  0x65ba0 undefined DebugObject(void)
  0x65c70 undefined ~PBondCar(void)
  0x65d50 undefined4 __thiscall GetAudio(PBondCar * this)
  0x65d60 void __thiscall SetAudio(PBondCar * this, int audio)
  0x65d70 void __thiscall SetAIGroundVehicle(PBondCar * this, undefined4 aiGV_1)
  0x65d80 undefined4 __thiscall GetAIGroundVehiclePtr(PBondCar * this)
  0x65d90 undefined4 __thiscall GetResetAvailable(PBondCar * this)
  0x65da0 undefined4 __thiscall GetCarType(PBondCar * this)
  0x65db0 void __thiscall SetCarClass(PBondCar * this, undefined4 class)
  0x65dc0 undefined4 __thiscall GetCarClass(PBondCar * this)
  0x65dd0 undefined4 __thiscall GetCarColour(PBondCar * this)
  0x65de0 void __thiscall SetCarControlSteering(PBondCar * this, float steer)
  0x65df0 void __thiscall SetCarControlSteeringVertical(PBondCar * this, float param_1)
  0x65e00 undefined SetCarControlStrafeHorizontal(undefined4 param_1)
  0x65e10 undefined SetCarControlStrafeVertical(undefined4 param_1)
  0x65e20 undefined SetCarControlGas(undefined4 param_1)
  0x65e30 undefined SetCarControlBrake(undefined4 param_1)
  0x65e40 undefined SetCarControlHandBrake(undefined1 param_1)
  0x65e50 undefined GetCarControlHandBrake(void)
  0x65e60 undefined SetCarControlFirePrimary(undefined1 param_1)
  0x65e70 undefined LockCarControlForever(void)
  0x65ea0 undefined SetTargetGas(undefined4 param_1)
  0x65eb0 undefined SetTargetBrake(undefined4 param_1)
  0x65ec0 undefined GetCarControl(undefined4 param_1)
  0x65ee0 undefined EmpActive(void)
  0x65ef0 float __thiscall GetCarSpeed(PBondCar * this)
  0x65f00 undefined IsTyreShredded(undefined1 param_1)
  0x65f20 undefined4 __thiscall GetTyreTrackPtr(PBondCar * this, int tyreNum)
  0x65f30 undefined SetScoreable(undefined1 param_1)
  0x65f40 bool __thiscall InShock(PBondCar * this)
  0x65f50 void __thiscall SetOilSlick(PBondCar * this, undefined param_1)
  0x65f60 undefined GetNumWheelsOnGround(void)
  0x65f70 undefined GetDamageByPlayerTimer(void)
  0x65f80 undefined GetCarWheelSpinAngle(undefined1 param_1)
  0x65f90 float __thiscall GetCarSteer(PBondCar * this)
  0x65fa0 undefined GetSuspensionCompression(undefined4 param_1)
  0x65fb0 undefined IsReversing(void)
  0x65fc0 undefined SetAgainstWallFlag(undefined1 param_1)
  0x65fd0 undefined GetWheelRoadHeight(undefined4 param_1)
  0x65ff0 undefined __thiscall GetWheelRoadSurface(PBondCar * this, int wheelNum)
  0x66010 int __thiscall GetWheelRoadNormal(PBondCar * this, int tyreNum)
  0x66020 undefined SetShieldPointLoc(undefined4 param_1)
  0x66030 undefined IsWheelOnGround(undefined1 param_1)
  0x66060 undefined GetSuspensionCompression(void)
  0x66070 undefined GetCarWheelSlip(undefined4 param_1)
  0x66080 undefined DisableTyreBlowOuts(void)
  0x66090 undefined GetIsInTwoWheelMode(void)
  0x660a0 undefined GetWasInAir(void)
  0x660b0 undefined SetWasInAir(undefined1 param_1)
  0x660c0 undefined SetImmunity(undefined1 param_1)
  0x660d0 undefined GetWheelPos(undefined4 param_1)
  0x660e0 undefined GetForceStop(void)
  0x660f0 undefined ForceStopOn(undefined1 param_1)
  0x66110 undefined ForceStopOff(undefined1 param_1)
  0x66130 undefined ForceRollDirection(undefined1 param_1)
  0x66140 undefined RollSub(undefined1 param_1, undefined1 param_2)
  0x66160 undefined GetSecondaryType(void)
  0x66170 undefined GetTargetBeacon(void)
  0x66180 undefined SetVisualDamage(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0x66390 undefined ResetCar(void)
  0x66640 void __thiscall GetControllerInput(PBondCar * this)
  0x66c90 undefined TwoWheelStunt(void)
  0x66ee0 undefined ControlTyreTracks(void)
  0x670e0 undefined AddWheelForces(undefined param_1, undefined param_2, undefined param_3, undefined4 param_4, undefine
  0x67bd0 undefined ProcessPhysics(void)
  0x690a0 undefined AddSimpleWheelForces(undefined param_1, undefined param_2, undefined4 param_3, undefined4 param_4)
  0x69710 undefined ProcessSimplePhysics(void)
  0x6a460 void __thiscall ChangeCarType(PBondCar * this, char * newType, bool param_2)
  0x6a840 undefined Simulate(void)
  0x6b6f0 undefined ApplyDamage(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, undefine
  0x6cc20 undefined InitAudioObject(undefined4 param_1)
  0x6d350 undefined4 * __thiscall PBondCar(PBondCar * this, int class, char * type, undefined4 colour, undefined4 * para

PS2 methods (119):
  0x185668 PBondCar::InitializeBondCarGlobals
  0x185e40 PBondCar::Shutdown
  0x185e48 PBondCar::Debug
  0x185e50 PBondCar::ChangeCarType
  0x1862c0 PBondCar::PBondCar
  0x186878 PBondCar::InitAudioObject
  0x186fb0 PBondCar::~PBondCar
  0x1870b0 PBondCar::InitTyreTracks
  0x1872d8 PBondCar::InitializeCarControls
  0x187330 PBondCar::FlushCarControls
  0x187350 PBondCar::InitializeCarVariables
  0x187578 PBondCar::ResetDamage
  0x187608 PBondCar::AddDamageByPlayer
  0x1876c8 PBondCar::SetVisualDamage
  0x1879c8 PBondCar::GetNumTires
  0x187a78 PBondCar::SetInShock
  0x187b00 PBondCar::ResetCar
  0x187eb8 PBondCar::ResetCar
  0x188200 PBondCar::Simulate
  0x189598 PBondCar::ApplyDamage
  0x18aa78 PBondCar::GetDamageZones
  0x18aa88 PBondCar::CalculateRPM
  0x18ac08 PBondCar::FireLaser
  0x18ac28 PBondCar::AttackWithEmp
  0x18ad28 PBondCar::GetSplinePath
  0x18ad50 PBondCar::EnableTargetBeacon
  0x18adc0 PBondCar::DisableTargetBeacon
  0x18add8 PBondCar::GetControllerInput
  0x18b328 PBondCar::GlareOn
  0x18b408 PBondCar::GlareOff
  0x18b4e0 PBondCar::SignalLeft
  0x18b530 PBondCar::SignalRight
  0x18b580 PBondCar::HandleTurnSignals
  0x18b730 PBondCar::ClearEMPState
  0x18b768 PBondCar::EnableRocketBoost
  0x18b820 PBondCar::EnableTwoWheelStunt
  0x18b830 PBondCar::TwoWheelStunt
  0x18bb00 PBondCar::DisableTwoWheelStunt
  0x18bb90 PBondCar::ImproveLanding
  0x18bf08 PBondCar::ControlTyreTracks
  0x18c1b0 PBondCar::AddWheelForces
  0x18cef8 PBondCar::ProcessPhysics
  0x18e548 PBondCar::AddSimpleWheelForces
  0x18ecb8 PBondCar::ProcessSimplePhysics
  0x18fc00 PBondCar::AddSnowmobileForces
  0x190510 PBondCar::ProcessSnowmobilePhysics
  0x190f88 PBondCar::ProcessSubmarinePhysics
  0x192498 PBondCar::ProcessSplinePhysics
  0x192a18 PBondCar::DebugObject
  0x192f90 PBondCar::operator.new
  0x192fb0 PBondCar::operator.delete
  0x192fd0 PBondCar::GetAudio
  0x192fd8 PBondCar::SetAudio
  0x192fe0 PBondCar::SetAIGroundVehicle
  0x192fe8 PBondCar::GetAIGroundVehiclePtr
  0x192ff0 PBondCar::GetResetAvailable
  0x192ff8 PBondCar::GetCarType
  0x193000 PBondCar::SetCarClass
  0x193008 PBondCar::GetCarClass
  0x193010 PBondCar::GetCarColour
  0x193018 PBondCar::SetCarControlSteering
  0x193020 PBondCar::SetCarControlSteeringVertical
  0x193028 PBondCar::SetCarControlStrafeHorizontal
  0x193030 PBondCar::SetCarControlStrafeVertical
  0x193038 PBondCar::SetCarControlGas
  0x193040 PBondCar::SetCarControlBrake
  0x193048 PBondCar::SetCarControlHandBrake
  0x193050 PBondCar::GetCarControlHandBrake
  0x193058 PBondCar::SetCarControlFirePrimary
  0x193060 PBondCar::LockCarControlForever
  0x1930b0 PBondCar::SetTargetGas
  0x1930b8 PBondCar::SetTargetBrake
  0x1930c0 PBondCar::GetCarControl
  0x193108 PBondCar::EmpActive
  0x193110 PBondCar::GetCarSpeed
  0x193118 PBondCar::IsTyreShredded
  0x193130 PBondCar::GetTyreTrackPtr
  0x193140 PBondCar::SetScoreable
  0x193148 PBondCar::InShock
  0x193158 PBondCar::SetOilSlick
  0x193160 PBondCar::GetNumWheelsOnGround
  0x193168 PBondCar::GetDamageByPlayerTimer
  0x193170 PBondCar::GetPhysics
  0x193178 PBondCar::GetCarWheelSpinAngle
  0x193190 PBondCar::GetCarSteer
  0x193198 PBondCar::GetSuspensionCompression
  0x1931a8 PBondCar::IsReversing
  0x1931b8 PBondCar::SetAgainstWallFlag
  0x1931c0 PBondCar::GetWheelRoadHeight
  0x1931d0 PBondCar::GetWheelRoadSurface
  0x1931e0 PBondCar::GetWheelRoadNormal
  0x1931f0 PBondCar::SetShieldPointLoc
  0x1931f8 PBondCar::IsWheelOnGround
  0x193228 PBondCar::GetSuspensionCompression
  0x193230 PBondCar::GetCarWheelSlip
  0x193240 PBondCar::DisableTyreBlowOuts
  0x193248 PBondCar::GetIsInTwoWheelMode
  0x193258 PBondCar::GetWasInAir
  0x193260 PBondCar::SetWasInAir
  0x193268 PBondCar::SetImmunity
  0x193270 PBondCar::GetWheelPos
  0x193280 PBondCar::GetForceStop
  0x193288 PBondCar::ForceStopOn
  0x1932a0 PBondCar::ForceStopOff
  0x1932b8 PBondCar::ForceRollDirection
  0x1932c0 PBondCar::RollSub
  0x1932d0 PBondCar::GetSecondaryType
  0x1932d8 PBondCar::GetTargetBeacon
  0x1932e0 PBondCar::GetCarControlSteering
  0x1932e8 PBondCar::SetCarControlGear
  0x1932f0 PBondCar::LockCarControl
  0x193348 PBondCar::UnlockCarControl
  0x193358 PBondCar::SetCarWheelSpin
  0x193360 PBondCar::GetCarToggleInfo
  0x193368 PBondCar::GetCarWheelSpeed
  0x193370 PBondCar::GetyDrawModifier
  0x193378 PBondCar::GetReverseTimer
  0x193380 PBondCar::EnableTyreBlowOuts
  0x193390 PBondCar::GetScoreable

Sheet rows:
  PBondCar::InitializeBondCarGlobals(void)
  PBondCar::Shutdown(void)
  PBondCar::Debug(void)
  PBondCar::ChangeCarType(char *, bool)
  PBondCar::PBondCar(CarClass, char *, unsigned int, COORD3 &, CO
  PBondCar::InitAudioObject(CarClass)
  PBondCar::~PBondCar(void)
  PBondCar::InitTyreTracks(void)
  PBondCar::InitializeCarControls(void)
  PBondCar::FlushCarControls(void)
  PBondCar::InitializeCarVariables(bool)
  PBondCar::ResetDamage(void)
  PBondCar::AddDamageByPlayer(float)
  PBondCar::SetVisualDamage(float, float, float, float)
  PBondCar::GetNumTires(void)
  PBondCar::SetInShock(float)
  PBondCar::ResetCar(bool)
  PBondCar::ResetCar(COORD3 &, COORD3 &)
  PBondCar::Simulate(void)
  PBondCar::ApplyDamage(COORD3 &, COORD3 &, float, float, DamageT
  PBondCar::GetDamageZones(unsigned int &)
  PBondCar::CalculateRPM(void)
  PBondCar::FireLaser(void)
  PBondCar::AttackWithEmp(void)
  PBondCar::GetSplinePath(void)
  PBondCar::EnableTargetBeacon(bool)
  PBondCar::DisableTargetBeacon(void)
  PBondCar::GetControllerInput(void)
  PBondCar::GlareOn(CarGlare)
  PBondCar::GlareOff(CarGlare)
  PBondCar::SignalLeft(void)
  PBondCar::SignalRight(void)
  PBondCar::HandleTurnSignals(void)
  PBondCar::ClearEMPState(void)
  PBondCar::EnableRocketBoost(void)
  PBondCar::EnableTwoWheelStunt(void)
  PBondCar::TwoWheelStunt(void)
  PBondCar::DisableTwoWheelStunt(void)
  PBondCar::ImproveLanding(void)
  PBondCar::ControlTyreTracks(void)
  PBondCar::AddWheelForces(WHEEL_INFO1 *, float *, COORD4 *, COOR
  PBondCar::ProcessPhysics(void)
  PBondCar::AddSimpleWheelForces(WHEEL_INFO1 *, COORD4 *, COORD4
  PBondCar::ProcessSimplePhysics(void)
  PBondCar::AddSnowmobileForces(WHEEL_INFO1 *, COORD4 *, COORD4 *
  PBondCar::ProcessSnowmobilePhysics(void)
  PBondCar::ProcessSubmarinePhysics(void)
  PBondCar::ProcessSplinePhysics(void)
  PBondCar::DebugObject(void)
  PBondCar type_info function
  PBondCar::operator new(unsigned int)
  PBondCar::operator delete(void *, unsigned int)
  PBondCar::GetAudio(void) const
  PBondCar::SetAudio(AVehicle *)
  PBondCar::SetAIGroundVehicle(AIGroundVehicle *)
  PBondCar::GetAIGroundVehiclePtr(void) const
  PBondCar::GetResetAvailable(void) const
  PBondCar::GetCarType(void) const
  PBondCar::SetCarClass(CarClass)
  PBondCar::GetCarClass(void) const
  PBondCar::GetCarColour(void) const
  PBondCar::SetCarControlSteering(float)
  PBondCar::SetCarControlSteeringVertical(float)
  PBondCar::SetCarControlStrafeHorizontal(float)
  PBondCar::SetCarControlStrafeVertical(float)
  PBondCar::SetCarControlGas(float)
  PBondCar::SetCarControlBrake(float)
  PBondCar::SetCarControlHandBrake(char)
  PBondCar::GetCarControlHandBrake(void)
  PBondCar::SetCarControlFirePrimary(char)
  PBondCar::LockCarControlForever(void)
  PBondCar::SetTargetGas(float)
  PBondCar::SetTargetBrake(float)
  PBondCar::GetCarControl(void) const
  PBondCar::EmpActive(void)
  PBondCar::GetCarSpeed(void) const
  PBondCar::IsTyreShredded(char) const
  PBondCar::GetTyreTrackPtr(int)
  PBondCar::SetScoreable(bool)
  PBondCar::InShock(void) const

Xbox methods treated as members (101 of 102; untyped ones count when ECX is read before it is written): AddDamageByPlayer, AddSimpleWheelForces, AddSnowmobileForces, AddWheelForces, ApplyDamage, AttackWithEmp, CalculateRPM, ChangeCarType, ClearEMPState, ControlTyreTracks, DebugObject, DisableTargetBeacon, DisableTwoWheelStunt, DisableTyreBlowOuts, EmpActive, EnableRocketBoost, EnableTargetBeacon, EnableTwoWheelStunt, FireLaser, FlushCarControls, ForceRollDirection, ForceStopOff, ForceStopOn, GetAIGroundVehiclePtr, GetAudio, GetCarClass, GetCarColour, GetCarControl, GetCarControlHandBrake, GetCarSpeed, GetCarSteer, GetCarType, GetCarWheelSlip, GetCarWheelSpinAngle, GetControllerInput, GetDamageByPlayerTimer, GetDamageZones, GetForceStop, GetIsInTwoWheelMode, GetNumTires, GetNumWheelsOnGround, GetPhysics, GetResetAvailable, GetSecondaryType, GetSplinePath, GetSuspensionCompression, GetTargetBeacon, GetTyreTrackPtr, GetWasInAir, GetWheelPos, GetWheelRoadHeight, GetWheelRoadNormal, GetWheelRoadSurface, GlareOff, GlareOn, HandleTurnSignals, ImproveLanding, InShock, InitAudioObject, InitTyreTracks, InitializeCarControls, InitializeCarVariables, IsReversing, IsTyreShredded, IsWheelOnGround, LockCarControlForever, PBondCar, ProcessPhysics, ProcessSimplePhysics, ProcessSnowmobilePhysics, ProcessSplinePhysics, ProcessSubmarinePhysics, ResetCar, ResetDamage, RollSub, SetAIGroundVehicle, SetAgainstWallFlag, SetAudio, SetCarClass, SetCarControlBrake, SetCarControlFirePrimary, SetCarControlGas, SetCarControlHandBrake, SetCarControlSteering, SetCarControlSteeringVertical, SetCarControlStrafeHorizontal, SetCarControlStrafeVertical, SetImmunity, SetInShock, SetOilSlick, SetScoreable, SetShieldPointLoc, SetTargetBrake, SetTargetGas, SetVisualDamage, SetWasInAir, Simulate, TwoWheelStunt, ~PBondCar

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W [15: AddSnowmobileForces@632a0, AddWheelForces@670e0, AttackWithEmp@62b90, ChangeCarType@6a460, ControlTyreTracks@66ee0, GetNumTires@626e0…]
  +0x04a  w[2] R [19: AddSimpleWheelForces@690a0, AddSnowmobileForces@632a0, AddWheelForces@670e0, CalculateRPM@62a60, ChangeCarType@6a460, ControlTyreTracks@66ee0…]
  +0x04c  w[4] R -> RSceneObj::GetBoundingDimensions, RSceneObj::Reset, RVehicle::SetEMPVictimEffect [10: AttackWithEmp@62b90, ChangeCarType@6a460, ClearEMPState@62ec0, GlareOff@62d70, GlareOn@62d00, PBondCar@6d350…]
  +0x060  w- LEA addr-taken -> AttributeSet::LookupFloat, AttributeSet::LookupInt, AttributeSet::SetName [10: AddDamageByPlayer@62660, ChangeCarType@6a460, EnableRocketBoost@62ee0, InitAudioObject@6cc20, InitTyreTracks@62120, PBondCar@6d350…]
  +0x074  w[4] LEA/W addr-taken [5: ControlTyreTracks@66ee0, InitTyreTracks@62120, ResetCar@627c0, ResetCar@66390, ~PBondCar@65c70]
  +0x078  w[4] W [1: InitTyreTracks@62120]
  +0x07c  w[4] W [1: InitTyreTracks@62120]
  +0x080  w[4] W [1: InitTyreTracks@62120]
  +0x090  w- LEA addr-taken [6: PBondCar@6d350, ProcessSimplePhysics@69710, ProcessSnowmobilePhysics@63ad0, ProcessSplinePhysics@65740, ResetCar@627c0, ~PBondCar@65c70]
  +0x0bc  w- LEA addr-taken [2: ControlTyreTracks@66ee0, ProcessPhysics@67bd0]
  +0x190  w- LEA addr-taken [2: ProcessPhysics@67bd0, ProcessSnowmobilePhysics@63ad0]
  +0x19c  w[4] W float [2: ProcessPhysics@67bd0, ProcessSnowmobilePhysics@63ad0]
  +0x1ac  w[4] W float [2: ProcessPhysics@67bd0, ProcessSnowmobilePhysics@63ad0]
  +0x1bc  w[4] W float [2: ProcessPhysics@67bd0, ProcessSnowmobilePhysics@63ad0]
  +0x1cc  w[4] W float [2: ProcessPhysics@67bd0, ProcessSnowmobilePhysics@63ad0]
  +0x1d0  w- LEA addr-taken [5: ControlTyreTracks@66ee0, ProcessPhysics@67bd0, ProcessSnowmobilePhysics@63ad0, ProcessSplinePhysics@65740, ResetCar@627c0]
  +0x1d4  w[4] W float [2: ProcessPhysics@67bd0, ProcessSnowmobilePhysics@63ad0]
  +0x1e4  w[4] W float [2: ProcessPhysics@67bd0, ProcessSnowmobilePhysics@63ad0]
  +0x1f4  w[4] W float [2: ProcessPhysics@67bd0, ProcessSnowmobilePhysics@63ad0]
  +0x204  w[4] W float [2: ProcessPhysics@67bd0, ProcessSnowmobilePhysics@63ad0]
  +0x210  w[4] LEA/W addr-taken [4: GetSuspensionCompression@66060, ProcessSplinePhysics@65740, ProcessSubmarinePhysics@644c0, ResetCar@627c0]
  +0x214  w[4] W [1: ProcessSubmarinePhysics@644c0]
  +0x218  w[4] W [1: ProcessSubmarinePhysics@644c0]
  +0x21c  w[4] W [1: ProcessSubmarinePhysics@644c0]
  +0x220  w[4] LEA/R/W float addr-taken [3: ProcessPhysics@67bd0, ProcessSnowmobilePhysics@63ad0, ProcessSplinePhysics@65740]
  +0x224  w[4] W float [3: ProcessPhysics@67bd0, ProcessSnowmobilePhysics@63ad0, ProcessSplinePhysics@65740]
  +0x228  w[4] W float [3: ProcessPhysics@67bd0, ProcessSnowmobilePhysics@63ad0, ProcessSplinePhysics@65740]
  +0x22c  w[4] R/W float [3: ProcessPhysics@67bd0, ProcessSnowmobilePhysics@63ad0, ProcessSplinePhysics@65740]
  +0x230  w[4] W float [2: ProcessPhysics@67bd0, ProcessSplinePhysics@65740]
  +0x234  w[4] W float [2: ProcessPhysics@67bd0, ProcessSplinePhysics@65740]
  +0x238  w[4] W [3: ProcessPhysics@67bd0, ProcessSimplePhysics@69710, ProcessSnowmobilePhysics@63ad0]
  +0x23c  w[4] R/W -> RigidBody::GetLocalAngularVelocity, VU0_v4scale [14: AddSimpleWheelForces@690a0, AddSnowmobileForces@632a0, AddWheelForces@670e0, CalculateRPM@62a60, ChangeCarType@6a460, DebugObject@65ba0…]
  +0x240  w[4] LEA/R/W float addr-taken [10: GetCarControl@65ec0, GetControllerInput@66640, HandleTurnSignals@62de0, InitializeCarControls@623b0, LockCarControlForever@65e70, ProcessPhysics@67bd0…]
  +0x244  w[4] R/W [4: GetControllerInput@66640, InitializeCarControls@623b0, ProcessSubmarinePhysics@644c0, SetCarControlSteeringVertical@65df0]
  +0x248  w[4] W [2: InitializeCarControls@623b0, SetCarControlStrafeVertical@65e10]
  +0x24c  w[4] W [2: InitializeCarControls@623b0, SetCarControlStrafeHorizontal@65e00]
  +0x250  w[4] W float [7: GetControllerInput@66640, InitializeCarControls@623b0, ProcessPhysics@67bd0, ProcessSimplePhysics@69710, ProcessSnowmobilePhysics@63ad0, ProcessSubmarinePhysics@644c0…]
  +0x254  w[4] R/W float [7: GetControllerInput@66640, InitializeCarControls@623b0, ProcessPhysics@67bd0, ProcessSimplePhysics@69710, ProcessSnowmobilePhysics@63ad0, ProcessSubmarinePhysics@644c0…]
  +0x258  w[1] W [1: InitializeCarControls@623b0]
  +0x259  w[1] R/W [8: GetCarControlHandBrake@65e50, GetControllerInput@66640, InitializeCarControls@623b0, ProcessPhysics@67bd0, ProcessSimplePhysics@69710, ProcessSnowmobilePhysics@63ad0…]
  +0x25a  w[1] W [5: ChangeCarType@6a460, GetControllerInput@66640, InitializeCarControls@623b0, PBondCar@6d350, SetCarControlFirePrimary@65e60]
  +0x25b  w[1] W [2: GetControllerInput@66640, InitializeCarControls@623b0]
  +0x26c  w[4] R/W -> ActionQueue::~ActionQueue [3: FlushCarControls@62430, PBondCar@6d350, ~PBondCar@65c70]
  +0x274  w[4] W float [3: ProcessPhysics@67bd0, ProcessSimplePhysics@69710, ProcessSplinePhysics@65740]
  +0x278  w[4] W [2: GetControllerInput@66640, InitializeCarControls@623b0]
  +0x27c  w[4] W [2: GetControllerInput@66640, InitializeCarControls@623b0]
  +0x280  w[4] W [1: InitializeCarControls@623b0]
  +0x284  w[4] W [1: InitializeCarControls@623b0]
  +0x288  w[4] W float [8: AddWheelForces@670e0, GetCarSpeed@65ef0, ProcessPhysics@67bd0, ProcessSimplePhysics@69710, ProcessSnowmobilePhysics@63ad0, ProcessSplinePhysics@65740…]
  +0x28c  w[4] W float [6: GetCarSteer@65f90, ProcessPhysics@67bd0, ProcessSimplePhysics@69710, ProcessSnowmobilePhysics@63ad0, ProcessSplinePhysics@65740, ProcessSubmarinePhysics@644c0]
  +0x294  w[4] R/W [3: EnableRocketBoost@62ee0, ProcessPhysics@67bd0, ProcessSubmarinePhysics@644c0]
  +0x298  w[1] R/W [3: ProcessPhysics@67bd0, ProcessSimplePhysics@69710, ProcessSnowmobilePhysics@63ad0]
  +0x299  w[1] R/W [7: AddWheelForces@670e0, GetNumWheelsOnGround@65f60, ProcessPhysics@67bd0, ProcessSimplePhysics@69710, ProcessSnowmobilePhysics@63ad0, ProcessSubmarinePhysics@644c0…]
  +0x29a  w[1] R/W [2: ProcessPhysics@67bd0, ProcessSubmarinePhysics@644c0]
  +0x29b  w[1] R [1: ProcessPhysics@67bd0]
  +0x29c  w[1] R/W [3: IsReversing@65fb0, ProcessPhysics@67bd0, ProcessSubmarinePhysics@644c0]
  +0x29d  w[1] R/W [5: AddWheelForces@670e0, InShock@65f40, ProcessSimplePhysics@69710, ProcessSnowmobilePhysics@63ad0, SetInShock@62740]
  +0x29e  w[1] R/W [3: ProcessPhysics@67bd0, ProcessSubmarinePhysics@644c0, SetAgainstWallFlag@65fc0]
  +0x29f  w[1] R [1: ProcessPhysics@67bd0]
  +0x2a0  w[4] LEA/R addr-taken [1: ProcessSimplePhysics@69710]
  +0x2a4  w[4] R [1: ProcessSimplePhysics@69710]
  +0x2a8  w[4] R [1: ProcessSimplePhysics@69710]
  +0x2ac  w[4] R [1: ProcessSimplePhysics@69710]
  +0x2b1  w[1] R/W [2: ProcessPhysics@67bd0, TwoWheelStunt@66c90]
  +0x2b2  w[1] R/W [5: AddWheelForces@670e0, DisableTwoWheelStunt@62f60, EnableTwoWheelStunt@62f50, GetIsInTwoWheelMode@66090, ProcessPhysics@67bd0]
  +0x2b3  w[1] R/W [2: DisableTwoWheelStunt@62f60, ProcessPhysics@67bd0]
  +0x2b4  w[4] R/W float [3: DisableTwoWheelStunt@62f60, ProcessPhysics@67bd0, TwoWheelStunt@66c90]
  +0x2bc  w[4] W [2: InitializeCarControls@623b0, SetTargetGas@65ea0]
  +0x2c0  w[4] W [2: InitializeCarControls@623b0, SetTargetBrake@65eb0]
  +0x2c4  w[4] W [2: GetControllerInput@66640, InitializeCarControls@623b0]
  +0x2c8  w[4] R/W [12: AddDamageByPlayer@62660, ApplyDamage@6b6f0, ChangeCarType@6a460, GetCarClass@65dc0, PBondCar@6d350, ProcessPhysics@67bd0…]
  +0x2cc  w[4] R/W -> GetCarColourVariation, __stricmp [4: ChangeCarType@6a460, GetCarType@65da0, InitAudioObject@6cc20, PBondCar@6d350]
  +0x2d0  w[4] R/W -> GetCarColourVariation [3: ChangeCarType@6a460, GetCarColour@65dd0, PBondCar@6d350]
  +0x2d6  w[1] R/W [3: PBondCar@6d350, ProcessSubmarinePhysics@644c0, RollSub@66140]
  +0x2d7  w[1] R/W [2: AddWheelForces@670e0, ProcessPhysics@67bd0]
  +0x2d8  w[1] R [2: AddWheelForces@670e0, ProcessPhysics@67bd0]
  +0x2d9  w[1] R/W [1: ProcessPhysics@67bd0]
  +0x2da  w[1] R/W [2: AddWheelForces@670e0, ProcessPhysics@67bd0]
  +0x2db  w[1] R/W [3: ImproveLanding@62fe0, ProcessPhysics@67bd0, ProcessSnowmobilePhysics@63ad0]
  +0x2dc  w[4] W [1: PBondCar@6d350]
  +0x2e0  w[4] R/W -> WTargetable::RemoveReference [5: DisableTargetBeacon@62cf0, EnableTargetBeacon@62c60, GetTargetBeacon@66170, PBondCar@6d350, ~PBondCar@65c70]
  +0x2e4  w[4] R/W [5: GetAIGroundVehiclePtr@65d80, GetSplinePath@62c40, PBondCar@6d350, ResetCar@66390, SetAIGroundVehicle@65d70]
  +0x2e8  w[4] W float [2: AddWheelForces@670e0, ProcessPhysics@67bd0]
  +0x2ec  w[4] W float [5: AddSnowmobileForces@632a0, AddWheelForces@670e0, ProcessPhysics@67bd0, ProcessSimplePhysics@69710, ProcessSnowmobilePhysics@63ad0]
  +0x2f0  w- LEA addr-taken [2: GetDamageZones@62a40, ResetDamage@62600]
  +0x370  w[4] W float [2: AddDamageByPlayer@62660, ResetDamage@62600]
  +0x374  w[1] R/W [3: AddDamageByPlayer@62660, GetDamageByPlayerTimer@65f70, ResetDamage@62600]
  +0x375  w[1] W [1: PBondCar@6d350]
  +0x376  w[1] R/W [3: ApplyDamage@6b6f0, DisableTyreBlowOuts@66080, PBondCar@6d350]
  +0x377  w[1] R/W [4: AttackWithEmp@62b90, ClearEMPState@62ec0, EmpActive@65ee0, PBondCar@6d350]
  +0x378  w[4] W [3: AttackWithEmp@62b90, ClearEMPState@62ec0, PBondCar@6d350]
  +0x37c  w[4] R/W -> ABaseSound::operator_new, AVehicle::ActivateWind, AVehicle::ChooseHorn [3: GetAudio@65d50, InitAudioObject@6cc20, SetAudio@65d60]
  +0x380  w[4] R/W [2: HandleTurnSignals@62de0, PBondCar@6d350]
  +0x384  w[4] R/W [2: HandleTurnSignals@62de0, PBondCar@6d350]
  +0x388  w[4] W float [2: HandleTurnSignals@62de0, PBondCar@6d350]
  +0x38c  w[4] W float [1: HandleTurnSignals@62de0]
  +0x390  w[4] R/W [2: HandleTurnSignals@62de0, PBondCar@6d350]
  +0x394  w[1] R/W [2: FireLaser@62b70, PBondCar@6d350]
  +0x395  w[1] W [1: PBondCar@6d350]
  +0x396  w[1] W [1: PBondCar@6d350]
  +0x397  w[1] W [1: PBondCar@6d350]
  +0x398  w[4] W [1: FireLaser@62b70]
  +0x39c  w[4] W [2: PBondCar@6d350, ResetDamage@62600]
  +0x3a0  w[4] W [2: PBondCar@6d350, ResetDamage@62600]
  +0x3a4  w[4] W [2: PBondCar@6d350, ResetDamage@62600]
  +0x3a8  w[4] W [2: PBondCar@6d350, ResetDamage@62600]
  +0x3ac  w[4] R [1: GetResetAvailable@65d90]
  +0x3b0  w- LEA addr-taken [3: ChangeCarType@6a460, PBondCar@6d350, ResetCar@66390]
  +0x3bc  w- LEA addr-taken [1: LockCarControlForever@65e70]
  +0x3dc  w[4] W [1: LockCarControlForever@65e70]
  +0x3e0  w[4] W float [2: ProcessSimplePhysics@69710, ProcessSubmarinePhysics@644c0]
  +0x3e4  w[4] R/W float -> sin_fractionalangle [2: ProcessSimplePhysics@69710, ProcessSubmarinePhysics@644c0]
  +0x3e8  w[4] R/W float -> sin_fractionalangle [2: ProcessSimplePhysics@69710, ProcessSubmarinePhysics@644c0]
  +0x3ec  w[4] LEA/W addr-taken [2: PBondCar@6d350, ResetDamage@62600]
  +0x3f0  w[1] R/W [3: ApplyDamage@6b6f0, PBondCar@6d350, SetImmunity@660c0]
  +0x3f4  w[4] R/W [3: ApplyDamage@6b6f0, PBondCar@6d350, SetShieldPointLoc@66020]
  +0x3f8  w[4] W [1: InitializeCarControls@623b0]
  +0x3fc  w[1] R/W [3: ProcessPhysics@67bd0, ProcessSimplePhysics@69710, SetOilSlick@65f50]
  +0x3fd  w[1] R/W [1: ProcessPhysics@67bd0]
  +0x3fe  w[1] R/W [3: ControlTyreTracks@66ee0, ProcessPhysics@67bd0, ProcessSplinePhysics@65740]
  +0x3ff  w[1] W [2: PBondCar@6d350, SetScoreable@65f30]
  +0x400  w[4] W [2: GetControllerInput@66640, InitializeCarControls@623b0]
  +0x404  w[4] W [1: InitializeCarControls@623b0]
  +0x408  w[1] R/W [2: PBondCar@6d350, ProcessSubmarinePhysics@644c0]
  +0x409  w[1] R/W [3: ForceRollDirection@66130, PBondCar@6d350, ProcessSubmarinePhysics@644c0]
  +0x40a  w[1] R/W [5: ForceStopOff@66110, ForceStopOn@660f0, GetControllerInput@66640, GetForceStop@660e0, PBondCar@6d350]
  +0x40b  w[1] R/W [3: PBondCar@6d350, ProcessSubmarinePhysics@644c0, RollSub@66140]
  +0x40c  w[4] R/W [2: GetSecondaryType@66160, PBondCar@6d350]
  +0x411  w[1] R/W [2: GetWasInAir@660a0, SetWasInAir@660b0]

PS2 this-relative accesses (PS2 offsets):
  +0x046  w[2] R [20: AddSimpleWheelForces@18e548, AddSnowmobileForces@18fc00, AddWheelForces@18c1b0, ApplyDamage@189598, CalculateRPM@18aa88, ChangeCarType@185e50…]
  +0x048  w[4] R -> AttributeSet::LookupString, RSceneObj::GetBoundingDimensions, RSceneObj::Reset, RVehicle::SetEMPVictimEffect, RVehicle:: [14: ApplyDamage@189598, AttackWithEmp@18ac28, ChangeCarType@185e50, ClearEMPState@18b730, EnableRocketBoost@18b768, GlareOff@18b408…]
  +0x05c  w- LEA addr-taken [11: ApplyDamage@189598, ChangeCarType@185e50, EnableRocketBoost@18b768, InitAudioObject@186878, InitializeCarVariables@187350, PBondCar@1862c0…]
  +0x068  w[4] R/W [21: AddSimpleWheelForces@18e548, AddSnowmobileForces@18fc00, AddWheelForces@18c1b0, ApplyDamage@189598, AttackWithEmp@18ac28, ChangeCarType@185e50…]
  +0x06c  w[4] R [1: Simulate@188200]
  +0x070  w[4] R [1: Simulate@188200]
  +0x074  w[4] LEA/W addr-taken [5: ControlTyreTracks@18bf08, InitTyreTracks@1870b0, ResetCar@187b00, ResetCar@187eb8, ~PBondCar@186fb0]
  +0x078  w[4] W [1: InitTyreTracks@1870b0]
  +0x07c  w[4] LEA/W addr-taken [1: InitTyreTracks@1870b0]
  +0x080  w[4] LEA/W addr-taken [1: InitTyreTracks@1870b0]
  +0x090  w- LEA addr-taken [10: AddWheelForces@18c1b0, ControlTyreTracks@18bf08, InitializeCarVariables@187350, PBondCar@1862c0, ProcessPhysics@18cef8, ProcessSimplePhysics@18ecb8…]
  +0x0bc  w[1] R [1: Simulate@188200]
  +0x0fc  w[1] R [1: Simulate@188200]
  +0x13c  w[1] R [1: Simulate@188200]
  +0x17c  w[1] R [1: Simulate@188200]
  +0x190  w- LEA addr-taken [6: ProcessPhysics@18cef8, ProcessSimplePhysics@18ecb8, ProcessSnowmobilePhysics@190510, ProcessSplinePhysics@192498, ResetCar@187eb8, ~PBondCar@186fb0]
  +0x19c  w- LEA addr-taken [5: ProcessPhysics@18cef8, ProcessSimplePhysics@18ecb8, ProcessSnowmobilePhysics@190510, ProcessSplinePhysics@192498, ResetCar@187eb8]
  +0x1d0  w[8] LEA/W addr-taken [6: ControlTyreTracks@18bf08, ProcessPhysics@18cef8, ProcessSimplePhysics@18ecb8, ProcessSnowmobilePhysics@190510, ProcessSplinePhysics@192498, ResetCar@187eb8]
  +0x1d4  w- LEA addr-taken [4: ProcessPhysics@18cef8, ProcessSimplePhysics@18ecb8, ProcessSnowmobilePhysics@190510, ResetCar@187eb8]
  +0x1d8  w[8] W [4: ProcessPhysics@18cef8, ProcessSnowmobilePhysics@190510, ProcessSplinePhysics@192498, ResetCar@187eb8]
  +0x210  w[4] LEA/R/W float addr-taken [8: AddSimpleWheelForces@18e548, AddSnowmobileForces@18fc00, AddWheelForces@18c1b0, GetSuspensionCompression@193228, ProcessSplinePhysics@192498, ProcessSubmarinePhysics@190f88…]
  +0x214  w[4] R/W float [2: AddSnowmobileForces@18fc00, ProcessSubmarinePhysics@190f88]
  +0x218  w[4] R/W float [2: AddSnowmobileForces@18fc00, ProcessSubmarinePhysics@190f88]
  +0x21c  w[4] LEA/R/W float addr-taken [3: AddSnowmobileForces@18fc00, InitializeCarVariables@187350, ProcessSubmarinePhysics@190f88]
  +0x220  w[4] LEA/R/W float addr-taken [5: ControlTyreTracks@18bf08, ProcessPhysics@18cef8, ProcessSimplePhysics@18ecb8, ProcessSnowmobilePhysics@190510, ProcessSplinePhysics@192498]
  +0x224  w[4] R/W float [3: ProcessPhysics@18cef8, ProcessSimplePhysics@18ecb8, ProcessSplinePhysics@192498]
  +0x228  w[4] R/W float [3: ProcessPhysics@18cef8, ProcessSimplePhysics@18ecb8, ProcessSplinePhysics@192498]
  +0x22c  w[4] R/W float [3: ProcessPhysics@18cef8, ProcessSimplePhysics@18ecb8, ProcessSplinePhysics@192498]
  +0x230  w[4] R/W float [4: InitializeCarVariables@187350, ProcessPhysics@18cef8, ProcessSimplePhysics@18ecb8, ProcessSplinePhysics@192498]
  +0x234  w[4] R/W float [5: InitializeCarVariables@187350, ProcessPhysics@18cef8, ProcessSimplePhysics@18ecb8, ProcessSnowmobilePhysics@190510, ProcessSplinePhysics@192498]
  +0x238  w[4] R/W float [4: CalculateRPM@18aa88, ProcessPhysics@18cef8, ProcessSimplePhysics@18ecb8, ProcessSnowmobilePhysics@190510]
  +0x23c  w[4] R/W [16: AddSimpleWheelForces@18e548, AddSnowmobileForces@18fc00, AddWheelForces@18c1b0, ApplyDamage@189598, CalculateRPM@18aa88, ChangeCarType@185e50…]
  +0x240  w[4, 8] R/W float [17: AddSnowmobileForces@18fc00, ApplyDamage@189598, GetCarControl@1930c0, GetCarControlSteering@1932e0, GetControllerInput@18add8, InitializeCarControls@1872d8…]
  +0x244  w[4] R/W float [7: ApplyDamage@189598, GetControllerInput@18add8, InitializeCarControls@1872d8, InitializeCarVariables@187350, ProcessSubmarinePhysics@190f88, SetCarControlSteeringVertical@193020…]
  +0x248  w[4, 8] R/W float [7: GetCarControl@1930c0, InitializeCarControls@1872d8, InitializeCarVariables@187350, LockCarControl@1932f0, LockCarControlForever@193060, SetCarControlStrafeVertical@193030…]
  +0x24c  w[4] W float [3: InitializeCarControls@1872d8, InitializeCarVariables@187350, SetCarControlStrafeHorizontal@193028]
  +0x250  w[4, 8] R/W float [15: AddSnowmobileForces@18fc00, ApplyDamage@189598, CalculateRPM@18aa88, GetCarControl@1930c0, GetControllerInput@18add8, InitializeCarControls@1872d8…]
  +0x254  w[4] R/W float [11: AddSnowmobileForces@18fc00, ApplyDamage@189598, GetControllerInput@18add8, InitializeCarControls@1872d8, InitializeCarVariables@187350, ProcessPhysics@18cef8…]
  +0x258  w[1, 8] R/W [8: CalculateRPM@18aa88, GetCarControl@1930c0, InitializeCarControls@1872d8, InitializeCarVariables@187350, LockCarControl@1932f0, LockCarControlForever@193060…]
  +0x259  w[1] R/W [9: GetCarControlHandBrake@193050, GetControllerInput@18add8, InitializeCarControls@1872d8, InitializeCarVariables@187350, ProcessPhysics@18cef8, ProcessSimplePhysics@18ecb8…]
  +0x25a  w[1] W [5: ChangeCarType@185e50, GetControllerInput@18add8, InitializeCarControls@1872d8, PBondCar@1862c0, SetCarControlFirePrimary@193058]
  +0x25b  w[1] R/W [4: GetControllerInput@18add8, InitializeCarControls@1872d8, InitializeCarVariables@187350, Simulate@188200]
  +0x260  w[4] W float [1: GetControllerInput@18add8]
  +0x264  w[4] W float [1: GetControllerInput@18add8]
  +0x268  w[4] W float [1: InitializeCarVariables@187350]
  +0x26c  w[4] R/W -> ActionQueue::IsEmpty, ActionQueue::~ActionQueue [3: GetControllerInput@18add8, PBondCar@1862c0, ~PBondCar@186fb0]
  +0x270  w[4] R float [1: GetyDrawModifier@193370]
  +0x274  w[4] R/W float [5: GetCarWheelSpeed@193368, InitializeCarVariables@187350, ProcessPhysics@18cef8, ProcessSimplePhysics@18ecb8, ProcessSplinePhysics@192498]
  +0x278  w[4] R/W float [3: GetControllerInput@18add8, InitializeCarControls@1872d8, Simulate@188200]
  +0x27c  w[4] R/W float [3: GetControllerInput@18add8, InitializeCarControls@1872d8, Simulate@188200]
  +0x280  w[4] R/W float [2: GetControllerInput@18add8, InitializeCarControls@1872d8]
  +0x284  w[4] R/W float [2: GetControllerInput@18add8, InitializeCarControls@1872d8]
  +0x288  w[4] R/W float [12: AddSimpleWheelForces@18e548, AddSnowmobileForces@18fc00, AddWheelForces@18c1b0, GetCarSpeed@193110, GetControllerInput@18add8, ProcessPhysics@18cef8…]
  +0x28c  w[4] R/W float [6: GetCarSteer@193190, ProcessPhysics@18cef8, ProcessSimplePhysics@18ecb8, ProcessSnowmobilePhysics@190510, ProcessSplinePhysics@192498, ProcessSubmarinePhysics@190f88]
  +0x294  w[4] R/W [4: EnableRocketBoost@18b768, InitializeCarVariables@187350, ProcessPhysics@18cef8, ProcessSubmarinePhysics@190f88]
  +0x298  w[1] R/W [3: ProcessPhysics@18cef8, ProcessSimplePhysics@18ecb8, ProcessSnowmobilePhysics@190510]
  +0x299  w[1] R/W [11: AddSimpleWheelForces@18e548, AddSnowmobileForces@18fc00, AddWheelForces@18c1b0, CalculateRPM@18aa88, GetControllerInput@18add8, GetNumWheelsOnGround@193160…]
  +0x29a  w[1] R/W [4: GetReverseTimer@193378, InitializeCarVariables@187350, ProcessPhysics@18cef8, ProcessSubmarinePhysics@190f88]
  +0x29b  w[1] R/W [4: GetControllerInput@18add8, InitializeCarVariables@187350, ProcessPhysics@18cef8, SetCarWheelSpin@193358]
  +0x29c  w[1] R/W [3: IsReversing@1931a8, ProcessPhysics@18cef8, ProcessSubmarinePhysics@190f88]
  +0x29d  w[1] R/W [9: AddSimpleWheelForces@18e548, AddSnowmobileForces@18fc00, AddWheelForces@18c1b0, InShock@193148, InitializeCarVariables@187350, ProcessSimplePhysics@18ecb8…]
  +0x29e  w[1] R/W [4: GetControllerInput@18add8, ProcessPhysics@18cef8, ProcessSubmarinePhysics@190f88, SetAgainstWallFlag@1931b8]
  +0x29f  w[1] R/W [2: GetControllerInput@18add8, ProcessPhysics@18cef8]
  +0x2a0  w[4] LEA/R/W addr-taken [3: ApplyDamage@189598, InitializeCarVariables@187350, ProcessSimplePhysics@18ecb8]
  +0x2a4  w[4] R/W [2: InitializeCarVariables@187350, ProcessSimplePhysics@18ecb8]
  +0x2a8  w[4] R/W [2: InitializeCarVariables@187350, ProcessSimplePhysics@18ecb8]
  +0x2ac  w[4] R/W [2: InitializeCarVariables@187350, ProcessSimplePhysics@18ecb8]
  +0x2b1  w[1] R/W [3: InitializeCarVariables@187350, ProcessPhysics@18cef8, TwoWheelStunt@18b830]
  +0x2b2  w[1] R/W [7: AddSimpleWheelForces@18e548, AddWheelForces@18c1b0, DisableTwoWheelStunt@18bb00, EnableTwoWheelStunt@18b820, GetIsInTwoWheelMode@193248, InitializeCarVariables@187350…]
  +0x2b3  w[1] R/W [3: DisableTwoWheelStunt@18bb00, InitializeCarVariables@187350, ProcessPhysics@18cef8]
  +0x2b4  w[4] R/W float [4: DisableTwoWheelStunt@18bb00, InitializeCarVariables@187350, ProcessPhysics@18cef8, TwoWheelStunt@18b830]
  +0x2bc  w[4] R/W float [3: GetControllerInput@18add8, InitializeCarControls@1872d8, SetTargetGas@1930b0]
  +0x2c0  w[4] R/W float [3: GetControllerInput@18add8, InitializeCarControls@1872d8, SetTargetBrake@1930b8]
  +0x2c4  w[4] R/W float [2: GetControllerInput@18add8, InitializeCarControls@1872d8]
  +0x2c8  w[4] R/W [17: AddDamageByPlayer@187608, AddSimpleWheelForces@18e548, AddSnowmobileForces@18fc00, ApplyDamage@189598, ChangeCarType@185e50, GetCarClass@193008…]
  +0x2cc  w[4] R/W -> GetCarColourVariation, PVehicle::NameToIndex, strcasecmp [8: AddSnowmobileForces@18fc00, ChangeCarType@185e50, GetCarType@192ff8, InitAudioObject@186878, InitTyreTracks@1870b0, PBondCar@1862c0…]
  +0x2d0  w[4] R/W [3: ChangeCarType@185e50, GetCarColour@193010, PBondCar@1862c0]
  +0x2d4  w[1] R/W [2: GetControllerInput@18add8, InitializeCarVariables@187350]
  +0x2d6  w[1] R/W [3: PBondCar@1862c0, ProcessSubmarinePhysics@190f88, RollSub@1932c0]
  +0x2d7  w[1] R/W [4: AddWheelForces@18c1b0, CalculateRPM@18aa88, InitializeCarVariables@187350, ProcessPhysics@18cef8]
  +0x2d8  w[1] R/W [4: AddWheelForces@18c1b0, CalculateRPM@18aa88, InitializeCarVariables@187350, ProcessPhysics@18cef8]
  +0x2d9  w[1] R/W [3: CalculateRPM@18aa88, InitializeCarVariables@187350, ProcessPhysics@18cef8]
  +0x2da  w[1] R/W [2: AddWheelForces@18c1b0, ProcessPhysics@18cef8]
  +0x2db  w[1] R/W [5: ImproveLanding@18bb90, InitializeCarVariables@187350, ProcessPhysics@18cef8, ProcessSimplePhysics@18ecb8, ProcessSnowmobilePhysics@190510]
  +0x2dc  w[4] R/W float [2: ApplyDamage@189598, PBondCar@1862c0]
  +0x2e0  w[4] R/W -> PBondCar::ControlTyreTracks [7: ApplyDamage@189598, DisableTargetBeacon@18adc0, EnableTargetBeacon@18ad50, GetTargetBeacon@1932d8, PBondCar@1862c0, Simulate@188200…]
  +0x2e4  w[4] R/W -> AIVehicle::GetSplinePath [6: GetAIGroundVehiclePtr@192fe8, GetSplinePath@18ad28, PBondCar@1862c0, ResetCar@187b00, SetAIGroundVehicle@192fe0, Simulate@188200]
  +0x2e8  w[4] R/W float [3: AddWheelForces@18c1b0, InitializeCarVariables@187350, ProcessPhysics@18cef8]
  +0x2ec  w[4] R/W float [7: AddSimpleWheelForces@18e548, AddSnowmobileForces@18fc00, AddWheelForces@18c1b0, ProcessPhysics@18cef8, ProcessSimplePhysics@18ecb8, ProcessSnowmobilePhysics@190510…]
  +0x2f0  w- LEA addr-taken [4: ApplyDamage@189598, GetDamageZones@18aa78, ResetDamage@187578, SetVisualDamage@1876c8]
  +0x2f4  w- LEA addr-taken [2: ApplyDamage@189598, SetVisualDamage@1876c8]
  +0x370  w[4] R/W float [3: AddDamageByPlayer@187608, InitializeCarVariables@187350, ResetDamage@187578]
  +0x374  w[1] R/W [5: AddDamageByPlayer@187608, GetDamageByPlayerTimer@193168, InitializeCarVariables@187350, ResetDamage@187578, Simulate@188200]
  +0x378  w[4] W [1: PBondCar@1862c0]
  +0x37c  w[4] R/W [4: ApplyDamage@189598, DisableTyreBlowOuts@193240, EnableTyreBlowOuts@193380, PBondCar@1862c0]
  +0x380  w[4] R/W [6: AttackWithEmp@18ac28, ClearEMPState@18b730, EmpActive@193108, InitializeCarVariables@187350, PBondCar@1862c0, Simulate@188200]
  +0x384  w[4] R/W [5: AttackWithEmp@18ac28, ClearEMPState@18b730, InitializeCarVariables@187350, PBondCar@1862c0, Simulate@188200]
  +0x388  w[4] R/W -> AVehicle::ChooseHorn [4: ApplyDamage@189598, GetAudio@192fd0, InitAudioObject@186878, SetAudio@192fd8]
  +0x38c  w[4] W [3: PBondCar@1862c0, SignalLeft@18b4e0, SignalRight@18b530]
  +0x390  w[4] W [1: PBondCar@1862c0]
  +0x394  w[4] W float [1: PBondCar@1862c0]
  +0x398  w[4] W float [2: SignalLeft@18b4e0, SignalRight@18b530]
  +0x39c  w[4] W [3: PBondCar@1862c0, SignalLeft@18b4e0, SignalRight@18b530]
  +0x3a0  w[1] LEA/R/W addr-taken [4: FireLaser@18ac08, GetCarToggleInfo@193360, PBondCar@1862c0, Simulate@188200]
  +0x3a1  w[1] W [1: PBondCar@1862c0]
  +0x3a2  w[1] W [1: PBondCar@1862c0]
  +0x3a3  w[1] W [1: PBondCar@1862c0]
  +0x3a4  w[4] R/W [3: FireLaser@18ac08, InitializeCarVariables@187350, Simulate@188200]
  +0x3a8  w- LEA addr-taken [2: GlareOff@18b408, GlareOn@18b328]
  +0x3e4  w- LEA addr-taken [2: PBondCar@1862c0, ResetDamage@187578]
  +0x3e8  w[4] R/W [3: GetResetAvailable@192ff0, InitializeCarVariables@187350, Simulate@188200]
  +0x3f0  w[8] LEA/R/W addr-taken [3: ChangeCarType@185e50, PBondCar@1862c0, ResetCar@187b00]
  +0x3f8  w[4] R/W [2: PBondCar@1862c0, ResetCar@187b00]
  +0x3fc  w[8] R/W [3: LockCarControl@1932f0, LockCarControlForever@193060, Simulate@188200]
  +0x404  w[8] R/W [3: LockCarControl@1932f0, LockCarControlForever@193060, Simulate@188200]
  +0x40c  w[8] R/W [3: LockCarControl@1932f0, LockCarControlForever@193060, Simulate@188200]
  +0x414  w[8] R/W [3: LockCarControl@1932f0, LockCarControlForever@193060, Simulate@188200]
  +0x41c  w[4] R/W [5: InitializeCarVariables@187350, LockCarControl@1932f0, LockCarControlForever@193060, Simulate@188200, UnlockCarControl@193348]
  +0x420  w[4] R/W float [3: InitializeCarVariables@187350, ProcessSimplePhysics@18ecb8, ProcessSubmarinePhysics@190f88]
  +0x424  w[4] R/W float [3: InitializeCarVariables@187350, ProcessSimplePhysics@18ecb8, ProcessSubmarinePhysics@190f88]
  +0x428  w[4] R/W float [3: InitializeCarVariables@187350, ProcessSimplePhysics@18ecb8, ProcessSubmarinePhysics@190f88]
  +0x42c  w[4] LEA/W float addr-taken [2: PBondCar@1862c0, ResetDamage@187578]
  +0x430  w[4] R/W [3: ApplyDamage@189598, PBondCar@1862c0, SetImmunity@193268]
  +0x434  w[4] R/W [3: ApplyDamage@189598, PBondCar@1862c0, SetShieldPointLoc@1931f0]
  +0x438  w[4] R/W float [2: GetControllerInput@18add8, InitializeCarControls@1872d8]
  +0x43c  w[1] R/W [4: InitializeCarVariables@187350, ProcessPhysics@18cef8, ProcessSimplePhysics@18ecb8, SetOilSlick@193158]
  +0x43d  w[1] R/W [3: InitializeCarVariables@187350, ProcessPhysics@18cef8, ProcessSimplePhysics@18ecb8]
  +0x43e  w[1] R/W [5: ControlTyreTracks@18bf08, InitializeCarVariables@187350, ProcessPhysics@18cef8, ProcessSimplePhysics@18ecb8, ProcessSplinePhysics@192498]
  +0x440  w[4] R/W [5: ApplyDamage@189598, GetScoreable@193390, PBondCar@1862c0, SetScoreable@193140, Simulate@188200]
  +0x444  w[4] R/W float [2: GetControllerInput@18add8, InitializeCarControls@1872d8]
  +0x448  w[4] R/W float [2: GetControllerInput@18add8, InitializeCarControls@1872d8]
  +0x44c  w[1] R/W [2: PBondCar@1862c0, ProcessSubmarinePhysics@190f88]
  +0x44d  w[1] R/W [3: ForceRollDirection@1932b8, PBondCar@1862c0, ProcessSubmarinePhysics@190f88]
  +0x44e  w[1] R/W [5: ForceStopOff@1932a0, ForceStopOn@193288, GetControllerInput@18add8, GetForceStop@193280, PBondCar@1862c0]
  +0x44f  w[1] R/W [3: PBondCar@1862c0, ProcessSubmarinePhysics@190f88, RollSub@1932c0]
  +0x450  w[4] R/W [2: GetSecondaryType@1932d0, PBondCar@1862c0]
  +0x458  w[4] R/W [3: GetWasInAir@193258, InitializeCarVariables@187350, SetWasInAir@193260]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  GetPhysics: R +0x23c w4
  FlushCarControls: R +0x26c w4
  GetDamageZones: LEA +0x2f0 w0
  GetSplinePath: R +0x2e4 w4
  DisableTargetBeacon: R +0x2e0 w4
  EnableTwoWheelStunt: W +0x2b2 w1
  GetAudio: R +0x37c w4
  SetAudio: W +0x37c w4
  SetAIGroundVehicle: W +0x2e4 w4
  GetAIGroundVehiclePtr: R +0x2e4 w4
  GetResetAvailable: R +0x3ac w4
  GetCarType: R +0x2cc w4
  SetCarClass: W +0x2c8 w4
  GetCarClass: R +0x2c8 w4
  GetCarColour: R +0x2d0 w4
  SetCarControlSteering: W +0x240 w4
  SetCarControlSteeringVertical: W +0x244 w4
  SetCarControlStrafeHorizontal: W +0x24c w4
  SetCarControlStrafeVertical: W +0x248 w4
  SetCarControlGas: W +0x250 w4
  SetCarControlBrake: W +0x254 w4
  SetCarControlHandBrake: W +0x259 w1
  GetCarControlHandBrake: R +0x259 w1
  SetCarControlFirePrimary: W +0x25a w1
  SetTargetGas: W +0x2bc w4
  SetTargetBrake: W +0x2c0 w4
  EmpActive: R +0x377 w1
  GetCarSpeed: W +0x288 w4 float
  SetScoreable: W +0x3ff w1
  InShock: R +0x29d w1
  SetOilSlick: W +0x3fc w1
  GetNumWheelsOnGround: R +0x299 w1
  GetDamageByPlayerTimer: R +0x374 w1
  GetCarSteer: W +0x28c w4 float
  IsReversing: R +0x29c w1
  SetAgainstWallFlag: W +0x29e w1
  SetShieldPointLoc: W +0x3f4 w4
  GetSuspensionCompression: LEA +0x210 w0
  DisableTyreBlowOuts: W +0x376 w1
  GetIsInTwoWheelMode: R +0x2b2 w1
  GetWasInAir: R +0x411 w1
  SetWasInAir: W +0x411 w1
  SetImmunity: W +0x3f0 w1
  GetForceStop: R +0x40a w1
  ForceStopOn: R +0x40a w1
  ForceStopOff: R +0x40a w1
  ForceRollDirection: W +0x409 w1
  GetSecondaryType: R +0x40c w4
  GetTargetBeacon: R +0x2e0 w4
