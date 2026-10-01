# ASubmersible

FastAlloc/constructed sizes under its tag: {'allocated': [576], 'constructed': []}
deleting destructor 0x12c4e0 frees/deletes with size 0x240 (call to ABaseSound::operator_delete)
Xbox vtable 0x001a2d44 (10 slots) stored by its constructor
PS2 sheet virtual table row: ['ASubmersible virtual table']
constructor 0x12b960 first calls: ['AVehicle::AVehicle', 'UMemory::FastAlloc', 'AIndex::Lookup']

Xbox methods (9):
  0x12b960 undefined ASubmersible(undefined4 param_1)
  0x12bb60 undefined SetCreakLevel(void)
  0x12bb80 undefined PlayCreaks(void)
  0x12bc60 undefined PlayFanMotors(undefined4 param_1)
  0x12be30 undefined ~ASubmersible(void)
  0x12bf00 undefined PlayCavitation(undefined4 param_1)
  0x12c260 undefined PlayServos(void)
  0x12c4e0 undefined scalar_deleting_destructor(undefined1 param_1)
  0x12c510 undefined Play(void)

PS2 methods (11):
  0x2e7020 ASubmersible::ASubmersible
  0x2e7210 ASubmersible::~ASubmersible
  0x2e7358 ASubmersible::Play
  0x2e73b8 ASubmersible::PlayCreaks
  0x2e74d8 ASubmersible::PlayFanMotors
  0x2e76f8 ASubmersible::PlayCavitation
  0x2e7d60 ASubmersible::PlayServos
  0x2e8450 ASubmersible::GetName
  0x2e8650 ASubmersible::SetCreakLevel
  0x2e8660 ASubmersible::operator_new
  0x2e8680 ASubmersible::ASubmersible_global_ctors

Sheet rows:
  ASubmersible::ASubmersible(char *)
  ASubmersible::~ASubmersible(void)
  ASubmersible::Play(APath &)
  ASubmersible::PlayCreaks(APath &)
  ASubmersible::PlayFanMotors(APath &)
  ASubmersible::PlayCavitation(APath &)
  ASubmersible::PlayServos(APath &)
  ASubmersible::GetName(void) const
  ASubmersible type_info function
  ASubmersible::SetCreakLevel(short, int)
  ASubmersible::operator new(unsigned int)
  ASubmersible virtual table
  ASubmersible type_info node

