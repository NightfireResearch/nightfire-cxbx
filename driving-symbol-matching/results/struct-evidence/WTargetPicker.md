# WTargetPicker

FastAlloc/constructed sizes under its tag: None
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none

Xbox methods (18):
  0xcdc10 undefined GetOffScreenPos(undefined4 param_1, undefined4 param_2)
  0xcdee0 undefined IsPointOnScreen(undefined4 param_1)
  0xcdfb0 undefined GetSelectedTarget(void)
  0xcdfd0 undefined GetWorldTarget(void)
  0xce050 undefined CompareDistFromScreenCenter(void)
  0xce0a0 undefined CompareDistFromCamera(void)
  0xce0f0 void __thiscall ActivateTargeting(WTargetPicker * this, undefined4 * param_1_00, int param_2)
  0xce150 undefined DeactivateTargeting(void)
  0xce170 undefined CycleToNextTarget(void)
  0xce1b0 undefined UpdateAutoDriveTargeting(void)
  0xce2b0 undefined GetScreenPos(undefined4 param_1, undefined4 param_2)
  0xce410 undefined MoveTargetingCursors(void)
  0xce4f0 undefined CompareDistFromScreenPos(undefined4 param_1, undefined4 param_2)
  0xce5a0 void __thiscall DrawTargetingSystem(WTargetPicker * this)
  0xce620 undefined UpdateSelection(void)
  0xce950 undefined UpdateTargets(void)
  0xceb60 undefined UnregisterTarget(undefined4 param_1)
  0xced50 undefined RegisterTarget(undefined4 param_1)

PS2 methods (33):
  0x229f40 WTargetPicker::WTargetPicker
  0x229ff0 WTargetPicker::~WTargetPicker
  0x22a070 WTargetPicker::GetViewMatrix
  0x22a128 WTargetPicker::GetScreenPos
  0x22a430 WTargetPicker::GetOffScreenPos
  0x22a728 WTargetPicker::IsPointOnScreen
  0x22a818 WTargetPicker::IsPointLockable
  0x22a8a8 WTargetPicker::InterpolateAndSnap2D
  0x22a960 WTargetPicker::MoveTargetingCursors
  0x22aab0 WTargetPicker::UpdateSelection
  0x22aed0 WTargetPicker::GetLockOnPercent
  0x22af38 WTargetPicker::GetSelectedTarget
  0x22af58 WTargetPicker::GetWorldTarget
  0x22afa8 WTargetPicker::RegisterTarget
  0x22b098 WTargetPicker::UnregisterTarget
  0x22b130 WTargetPicker::ClearAllTargets
  0x22b150 WTargetPicker::CompareDistFromScreenCenter
  0x22b1c0 WTargetPicker::CompareDistFromScreenPos
  0x22b2a0 WTargetPicker::CompareDistFromCamera
  0x22b318 WTargetPicker::UpdateTargets
  0x22b638 WTargetPicker::ActivateTargeting
  0x22b698 WTargetPicker::DeactivateTargeting
  0x22b6b8 WTargetPicker::CycleToNextTarget
  0x22b720 WTargetPicker::SlerpTowardsTarget
  0x22b808 WTargetPicker::GetDirectionToTarget
  0x22b858 WTargetPicker::GetDirectionToTarget
  0x22b918 WTargetPicker::UpdateAutoDriveTargeting
  0x22bac0 WTargetPicker::GetCheatDirectionToTarget
  0x22bda0 WTargetPicker::DrawTargetingSystem
  0x22be58 WTargetPicker::DebugDrawCursors
  0x22be60 WTargetPicker::DebugDrawTargets
  0x22c168 WTargetPicker::kTargetRangeBits_global_ctors
  0x22c188 WTargetPicker::kTargetRangeBits_global_dtors

