# ASnowMobile

FastAlloc/constructed sizes under its tag: {'allocated': [608], 'constructed': []}
deleting destructor 0x12b510 frees/deletes with size 0x260 (call to ABaseSound::operator_delete)
Xbox vtable 0x001a2c40 (9 slots) stored by its constructor
PS2 sheet virtual table row: ['ASnowMobile virtual table']
constructor 0x12b5f0 first calls: ['AVehicle::AVehicle', 'AMix::Get', '__builtin_new']

Xbox methods (6):
  0x12a890 undefined ~ASnowMobile(void)
  0x12aa10 undefined PlayMotor(undefined4 param_1)
  0x12b060 undefined PlayCarving(undefined4 param_1)
  0x12b510 undefined scalar_deleting_destructor(undefined1 param_1)
  0x12b540 undefined Play(void)
  0x12b5f0 undefined ASnowMobile(void)

PS2 methods (8):
  0x2ed7c0 ASnowMobile::ASnowMobile
  0x2edb08 ASnowMobile::~ASnowMobile
  0x2edd88 ASnowMobile::Play
  0x2ede60 ASnowMobile::PlayMotor
  0x2ee768 ASnowMobile::PlayCarving
  0x2ef6a8 ASnowMobile::IsTracked
  0x2ef6b0 ASnowMobile::operator_new
  0x2ef6d0 ASnowMobile::ASnowMobile_global_ctors

Sheet rows:
  ASnowMobile::ASnowMobile(char *)
  ASnowMobile::~ASnowMobile(void)
  ASnowMobile::Play(APath &)
  ASnowMobile::PlayMotor(APath &)
  ASnowMobile::PlayCarving(APath &)
  ASnowMobile type_info function
  ASnowMobile::IsTracked(void)
  ASnowMobile::operator new(unsigned int)
  ASnowMobile virtual table
  ASnowMobile type_info node

