# ATrafficVehicle

FastAlloc/constructed sizes under its tag: {'allocated': [576], 'constructed': []}
deleting destructor 0x12d400 frees/deletes with size 0x240 (call to ABaseSound::operator_delete)
Xbox vtable 0x001a2ed8 (9 slots) stored by its constructor
PS2 sheet virtual table row: ['ATrafficVehicle virtual table']
constructor 0x12cb90 first calls: ['AVehicle::AVehicle', 'sprintf', 'UMemory::FastAlloc']

Xbox methods (7):
  0x12cb90 undefined ATrafficVehicle(undefined4 param_1, undefined4 param_2)
  0x12ce10 undefined IsTracked(void)
  0x12ce20 undefined GetName(void)
  0x12ceb0 undefined PlaySnowmobileSpecifics(void)
  0x12d300 undefined ~ATrafficVehicle(void)
  0x12d400 undefined scalar_deleting_destructor(undefined1 param_1)
  0x12d430 undefined Play(void)

PS2 methods (9):
  0x2e5340 ATrafficVehicle::ATrafficVehicle
  0x2e5598 ATrafficVehicle::Play
  0x2e58f8 ATrafficVehicle::ActivateSiren
  0x2e5968 ATrafficVehicle::PlaySnowmobileSpecifics
  0x2e6640 ATrafficVehicle::~ATrafficVehicle
  0x2e67e0 ATrafficVehicle::GetName
  0x2e6a20 ATrafficVehicle::IsTracked
  0x2e6a48 ATrafficVehicle::operator_new
  0x2e6a68 ATrafficVehicle::ATrafficVehicle_global_ctors

Sheet rows:
  ATrafficVehicle::ATrafficVehicle(char *, char *)
  ATrafficVehicle::Play(APath &)
  ATrafficVehicle::ActivateSiren(char *)
  ATrafficVehicle::PlaySnowmobileSpecifics(float)
  ATrafficVehicle::~ATrafficVehicle(void)
  ATrafficVehicle::GetName(void) const
  ATrafficVehicle type_info function
  ATrafficVehicle::AddPitch(float)
  ATrafficVehicle::IsTracked(void)
  ATrafficVehicle::TurnSirenOn(void)
  ATrafficVehicle::TurnSirenOff(void)
  ATrafficVehicle::operator new(unsigned int)
  ATrafficVehicle virtual table
  ATrafficVehicle type_info node

