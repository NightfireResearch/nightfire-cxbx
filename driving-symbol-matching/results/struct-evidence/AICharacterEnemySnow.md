# AICharacterEnemySnow

FastAlloc/constructed sizes under its tag: None
deleting destructor 0x1fdd0 frees/deletes with size 0x170 (call to UMemory::FastFree)
Xbox vtable 0x0018a948 (36 slots) stored by its constructor
PS2 sheet virtual table row: ['AICharacterEnemySnow virtual table']
constructor 0x1fd40 first calls: ['AICharacter::AICharacter']

Xbox methods (18):
  0x1fac0 undefined GetRotPos(undefined4 param_1, undefined4 param_2)
  0x1fb70 undefined GetRotPos(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0x1fc70 undefined ~AICharacterEnemySnow(void)
  0x1fcd0 undefined DoTilting(void)
  0x1fcf0 undefined HandleInterrupts(void)
  0x1fd40 undefined AICharacterEnemySnow(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0x1fdd0 undefined scalar_deleting_destructor(undefined1 param_1)
  0x1fe00 undefined DoNeutral(void)
  0x1ff90 undefined DoArmed(void)
  0x20090 undefined DoAiming(void)
  0x203c0 undefined DoDying(void)
  0x205b0 undefined DoRunning(void)
  0x205e0 undefined DoLeaning(void)
  0x206d0 undefined UpdateRotPos(void)
  0x20710 undefined TargetIsInRange(void)
  0x20870 undefined PlayZoneInjuryAnim(undefined param_1, undefined4 param_2)
  0x21df0 undefined SetActor(undefined4 param_1)
  0x224f0 undefined TargetIsAcquired(void)

PS2 methods (26):
  0x11e198 AICharacterEnemySnow::GetRotPos
  0x11e2f0 AICharacterEnemySnow::GetRotPos
  0x11e490 AICharacterEnemySnow::AICharacterEnemySnow
  0x11e5b8 AICharacterEnemySnow::~AICharacterEnemySnow
  0x11e610 AICharacterEnemySnow::SetActor
  0x11e6a8 AICharacterEnemySnow::DoInitial
  0x11e770 AICharacterEnemySnow::DoNeutral
  0x11e930 AICharacterEnemySnow::DoIdling
  0x11e968 AICharacterEnemySnow::DoArming
  0x11e9a0 AICharacterEnemySnow::DoArmed
  0x11ead0 AICharacterEnemySnow::DoAiming
  0x11ef30 AICharacterEnemySnow::DoFiring
  0x11ef68 AICharacterEnemySnow::DoUnarming
  0x11efa0 AICharacterEnemySnow::DoHurting
  0x11efd8 AICharacterEnemySnow::DoDying
  0x11f268 AICharacterEnemySnow::DoTilting
  0x11f2c8 AICharacterEnemySnow::DoRunning
  0x11f308 AICharacterEnemySnow::DoLeaning
  0x11f460 AICharacterEnemySnow::DoDead
  0x11f468 AICharacterEnemySnow::HandleInterrupts
  0x11f4d8 AICharacterEnemySnow::UpdateRotPos
  0x11f520 AICharacterEnemySnow::GetVehiclePtr
  0x11f528 AICharacterEnemySnow::TargetIsInRange
  0x11f768 AICharacterEnemySnow::TargetIsAcquired
  0x11f9a8 AICharacterEnemySnow::PlayZoneInjuryAnim
  0x11fc80 AICharacterEnemySnow::GetRotPos_global_ctors

Sheet rows:
  AICharacterEnemySnow::GetRotPos(int, MATRIX4 &)
  AICharacterEnemySnow::GetRotPos(int, MATRIX4 &, COORD3 &)
  AICharacterEnemySnow::AICharacterEnemySnow(MATRIX4 &, int, PVeh
  AICharacterEnemySnow::~AICharacterEnemySnow(void)
  AICharacterEnemySnow::SetActor(_List_iterator<ActActor *, ActAc
  AICharacterEnemySnow::DoInitial(void)
  AICharacterEnemySnow::DoNeutral(void)
  AICharacterEnemySnow::DoIdling(void)
  AICharacterEnemySnow::DoArming(void)
  AICharacterEnemySnow::DoArmed(void)
  AICharacterEnemySnow::DoAiming(void)
  AICharacterEnemySnow::DoFiring(void)
  AICharacterEnemySnow::DoUnarming(void)
  AICharacterEnemySnow::DoHurting(void)
  AICharacterEnemySnow::DoDying(void)
  AICharacterEnemySnow::DoTilting(void)
  AICharacterEnemySnow::DoRunning(void)
  AICharacterEnemySnow::DoLeaning(void)
  AICharacterEnemySnow::DoDead(void)
  AICharacterEnemySnow::HandleInterrupts(void)
  AICharacterEnemySnow::UpdateRotPos(void)
  AICharacterEnemySnow::GetVehiclePtr(void)
  AICharacterEnemySnow::TargetIsInRange(void)
  AICharacterEnemySnow::TargetIsAcquired(void)
  AICharacterEnemySnow::PlayZoneInjuryAnim(int, float, int)
  AICharacterEnemySnow type_info function
  AICharacterEnemySnow virtual table
  AICharacterEnemySnow type_info node

Xbox methods treated as members (16 of 18; untyped ones count when ECX is read before it is written): AICharacterEnemySnow, DoAiming, DoArmed, DoDying, DoLeaning, DoNeutral, DoRunning, DoTilting, HandleInterrupts, PlayZoneInjuryAnim, SetActor, TargetIsAcquired, TargetIsInRange, UpdateRotPos, scalar_deleting_destructor, ~AICharacterEnemySnow

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W [8: AICharacterEnemySnow@1fd40, DoAiming@20090, DoArmed@1ff90, DoTilting@1fcd0, HandleInterrupts@1fcf0, TargetIsAcquired@224f0…]
  +0x010  w- LEA addr-taken [2: AICharacterEnemySnow@1fd40, UpdateRotPos@206d0]
  +0x05c  w[4] R/W [6: DoArmed@1ff90, DoDying@203c0, DoNeutral@1fe00, DoRunning@205b0, HandleInterrupts@1fcf0, PlayZoneInjuryAnim@20870]
  +0x060  w[4] R/W [3: DoNeutral@1fe00, DoRunning@205b0, DoTilting@1fcd0]
  +0x064  w[4] W [5: DoArmed@1ff90, DoDying@203c0, DoLeaning@205e0, DoNeutral@1fe00, PlayZoneInjuryAnim@20870]
  +0x068  w[4] R/W [2: AICharacterEnemySnow@1fd40, DoAiming@20090]
  +0x074  w[4] W float [1: TargetIsInRange@20710]
  +0x078  w[4] R [1: PlayZoneInjuryAnim@20870]
  +0x088  w[4] R/W [6: AICharacterEnemySnow@1fd40, DoArmed@1ff90, DoDying@203c0, DoLeaning@205e0, DoNeutral@1fe00, PlayZoneInjuryAnim@20870]
  +0x094  w[4] R/W [2: DoAiming@20090, TargetIsAcquired@224f0]
  +0x0a8  w[4] R/W [10: DoAiming@20090, DoArmed@1ff90, DoDying@203c0, DoLeaning@205e0, DoNeutral@1fe00, DoRunning@205b0…]
  +0x0b0  w[4] R/W [5: DoArmed@1ff90, DoDying@203c0, DoLeaning@205e0, DoNeutral@1fe00, PlayZoneInjuryAnim@20870]
  +0x0b4  w[4] W [1: AICharacterEnemySnow@1fd40]
  +0x0bd  w[1] R/W [1: DoDying@203c0]
  +0x0c0  w- LEA addr-taken [1: TargetIsAcquired@224f0]
  +0x0cc  w[1] W [1: TargetIsAcquired@224f0]
  +0x0d0  w- LEA addr-taken [1: DoDying@203c0]
  +0x110  w- LEA addr-taken [1: DoDying@203c0]
  +0x114  w[4] W float [1: DoDying@203c0]
  +0x11c  w[1] W [1: DoDying@203c0]
  +0x11d  w[1] W [1: DoDying@203c0]
  +0x140  w[4] R/W -> PhysicsObject::GetHitPoints [6: AICharacterEnemySnow@1fd40, DoDying@203c0, DoLeaning@205e0, DoNeutral@1fe00, HandleInterrupts@1fcf0, PlayZoneInjuryAnim@20870]
  +0x144  w[4] R/W [3: AICharacterEnemySnow@1fd40, DoLeaning@205e0, DoNeutral@1fe00]
  +0x148  w[4] R/W [2: AICharacterEnemySnow@1fd40, DoAiming@20090]
  +0x14c  w[4] R/W [2: AICharacterEnemySnow@1fd40, DoAiming@20090]
  +0x150  w[4] R/W [2: AICharacterEnemySnow@1fd40, DoAiming@20090]
  +0x154  w[4] W float [3: AICharacterEnemySnow@1fd40, DoAiming@20090, TargetIsInRange@20710]
  +0x158  w[4] R/W float [1: DoAiming@20090]
  +0x15c  w[4] R/W float [1: DoAiming@20090]
  +0x160  w- LEA addr-taken [2: AICharacterEnemySnow@1fd40, DoAiming@20090]
  +0x168  w[4] W float [3: DoAiming@20090, DoArmed@1ff90, PlayZoneInjuryAnim@20870]
  +0x16c  w[4] W float [2: DoArmed@1ff90, PlayZoneInjuryAnim@20870]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[8] W [1: AICharacterEnemySnow@11e490]
  +0x008  w[8] W [1: AICharacterEnemySnow@11e490]
  +0x010  w[8] W [1: AICharacterEnemySnow@11e490]
  +0x018  w[8] W [1: AICharacterEnemySnow@11e490]
  +0x020  w[8] W [1: AICharacterEnemySnow@11e490]
  +0x028  w[8] W [1: AICharacterEnemySnow@11e490]
  +0x030  w[8] W [1: AICharacterEnemySnow@11e490]
  +0x038  w[8] W [1: AICharacterEnemySnow@11e490]
  +0x04c  w[4] R/W [12: DoArmed@11e9a0, DoArming@11e968, DoDying@11efd8, DoFiring@11ef30, DoHurting@11efa0, DoIdling@11e930…]
  +0x050  w[4] R/W [4: DoLeaning@11f308, DoNeutral@11e770, DoRunning@11f2c8, DoTilting@11f268]
  +0x054  w[4] W [5: DoArmed@11e9a0, DoDying@11efd8, DoLeaning@11f308, DoNeutral@11e770, PlayZoneInjuryAnim@11f9a8]
  +0x058  w[4] R/W [3: AICharacterEnemySnow@11e490, DoAiming@11ead0, DoNeutral@11e770]
  +0x064  w[4] R float [1: TargetIsInRange@11f528]
  +0x068  w[4] R [1: PlayZoneInjuryAnim@11f9a8]
  +0x078  w[4] R/W [6: AICharacterEnemySnow@11e490, DoArmed@11e9a0, DoDying@11efd8, DoLeaning@11f308, DoNeutral@11e770, PlayZoneInjuryAnim@11f9a8]
  +0x07c  w[4] R/W [1: DoArmed@11e9a0]
  +0x084  w[4] R/W [2: DoAiming@11ead0, TargetIsAcquired@11f768]
  +0x08c  w[4] R [1: DoArmed@11e9a0]
  +0x090  w[4] R/W [1: DoArmed@11e9a0]
  +0x098  w[4] R/W [15: DoAiming@11ead0, DoArmed@11e9a0, DoArming@11e968, DoDying@11efd8, DoFiring@11ef30, DoHurting@11efa0…]
  +0x0a0  w[4] R/W [5: DoArmed@11e9a0, DoDying@11efd8, DoLeaning@11f308, DoNeutral@11e770, PlayZoneInjuryAnim@11f9a8]
  +0x0a4  w[4] W [1: AICharacterEnemySnow@11e490]
  +0x0ac  w[4] R [1: DoArmed@11e9a0]
  +0x0b0  w[4] R/W [1: DoDying@11efd8]
  +0x0c0  w[8] W [1: TargetIsAcquired@11f768]
  +0x0c8  w[4] W [1: TargetIsAcquired@11f768]
  +0x0cc  w[4] W [1: TargetIsAcquired@11f768]
  +0x0d0  w- LEA addr-taken [3: DoDying@11efd8, GetRotPos@11e198, GetRotPos@11e2f0]
  +0x110  w[8] LEA/R addr-taken [2: DoDying@11efd8, GetRotPos@11e2f0]
  +0x114  w[4] R/W float [1: DoDying@11efd8]
  +0x118  w[4] R [1: GetRotPos@11e2f0]
  +0x11c  w[4] R/W [3: DoDying@11efd8, GetRotPos@11e198, GetRotPos@11e2f0]
  +0x120  w[4] W [1: DoDying@11efd8]
  +0x140  w[4] R/W [9: AICharacterEnemySnow@11e490, DoAiming@11ead0, DoArmed@11e9a0, DoNeutral@11e770, DoTilting@11f268, HandleInterrupts@11f468…]
  +0x150  w[4] R/W -> PhysicsObject::GetHitPoints [9: AICharacterEnemySnow@11e490, DoDying@11efd8, DoLeaning@11f308, DoNeutral@11e770, GetRotPos@11e198, GetRotPos@11e2f0…]
  +0x154  w[4] R/W [3: AICharacterEnemySnow@11e490, DoLeaning@11f308, DoNeutral@11e770]
  +0x158  w[4] R/W [2: AICharacterEnemySnow@11e490, DoAiming@11ead0]
  +0x15c  w[4] R/W [2: AICharacterEnemySnow@11e490, DoAiming@11ead0]
  +0x160  w[4] R/W [2: AICharacterEnemySnow@11e490, DoAiming@11ead0]
  +0x164  w[4] R/W float [3: AICharacterEnemySnow@11e490, DoAiming@11ead0, TargetIsInRange@11f528]
  +0x168  w[4] R/W float [1: DoAiming@11ead0]
  +0x16c  w[4] R/W float [1: DoAiming@11ead0]
  +0x170  w[4] LEA/R float addr-taken [1: DoAiming@11ead0]
  +0x178  w[4] LEA/W float addr-taken [5: AICharacterEnemySnow@11e490, DoAiming@11ead0, DoArmed@11e9a0, DoNeutral@11e770, PlayZoneInjuryAnim@11f9a8]
  +0x17c  w[4] W float [3: DoArmed@11e9a0, DoNeutral@11e770, PlayZoneInjuryAnim@11f9a8]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  ~AICharacterEnemySnow: W +0x0 w4
