# AICharacterEnemyDriver

FastAlloc/constructed sizes under its tag: None
deleting destructor 0x1d650 frees/deletes with size 0x150 (call to UMemory::FastFree)
Xbox vtable 0x0018a788 (33 slots) stored by its constructor
PS2 sheet virtual table row: ['AICharacterEnemyDriver virtual table']
constructor 0x1d5f0 first calls: ['AICharacter::AICharacter']

Xbox methods (9):
  0x1d530 undefined GetRotPos(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0x1d5f0 undefined AICharacterEnemyDriver(undefined4 param_1, undefined4 param_2)
  0x1d640 undefined ~AICharacterEnemyDriver(void)
  0x1d650 undefined scalar_deleting_destructor(undefined1 param_1)
  0x1d680 undefined DoInitial(void)
  0x1d7f0 undefined DoNeutral(void)
  0x1d840 undefined HandleInterrupts(void)
  0x1d990 undefined UpdateRotPos(void)
  0x20e30 undefined GetRotPos(undefined4 param_1, undefined4 param_2)

PS2 methods (10):
  0x11aa30 AICharacterEnemyDriver::GetRotPos
  0x11ab50 AICharacterEnemyDriver::GetRotPos
  0x11aca8 AICharacterEnemyDriver::AICharacterEnemyDriver
  0x11ad88 AICharacterEnemyDriver::~AICharacterEnemyDriver
  0x11ade0 AICharacterEnemyDriver::DoInitial
  0x11af60 AICharacterEnemyDriver::DoNeutral
  0x11afc0 AICharacterEnemyDriver::DoIdling
  0x11aff8 AICharacterEnemyDriver::HandleInterrupts
  0x11b128 AICharacterEnemyDriver::UpdateRotPos
  0x11b2c0 AICharacterEnemyDriver::GetRotPos_global_ctors

Sheet rows:
  AICharacterEnemyDriver::GetRotPos(int, MATRIX4 &)
  AICharacterEnemyDriver::GetRotPos(int, MATRIX4 &, COORD3 &)
  AICharacterEnemyDriver::AICharacterEnemyDriver(MATRIX4 &, PVehi
  AICharacterEnemyDriver::~AICharacterEnemyDriver(void)
  AICharacterEnemyDriver::DoInitial(void)
  AICharacterEnemyDriver::DoNeutral(void)
  AICharacterEnemyDriver::DoIdling(void)
  AICharacterEnemyDriver::HandleInterrupts(void)
  AICharacterEnemyDriver::UpdateRotPos(void)
  AICharacterEnemyDriver type_info function
  AICharacterEnemyDriver virtual table
  AICharacterEnemyDriver type_info node

Xbox methods treated as members (7 of 9; untyped ones count when ECX is read before it is written): AICharacterEnemyDriver, DoInitial, DoNeutral, HandleInterrupts, UpdateRotPos, scalar_deleting_destructor, ~AICharacterEnemyDriver

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: AICharacterEnemyDriver@1d5f0, ~AICharacterEnemyDriver@1d640]
  +0x010  w- LEA addr-taken [2: AICharacterEnemyDriver@1d5f0, UpdateRotPos@1d990]
  +0x05c  w[4] W [2: DoInitial@1d680, DoNeutral@1d7f0]
  +0x064  w[4] W [1: DoNeutral@1d7f0]
  +0x088  w[4] R/W [2: AICharacterEnemyDriver@1d5f0, DoNeutral@1d7f0]
  +0x0a8  w[4] R [4: DoInitial@1d680, DoNeutral@1d7f0, HandleInterrupts@1d840, UpdateRotPos@1d990]
  +0x140  w[4] R/W [3: AICharacterEnemyDriver@1d5f0, DoInitial@1d680, HandleInterrupts@1d840]
  +0x144  w[4] R/W [3: AICharacterEnemyDriver@1d5f0, DoInitial@1d680, HandleInterrupts@1d840]
  +0x148  w[4] R/W [3: AICharacterEnemyDriver@1d5f0, DoInitial@1d680, HandleInterrupts@1d840]
  +0x14c  w[4] R/W [2: AICharacterEnemyDriver@1d5f0, DoNeutral@1d7f0]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[8] W [1: AICharacterEnemyDriver@11aca8]
  +0x008  w[8] W [1: AICharacterEnemyDriver@11aca8]
  +0x010  w[8] W [1: AICharacterEnemyDriver@11aca8]
  +0x018  w[8] W [1: AICharacterEnemyDriver@11aca8]
  +0x020  w[8] W [1: AICharacterEnemyDriver@11aca8]
  +0x028  w[8] W [1: AICharacterEnemyDriver@11aca8]
  +0x030  w[8] W [1: AICharacterEnemyDriver@11aca8]
  +0x038  w[8] W [1: AICharacterEnemyDriver@11aca8]
  +0x04c  w[4] W [3: DoIdling@11afc0, DoInitial@11ade0, DoNeutral@11af60]
  +0x054  w[4] W [1: DoNeutral@11af60]
  +0x078  w[4] R/W [2: AICharacterEnemyDriver@11aca8, DoNeutral@11af60]
  +0x098  w[4] R [4: DoIdling@11afc0, DoInitial@11ade0, DoNeutral@11af60, HandleInterrupts@11aff8]
  +0x140  w[4] W [2: AICharacterEnemyDriver@11aca8, ~AICharacterEnemyDriver@11ad88]
  +0x150  w[4] R/W [5: AICharacterEnemyDriver@11aca8, DoInitial@11ade0, GetRotPos@11aa30, GetRotPos@11ab50, HandleInterrupts@11aff8]
  +0x154  w[4] R/W [3: AICharacterEnemyDriver@11aca8, DoInitial@11ade0, HandleInterrupts@11aff8]
  +0x158  w[4] R/W [3: AICharacterEnemyDriver@11aca8, DoInitial@11ade0, HandleInterrupts@11aff8]
  +0x15c  w[4] R/W [2: AICharacterEnemyDriver@11aca8, DoNeutral@11af60]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  ~AICharacterEnemyDriver: W +0x0 w4