Xbox methods treated as members (9 of 9; untyped ones count when ECX is read before it is written): ASubmersible, Play, PlayCavitation, PlayCreaks, PlayFanMotors, PlayServos, SetCreakLevel, scalar_deleting_destructor, ~ASubmersible

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: ASubmersible@12b960, ~ASubmersible@12be30]
  +0x010  w- LEA addr-taken [2: PlayCavitation@12bf00, PlayServos@12c260]
  +0x070  w[4] W [1: ASubmersible@12b960]
  +0x074  w[4] W [1: ASubmersible@12b960]
  +0x07c  w[4] W float [3: PlayCavitation@12bf00, PlayFanMotors@12bc60, PlayServos@12c260]
  +0x094  w[4] R -> AMix::GetVolume [3: PlayCavitation@12bf00, PlayFanMotors@12bc60, PlayServos@12c260]
  +0x0b8  w[1] R [1: PlayFanMotors@12bc60]
  +0x0d0  w[4] W [1: ASubmersible@12b960]
  +0x11c  w[4] W float [2: PlayCavitation@12bf00, PlayFanMotors@12bc60]
  +0x120  w[4] W float [2: PlayCavitation@12bf00, PlayFanMotors@12bc60]
  +0x13c  w[4] R/W float [2: PlayCavitation@12bf00, PlayFanMotors@12bc60]
  +0x140  w[4] W float [1: PlayCavitation@12bf00]
  +0x17a  w[1] R [2: PlayCavitation@12bf00, PlayFanMotors@12bc60]
  +0x17b  w[1] R [2: PlayCavitation@12bf00, PlayFanMotors@12bc60]
  +0x17c  w[1] R/W [1: PlayServos@12c260]
  +0x17d  w[1] R/W [1: PlayServos@12c260]
  +0x180  w[4] W float [1: PlayCavitation@12bf00]
  +0x184  w[4] W float [1: PlayCavitation@12bf00]
  +0x1f0  w- LEA addr-taken [1: ASubmersible@12b960]
  +0x200  w[4] R/W -> AVoice::Play [3: ASubmersible@12b960, PlayCavitation@12bf00, ~ASubmersible@12be30]
  +0x204  w[4] R/W -> AVoice::Play [3: ASubmersible@12b960, PlayCavitation@12bf00, ~ASubmersible@12be30]
  +0x208  w[4] R/W -> AVoice::Play [3: ASubmersible@12b960, PlayFanMotors@12bc60, ~ASubmersible@12be30]
  +0x20c  w[4] W [2: ASubmersible@12b960, PlayCavitation@12bf00]
  +0x210  w[4] W [2: ASubmersible@12b960, PlayServos@12c260]
  +0x214  w[2] R/RW/W [2: ASubmersible@12b960, PlayServos@12c260]
  +0x216  w[2] R/RW/W [2: ASubmersible@12b960, PlayServos@12c260]
  +0x218  w[2] R/RW/W [2: ASubmersible@12b960, PlayCavitation@12bf00]
  +0x21a  w[2] W [1: ASubmersible@12b960]
  +0x21c  w[1] W [1: ASubmersible@12b960]
  +0x21d  w[1] R/W [2: ASubmersible@12b960, PlayFanMotors@12bc60]
  +0x21e  w[1] W [1: ASubmersible@12b960]
  +0x21f  w[1] R/W [2: ASubmersible@12b960, PlayCavitation@12bf00]
  +0x220  w[1] R/W [2: ASubmersible@12b960, PlayCavitation@12bf00]
  +0x224  w[4] W float [2: ASubmersible@12b960, PlayCavitation@12bf00]
  +0x228  w[4] W float [2: ASubmersible@12b960, PlayCavitation@12bf00]
  +0x22c  w[4] W float [2: ASubmersible@12b960, PlayCavitation@12bf00]
  +0x230  w[2] R/W [3: ASubmersible@12b960, PlayCreaks@12bb80, SetCreakLevel@12bb60]
  +0x234  w[4] W [2: ASubmersible@12b960, SetCreakLevel@12bb60]
  +0x238  w[4] R/W [2: ASubmersible@12b960, PlayCreaks@12bb80]
  +0x23c  w[4] R/W [2: ASubmersible@12b960, PlayCreaks@12bb80]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[8] R [2: PlayCavitation@2e76f8, PlayServos@2e7d60]
  +0x008  w[4] R [2: PlayCavitation@2e76f8, PlayServos@2e7d60]
  +0x060  w[4] W [1: ASubmersible@2e7020]
  +0x064  w[4] W [1: ASubmersible@2e7020]
  +0x06c  w[4] R float [3: PlayCavitation@2e76f8, PlayFanMotors@2e74d8, PlayServos@2e7d60]
  +0x084  w[4] R -> AMix::GetVolume [3: PlayCavitation@2e76f8, PlayFanMotors@2e74d8, PlayServos@2e7d60]
  +0x0a8  w[4] R [1: PlayFanMotors@2e74d8]
  +0x0ac  w[4] W [2: ASubmersible@2e7020, ~ASubmersible@2e7210]
  +0x0d0  w[4] W [1: ASubmersible@2e7020]
  +0x11c  w[4] R float [2: PlayCavitation@2e76f8, PlayFanMotors@2e74d8]
  +0x120  w[4] R float [2: PlayCavitation@2e76f8, PlayFanMotors@2e74d8]
  +0x13c  w[4] R float [2: PlayCavitation@2e76f8, PlayFanMotors@2e74d8]
  +0x140  w[4] R float [1: PlayCavitation@2e76f8]
  +0x18c  w[4] R [2: PlayCavitation@2e76f8, PlayFanMotors@2e74d8]
  +0x190  w[4] R [2: PlayCavitation@2e76f8, PlayFanMotors@2e74d8]
  +0x194  w[4] R/W [1: PlayServos@2e7d60]
  +0x198  w[4] R/W [1: PlayServos@2e7d60]
  +0x19c  w[4] R float [1: PlayCavitation@2e76f8]
  +0x1a0  w[4] R float [1: PlayCavitation@2e76f8]
  +0x210  w- LEA addr-taken [2: ASubmersible@2e7020, GetName@2e8450]
  +0x220  w[4] R/W -> AVoice::Play [3: ASubmersible@2e7020, PlayCavitation@2e76f8, ~ASubmersible@2e7210]
  +0x224  w[4] R/W -> AVoice::Play [3: ASubmersible@2e7020, PlayCavitation@2e76f8, ~ASubmersible@2e7210]
  +0x228  w[4] R/W -> AVoice::Play [3: ASubmersible@2e7020, PlayFanMotors@2e74d8, ~ASubmersible@2e7210]
  +0x22c  w[4] W [2: ASubmersible@2e7020, PlayCavitation@2e76f8]
  +0x230  w[4] W [2: ASubmersible@2e7020, PlayServos@2e7d60]
  +0x234  w[2] R/W [2: ASubmersible@2e7020, PlayServos@2e7d60]
  +0x236  w[2] R/W [2: ASubmersible@2e7020, PlayServos@2e7d60]
  +0x238  w[2] R/W [2: ASubmersible@2e7020, PlayCavitation@2e76f8]
  +0x23a  w[2] W [1: ASubmersible@2e7020]
  +0x23c  w[4] W [1: ASubmersible@2e7020]
  +0x240  w[4] R/W [2: ASubmersible@2e7020, PlayFanMotors@2e74d8]
  +0x244  w[4] W [1: ASubmersible@2e7020]
  +0x248  w[4] R/W [2: ASubmersible@2e7020, PlayCavitation@2e76f8]
  +0x24c  w[4] R/W [2: ASubmersible@2e7020, PlayCavitation@2e76f8]
  +0x250  w[4] R/W float [2: ASubmersible@2e7020, PlayCavitation@2e76f8]
  +0x254  w[4] R/W float [2: ASubmersible@2e7020, PlayCavitation@2e76f8]
  +0x258  w[4] R/W float [2: ASubmersible@2e7020, PlayCavitation@2e76f8]
  +0x25c  w[2] R/W [3: ASubmersible@2e7020, PlayCreaks@2e73b8, SetCreakLevel@2e8650]
  +0x260  w[4] R/W [3: ASubmersible@2e7020, PlayCreaks@2e73b8, SetCreakLevel@2e8650]
  +0x264  w[4] R/W [2: ASubmersible@2e7020, PlayCreaks@2e73b8]
  +0x268  w[4] R/W [2: ASubmersible@2e7020, PlayCreaks@2e73b8]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
