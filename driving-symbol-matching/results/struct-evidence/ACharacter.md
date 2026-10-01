# ACharacter

FastAlloc/constructed sizes under its tag: {'allocated': [208], 'constructed': []}
deleting destructor 0x1c110 frees/deletes with size 0xd0 (call to ABaseSound::operator_delete)
Xbox vtable 0x0018a520 (4 slots) stored by its constructor
PS2 sheet virtual table row: ['ACharacter virtual table']
constructor 0x1c0c0 first calls: ['ABaseSound::ABaseSound']

Xbox methods (6):
  0x1c0c0 undefined ACharacter(undefined4 param_1)
  0x1c110 undefined scalar_deleting_destructor(undefined1 param_1)
  0x11dad0 undefined Say(undefined4 param_1)
  0x11dd00 undefined GetName(void)
  0x11dd30 undefined ~ACharacter(void)
  0x11ddb0 undefined Play(void)

PS2 methods (6):
  0x2fbe00 ACharacter::Say
  0x2fc130 ACharacter::Play
  0x2fc268 ACharacter::~ACharacter
  0x2fc318 ACharacter::GetName
  0x2fc468 ACharacter::ACharacter
  0x2fc5c0 ACharacter::IsTalking

Sheet rows:
  ACharacter::Say(char *)
  ACharacter::Play(APath &)
  ACharacter::~ACharacter(void)
  ACharacter::GetName(void) const
  ACharacter type_info function
  ACharacter::ACharacter(char *)
  ACharacter::operator new(unsigned int)
  ACharacter::IsTalking(void)
  ACharacter virtual table
  ACharacter type_info node

Xbox methods treated as members (6 of 6; untyped ones count when ECX is read before it is written): ACharacter, GetName, Play, Say, scalar_deleting_destructor, ~ACharacter

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: ACharacter@1c0c0, ~ACharacter@11dd30]
  +0x06c  w[4] W [2: ACharacter@1c0c0, Say@11dad0]
  +0x070  w[4] W [2: ACharacter@1c0c0, Say@11dad0]
  +0x074  w[4] W [2: ACharacter@1c0c0, Say@11dad0]
  +0x07c  w[4] W float [1: Play@11ddb0]
  +0x094  w[4] R -> AMix::GetVolume [2: Play@11ddb0, Say@11dad0]
  +0x0c0  w[4] R/W -> AVoice::Play, FUN_0003f400, FUN_000d36c0 [5: ACharacter@1c0c0, GetName@11dd00, Play@11ddb0, Say@11dad0, ~ACharacter@11dd30]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] W [1: ACharacter@2fc468]
  +0x004  w[4] W [1: ACharacter@2fc468]
  +0x008  w[4] W [1: ACharacter@2fc468]
  +0x010  w[4] W [1: ACharacter@2fc468]
  +0x014  w[4] W [1: ACharacter@2fc468]
  +0x018  w[4] W [1: ACharacter@2fc468]
  +0x020  w[4] W [1: ACharacter@2fc468]
  +0x024  w[4] W [1: ACharacter@2fc468]
  +0x028  w[4] W float [1: ACharacter@2fc468]
  +0x030  w[4] W [1: ACharacter@2fc468]
  +0x034  w[4] W [1: ACharacter@2fc468]
  +0x038  w[4] W float [1: ACharacter@2fc468]
  +0x040  w[4] W [1: ACharacter@2fc468]
  +0x044  w[4] W [1: ACharacter@2fc468]
  +0x048  w[4] W float [1: ACharacter@2fc468]
  +0x05c  w[4] W float [2: ACharacter@2fc468, Say@2fbe00]
  +0x060  w[4] W float [2: ACharacter@2fc468, Say@2fbe00]
  +0x064  w[4] W float [2: ACharacter@2fc468, Say@2fbe00]
  +0x068  w[4] W [1: ACharacter@2fc468]
  +0x06c  w[4] R/W float [2: ACharacter@2fc468, Play@2fc130]
  +0x070  w[4] W float [1: ACharacter@2fc468]
  +0x07c  w- LEA addr-taken [1: ACharacter@2fc468]
  +0x080  w[4] W [1: ACharacter@2fc468]
  +0x084  w[4] R/W -> AMix::GetVolume [3: ACharacter@2fc468, Play@2fc130, Say@2fbe00]
  +0x088  w[4] R/W [1: ACharacter@2fc468]
  +0x09c  w[4] W [1: ACharacter@2fc468]
  +0x0a0  w[4] W [1: ACharacter@2fc468]
  +0x0a4  w[4] W [1: ACharacter@2fc468]
  +0x0a8  w[4] W [1: ACharacter@2fc468]
  +0x0ac  w[4] W [2: ACharacter@2fc468, ~ACharacter@2fc268]
  +0x0b0  w[4] R/W [6: ACharacter@2fc468, GetName@2fc318, IsTalking@2fc5c0, Play@2fc130, Say@2fbe00, ~ACharacter@2fc268]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
