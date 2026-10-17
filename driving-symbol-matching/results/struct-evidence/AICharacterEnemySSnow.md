# AICharacterEnemySSnow

FastAlloc/constructed sizes under its tag: None
deleting destructor 0x209d0 frees/deletes with size 0x150 (call to UMemory::FastFree)
Xbox vtable 0x0018aa18 (36 slots) stored by its constructor
PS2 sheet virtual table row: ['AICharacterEnemySSnow virtual table']

Xbox methods (9):
  0x1fd30 undefined GetVehiclePtr(void)
  0x20970 undefined ~AICharacterEnemySSnow(void)
  0x209d0 undefined scalar_deleting_destructor(undefined1 param_1)
  0x20a00 undefined DoNeutral(void)
  0x20a60 undefined DoAiming(void)
  0x20b40 undefined DoDying(void)
  0x20bb0 undefined HandleInterrupts(void)
  0x20c30 undefined TargetIsInRange(void)
  0x20c70 undefined PlayZoneInjuryAnim(undefined param_1, undefined4 param_2)

PS2 methods (17):
  0x11fca0 AICharacterEnemySSnow::GetRotPos
  0x11fdc0 AICharacterEnemySSnow::GetRotPos
  0x11ff18 AICharacterEnemySSnow::AICharacterEnemySSnow
  0x120010 AICharacterEnemySSnow::~AICharacterEnemySSnow
  0x120068 AICharacterEnemySSnow::DoInitial
  0x1200a0 AICharacterEnemySSnow::DoNeutral
  0x120128 AICharacterEnemySSnow::DoIdling
  0x120160 AICharacterEnemySSnow::DoAiming
  0x1202a8 AICharacterEnemySSnow::DoHurting
  0x1202e0 AICharacterEnemySSnow::DoDying
  0x120368 AICharacterEnemySSnow::DoDead
  0x120370 AICharacterEnemySSnow::HandleInterrupts
  0x120428 AICharacterEnemySSnow::UpdateRotPos
  0x120470 AICharacterEnemySSnow::GetVehiclePtr
  0x120478 AICharacterEnemySSnow::TargetIsInRange
  0x120500 AICharacterEnemySSnow::PlayZoneInjuryAnim
  0x1208d8 AICharacterEnemySSnow::GetRotPos_global_ctors