Sheet rows:
  WTargetPicker::WTargetPicker(void)
  WTargetPicker::~WTargetPicker(void)
  WTargetPicker::GetViewMatrix(MATRIX4 &)
  WTargetPicker::GetScreenPos(COORD3 &)
  WTargetPicker::GetOffScreenPos(COORD3 &)
  WTargetPicker::IsPointOnScreen(COORD2 &)
  WTargetPicker::IsPointLockable(COORD2 &)
  WTargetPicker::InterpolateAndSnap2D(COORD2 &, COORD2 &, float,
  WTargetPicker::MoveTargetingCursors(void)
  WTargetPicker::UpdateSelection(void)
  WTargetPicker::GetLockOnPercent(void) const
  WTargetPicker::GetSelectedTarget(void)
  WTargetPicker::GetWorldTarget(void)
  WTargetPicker::RegisterTarget(WTargetable *)
  WTargetPicker::UnregisterTarget(WTargetable *)
  WTargetPicker::ClearAllTargets(void)
  WTargetPicker::CompareDistFromScreenCenter(void *, void *)
  WTargetPicker::CompareDistFromScreenPos(void *, void *)
  WTargetPicker::CompareDistFromCamera(void *, void *)
  WTargetPicker::UpdateTargets(void)
  WTargetPicker::ActivateTargeting(TargetingState)
  WTargetPicker::DeactivateTargeting(void)
  WTargetPicker::CycleToNextTarget(void)
  WTargetPicker::SlerpTowardsTarget(MATRIX4 &, float, MATRIX4 &)
  WTargetPicker::GetDirectionToTarget(COORD3 &)
  WTargetPicker::GetDirectionToTarget(MATRIX4 &)
  WTargetPicker::UpdateAutoDriveTargeting(void)
  WTargetPicker::GetCheatDirectionToTarget(COORD3 &, COORD3 &, CO
  WTargetPicker::DrawTargetingSystem(void)
  WTargetPicker::DebugDrawCursors(void)
  WTargetPicker::DebugDrawTargets(void)
  WTargetPicker::DrawAutoDriveTargeting(void)
  WTargetPicker::gSort
  WTargetPicker::kTargetRangeBits

Xbox methods treated as members (12 of 18; untyped ones count when ECX is read before it is written): ActivateTargeting, CycleToNextTarget, DeactivateTargeting, DrawTargetingSystem, GetSelectedTarget, GetWorldTarget, MoveTargetingCursors, RegisterTarget, UnregisterTarget, UpdateAutoDriveTargeting, UpdateSelection, UpdateTargets

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: ActivateTargeting@ce0f0, UpdateAutoDriveTargeting@ce1b0]
  +0x004  w[4] W [2: ActivateTargeting@ce0f0, UpdateAutoDriveTargeting@ce1b0]
  +0x008  w[4] W [2: ActivateTargeting@ce0f0, UpdateAutoDriveTargeting@ce1b0]
  +0x00c  w[4] W [2: ActivateTargeting@ce0f0, UpdateAutoDriveTargeting@ce1b0]
  +0x010  w[4] LEA/R addr-taken [2: GetWorldTarget@cdfd0, UpdateSelection@ce620]
  +0x014  w[4] R [2: GetWorldTarget@cdfd0, UpdateSelection@ce620]
  +0x018  w[4] R [2: GetWorldTarget@cdfd0, UpdateSelection@ce620]
  +0x01c  w[4] R [1: MoveTargetingCursors@ce410]
  +0x020  w- LEA addr-taken [1: UpdateSelection@ce620]
  +0x024  w[4] W float [1: UpdateSelection@ce620]
  +0x028  w[4] R/W -> WTargetable::DistFromScreenPos [7: ActivateTargeting@ce0f0, CycleToNextTarget@ce170, DeactivateTargeting@ce150, GetSelectedTarget@cdfb0, MoveTargetingCursors@ce410, UnregisterTarget@ceb60…]
  +0x02c  w[4] R [3: RegisterTarget@ced50, UnregisterTarget@ceb60, UpdateTargets@ce950]
  +0x030  w[4] LEA/R addr-taken [2: UpdateSelection@ce620, UpdateTargets@ce950]
  +0x070  w[4] R/W [3: CycleToNextTarget@ce170, UpdateSelection@ce620, UpdateTargets@ce950]
  +0x074  w[4] R/W [2: CycleToNextTarget@ce170, UpdateSelection@ce620]
  +0x07c  w[4] R -> ATargeting::SetState [1: MoveTargetingCursors@ce410]
  +0x084  w[4] W [1: ActivateTargeting@ce0f0]
  +0x088  w[4] W [1: ActivateTargeting@ce0f0]
  +0x08c  w[4] W [1: MoveTargetingCursors@ce410]
  +0x090  w[4] W [1: MoveTargetingCursors@ce410]
  +0x094  w[4] W [1: ActivateTargeting@ce0f0]
  +0x098  w[4] W [1: ActivateTargeting@ce0f0]
  +0x09c  w[1] R/W [4: ActivateTargeting@ce0f0, DeactivateTargeting@ce150, DrawTargetingSystem@ce5a0, UpdateTargets@ce950]
  +0x0a0  w[4] R/W -> GHud::SetTargetLockState [5: ActivateTargeting@ce0f0, DeactivateTargeting@ce150, DrawTargetingSystem@ce5a0, MoveTargetingCursors@ce410, UpdateSelection@ce620]
  +0x0a4  w[4] R/W [5: ActivateTargeting@ce0f0, DrawTargetingSystem@ce5a0, MoveTargetingCursors@ce410, UpdateSelection@ce620, UpdateTargets@ce950]
  +0x0a8  w[4] R [1: UpdateTargets@ce950]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] R/W -> WTargetable::DistFromCamera [5: ActivateTargeting@22b638, CompareDistFromCamera@22b2a0, CompareDistFromScreenPos@22b1c0, UpdateAutoDriveTargeting@22b918, WTargetPicker@229f40]
  +0x004  w[4] W [3: ActivateTargeting@22b638, UpdateAutoDriveTargeting@22b918, WTargetPicker@229f40]
  +0x008  w[4] W [3: ActivateTargeting@22b638, UpdateAutoDriveTargeting@22b918, WTargetPicker@229f40]
  +0x00c  w[4] W float [3: ActivateTargeting@22b638, UpdateAutoDriveTargeting@22b918, WTargetPicker@229f40]
  +0x010  w[4, 8] LEA/R/W float addr-taken [3: GetWorldTarget@22af58, MoveTargetingCursors@22a960, UpdateSelection@22aab0]
  +0x014  w[4] R float [1: GetWorldTarget@22af58]
  +0x018  w[4] R/W float [2: GetWorldTarget@22af58, UpdateSelection@22aab0]
  +0x01c  w[4] R/W [2: GetLockOnPercent@22aed0, MoveTargetingCursors@22a960]
  +0x020  w[4, 8] LEA/W float addr-taken [1: UpdateSelection@22aab0]
  +0x024  w[4] W float [1: UpdateSelection@22aab0]
  +0x028  w[4] R/W -> WTargetable::DistFromScreenPos, WTargetable::GetVelocity [11: ActivateTargeting@22b638, CycleToNextTarget@22b6b8, DeactivateTargeting@22b698, GetCheatDirectionToTarget@22bac0, GetSelectedTarget@22af38, MoveTargetingCursors@22a960…]
  +0x02c  w[4] R/W -> list<WTargetable_*,_allocator<WTargetable_*>_>::remove [5: RegisterTarget@22afa8, UnregisterTarget@22b098, UpdateTargets@22b318, WTargetPicker@229f40, ~WTargetPicker@229ff0]
  +0x030  w[4] LEA/R addr-taken [3: CycleToNextTarget@22b6b8, UpdateSelection@22aab0, UpdateTargets@22b318]
  +0x070  w[4] R/W [4: CycleToNextTarget@22b6b8, UpdateSelection@22aab0, UpdateTargets@22b318, WTargetPicker@229f40]
  +0x074  w[4] R/W [2: CycleToNextTarget@22b6b8, UpdateSelection@22aab0]
  +0x078  w[4] W [1: WTargetPicker@229f40]
  +0x07c  w[4] R/W -> ATargeting::SetState, UMemory::FastFree [3: MoveTargetingCursors@22a960, WTargetPicker@229f40, ~WTargetPicker@229ff0]
  +0x080  w[4] W [1: WTargetPicker@229f40]
  +0x084  w[4] W [1: ActivateTargeting@22b638]
  +0x088  w[4] W [1: ActivateTargeting@22b638]
  +0x08c  w[8] W [1: MoveTargetingCursors@22a960]
  +0x094  w[4] W [1: ActivateTargeting@22b638]
  +0x098  w[4] W [1: ActivateTargeting@22b638]
  +0x09c  w[4] R/W [4: ActivateTargeting@22b638, DeactivateTargeting@22b698, UpdateTargets@22b318, WTargetPicker@229f40]
  +0x0a0  w[4] R/W [9: ActivateTargeting@22b638, DeactivateTargeting@22b698, GetCheatDirectionToTarget@22bac0, GetLockOnPercent@22aed0, MoveTargetingCursors@22a960, SlerpTowardsTarget@22b720…]
  +0x0a4  w[4] R/W [4: ActivateTargeting@22b638, MoveTargetingCursors@22a960, UpdateSelection@22aab0, UpdateTargets@22b318]
  +0x0a8  w[4] R/W [2: UpdateTargets@22b318, WTargetPicker@229f40]
  +0x0ac  w[4] W [1: WTargetPicker@229f40]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  GetSelectedTarget: R +0x28 w4