Xbox methods treated as members (7 of 7; untyped ones count when ECX is read before it is written): ATrafficVehicle, GetName, IsTracked, Play, PlaySnowmobileSpecifics, scalar_deleting_destructor, ~ATrafficVehicle

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: ATrafficVehicle@12cb90, ~ATrafficVehicle@12d300]
  +0x010  w- LEA addr-taken [1: PlaySnowmobileSpecifics@12ceb0]
  +0x020  w- LEA addr-taken [1: PlaySnowmobileSpecifics@12ceb0]
  +0x07c  w[4] W float [2: Play@12d430, PlaySnowmobileSpecifics@12ceb0]
  +0x094  w[4] R -> AMix::GetVolume [3: ATrafficVehicle@12cb90, Play@12d430, PlaySnowmobileSpecifics@12ceb0]
  +0x0d0  w[4] R/W [3: ATrafficVehicle@12cb90, IsTracked@12ce10, Play@12d430]
  +0x0dc  w[4] R [1: Play@12d430]
  +0x0e0  w[4] R [1: Play@12d430]
  +0x0f4  w[4] W float [1: PlaySnowmobileSpecifics@12ceb0]
  +0x0f8  w[4] W float [1: PlaySnowmobileSpecifics@12ceb0]
  +0x0fc  w[4] W float [1: PlaySnowmobileSpecifics@12ceb0]
  +0x100  w[4] W float [1: PlaySnowmobileSpecifics@12ceb0]
  +0x118  w[4] W float [2: Play@12d430, PlaySnowmobileSpecifics@12ceb0]
  +0x13c  w[4] R/W float [1: Play@12d430]
  +0x158  w[1] R [1: Play@12d430]
  +0x1f0  w- LEA addr-taken [2: ATrafficVehicle@12cb90, GetName@12ce20]
  +0x200  w[4] R/W [3: ATrafficVehicle@12cb90, Play@12d430, ~ATrafficVehicle@12d300]
  +0x204  w[4] R/W [3: ATrafficVehicle@12cb90, Play@12d430, ~ATrafficVehicle@12d300]
  +0x208  w[4] R/W -> AVoice::Play [3: ATrafficVehicle@12cb90, Play@12d430, ~ATrafficVehicle@12d300]
  +0x20c  w[4] R/W [3: ATrafficVehicle@12cb90, Play@12d430, ~ATrafficVehicle@12d300]
  +0x210  w[4] W float [2: ATrafficVehicle@12cb90, Play@12d430]
  +0x214  w[4] W float [2: ATrafficVehicle@12cb90, Play@12d430]
  +0x218  w[4] W float [2: ATrafficVehicle@12cb90, Play@12d430]
  +0x21c  w[1] R [1: ~ATrafficVehicle@12d300]
  +0x21d  w[1] R [1: ~ATrafficVehicle@12d300]
  +0x21e  w[1] R/W [2: ATrafficVehicle@12cb90, Play@12d430]
  +0x220  w[2] R/W [2: ATrafficVehicle@12cb90, PlaySnowmobileSpecifics@12ceb0]
  +0x222  w[1] R/W [2: ATrafficVehicle@12cb90, PlaySnowmobileSpecifics@12ceb0]
  +0x223  w[1] R/W [2: ATrafficVehicle@12cb90, PlaySnowmobileSpecifics@12ceb0]
  +0x224  w[4] W float [2: ATrafficVehicle@12cb90, PlaySnowmobileSpecifics@12ceb0]
  +0x228  w[4] W float [2: ATrafficVehicle@12cb90, PlaySnowmobileSpecifics@12ceb0]
  +0x22c  w[4] W [1: ATrafficVehicle@12cb90]
  +0x230  w[4] W [1: ATrafficVehicle@12cb90]
  +0x234  w[4] W float [1: PlaySnowmobileSpecifics@12ceb0]
  +0x238  w[4] W float [1: PlaySnowmobileSpecifics@12ceb0]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[8] R [1: PlaySnowmobileSpecifics@2e5968]
  +0x008  w[4] R [1: PlaySnowmobileSpecifics@2e5968]
  +0x010  w- LEA addr-taken [1: PlaySnowmobileSpecifics@2e5968]
  +0x06c  w[4] R float [2: Play@2e5598, PlaySnowmobileSpecifics@2e5968]
  +0x084  w[4] R -> AMix::GetVolume [3: ATrafficVehicle@2e5340, Play@2e5598, PlaySnowmobileSpecifics@2e5968]
  +0x0ac  w[4] W [2: ATrafficVehicle@2e5340, ~ATrafficVehicle@2e6640]
  +0x0d0  w[4] R/W [3: ATrafficVehicle@2e5340, IsTracked@2e6a20, Play@2e5598]
  +0x0dc  w[4] R [1: Play@2e5598]
  +0x0e0  w[4] R [1: Play@2e5598]
  +0x0f4  w[4] LEA/R float addr-taken [1: PlaySnowmobileSpecifics@2e5968]
  +0x118  w[4] R float [2: Play@2e5598, PlaySnowmobileSpecifics@2e5968]
  +0x13c  w[4] R float [1: Play@2e5598]
  +0x164  w[4] R [1: Play@2e5598]
  +0x210  w- LEA addr-taken [2: ATrafficVehicle@2e5340, GetName@2e67e0]
  +0x220  w[4] R/W [3: ATrafficVehicle@2e5340, Play@2e5598, ~ATrafficVehicle@2e6640]
  +0x224  w[4] R/W [3: ATrafficVehicle@2e5340, Play@2e5598, ~ATrafficVehicle@2e6640]
  +0x228  w[4] R/W -> AVoice::Play [3: ATrafficVehicle@2e5340, Play@2e5598, ~ATrafficVehicle@2e6640]
  +0x22c  w[4] R/W [4: ATrafficVehicle@2e5340, ActivateSiren@2e58f8, Play@2e5598, ~ATrafficVehicle@2e6640]
  +0x230  w[4] R/W float [2: ATrafficVehicle@2e5340, Play@2e5598]
  +0x234  w[4] R/W float [2: ATrafficVehicle@2e5340, Play@2e5598]
  +0x238  w[4] R/W float [2: ATrafficVehicle@2e5340, Play@2e5598]
  +0x23c  w[4] R [1: ~ATrafficVehicle@2e6640]
  +0x240  w[4] R [1: ~ATrafficVehicle@2e6640]
  +0x244  w[4] R/W [2: ATrafficVehicle@2e5340, Play@2e5598]
  +0x248  w[2] R/W [2: ATrafficVehicle@2e5340, PlaySnowmobileSpecifics@2e5968]
  +0x24c  w[4] R/W [2: ATrafficVehicle@2e5340, PlaySnowmobileSpecifics@2e5968]
  +0x250  w[4] R/W [2: ATrafficVehicle@2e5340, PlaySnowmobileSpecifics@2e5968]
  +0x254  w[4] R/W float [2: ATrafficVehicle@2e5340, PlaySnowmobileSpecifics@2e5968]
  +0x258  w[4] R/W float [2: ATrafficVehicle@2e5340, PlaySnowmobileSpecifics@2e5968]
  +0x25c  w[4] W float [1: ATrafficVehicle@2e5340]
  +0x260  w[4] W float [1: ATrafficVehicle@2e5340]
  +0x264  w[4] R/W float [1: PlaySnowmobileSpecifics@2e5968]
  +0x268  w[4] R/W float [1: PlaySnowmobileSpecifics@2e5968]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  IsTracked: R +0xd0 w4
  GetName: LEA +0x1f0 w0
