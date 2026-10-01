# AICharacterBond

FastAlloc/constructed sizes under its tag: {'allocated': [], 'constructed': [336]}
deleting destructor 0x1cc00 frees/deletes with size 0x150 (call to UMemory::FastFree)
Xbox vtable 0x0018a688 (33 slots) stored by its constructor
PS2 sheet virtual table row: ['AICharacterBond virtual table']
constructor 0x1cbb0 first calls: ['AICharacter::AICharacter']

Xbox methods (11):
  0x1cbb0 undefined AICharacterBond(undefined4 param_1, undefined4 param_2)
  0x1cbf0 undefined ~AICharacterBond(void)
  0x1cc00 undefined scalar_deleting_destructor(undefined1 param_1)
  0x1cc30 undefined GetRotPos(undefined param_1, undefined4 param_2)
  0x1cd80 undefined Init(void)
  0x1d090 undefined DoNeutral(void)
  0x1d0c0 undefined DoInitial(void)
  0x1d0e0 undefined DoArming(void)
  0x1d1c0 undefined DoFiring(void)
  0x1d2a0 undefined HandleInterrupts(void)
  0x23950 undefined UpdateRotPos(void)

PS2 methods (16):
  0x119b98 AICharacterBond::GetRotPos
  0x119cc0 AICharacterBond::Init
  0x119e20 AICharacterBond::Init
  0x11a1a8 AICharacterBond::Shutdown
  0x11a1b0 AICharacterBond::AICharacterBond
  0x11a288 AICharacterBond::~AICharacterBond
  0x11a2e0 AICharacterBond::DoInitial
  0x11a318 AICharacterBond::DoNeutral
  0x11a360 AICharacterBond::DoIdling
  0x11a398 AICharacterBond::DoArming
  0x11a4a0 AICharacterBond::DoFiring
  0x11a5b8 AICharacterBond::HandleInterrupts
  0x11a858 AICharacterBond::UpdateRotPos
  0x11a9f0 AICharacterBond::SetLeftButton
  0x11aa00 AICharacterBond::SetRightButton
  0x11aa10 AICharacterBond::GetRotPos_global_ctors

Sheet rows:
  AICharacterBond::GetRotPos(int, MATRIX4 &)
  AICharacterBond::GetRotPos(int, MATRIX4 &, COORD3 &)
  AICharacterBond::Init(void)
  AICharacterBond::Shutdown(void)
  AICharacterBond::AICharacterBond(MATRIX4 &, int)
  AICharacterBond::~AICharacterBond(void)
  AICharacterBond::DoInitial(void)
  AICharacterBond::DoNeutral(void)
  AICharacterBond::DoIdling(void)
  AICharacterBond::DoArming(void)
  AICharacterBond::DoFiring(void)
  AICharacterBond::HandleInterrupts(void)
  AICharacterBond::UpdateRotPos(void)
  AICharacterBond type_info function
  AICharacterBond::SetLeftButton(bool)
  AICharacterBond::SetRightButton(bool)
  AICharacterBond::fgLeftButton
  AICharacterBond::fgRightButton
  AICharacterBond virtual table
  AICharacterBond type_info node

Xbox methods treated as members (9 of 11; untyped ones count when ECX is read before it is written): AICharacterBond, DoArming, DoFiring, DoInitial, DoNeutral, HandleInterrupts, UpdateRotPos, scalar_deleting_destructor, ~AICharacterBond

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: AICharacterBond@1cbb0, ~AICharacterBond@1cbf0]
  +0x010  w- LEA addr-taken [2: AICharacterBond@1cbb0, UpdateRotPos@23950]
  +0x05c  w[4] W [4: DoArming@1d0e0, DoFiring@1d1c0, DoInitial@1d0c0, DoNeutral@1d090]
  +0x060  w[4] R/W [2: DoArming@1d0e0, DoFiring@1d1c0]
  +0x064  w[4] W [1: DoNeutral@1d090]
  +0x088  w[4] R/W [2: AICharacterBond@1cbb0, DoNeutral@1d090]
  +0x0a8  w[4] R [6: DoArming@1d0e0, DoFiring@1d1c0, DoInitial@1d0c0, DoNeutral@1d090, HandleInterrupts@1d2a0, UpdateRotPos@23950]
  +0x140  w[4] R/W [2: AICharacterBond@1cbb0, DoNeutral@1d090]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[8] W [1: AICharacterBond@11a1b0]
  +0x008  w[8] W [1: AICharacterBond@11a1b0]
  +0x010  w[8] W [1: AICharacterBond@11a1b0]
  +0x018  w[8] W [1: AICharacterBond@11a1b0]
  +0x020  w[8] W [1: AICharacterBond@11a1b0]
  +0x028  w[8] W [1: AICharacterBond@11a1b0]
  +0x030  w[8] W [1: AICharacterBond@11a1b0]
  +0x038  w[8] W [1: AICharacterBond@11a1b0]
  +0x04c  w[4] R/W [6: DoArming@11a398, DoFiring@11a4a0, DoIdling@11a360, DoInitial@11a2e0, DoNeutral@11a318, HandleInterrupts@11a5b8]
  +0x050  w[4] R/W [3: DoArming@11a398, DoFiring@11a4a0, HandleInterrupts@11a5b8]
  +0x054  w[4] W [4: DoArming@11a398, DoFiring@11a4a0, DoNeutral@11a318, HandleInterrupts@11a5b8]
  +0x078  w[4] R/W [5: AICharacterBond@11a1b0, DoArming@11a398, DoFiring@11a4a0, DoNeutral@11a318, HandleInterrupts@11a5b8]
  +0x098  w[4] R [6: DoArming@11a398, DoFiring@11a4a0, DoIdling@11a360, DoInitial@11a2e0, DoNeutral@11a318, HandleInterrupts@11a5b8]
  +0x140  w[4] W [2: AICharacterBond@11a1b0, ~AICharacterBond@11a288]
  +0x150  w[4] R/W [3: AICharacterBond@11a1b0, DoNeutral@11a318, HandleInterrupts@11a5b8]
  +0x154  w[4] R [1: HandleInterrupts@11a5b8]
  +0x158  w[4] R [1: HandleInterrupts@11a5b8]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  ~AICharacterBond: W +0x0 w4