Xbox methods treated as members (6 of 6; untyped ones count when ECX is read before it is written): ASnowMobile, Play, PlayCarving, PlayMotor, scalar_deleting_destructor, ~ASnowMobile

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: ASnowMobile@12b5f0, ~ASnowMobile@12a890]
  +0x010  w- LEA addr-taken [2: PlayCarving@12b060, PlayMotor@12aa10]
  +0x020  w- LEA addr-taken [2: PlayCarving@12b060, PlayMotor@12aa10]
  +0x030  w- LEA addr-taken [1: Play@12b540]
  +0x040  w- LEA addr-taken -> v3dotprod [1: Play@12b540]
  +0x060  w- LEA addr-taken [1: Play@12b540]
  +0x07c  w[4] W float [2: PlayCarving@12b060, PlayMotor@12aa10]
  +0x094  w[4] R -> AMix::GetVolume [3: ASnowMobile@12b5f0, PlayCarving@12b060, PlayMotor@12aa10]
  +0x0c5  w[1] W [1: ASnowMobile@12b5f0]
  +0x0d0  w[4] W [1: ASnowMobile@12b5f0]
  +0x0d4  w[4] R [1: PlayCarving@12b060]
  +0x0d8  w[4] R [1: PlayCarving@12b060]
  +0x0e0  w[4] R [1: PlayCarving@12b060]
  +0x0e4  w[4] R [1: PlayCarving@12b060]
  +0x0f4  w[4] W float [1: PlayMotor@12aa10]
  +0x0f8  w[4] W float [1: PlayMotor@12aa10]
  +0x0fc  w[4] W float [1: PlayMotor@12aa10]
  +0x100  w[4] W float [1: PlayMotor@12aa10]
  +0x118  w[4] W float [1: PlayMotor@12aa10]
  +0x11c  w[4] W float [1: PlayMotor@12aa10]
  +0x120  w[4] W float [1: PlayMotor@12aa10]
  +0x13c  w[4] R [1: PlayCarving@12b060]
  +0x188  w[4] R -> AGun::SetFirePatch [1: ASnowMobile@12b5f0]
  +0x1f0  w[4] R/W -> AVehicleWind::Play, AVehicleWind::Stop, AVehicleWind::~AVehicleWind [3: ASnowMobile@12b5f0, Play@12b540, ~ASnowMobile@12a890]
  +0x1f4  w[4] R/W -> AVoice::Play [3: ASnowMobile@12b5f0, PlayCarving@12b060, ~ASnowMobile@12a890]
  +0x1f8  w[4] R/W -> AVoice::Play [3: ASnowMobile@12b5f0, PlayCarving@12b060, ~ASnowMobile@12a890]
  +0x1fc  w[4] R/W -> AVoice::Play [3: ASnowMobile@12b5f0, PlayCarving@12b060, ~ASnowMobile@12a890]
  +0x200  w[4] R/W -> AVoice::Play [3: ASnowMobile@12b5f0, PlayCarving@12b060, ~ASnowMobile@12a890]
  +0x204  w[4] R/W -> AVoice::Play [3: ASnowMobile@12b5f0, PlayMotor@12aa10, ~ASnowMobile@12a890]
  +0x208  w[4] R/W [3: ASnowMobile@12b5f0, PlayMotor@12aa10, ~ASnowMobile@12a890]
  +0x20c  w[4] R/W -> AVoice::Play, FUN_000d36c0 [3: ASnowMobile@12b5f0, PlayMotor@12aa10, ~ASnowMobile@12a890]
  +0x210  w[4] R/W -> AMix::GetVolume [2: ASnowMobile@12b5f0, PlayCarving@12b060]
  +0x214  w[4] R/W [2: ASnowMobile@12b5f0, PlayMotor@12aa10]
  +0x218  w[2] R/W [3: ASnowMobile@12b5f0, PlayCarving@12b060, PlayMotor@12aa10]
  +0x21a  w[1] R/W [2: ASnowMobile@12b5f0, PlayMotor@12aa10]
  +0x21b  w[1] R/W [2: ASnowMobile@12b5f0, PlayMotor@12aa10]
  +0x21c  w[1] R/W [2: ASnowMobile@12b5f0, PlayMotor@12aa10]
  +0x220  w[4] W float [3: ASnowMobile@12b5f0, Play@12b540, PlayCarving@12b060]
  +0x224  w[4] W float [3: ASnowMobile@12b5f0, Play@12b540, PlayCarving@12b060]
  +0x228  w[4] W float [1: PlayMotor@12aa10]
  +0x22c  w[4] W float [2: ASnowMobile@12b5f0, PlayCarving@12b060]
  +0x230  w[4] W float [2: ASnowMobile@12b5f0, PlayCarving@12b060]
  +0x234  w[4] W [1: ASnowMobile@12b5f0]
  +0x238  w[4] W [1: ASnowMobile@12b5f0]
  +0x23c  w[4] W [1: ASnowMobile@12b5f0]
  +0x240  w[4] W float [2: ASnowMobile@12b5f0, PlayMotor@12aa10]
  +0x244  w[4] W float [3: ASnowMobile@12b5f0, PlayCarving@12b060, PlayMotor@12aa10]
  +0x248  w[4] W float [2: ASnowMobile@12b5f0, PlayMotor@12aa10]
  +0x24c  w[4] W float [2: ASnowMobile@12b5f0, PlayMotor@12aa10]
  +0x250  w[4] W [1: ASnowMobile@12b5f0]
  +0x254  w[4] W float [2: ASnowMobile@12b5f0, PlayMotor@12aa10]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[8] R [2: PlayCarving@2ee768, PlayMotor@2ede60]
  +0x008  w[4] R [2: PlayCarving@2ee768, PlayMotor@2ede60]
  +0x010  w- LEA addr-taken [2: PlayCarving@2ee768, PlayMotor@2ede60]
  +0x06c  w[4] R float [2: PlayCarving@2ee768, PlayMotor@2ede60]
  +0x084  w[4] R -> AMix::GetVolume [3: ASnowMobile@2ed7c0, PlayCarving@2ee768, PlayMotor@2ede60]
  +0x0ac  w[4] W [2: ASnowMobile@2ed7c0, ~ASnowMobile@2edb08]
  +0x0c4  w[4] W [1: ASnowMobile@2ed7c0]
  +0x0d0  w[4] W [1: ASnowMobile@2ed7c0]
  +0x0d4  w[4] R [1: PlayCarving@2ee768]
  +0x0d8  w[4] R [1: PlayCarving@2ee768]
  +0x0e0  w[4] R [1: PlayCarving@2ee768]
  +0x0e4  w[4] R [1: PlayCarving@2ee768]
  +0x0f4  w[4] LEA/R float addr-taken [1: PlayMotor@2ede60]
  +0x118  w[4] R float [1: PlayMotor@2ede60]
  +0x11c  w[4] R float [1: PlayMotor@2ede60]
  +0x120  w[4] R float [1: PlayMotor@2ede60]
  +0x13c  w[4] R float [1: PlayCarving@2ee768]
  +0x1a4  w[4] R -> AGun::SetFirePatch [1: ASnowMobile@2ed7c0]
  +0x210  w[4] R/W -> AVehicleWind::Stop, AVehicleWind::~AVehicleWind [3: ASnowMobile@2ed7c0, Play@2edd88, ~ASnowMobile@2edb08]
  +0x214  w[4] R/W -> AVoice::Play [3: ASnowMobile@2ed7c0, PlayCarving@2ee768, ~ASnowMobile@2edb08]
  +0x218  w[4] R/W -> AVoice::Play [3: ASnowMobile@2ed7c0, PlayCarving@2ee768, ~ASnowMobile@2edb08]
  +0x21c  w[4] R/W -> AVoice::Play [3: ASnowMobile@2ed7c0, PlayCarving@2ee768, ~ASnowMobile@2edb08]
  +0x220  w[4] R/W -> AVoice::Play [3: ASnowMobile@2ed7c0, PlayCarving@2ee768, ~ASnowMobile@2edb08]
  +0x224  w[4] R/W -> AVoice::Play [3: ASnowMobile@2ed7c0, PlayMotor@2ede60, ~ASnowMobile@2edb08]
  +0x228  w[4] R/W -> AVoice::Play [3: ASnowMobile@2ed7c0, PlayMotor@2ede60, ~ASnowMobile@2edb08]
  +0x22c  w[4] R/W -> AVehicle::GetAirborneWheels, AVoice::Play, UMemory::FastAlloc [3: ASnowMobile@2ed7c0, PlayMotor@2ede60, ~ASnowMobile@2edb08]
  +0x230  w[4] R/W -> AMix::GetVolume [2: ASnowMobile@2ed7c0, PlayCarving@2ee768]
  +0x234  w[4] R/W [2: ASnowMobile@2ed7c0, PlayMotor@2ede60]
  +0x238  w[2] R/W [3: ASnowMobile@2ed7c0, PlayCarving@2ee768, PlayMotor@2ede60]
  +0x23c  w[4] R/W [2: ASnowMobile@2ed7c0, PlayMotor@2ede60]
  +0x240  w[4] R/W [2: ASnowMobile@2ed7c0, PlayMotor@2ede60]
  +0x244  w[4] R/W [2: ASnowMobile@2ed7c0, PlayMotor@2ede60]
  +0x248  w[4] R/W float [3: ASnowMobile@2ed7c0, Play@2edd88, PlayCarving@2ee768]
  +0x24c  w[4] R/W float [3: ASnowMobile@2ed7c0, Play@2edd88, PlayCarving@2ee768]
  +0x250  w[4] R/W float [1: PlayMotor@2ede60]
  +0x254  w[4] R/W float [2: ASnowMobile@2ed7c0, PlayCarving@2ee768]
  +0x258  w[4] R/W float [2: ASnowMobile@2ed7c0, PlayCarving@2ee768]
  +0x25c  w[4] W float [1: ASnowMobile@2ed7c0]
  +0x260  w[4] W float [1: ASnowMobile@2ed7c0]
  +0x264  w[4] W float [1: ASnowMobile@2ed7c0]
  +0x268  w[4] R/W float [2: ASnowMobile@2ed7c0, PlayMotor@2ede60]
  +0x26c  w[4] R/W float [3: ASnowMobile@2ed7c0, PlayCarving@2ee768, PlayMotor@2ede60]
  +0x270  w[4] R/W float [2: ASnowMobile@2ed7c0, PlayMotor@2ede60]
  +0x274  w[4] R/W float [2: ASnowMobile@2ed7c0, PlayMotor@2ede60]
  +0x278  w[4] W float [1: ASnowMobile@2ed7c0]
  +0x27c  w[4] R/W float [2: ASnowMobile@2ed7c0, PlayMotor@2ede60]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
