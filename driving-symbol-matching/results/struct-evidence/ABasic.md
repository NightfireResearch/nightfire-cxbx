# ABasic

FastAlloc/constructed sizes under its tag: {'allocated': [240], 'constructed': []}
deleting destructor 0x12fc00 frees/deletes with size 0xf0 (call to ABaseSound::operator_delete)
Xbox vtable 0x001a3060 (5 slots) stored by its constructor
PS2 sheet virtual table row: ['ABasic virtual table']
constructor 0x12fa40 first calls: ['ABaseSound::ABaseSound', 'sprintf']

Xbox methods (5):
  0x12fa40 undefined ABasic(undefined4 param_1, undefined4 param_2)
  0x12fa90 undefined Play(undefined4 param_1)
  0x12fb80 undefined ~ABasic(void)
  0x12fc00 undefined scalar_deleting_destructor(undefined1 param_1)
  0x12fc30 undefined Stop(void)

PS2 methods (5):
  0x2fc5d0 ABasic::ABasic
  0x2fc700 ABasic::Play
  0x2fc7c8 ABasic::Stop
  0x2fc848 ABasic::~ABasic
  0x2fca18 ABasic::operator_new

Sheet rows:
  ABasic::ABasic(char *, char *)
  ABasic::Play(APath &)
  ABasic::Stop(APath &)
  ABasic::~ABasic(void)
  ABasic type_info function
  ABasic::operator new(unsigned int)
  ABasic virtual table
  ABasic type_info node

Xbox methods treated as members (5 of 5; untyped ones count when ECX is read before it is written): ABasic, Play, Stop, scalar_deleting_destructor, ~ABasic

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: ABasic@12fa40, ~ABasic@12fb80]
  +0x07c  w[4] W float [1: Play@12fa90]
  +0x080  w[4] R [1: Play@12fa90]
  +0x094  w[4] R -> AMix::GetVolume [1: Play@12fa90]
  +0x0c0  w[4] R/W -> FUN_0003f400 [4: ABasic@12fa40, Play@12fa90, Stop@12fc30, ~ABasic@12fb80]
  +0x0c4  w- LEA addr-taken [2: ABasic@12fa40, Play@12fa90]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] W [1: ABasic@2fc5d0]
  +0x004  w[4] W [1: ABasic@2fc5d0]
  +0x008  w[4] W [1: ABasic@2fc5d0]
  +0x010  w[4] W [1: ABasic@2fc5d0]
  +0x014  w[4] W [1: ABasic@2fc5d0]
  +0x018  w[4] W [1: ABasic@2fc5d0]
  +0x020  w[4] W [1: ABasic@2fc5d0]
  +0x024  w[4] W [1: ABasic@2fc5d0]
  +0x028  w[4] W float [1: ABasic@2fc5d0]
  +0x030  w[4] W [1: ABasic@2fc5d0]
  +0x034  w[4] W [1: ABasic@2fc5d0]
  +0x038  w[4] W float [1: ABasic@2fc5d0]
  +0x040  w[4] W [1: ABasic@2fc5d0]
  +0x044  w[4] W [1: ABasic@2fc5d0]
  +0x048  w[4] W float [1: ABasic@2fc5d0]
  +0x05c  w[4] W float [1: ABasic@2fc5d0]
  +0x060  w[4] W float [1: ABasic@2fc5d0]
  +0x064  w[4] W float [1: ABasic@2fc5d0]
  +0x068  w[4] W [1: ABasic@2fc5d0]
  +0x06c  w[4] R/W float [2: ABasic@2fc5d0, Play@2fc700]
  +0x070  w[4] R/W float [2: ABasic@2fc5d0, Play@2fc700]
  +0x07c  w- LEA addr-taken [1: ABasic@2fc5d0]
  +0x080  w[4] W [1: ABasic@2fc5d0]
  +0x084  w[4] R/W -> AMix::GetVolume [2: ABasic@2fc5d0, Play@2fc700]
  +0x088  w[4] R/W [1: ABasic@2fc5d0]
  +0x09c  w[4] W [1: ABasic@2fc5d0]
  +0x0a0  w[4] W [1: ABasic@2fc5d0]
  +0x0a4  w[4] W [1: ABasic@2fc5d0]
  +0x0a8  w[4] W [1: ABasic@2fc5d0]
  +0x0ac  w[4] W [2: ABasic@2fc5d0, ~ABasic@2fc848]
  +0x0b0  w[4] R/W -> AVoice::Play [4: ABasic@2fc5d0, Play@2fc700, Stop@2fc7c8, ~ABasic@2fc848]
  +0x0b4  w- LEA addr-taken [2: ABasic@2fc5d0, Play@2fc700]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