Sheet rows:
  AICharacterEnemySSnow::GetRotPos(int, MATRIX4 &)
  AICharacterEnemySSnow::GetRotPos(int, MATRIX4 &, COORD3 &)
  AICharacterEnemySSnow::AICharacterEnemySSnow(MATRIX4 &, int, PV
  AICharacterEnemySSnow::~AICharacterEnemySSnow(void)
  AICharacterEnemySSnow::DoInitial(void)
  AICharacterEnemySSnow::DoNeutral(void)
  AICharacterEnemySSnow::DoIdling(void)
  AICharacterEnemySSnow::DoAiming(void)
  AICharacterEnemySSnow::DoHurting(void)
  AICharacterEnemySSnow::DoDying(void)
  AICharacterEnemySSnow::DoDead(void)
  AICharacterEnemySSnow::HandleInterrupts(void)
  AICharacterEnemySSnow::UpdateRotPos(void)
  AICharacterEnemySSnow::GetVehiclePtr(void)
  AICharacterEnemySSnow::TargetIsInRange(void)
  AICharacterEnemySSnow::PlayZoneInjuryAnim(int, float, int)
  AICharacterEnemySSnow type_info function
  AICharacterEnemySSnow virtual table
  AICharacterEnemySSnow type_info node

Xbox methods treated as members (9 of 9; untyped ones count when ECX is read before it is written): DoAiming, DoDying, DoNeutral, GetVehiclePtr, HandleInterrupts, PlayZoneInjuryAnim, TargetIsInRange, scalar_deleting_destructor, ~AICharacterEnemySSnow

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W [2: HandleInterrupts@20bb0, ~AICharacterEnemySSnow@20970]
  +0x030  w[4] R [1: DoAiming@20a60]
  +0x038  w[4] R [1: DoAiming@20a60]
  +0x040  w- LEA addr-taken [1: TargetIsInRange@20c30]
  +0x05c  w[4] R/W [4: DoDying@20b40, DoNeutral@20a00, HandleInterrupts@20bb0, PlayZoneInjuryAnim@20c70]
  +0x064  w[4] W [3: DoNeutral@20a00, HandleInterrupts@20bb0, PlayZoneInjuryAnim@20c70]
  +0x068  w[1, 4] R [3: DoNeutral@20a00, HandleInterrupts@20bb0, PlayZoneInjuryAnim@20c70]
  +0x074  w[4] W float [1: TargetIsInRange@20c30]
  +0x078  w[4] R [1: PlayZoneInjuryAnim@20c70]
  +0x088  w[4] R [3: DoNeutral@20a00, HandleInterrupts@20bb0, PlayZoneInjuryAnim@20c70]
  +0x0a8  w[4] R [5: DoAiming@20a60, DoDying@20b40, DoNeutral@20a00, HandleInterrupts@20bb0, PlayZoneInjuryAnim@20c70]
  +0x0b0  w[4] R/W [3: DoNeutral@20a00, HandleInterrupts@20bb0, PlayZoneInjuryAnim@20c70]
  +0x0bd  w[1] R/W [1: DoDying@20b40]
  +0x140  w[4] R -> PhysicsObject::GetHitPoints [5: DoAiming@20a60, DoNeutral@20a00, GetVehiclePtr@1fd30, HandleInterrupts@20bb0, PlayZoneInjuryAnim@20c70]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[8] W [1: AICharacterEnemySSnow@11ff18]
  +0x008  w[8] W [1: AICharacterEnemySSnow@11ff18]
  +0x010  w[8] W [1: AICharacterEnemySSnow@11ff18]
  +0x018  w[8] W [1: AICharacterEnemySSnow@11ff18]
  +0x020  w[4, 8] R/W float [2: AICharacterEnemySSnow@11ff18, DoAiming@120160]
  +0x028  w[4, 8] R/W float [2: AICharacterEnemySSnow@11ff18, DoAiming@120160]
  +0x030  w[8] W [1: AICharacterEnemySSnow@11ff18]
  +0x038  w[8] W [1: AICharacterEnemySSnow@11ff18]
  +0x04c  w[4] R/W [7: DoDying@1202e0, DoHurting@1202a8, DoIdling@120128, DoInitial@120068, DoNeutral@1200a0, HandleInterrupts@120370…]
  +0x054  w[4] W [3: DoNeutral@1200a0, HandleInterrupts@120370, PlayZoneInjuryAnim@120500]
  +0x058  w[4] R/W [4: AICharacterEnemySSnow@11ff18, DoNeutral@1200a0, HandleInterrupts@120370, PlayZoneInjuryAnim@120500]
  +0x064  w[4] R float [1: TargetIsInRange@120478]
  +0x068  w[4] R [1: PlayZoneInjuryAnim@120500]
  +0x078  w[4] R/W [4: AICharacterEnemySSnow@11ff18, DoNeutral@1200a0, HandleInterrupts@120370, PlayZoneInjuryAnim@120500]
  +0x098  w[4] R [8: DoAiming@120160, DoDying@1202e0, DoHurting@1202a8, DoIdling@120128, DoInitial@120068, DoNeutral@1200a0…]
  +0x0a0  w[4] R/W [3: DoNeutral@1200a0, HandleInterrupts@120370, PlayZoneInjuryAnim@120500]
  +0x0a4  w[4] W [1: AICharacterEnemySSnow@11ff18]
  +0x0b0  w[4] R/W [1: DoDying@1202e0]
  +0x140  w[4] W [2: AICharacterEnemySSnow@11ff18, ~AICharacterEnemySSnow@120010]
  +0x150  w[4] R/W -> AttributeSet::LookupInt [8: AICharacterEnemySSnow@11ff18, DoAiming@120160, DoNeutral@1200a0, GetRotPos@11fca0, GetRotPos@11fdc0, GetVehiclePtr@120470…]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  GetVehiclePtr: R +0x140 w4
  ~AICharacterEnemySSnow: W +0x0 w4
