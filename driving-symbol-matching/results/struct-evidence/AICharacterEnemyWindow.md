# AICharacterEnemyWindow

FastAlloc/constructed sizes under its tag: None
deleting destructor 0x21dc0 frees/deletes with size 0x160 (call to UMemory::FastFree)
Xbox vtable 0x0018ab68 (36 slots) stored by its constructor
PS2 sheet virtual table row: ['AICharacterEnemyWindow virtual table']
constructor 0x21d50 first calls: ['AICharacter::AICharacter']

Xbox methods (15):
  0x21600 undefined DoFiring(void)
  0x21d40 undefined ~AICharacterEnemyWindow(void)
  0x21d50 undefined AICharacterEnemyWindow(undefined4 param_1, undefined4 param_2)
  0x21dc0 undefined scalar_deleting_destructor(undefined1 param_1)
  0x21e70 undefined DoInitial(void)
  0x21f10 undefined DoNeutral(void)
  0x21f70 undefined DoIdling(void)
  0x22010 undefined DoArming(void)
  0x22050 undefined DoArmed(void)
  0x22150 undefined DoAiming(void)
  0x22280 undefined DoUnarming(void)
  0x222c0 undefined DoDying(void)
  0x22310 undefined HandleInterrupts(void)
  0x223a0 undefined TargetIsInRange(void)
  0x22680 undefined PlayZoneInjuryAnim(undefined param_1, undefined4 param_2)

PS2 methods (23):
  0x122238 AICharacterEnemyWindow::GetRotPos
  0x122358 AICharacterEnemyWindow::GetRotPos
  0x1224b0 AICharacterEnemyWindow::AICharacterEnemyWindow
  0x1225a8 AICharacterEnemyWindow::~AICharacterEnemyWindow
  0x122600 AICharacterEnemyWindow::SetActor
  0x122698 AICharacterEnemyWindow::DoInitial
  0x122780 AICharacterEnemyWindow::DoNeutral
  0x122818 AICharacterEnemyWindow::DoIdling
  0x1228d0 AICharacterEnemyWindow::DoArming
  0x122920 AICharacterEnemyWindow::DoArmed
  0x122a68 AICharacterEnemyWindow::DoAiming
  0x122be0 AICharacterEnemyWindow::DoFiring
  0x122c30 AICharacterEnemyWindow::DoUnarming
  0x122c88 AICharacterEnemyWindow::DoHurting
  0x122cc0 AICharacterEnemyWindow::DoDying
  0x122d20 AICharacterEnemyWindow::DoDead
  0x122d28 AICharacterEnemyWindow::HandleInterrupts
  0x122dd0 AICharacterEnemyWindow::UpdateRotPos
  0x122e18 AICharacterEnemyWindow::GetVehiclePtr
  0x122e20 AICharacterEnemyWindow::TargetIsInRange
  0x123058 AICharacterEnemyWindow::TargetIsAcquired
  0x123298 AICharacterEnemyWindow::PlayZoneInjuryAnim
  0x123578 AICharacterEnemyWindow::GetRotPos_global_ctors

