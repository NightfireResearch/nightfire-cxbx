# AVehicle

FastAlloc/constructed sizes under its tag: None
deleting destructor 0x120d50 frees/deletes with size 0x1f0 (call to ABaseSound::operator_delete)
Xbox vtable 0x001a2680 (9 slots) stored by its constructor
PS2 sheet virtual table row: ['AVehicle virtual table']
constructor 0x11e2e0 first calls: ['ASceneObj::ASceneObj', 'AMix::Get', 'AMix::Get']

Xbox methods (15):
  0x11e230 undefined Reset(void)
  0x11e2e0 undefined4 * __thiscall AVehicle(AVehicle * this, undefined4 * param_1_00, char * param_2, int param_3)
  0x11e9a0 undefined ActivateGun(undefined4 param_1)
  0x11ea10 undefined ActivateTurret(void)
  0x11ea80 undefined ActivateZoom(void)
  0x11eaf0 undefined ActivateWind(void)
  0x11eb50 void __thiscall CalculateSpeed(AVehicle * this)
  0x11ebf0 undefined GetAirborneWheels(void)
  0x11ec80 undefined CalculateRpm(void)
  0x11ed30 undefined PlayLanding(undefined param_1, undefined4 param_2)
  0x11f1a0 undefined PlayCollision(void)
  0x11f9b0 void __thiscall Play(AVehicle * this, float * param_1)
  0x120960 undefined ChooseHorn(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0x120a10 void __thiscall ~AVehicle(AVehicle * this)
  0x120d50 undefined scalar_deleting_destructor(undefined1 param_1)

PS2 methods (62):
  0x2def58 AVehicle::Reset
  0x2defd0 AVehicle::AVehicle
  0x2df538 AVehicle::ChooseHorn
  0x2df650 AVehicle::ActivateGun
  0x2df6a0 AVehicle::ActivateTurret
  0x2df6e8 AVehicle::ActivateZoom
  0x2df738 AVehicle::ActivateWind
  0x2df780 AVehicle::StartAlarm
  0x2df7c0 AVehicle::CalculateSpeed
  0x2df8a0 AVehicle::CalculateRpm
  0x2df9b0 AVehicle::GetAirborneWheels
  0x2dfa00 AVehicle::PlayLanding
  0x2e0890 AVehicle::PlayCollision
  0x2e1768 AVehicle::Play
  0x2e2b60 AVehicle::~AVehicle
  0x2e3320 AVehicle::SetVehicleType
  0x2e3328 AVehicle::GetVehicleType
  0x2e3330 AVehicle::HonkHorn
  0x2e3340 AVehicle::GetGun
  0x2e3348 AVehicle::GetTurret
  0x2e3350 AVehicle::GetZoomer
  0x2e3358 AVehicle::SetTerrain
  0x2e3360 AVehicle::GetTerrain
  0x2e3368 AVehicle::SetGasButton
  0x2e3370 AVehicle::SetBrakeButton
  0x2e3378 AVehicle::SetXYTrigger
  0x2e3380 AVehicle::SetXYReleaseTrigger
  0x2e3388 AVehicle::GetGasButton
  0x2e3390 AVehicle::GetBrakeButton
  0x2e3398 AVehicle::GetSteeringPadX
  0x2e33a0 AVehicle::GetSteeringPadY
  0x2e33a8 AVehicle::GetXYTrigger
  0x2e33b0 AVehicle::GetXYReleaseTrigger
  0x2e33b8 AVehicle::SetGas
  0x2e33c0 AVehicle::GetGas
  0x2e33c8 AVehicle::SetRpm
  0x2e33d0 AVehicle::GetRpm
  0x2e33e0 AVehicle::GetSpeed
  0x2e33e8 AVehicle::SetCarSpeed
  0x2e33f0 AVehicle::GetCarSpeed
  0x2e33f8 AVehicle::SetBoost
  0x2e3400 AVehicle::SetIgnition
  0x2e3408 AVehicle::GetIgnition
  0x2e3410 AVehicle::SetSkidMultiple
  0x2e3418 AVehicle::GetSkidMultiple
  0x2e3420 AVehicle::SetFrontSlip
  0x2e3450 AVehicle::GetFrontSlip
  0x2e3458 AVehicle::SetBackSlip
  0x2e3488 AVehicle::GetBackSlip
  0x2e3490 AVehicle::GetSurface
  0x2e34a0 AVehicle::GetLandingForce
  0x2e34a8 AVehicle::SetLandingForce
  0x2e34b0 AVehicle::IsWheelOffGround
  0x2e34d8 AVehicle::SetCompression
  0x2e3500 AVehicle::GetCompression
  0x2e3508 AVehicle::GetAsphaltMix
  0x2e3510 AVehicle::GetOffRoadMix
  0x2e3518 AVehicle::GetWind
  0x2e3520 AVehicle::operator_new
  0x2e3540 AVehicle::SetSurface
  0x2e3560 AVehicle::IsInAir
  0x2e3588 AVehicle::SetSteering

Sheet rows:
  AVehicle::Reset(void)
  AVehicle::AVehicle(char *, ASceneObj::SurfaceType)
  AVehicle::ChooseHorn(char *, float, float)
  AVehicle::ActivateGun(char *)
  AVehicle::ActivateTurret(void)
  AVehicle::ActivateZoom(void)
  AVehicle::ActivateWind(void)
  AVehicle::StartAlarm(void)
  AVehicle::CalculateSpeed(void)
  AVehicle::CalculateRpm(void)
  AVehicle::GetAirborneWheels(void) const
  AVehicle::PlayLanding(float, COORD3 &, COORD3 &)
  AVehicle::PlayCollision(float, COORD3 &, COORD3 &, ASceneObj::S
  AVehicle::Play(APath &)
  AVehicle::~AVehicle(void)
  AVehicle type_info function
  AVehicle::SetVehicleType(AVehicle::VehicleType)
  AVehicle::GetVehicleType(void)
  AVehicle::HonkHorn(void)
  AVehicle::GetGun(void)
  AVehicle::GetTurret(void)
  AVehicle::GetZoomer(void)
  AVehicle::SetTerrain(short)
  AVehicle::GetTerrain(void)
  AVehicle::SetGasButton(bool)
  AVehicle::SetBrakeButton(bool)
  AVehicle::SetXYTrigger(bool)
  AVehicle::SetXYReleaseTrigger(bool)
  AVehicle::GetGasButton(void)
  AVehicle::GetBrakeButton(void)
  AVehicle::GetSteeringPadX(void)
  AVehicle::GetSteeringPadY(void)
  AVehicle::GetXYTrigger(void)
  AVehicle::GetXYReleaseTrigger(void)
  AVehicle::SetGas(float)
  AVehicle::GetGas(void) const
  AVehicle::SetRpm(float)
  AVehicle::GetRpm(void) const
  AVehicle::GetSpeed(void) const
  AVehicle::SetCarSpeed(float)
  AVehicle::GetCarSpeed(void)
  AVehicle::SetBoost(bool)
  AVehicle::SetIgnition(bool)
  AVehicle::GetIgnition(void)
  AVehicle::SetSkidMultiple(float)
  AVehicle::GetSkidMultiple(void)
  AVehicle::SetFrontSlip(float)
  AVehicle::GetFrontSlip(void) const
  AVehicle::SetBackSlip(float)
  AVehicle::GetBackSlip(void) const
  AVehicle::GetSurface(int)
  AVehicle::GetLandingForce(void)
  AVehicle::SetLandingForce(float)
  AVehicle::IsWheelOffGround(void) const
  AVehicle::SetCompression(float *)
  AVehicle::GetCompression(void)
  AVehicle::GetAsphaltMix(void) const
  AVehicle::GetOffRoadMix(void) const
  AVehicle::GetWind(void)
  AVehicle::operator new(unsigned int)
  AVehicle::SetSurface(int, ASceneObj::SurfaceType)
  AVehicle::IsInAir(void) const
  AVehicle::SetSteering(float, float, float, float)
  AVehicle virtual table
  AVehicle type_info node

Xbox methods treated as members (15 of 15; untyped ones count when ECX is read before it is written): AVehicle, ActivateGun, ActivateTurret, ActivateWind, ActivateZoom, CalculateRpm, CalculateSpeed, ChooseHorn, GetAirborneWheels, Play, PlayCollision, PlayLanding, Reset, scalar_deleting_destructor, ~AVehicle

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W [3: AVehicle@11e2e0, CalculateRpm@11ec80, ~AVehicle@120a10]
  +0x010  w- LEA addr-taken [1: Play@11f9b0]
  +0x020  w- LEA addr-taken [2: CalculateSpeed@11eb50, Play@11f9b0]
  +0x0c0  w[1] R [1: Play@11f9b0]
  +0x0c1  w[1] R [1: Play@11f9b0]
  +0x0c2  w[1] R [1: Play@11f9b0]
  +0x0c3  w[1] R [1: Play@11f9b0]
  +0x0d0  w[4] R [1: Play@11f9b0]
  +0x0d4  w[4] R [3: Play@11f9b0, PlayCollision@11f1a0, PlayLanding@11ed30]
  +0x0d8  w[4] R [3: Play@11f9b0, PlayCollision@11f1a0, PlayLanding@11ed30]
  +0x0dc  w[4] R [3: Play@11f9b0, PlayCollision@11f1a0, PlayLanding@11ed30]
  +0x0e0  w[4] R [3: Play@11f9b0, PlayCollision@11f1a0, PlayLanding@11ed30]
  +0x0e4  w- LEA addr-taken [2: AVehicle@11e2e0, PlayLanding@11ed30]
  +0x104  w[4] R/W float [2: Play@11f9b0, Reset@11e230]
  +0x108  w[4] R/W float [2: Play@11f9b0, Reset@11e230]
  +0x10c  w[4] W float [2: Play@11f9b0, Reset@11e230]
  +0x110  w[4] W float [2: Play@11f9b0, Reset@11e230]
  +0x114  w[4] W float [2: AVehicle@11e2e0, Play@11f9b0]
  +0x118  w[4] W [1: Reset@11e230]
  +0x11c  w[4] W float [2: CalculateRpm@11ec80, Reset@11e230]
  +0x120  w[4] W float [3: CalculateRpm@11ec80, Play@11f9b0, Reset@11e230]
  +0x124  w[4] W float [2: Play@11f9b0, Reset@11e230]
  +0x128  w[4] W float [2: Play@11f9b0, Reset@11e230]
  +0x12c  w[4] W float [2: Play@11f9b0, Reset@11e230]
  +0x130  w[4] W float [2: Play@11f9b0, Reset@11e230]
  +0x134  w[4] W float [2: Play@11f9b0, Reset@11e230]
  +0x138  w[4] W float [2: Play@11f9b0, Reset@11e230]
  +0x13c  w[4] W float [4: CalculateRpm@11ec80, CalculateSpeed@11eb50, Play@11f9b0, Reset@11e230]
  +0x144  w[4] W float [2: Play@11f9b0, Reset@11e230]
  +0x148  w[4] W float [2: Play@11f9b0, Reset@11e230]
  +0x14c  w[4] W float [2: Play@11f9b0, Reset@11e230]
  +0x150  w[4] W float [1: PlayLanding@11ed30]
  +0x154  w[1] R/W [2: Play@11f9b0, Reset@11e230]
  +0x155  w[1] R/W [2: Play@11f9b0, Reset@11e230]
  +0x156  w[1] R/W [2: Play@11f9b0, Reset@11e230]
  +0x157  w[1] W [1: Reset@11e230]
  +0x158  w[1] W [1: Reset@11e230]
  +0x159  w[1] W [1: AVehicle@11e2e0]
  +0x15c  w[4] R/W [2: AVehicle@11e2e0, Play@11f9b0]
  +0x160  w[4] R/W [2: AVehicle@11e2e0, Play@11f9b0]
  +0x164  w[4] R/W [2: AVehicle@11e2e0, Play@11f9b0]
  +0x168  w[4] R/W [2: AVehicle@11e2e0, Play@11f9b0]
  +0x16c  w[4] R/W [3: Play@11f9b0, PlayCollision@11f1a0, Reset@11e230]
  +0x170  w[4] R/W [3: Play@11f9b0, PlayCollision@11f1a0, Reset@11e230]
  +0x174  w[4] R/W [3: Play@11f9b0, PlayCollision@11f1a0, Reset@11e230]
  +0x178  w[2] W [2: AVehicle@11e2e0, Reset@11e230]
  +0x188  w[4] R/W -> AGun::~AGun [4: AVehicle@11e2e0, ActivateGun@11e9a0, Play@11f9b0, ~AVehicle@120a10]
  +0x18c  w[4] R/W -> ATurret::~ATurret [4: AVehicle@11e2e0, ActivateTurret@11ea10, Play@11f9b0, ~AVehicle@120a10]
  +0x190  w[4] R/W -> AZoomObj::~AZoomObj, FUN_00127e50 [4: AVehicle@11e2e0, ActivateZoom@11ea80, Play@11f9b0, ~AVehicle@120a10]
  +0x194  w[4] R/W [3: AVehicle@11e2e0, Play@11f9b0, PlayCollision@11f1a0]
  +0x198  w[4] R/W -> AVoice::Play [3: AVehicle@11e2e0, Play@11f9b0, ~AVehicle@120a10]
  +0x19c  w[4] R/W -> AVoice::Play [3: AVehicle@11e2e0, Play@11f9b0, ~AVehicle@120a10]
  +0x1a0  w[4] R/W -> AVoice::Play [3: AVehicle@11e2e0, Play@11f9b0, ~AVehicle@120a10]
  +0x1a4  w[4] R/W -> AVoice::Play [3: AVehicle@11e2e0, Play@11f9b0, ~AVehicle@120a10]
  +0x1a8  w[4] R/W -> AVoice::Play [3: AVehicle@11e2e0, Play@11f9b0, ~AVehicle@120a10]
  +0x1ac  w[4] R/W -> AVoice::Play [3: AVehicle@11e2e0, Play@11f9b0, ~AVehicle@120a10]
  +0x1b0  w[4] R/W -> AVoice::Play [3: AVehicle@11e2e0, Play@11f9b0, ~AVehicle@120a10]
  +0x1b4  w[4] R/W -> AVoice::Play [3: AVehicle@11e2e0, Play@11f9b0, ~AVehicle@120a10]
  +0x1b8  w[4] R/W -> AVoice::Play [3: AVehicle@11e2e0, Play@11f9b0, ~AVehicle@120a10]
  +0x1bc  w[4] R/W -> AVoice::Play [3: AVehicle@11e2e0, Play@11f9b0, ~AVehicle@120a10]
  +0x1c0  w[4] R/W -> AVoice::Play [3: AVehicle@11e2e0, Play@11f9b0, ~AVehicle@120a10]
  +0x1c4  w[4] R/W -> AVoice::Play [3: AVehicle@11e2e0, Play@11f9b0, ~AVehicle@120a10]
  +0x1c8  w[4] R/W -> AVoice::Play [3: AVehicle@11e2e0, Play@11f9b0, ~AVehicle@120a10]
  +0x1cc  w[4] R/W -> AVoice::Play [3: AVehicle@11e2e0, Play@11f9b0, ~AVehicle@120a10]
  +0x1d0  w[4] R/W -> FUN_0011ec10 [2: AVehicle@11e2e0, ~AVehicle@120a10]
  +0x1d4  w[4] R/W [3: AVehicle@11e2e0, Play@11f9b0, ~AVehicle@120a10]
  +0x1d8  w[4] R/W -> AVehicleWind::~AVehicleWind [4: AVehicle@11e2e0, ActivateWind@11eaf0, Play@11f9b0, ~AVehicle@120a10]
  +0x1dc  w[4] R/W [6: AVehicle@11e2e0, CalculateRpm@11ec80, CalculateSpeed@11eb50, ChooseHorn@120960, Play@11f9b0, ~AVehicle@120a10]
  +0x1e0  w[4] W [1: AVehicle@11e2e0]
  +0x1e4  w[4] R/W -> AMix::GetVolume [2: AVehicle@11e2e0, Play@11f9b0]
  +0x1e8  w[4] R/W -> AMix::GetVolume [2: AVehicle@11e2e0, Play@11f9b0]
  +0x1ec  w[4] R/W -> AMix::GetVolume [2: AVehicle@11e2e0, Play@11f9b0]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4, 8] R [2: GetAirborneWheels@2df9b0, Play@2e1768]
  +0x008  w[4] R [1: Play@2e1768]
  +0x010  w- LEA addr-taken [3: Play@2e1768, PlayCollision@2e0890, PlayLanding@2dfa00]
  +0x0ac  w[4] R/W [3: AVehicle@2defd0, CalculateRpm@2df8a0, ~AVehicle@2e2b60]
  +0x0b0  w[4] R [1: Play@2e1768]
  +0x0b4  w[4] R [1: Play@2e1768]
  +0x0b8  w[4] R [1: Play@2e1768]
  +0x0bc  w[4] R [1: Play@2e1768]
  +0x0d0  w[4] R/W [3: GetVehicleType@2e3328, Play@2e1768, SetVehicleType@2e3320]
  +0x0d4  w[4] LEA/R addr-taken [6: AVehicle@2defd0, IsInAir@2e3560, Play@2e1768, PlayCollision@2e0890, PlayLanding@2dfa00, SetSurface@2e3540]
  +0x0d8  w[4, 8] R [3: IsInAir@2e3560, Play@2e1768, PlayCollision@2e0890]
  +0x0dc  w[4] R [3: IsWheelOffGround@2e34b0, Play@2e1768, PlayCollision@2e0890]
  +0x0e0  w[4] R [4: IsInAir@2e3560, IsWheelOffGround@2e34b0, Play@2e1768, PlayCollision@2e0890]
  +0x0e4  w- LEA addr-taken [1: AVehicle@2defd0]
  +0x0f4  w[4] LEA/W float addr-taken [2: GetCompression@2e3500, SetCompression@2e34d8]
  +0x0f8  w[4] W float [1: SetCompression@2e34d8]
  +0x0fc  w[4] W float [1: SetCompression@2e34d8]
  +0x100  w[4] W float [1: SetCompression@2e34d8]
  +0x104  w[4] R/W float [3: Play@2e1768, Reset@2def58, SetFrontSlip@2e3420]
  +0x108  w[4] R/W float [3: Play@2e1768, Reset@2def58, SetBackSlip@2e3458]
  +0x10c  w[4] R/W float [3: GetFrontSlip@2e3450, Play@2e1768, Reset@2def58]
  +0x110  w[4] R/W float [3: GetBackSlip@2e3488, Play@2e1768, Reset@2def58]
  +0x114  w[4] R/W float [4: AVehicle@2defd0, GetSkidMultiple@2e3418, Play@2e1768, SetSkidMultiple@2e3410]
  +0x118  w[4] R/W float [3: GetGas@2e33c0, Reset@2def58, SetGas@2e33b8]
  +0x11c  w[4] R/W float [4: CalculateRpm@2df8a0, GetRpm@2e33d0, Reset@2def58, SetRpm@2e33c8]
  +0x120  w[4] R/W float [4: CalculateRpm@2df8a0, GetRpm@2e33d0, Play@2e1768, Reset@2def58]
  +0x124  w[4] R/W float [2: Play@2e1768, Reset@2def58]
  +0x128  w[4] R/W float [2: Play@2e1768, Reset@2def58]
  +0x12c  w[4] R/W float [2: Play@2e1768, Reset@2def58]
  +0x130  w[4] R/W float [2: Play@2e1768, Reset@2def58]
  +0x134  w[4] R/W float [2: Play@2e1768, Reset@2def58]
  +0x138  w[4] R/W float [2: Play@2e1768, Reset@2def58]
  +0x13c  w[4] R/W float [5: CalculateRpm@2df8a0, CalculateSpeed@2df7c0, GetSpeed@2e33e0, Play@2e1768, Reset@2def58]
  +0x140  w[4] R/W float [2: GetCarSpeed@2e33f0, SetCarSpeed@2e33e8]
  +0x144  w[4] R/W float [2: Play@2e1768, Reset@2def58]
  +0x148  w[4] R/W float [2: Play@2e1768, Reset@2def58]
  +0x14c  w[4] R/W float [2: Play@2e1768, Reset@2def58]
  +0x150  w[4] R/W float [3: GetLandingForce@2e34a0, PlayLanding@2dfa00, SetLandingForce@2e34a8]
  +0x154  w[4] R/W [3: HonkHorn@2e3330, Play@2e1768, Reset@2def58]
  +0x158  w[4] R/W [2: Play@2e1768, Reset@2def58]
  +0x15c  w[4] R/W [2: Play@2e1768, Reset@2def58]
  +0x160  w[4] W [2: Reset@2def58, SetBoost@2e33f8]
  +0x164  w[4] R/W [3: GetIgnition@2e3408, Reset@2def58, SetIgnition@2e3400]
  +0x168  w[4] W [1: AVehicle@2defd0]
  +0x16c  w[4] R/W [2: AVehicle@2defd0, Play@2e1768]
  +0x170  w[4] R/W [2: AVehicle@2defd0, Play@2e1768]
  +0x174  w[4] R/W [2: AVehicle@2defd0, Play@2e1768]
  +0x178  w[4] R/W [2: AVehicle@2defd0, Play@2e1768]
  +0x17c  w[4] R/W [3: Play@2e1768, PlayCollision@2e0890, Reset@2def58]
  +0x180  w[4] R/W [3: Play@2e1768, PlayCollision@2e0890, Reset@2def58]
  +0x184  w[4] R/W [3: Play@2e1768, PlayCollision@2e0890, Reset@2def58]
  +0x188  w[2] R/W [4: AVehicle@2defd0, GetTerrain@2e3360, Reset@2def58, SetTerrain@2e3358]
  +0x18c  w[4] R/W [2: GetGasButton@2e3388, SetGasButton@2e3368]
  +0x190  w[4] R/W [2: GetBrakeButton@2e3390, SetBrakeButton@2e3370]
  +0x194  w[4] R/W [2: GetXYTrigger@2e33a8, SetXYTrigger@2e3378]
  +0x198  w[4] R/W [2: GetXYReleaseTrigger@2e33b0, SetXYReleaseTrigger@2e3380]
  +0x19c  w[4] R/W float [2: GetSteeringPadX@2e3398, SetSteering@2e3588]
  +0x1a0  w[4] R/W float [2: GetSteeringPadY@2e33a0, SetSteering@2e3588]
  +0x1a4  w[4] R/W -> AGun::Play, AGun::~AGun, AHorn::Play [5: AVehicle@2defd0, ActivateGun@2df650, GetGun@2e3340, Play@2e1768, ~AVehicle@2e2b60]
  +0x1a8  w[4] R/W -> ATurret::~ATurret [5: AVehicle@2defd0, ActivateTurret@2df6a0, GetTurret@2e3348, Play@2e1768, ~AVehicle@2e2b60]
  +0x1ac  w[4] R/W -> ATurret::~ATurret, AZoomObj::Play, AZoomObj::~AZoomObj [5: AVehicle@2defd0, ActivateZoom@2df6e8, GetZoomer@2e3350, Play@2e1768, ~AVehicle@2e2b60]
  +0x1b0  w[4] R/W -> AIndex::Lookup [3: AVehicle@2defd0, Play@2e1768, PlayCollision@2e0890]
  +0x1b4  w[4] R/W -> AVoice::Play [3: AVehicle@2defd0, Play@2e1768, ~AVehicle@2e2b60]
  +0x1b8  w[4] R/W -> AVoice::Play [3: AVehicle@2defd0, Play@2e1768, ~AVehicle@2e2b60]
  +0x1bc  w[4] R/W [3: AVehicle@2defd0, Play@2e1768, ~AVehicle@2e2b60]
  +0x1c0  w[4] R/W -> AVoice::Play [3: AVehicle@2defd0, Play@2e1768, ~AVehicle@2e2b60]
  +0x1c4  w[4] R/W -> AVoice::Play [3: AVehicle@2defd0, Play@2e1768, ~AVehicle@2e2b60]
  +0x1c8  w[4] R/W -> AVoice::Play [3: AVehicle@2defd0, Play@2e1768, ~AVehicle@2e2b60]
  +0x1cc  w[4] R/W -> AVoice::Play [3: AVehicle@2defd0, Play@2e1768, ~AVehicle@2e2b60]
  +0x1d0  w[4] R/W -> AVoice::Play [3: AVehicle@2defd0, Play@2e1768, ~AVehicle@2e2b60]
  +0x1d4  w[4] R/W -> AVoice::Play [3: AVehicle@2defd0, Play@2e1768, ~AVehicle@2e2b60]
  +0x1d8  w[4] R/W -> AVoice::Play [3: AVehicle@2defd0, Play@2e1768, ~AVehicle@2e2b60]
  +0x1dc  w[4] R/W -> AVoice::Play [3: AVehicle@2defd0, Play@2e1768, ~AVehicle@2e2b60]
  +0x1e0  w[4] R/W -> AVoice::Play [3: AVehicle@2defd0, Play@2e1768, ~AVehicle@2e2b60]
  +0x1e4  w[4] R/W -> AVoice::Play [3: AVehicle@2defd0, Play@2e1768, ~AVehicle@2e2b60]
  +0x1e8  w[4] R/W [3: AVehicle@2defd0, Play@2e1768, ~AVehicle@2e2b60]
  +0x1ec  w[4] R/W [2: AVehicle@2defd0, ~AVehicle@2e2b60]
  +0x1f0  w[4] R/W -> ACarAlarm::Play, AZoomObj::Play [4: AVehicle@2defd0, Play@2e1768, StartAlarm@2df780, ~AVehicle@2e2b60]
  +0x1f4  w[4] R/W -> AVehicleWind::Play, AVehicleWind::~AVehicleWind [5: AVehicle@2defd0, ActivateWind@2df738, GetWind@2e3518, Play@2e1768, ~AVehicle@2e2b60]
  +0x1f8  w[4] R/W -> AHorn::Play, AVehicleWind::Stop [6: AVehicle@2defd0, CalculateRpm@2df8a0, CalculateSpeed@2df7c0, ChooseHorn@2df538, Play@2e1768, ~AVehicle@2e2b60]
  +0x1fc  w[4] R/W [2: AVehicle@2defd0, GetAsphaltMix@2e3508]
  +0x200  w[4] R/W -> AMix::GetVolume [3: AVehicle@2defd0, GetOffRoadMix@2e3510, Play@2e1768]
  +0x204  w[4] R/W [2: AVehicle@2defd0, Play@2e1768]
  +0x208  w[4] R/W -> AMix::GetVolume [2: AVehicle@2defd0, Play@2e1768]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
