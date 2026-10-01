# AHelicopter

FastAlloc/constructed sizes under its tag: {'allocated': [784], 'constructed': []}
deleting destructor 0x12cb60 frees/deletes with size 0x310 (call to ABaseSound::operator_delete)
Xbox vtable 0x001a2e58 (9 slots) stored by its constructor
PS2 sheet virtual table row: ['AHelicopter virtual table']
constructor 0x12c7f0 first calls: ['AVehicle::AVehicle', '??_L@YGXPAXIHP6EX0@Z1@Z', '??_L@YGXPAXIHP6EX0@Z1@Z']

Xbox methods (5):
  0x12c7f0 undefined AHelicopter(undefined4 param_1)
  0x12c930 undefined PlayLanding(void)
  0x12c950 undefined Play(void)
  0x12cac0 undefined ~AHelicopter(void)
  0x12cb60 undefined scalar_deleting_destructor(undefined1 param_1)

PS2 methods (7):
  0x2f6718 AHelicopter::AHelicopter
  0x2f6900 AHelicopter::PlayLanding
  0x2f6938 AHelicopter::Play
  0x2f6b08 AHelicopter::~AHelicopter
  0x2f6e18 AHelicopter::operator_new
  0x2f6e38 AHelicopter::AddPitch
  0x2f6e40 AHelicopter::AHelicopter_global_ctors

Sheet rows:
  AHelicopter::AHelicopter(char *)
  AHelicopter::PlayLanding(float, COORD3 &, COORD3 &)
  AHelicopter::Play(APath &)
  AHelicopter::~AHelicopter(void)
  AHelicopter type_info function
  AHelicopter::operator new(unsigned int)
  AHelicopter::AddPitch(float)
  AHelicopter virtual table
  AHelicopter type_info node

Xbox methods treated as members (5 of 5; untyped ones count when ECX is read before it is written): AHelicopter, Play, PlayLanding, scalar_deleting_destructor, ~AHelicopter

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: AHelicopter@12c7f0, ~AHelicopter@12cac0]
  +0x06c  w[4] W [1: AHelicopter@12c7f0]
  +0x070  w[4] W [1: AHelicopter@12c7f0]
  +0x074  w[4] W [1: AHelicopter@12c7f0]
  +0x07c  w[4] W float [1: Play@12c950]
  +0x080  w[4] R/W float [1: Play@12c950]
  +0x094  w[4] R -> AMix::GetVolume [2: AHelicopter@12c7f0, Play@12c950]
  +0x0c1  w[1] R [1: Play@12c950]
  +0x1f0  w- LEA addr-taken -> AVoice::Play, AVoice::Set [2: AHelicopter@12c7f0, Play@12c950]
  +0x1f4  w- LEA addr-taken [1: ~AHelicopter@12cac0]
  +0x248  w- LEA addr-taken -> AVoice::Play [2: AHelicopter@12c7f0, Play@12c950]
  +0x24c  w- LEA addr-taken -> ??_M@YGXPAXIHP6EX0@Z@Z [1: ~AHelicopter@12cac0]
  +0x2a0  w[4] W [1: AHelicopter@12c7f0]
  +0x2a4  w- LEA addr-taken [2: AHelicopter@12c7f0, ~AHelicopter@12cac0]
  +0x2f8  w[4] W float [2: AHelicopter@12c7f0, Play@12c950]
  +0x2fc  w[4] W float [3: AHelicopter@12c7f0, Play@12c950, PlayLanding@12c930]
  +0x300  w[4] W float [2: AHelicopter@12c7f0, Play@12c950]

PS2 this-relative accesses (PS2 offsets):
  +0x05c  w[4] W float [1: AHelicopter@2f6718]
  +0x060  w[4] W float [1: AHelicopter@2f6718]
  +0x064  w[4] W float [1: AHelicopter@2f6718]
  +0x06c  w[4] R float [1: Play@2f6938]
  +0x070  w[4] R float [1: Play@2f6938]
  +0x084  w[4] R -> AMix::GetVolume [2: AHelicopter@2f6718, Play@2f6938]
  +0x0ac  w[4] W [2: AHelicopter@2f6718, ~AHelicopter@2f6b08]
  +0x0b4  w[4] R [1: Play@2f6938]
  +0x210  w[4] LEA/W addr-taken [3: AHelicopter@2f6718, Play@2f6938, ~AHelicopter@2f6b08]
  +0x214  w- LEA addr-taken [2: AHelicopter@2f6718, ~AHelicopter@2f6b08]
  +0x274  w[4] LEA/W addr-taken [3: AHelicopter@2f6718, Play@2f6938, ~AHelicopter@2f6b08]
  +0x278  w- LEA addr-taken [2: AHelicopter@2f6718, ~AHelicopter@2f6b08]
  +0x2d8  w[4] LEA/W addr-taken [2: AHelicopter@2f6718, ~AHelicopter@2f6b08]
  +0x2dc  w- LEA addr-taken [2: AHelicopter@2f6718, ~AHelicopter@2f6b08]
  +0x33c  w[4] LEA/R/W float addr-taken [3: AHelicopter@2f6718, Play@2f6938, ~AHelicopter@2f6b08]
  +0x340  w[4] R/W float [3: AHelicopter@2f6718, Play@2f6938, PlayLanding@2f6900]
  +0x344  w[4] R/W float [3: AHelicopter@2f6718, AddPitch@2f6e38, Play@2f6938]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  PlayLanding: W +0x2fc w4 float
