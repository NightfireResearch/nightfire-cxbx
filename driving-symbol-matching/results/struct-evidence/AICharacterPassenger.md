# AICharacterPassenger

FastAlloc/constructed sizes under its tag: {'allocated': [], 'constructed': [336]}
deleting destructor 0x236d0 frees/deletes with size 0x150 (call to UMemory::FastFree)
Xbox vtable 0x0018ae68 (33 slots) stored by its constructor
PS2 sheet virtual table row: ['AICharacterPassenger virtual table']
constructor 0x23670 first calls: ['AICharacter::AICharacter']

Xbox methods (6):
  0x23670 undefined AICharacterPassenger(undefined4 param_1, undefined4 param_2)
  0x236c0 undefined ~AICharacterPassenger(void)
  0x236d0 undefined scalar_deleting_destructor(undefined1 param_1)
  0x23700 undefined __stdcall Init(void)
  0x23890 undefined DoNeutral(void)
  0x238c0 undefined HandleInterrupts(void)

PS2 methods (15):
  0x1251b0 AICharacterPassenger::GetRotPos
  0x1252d8 AICharacterPassenger::GetRotPos
  0x125438 AICharacterPassenger::Init
  0x125640 AICharacterPassenger::Shutdown
  0x125648 AICharacterPassenger::AICharacterPassenger
  0x125738 AICharacterPassenger::~AICharacterPassenger
  0x125790 AICharacterPassenger::DoInitial
  0x1257c8 AICharacterPassenger::DoNeutral
  0x125810 AICharacterPassenger::DoIdling
  0x125848 AICharacterPassenger::DoTilting
  0x125850 AICharacterPassenger::DoRunning
  0x125858 AICharacterPassenger::DoLeaning
  0x125860 AICharacterPassenger::HandleInterrupts
  0x125920 AICharacterPassenger::UpdateRotPos
  0x125ab8 AICharacterPassenger::GetRotPos_global_ctors

Sheet rows:
  AICharacterPassenger::GetRotPos(int, MATRIX4 &)
  AICharacterPassenger::GetRotPos(int, MATRIX4 &, COORD3 &)
  AICharacterPassenger::Init(void)
  AICharacterPassenger::Shutdown(void)
  AICharacterPassenger::AICharacterPassenger(MATRIX4 &, int)
  AICharacterPassenger::~AICharacterPassenger(void)
  AICharacterPassenger::DoInitial(void)
  AICharacterPassenger::DoNeutral(void)
  AICharacterPassenger::DoIdling(void)
  AICharacterPassenger::DoTilting(void)
  AICharacterPassenger::DoRunning(void)
  AICharacterPassenger::DoLeaning(void)
  AICharacterPassenger::HandleInterrupts(void)
  AICharacterPassenger::UpdateRotPos(void)
  AICharacterPassenger type_info function
  AICharacterPassenger virtual table
  AICharacterPassenger type_info node

Xbox methods treated as members (5 of 6; untyped ones count when ECX is read before it is written): AICharacterPassenger, DoNeutral, HandleInterrupts, scalar_deleting_destructor, ~AICharacterPassenger

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: AICharacterPassenger@23670, ~AICharacterPassenger@236c0]
  +0x010  w- LEA addr-taken [1: AICharacterPassenger@23670]
  +0x05c  w[4] W [1: DoNeutral@23890]
  +0x064  w[4] W [1: DoNeutral@23890]
  +0x088  w[4] R/W [2: AICharacterPassenger@23670, DoNeutral@23890]
  +0x0a8  w[4] R [2: DoNeutral@23890, HandleInterrupts@238c0]
  +0x140  w[4] W [1: AICharacterPassenger@23670]
  +0x144  w[4] R/W [2: AICharacterPassenger@23670, DoNeutral@23890]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[8] W [1: AICharacterPassenger@125648]
  +0x008  w[8] W [1: AICharacterPassenger@125648]
  +0x010  w[8] W [1: AICharacterPassenger@125648]
  +0x018  w[8] W [1: AICharacterPassenger@125648]
  +0x020  w[8] W [1: AICharacterPassenger@125648]
  +0x028  w[8] W [1: AICharacterPassenger@125648]
  +0x030  w[8] W [1: AICharacterPassenger@125648]
  +0x038  w[8] W [1: AICharacterPassenger@125648]
  +0x04c  w[4] W [3: DoIdling@125810, DoInitial@125790, DoNeutral@1257c8]
  +0x054  w[4] W [1: DoNeutral@1257c8]
  +0x078  w[4] R/W [2: AICharacterPassenger@125648, DoNeutral@1257c8]
  +0x098  w[4] R [4: DoIdling@125810, DoInitial@125790, DoNeutral@1257c8, HandleInterrupts@125860]
  +0x140  w[4] W [2: AICharacterPassenger@125648, ~AICharacterPassenger@125738]
  +0x150  w[4] W [1: AICharacterPassenger@125648]
  +0x154  w[4] R/W [2: AICharacterPassenger@125648, DoNeutral@1257c8]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  ~AICharacterPassenger: W +0x0 w4