Sheet rows:
  AICharacterEnemyWindow::GetRotPos(int, MATRIX4 &)
  AICharacterEnemyWindow::GetRotPos(int, MATRIX4 &, COORD3 &)
  AICharacterEnemyWindow::AICharacterEnemyWindow(MATRIX4 &, PVehi
  AICharacterEnemyWindow::~AICharacterEnemyWindow(void)
  AICharacterEnemyWindow::SetActor(_List_iterator<ActActor *, Act
  AICharacterEnemyWindow::DoInitial(void)
  AICharacterEnemyWindow::DoNeutral(void)
  AICharacterEnemyWindow::DoIdling(void)
  AICharacterEnemyWindow::DoArming(void)
  AICharacterEnemyWindow::DoArmed(void)
  AICharacterEnemyWindow::DoAiming(void)
  AICharacterEnemyWindow::DoFiring(void)
  AICharacterEnemyWindow::DoUnarming(void)
  AICharacterEnemyWindow::DoHurting(void)
  AICharacterEnemyWindow::DoDying(void)
  AICharacterEnemyWindow::DoDead(void)
  AICharacterEnemyWindow::HandleInterrupts(void)
  AICharacterEnemyWindow::UpdateRotPos(void)
  AICharacterEnemyWindow::GetVehiclePtr(void)
  AICharacterEnemyWindow::TargetIsInRange(void)
  AICharacterEnemyWindow::TargetIsAcquired(void)
  AICharacterEnemyWindow::PlayZoneInjuryAnim(int, float, int)
  AICharacterEnemyWindow type_info function
  AICharacterEnemyWindow virtual table
  AICharacterEnemyWindow type_info node

Xbox methods treated as members (15 of 15; untyped ones count when ECX is read before it is written): AICharacterEnemyWindow, DoAiming, DoArmed, DoArming, DoDying, DoFiring, DoIdling, DoInitial, DoNeutral, DoUnarming, HandleInterrupts, PlayZoneInjuryAnim, TargetIsInRange, scalar_deleting_destructor, ~AICharacterEnemyWindow

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W [7: AICharacterEnemyWindow@21d50, DoAiming@22150, DoArmed@22050, DoFiring@21600, DoNeutral@21f10, TargetIsInRange@223a0…]
  +0x010  w- LEA addr-taken [1: AICharacterEnemyWindow@21d50]
  +0x05c  w[4] R/W [10: DoArmed@22050, DoArming@22010, DoDying@222c0, DoFiring@21600, DoIdling@21f70, DoInitial@21e70…]
  +0x060  w[4] R/W [4: DoDying@222c0, DoIdling@21f70, DoNeutral@21f10, DoUnarming@22280]
  +0x064  w[4] W [4: DoArmed@22050, DoIdling@21f70, HandleInterrupts@22310, PlayZoneInjuryAnim@22680]
  +0x074  w[4] W float [1: TargetIsInRange@223a0]
  +0x078  w[4] R [1: PlayZoneInjuryAnim@22680]
  +0x084  w[4] R -> Human::Activate, Human::Deactivate [2: DoInitial@21e70, DoNeutral@21f10]
  +0x088  w[4] R/W [5: AICharacterEnemyWindow@21d50, DoArmed@22050, DoIdling@21f70, HandleInterrupts@22310, PlayZoneInjuryAnim@22680]
  +0x08c  w[4] RW [1: DoArmed@22050]
  +0x094  w[4] R [1: DoAiming@22150]
  +0x09c  w[4] R [1: DoArmed@22050]
  +0x0a0  w[4] R/W [1: DoArmed@22050]
  +0x0a8  w[4] R [11: DoAiming@22150, DoArmed@22050, DoArming@22010, DoDying@222c0, DoFiring@21600, DoIdling@21f70…]
  +0x0b0  w[4] R/W [4: DoArmed@22050, DoIdling@21f70, HandleInterrupts@22310, PlayZoneInjuryAnim@22680]
  +0x0b4  w[4] W [1: AICharacterEnemyWindow@21d50]
  +0x0bc  w[1] R [1: DoArmed@22050]
  +0x0bd  w[1] R/W [1: DoDying@222c0]
  +0x140  w[4] R/W -> PhysicsObject::GetHitPoints [5: AICharacterEnemyWindow@21d50, DoArmed@22050, DoInitial@21e70, DoNeutral@21f10, HandleInterrupts@22310]
  +0x144  w[4] R/W float [2: AICharacterEnemyWindow@21d50, DoAiming@22150]
  +0x148  w[4] R/W [2: AICharacterEnemyWindow@21d50, DoInitial@21e70]
  +0x14c  w[4] R/W [2: AICharacterEnemyWindow@21d50, DoInitial@21e70]
  +0x150  w[4] R/W [2: AICharacterEnemyWindow@21d50, DoAiming@22150]
  +0x154  w[4] R/W [3: AICharacterEnemyWindow@21d50, DoArmed@22050, DoNeutral@21f10]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[8] W [1: AICharacterEnemyWindow@1224b0]
  +0x008  w[8] W [1: AICharacterEnemyWindow@1224b0]
  +0x010  w[8] W [1: AICharacterEnemyWindow@1224b0]
  +0x018  w[8] W [1: AICharacterEnemyWindow@1224b0]
  +0x020  w[8] W [1: AICharacterEnemyWindow@1224b0]
  +0x028  w[8] W [1: AICharacterEnemyWindow@1224b0]
  +0x030  w[8] W [1: AICharacterEnemyWindow@1224b0]
  +0x038  w[8] W [1: AICharacterEnemyWindow@1224b0]
  +0x04c  w[4] R/W [11: DoArmed@122920, DoArming@1228d0, DoDying@122cc0, DoFiring@122be0, DoHurting@122c88, DoIdling@122818…]
  +0x050  w[4] R/W [4: DoDying@122cc0, DoIdling@122818, DoNeutral@122780, DoUnarming@122c30]
  +0x054  w[4] W [4: DoArmed@122920, DoIdling@122818, HandleInterrupts@122d28, PlayZoneInjuryAnim@123298]
  +0x064  w[4] R float [1: TargetIsInRange@122e20]
  +0x068  w[4] R [1: PlayZoneInjuryAnim@123298]
  +0x074  w[4] R [3: DoIdling@122818, DoInitial@122698, DoNeutral@122780]
  +0x078  w[4] R/W [5: AICharacterEnemyWindow@1224b0, DoArmed@122920, DoIdling@122818, HandleInterrupts@122d28, PlayZoneInjuryAnim@123298]
  +0x07c  w[4] R/W [1: DoArmed@122920]
  +0x084  w[4] R/W [2: DoAiming@122a68, TargetIsAcquired@123058]
  +0x08c  w[4] R [1: DoArmed@122920]
  +0x090  w[4] R/W [1: DoArmed@122920]
  +0x098  w[4] R/W -> Human::Deactivate [14: DoAiming@122a68, DoArmed@122920, DoArming@1228d0, DoDying@122cc0, DoFiring@122be0, DoHurting@122c88…]
  +0x0a0  w[4] R/W [4: DoArmed@122920, DoIdling@122818, HandleInterrupts@122d28, PlayZoneInjuryAnim@123298]
  +0x0a4  w[4] W [1: AICharacterEnemyWindow@1224b0]
  +0x0ac  w[4] R [1: DoArmed@122920]
  +0x0b0  w[4] R/W [1: DoDying@122cc0]
  +0x0c0  w[8] W [1: TargetIsAcquired@123058]
  +0x0c8  w[4] W [1: TargetIsAcquired@123058]
  +0x0cc  w[4] W [1: TargetIsAcquired@123058]
  +0x140  w[4] R/W [8: AICharacterEnemyWindow@1224b0, DoAiming@122a68, DoArmed@122920, DoFiring@122be0, DoNeutral@122780, TargetIsAcquired@123058…]
  +0x150  w[4] R/W [8: AICharacterEnemyWindow@1224b0, DoArmed@122920, DoInitial@122698, DoNeutral@122780, GetRotPos@122238, GetRotPos@122358…]
  +0x154  w[4] R/W float [2: AICharacterEnemyWindow@1224b0, DoAiming@122a68]
  +0x158  w[4] R/W [2: AICharacterEnemyWindow@1224b0, DoInitial@122698]
  +0x15c  w[4] R/W [2: AICharacterEnemyWindow@1224b0, DoInitial@122698]
  +0x160  w[4] R/W [2: AICharacterEnemyWindow@1224b0, DoAiming@122a68]
  +0x164  w[4] R/W [4: AICharacterEnemyWindow@1224b0, DoArmed@122920, DoNeutral@122780, HandleInterrupts@122d28]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  ~AICharacterEnemyWindow: W +0x0 w4
