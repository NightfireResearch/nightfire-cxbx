# AICharacterPedestrian

FastAlloc/constructed sizes under its tag: None
deleting destructor 0x24700 frees/deletes with size 0x290 (call to UMemory::FastFree)
Xbox vtable 0x0018b470 (34 slots) stored by its constructor
PS2 sheet virtual table row: ['AICharacterPedestrian virtual table']

Xbox methods (44):
  0x239d0 undefined GetPedBase(void)
  0x23a90 undefined GetRandomConvAnim(undefined1 param_1)
  0x23ad0 undefined PedInRange(undefined4 param_1, undefined4 param_2, undefined1 param_3)
  0x23b70 undefined DoDead(void)
  0x23ba0 undefined SetTarget(undefined4 param_1)
  0x23bc0 undefined GetDegFromAng(undefined4 param_1, undefined1 param_2)
  0x23ce0 undefined UpdateClosestPed(undefined1 param_1)
  0x23e60 undefined UpdateThreatRelativeDirection(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0x23f50 undefined ~AICharacterPedestrian(void)
  0x23fd0 undefined __stdcall Destroy(void)
  0x24060 undefined __stdcall LoadPedTextures(void)
  0x240e0 undefined GetRandomPedTexture(void)
  0x24140 undefined KillSelf(undefined1 param_1)
  0x24280 undefined CheckVehicleThreat(undefined4 param_1, undefined1 param_2)
  0x24700 undefined scalar_deleting_destructor(undefined1 param_1)
  0x24730 undefined ForceRecycle(undefined4 param_1)
  0x24870 undefined UpdateRotPos(void)
  0x248b0 undefined SetActor(undefined4 param_1)
  0x24910 undefined DoAiming(void)
  0x24b30 undefined TargetIsInRange(void)
  0x24c30 undefined FaceTarget(undefined4 param_1)
  0x24db0 undefined UpdateAnimSpeed(void)
  0x24e50 undefined ReachedTarget(undefined1 param_1)
  0x24ee0 undefined FindClosestCar(undefined4 param_1)
  0x24f80 undefined FindPointAgainstWall(undefined4 param_1)
  0x253b0 undefined PlayAnim(undefined4 param_1, undefined4 param_2)
  0x25430 undefined InvalidateFurthestPed(undefined4 param_1)
  0x254c0 undefined Recycle(void)
  0x25d10 undefined PreventFalling(void)
  0x25e00 undefined SetNewTargetBasedOnThreat(undefined4 param_1)
  0x25ef0 undefined DoNeutral(void)
  0x25f40 undefined DoIdling(void)
  0x25fc0 undefined DoDying(void)
  0x26290 undefined DoTurning(void)
  0x263d0 undefined DoLeaning(void)
  0x26620 undefined MaintainHeading(undefined4 param_1)
  0x26760 undefined ThreatenedByVehicle(void)
  0x26800 void __stdcall Init(int param_1)
  0x26ed0 undefined __stdcall Manage(void)
  0x270b0 undefined DoWalking(void)
  0x27150 undefined DoStartled(void)
  0x27240 undefined DoAvoiding(void)
  0x274c0 undefined DoDodging(void)
  0x27800 undefined HandleInterrupts(void)

PS2 methods (56):
  0x125ad8 AICharacterPedestrian::AICharacterPedestrian
  0x125b48 AICharacterPedestrian::~AICharacterPedestrian
  0x125bb8 AICharacterPedestrian::Init
  0x126248 AICharacterPedestrian::Destroy
  0x1262f0 AICharacterPedestrian::Manage
  0x1265c0 AICharacterPedestrian::LoadPedTextures
  0x1266a8 AICharacterPedestrian::GetPedBase
  0x126768 AICharacterPedestrian::GetRandomPedTexture
  0x126800 AICharacterPedestrian::GetSpecialPedInfo
  0x126818 AICharacterPedestrian::InvalidateFurthestPed
  0x126928 AICharacterPedestrian::ForceRecycle
  0x126958 AICharacterPedestrian::GetRandomConvAnim
  0x1269c8 AICharacterPedestrian::SetBuddy
  0x1269e8 AICharacterPedestrian::PedInRange
  0x126ad8 AICharacterPedestrian::KillSelf
  0x126c98 AICharacterPedestrian::Recycle
  0x1274f0 AICharacterPedestrian::PreventFalling
  0x127610 AICharacterPedestrian::SetNewTargetBasedOnThreat
  0x127700 AICharacterPedestrian::DoInitial
  0x127708 AICharacterPedestrian::DoNeutral
  0x127798 AICharacterPedestrian::DoIdling
  0x127840 AICharacterPedestrian::DoWalking
  0x1278f8 AICharacterPedestrian::DoStartRunning
  0x127900 AICharacterPedestrian::DoRunning
  0x127908 AICharacterPedestrian::DoStartled
  0x127a30 AICharacterPedestrian::DoAvoiding
  0x127d58 AICharacterPedestrian::DoDodging
  0x1280c8 AICharacterPedestrian::DoDying
  0x1282e0 AICharacterPedestrian::DoTurning
  0x128488 AICharacterPedestrian::DoDead
  0x1284e0 AICharacterPedestrian::DoLeaning
  0x128788 AICharacterPedestrian::UpdateRotPos
  0x1287f8 AICharacterPedestrian::SetActor
  0x128860 AICharacterPedestrian::DoAiming
  0x128b28 AICharacterPedestrian::TargetIsInRange
  0x128cd8 AICharacterPedestrian::HandleInterrupts
  0x129098 AICharacterPedestrian::SetTarget
  0x1290b8 AICharacterPedestrian::MaintainHeading
  0x129298 AICharacterPedestrian::GetDegFromAng
  0x1294b0 AICharacterPedestrian::GetFacingDiff
  0x129578 AICharacterPedestrian::FaceTarget
  0x129790 AICharacterPedestrian::FaceDirection
  0x129868 AICharacterPedestrian::UpdateAnimSpeed
  0x129920 AICharacterPedestrian::ReachedTarget
  0x1299d0 AICharacterPedestrian::UpdateClosestPed
  0x129bc8 AICharacterPedestrian::ThreatenedByVehicle
  0x129c88 AICharacterPedestrian::FindClosestCar
  0x129d80 AICharacterPedestrian::CheckVehicleThreat
  0x12a398 AICharacterPedestrian::UpdateThreatRelativeDirection
  0x12a4a0 AICharacterPedestrian::FindPointAgainstWall
  0x12a8f0 AICharacterPedestrian::PlayAnim
  0x12a988 AICharacterPedestrian::PlayCrossFade
  0x12aa20 AICharacterPedestrian::SetNewState
  0x12acc0 AICharacterPedestrian::GetWalkToNav
  0x12acc8 AICharacterPedestrian::GetNumPeds
  0x12acd8 AICharacterPedestrian::GetPedestrian

Sheet rows:
  AICharacterPedestrian::AICharacterPedestrian(void)
  AICharacterPedestrian::~AICharacterPedestrian(void)
  AICharacterPedestrian::Init(int)
  AICharacterPedestrian::Destroy(void)
  AICharacterPedestrian::Manage(void)
  AICharacterPedestrian::LoadPedTextures(void)
  AICharacterPedestrian::GetPedBase(void)
  AICharacterPedestrian::GetRandomPedTexture(void)
  AICharacterPedestrian::GetSpecialPedInfo(int)
  AICharacterPedestrian::InvalidateFurthestPed(int)
  AICharacterPedestrian::ForceRecycle(int)
  AICharacterPedestrian::GetRandomConvAnim(bool)
  AICharacterPedestrian::SetBuddy(int, int)
  AICharacterPedestrian::PedInRange(COORD3 &, float, bool)
  AICharacterPedestrian::KillSelf(bool)
  AICharacterPedestrian::Recycle(void)
  AICharacterPedestrian::PreventFalling(void)
  AICharacterPedestrian::SetNewTargetBasedOnThreat(int)
  AICharacterPedestrian::DoInitial(void)
  AICharacterPedestrian::DoNeutral(void)
  AICharacterPedestrian::DoIdling(void)
  AICharacterPedestrian::DoWalking(void)
  AICharacterPedestrian::DoStartRunning(void)
  AICharacterPedestrian::DoRunning(void)
  AICharacterPedestrian::DoStartled(void)
  AICharacterPedestrian::DoAvoiding(void)
  AICharacterPedestrian::DoDodging(void)
  AICharacterPedestrian::DoDying(void)
  AICharacterPedestrian::DoTurning(void)
  AICharacterPedestrian::DoDead(void)
  AICharacterPedestrian::DoLeaning(void)
  AICharacterPedestrian::UpdateRotPos(void)
  AICharacterPedestrian::SetActor(_List_iterator<ActActor *, ActA
  AICharacterPedestrian::DoAiming(void)
  AICharacterPedestrian::TargetIsInRange(void)
  AICharacterPedestrian::HandleInterrupts(void)
  AICharacterPedestrian::SetTarget(COORD3 &)
  AICharacterPedestrian::MaintainHeading(int)
  AICharacterPedestrian::GetDegFromAng(float, bool)
  AICharacterPedestrian::GetFacingDiff(COORD3 &)
  AICharacterPedestrian::FaceTarget(int)
  AICharacterPedestrian::FaceDirection(float)
  AICharacterPedestrian::TooFarAway(COORD3 &)
  AICharacterPedestrian::IsVisible(void)
  AICharacterPedestrian::UpdateAnimSpeed(void)
  AICharacterPedestrian::ReachedTarget(bool)
  AICharacterPedestrian::UpdateClosestPed(bool)
  AICharacterPedestrian::ThreatenedByVehicle(void)
  AICharacterPedestrian::FindClosestCar(float)
  AICharacterPedestrian::CheckVehicleThreat(PVehicle *, bool)
  AICharacterPedestrian::UpdateThreatRelativeDirection(COORD3 &,
  AICharacterPedestrian::FindPointAgainstWall(float)
  AICharacterPedestrian::PlayAnim(int, float)
  AICharacterPedestrian::PlayCrossFade(int, float, float)
  AICharacterPedestrian::SetNewState(AICharacter::EState, int)
  AICharacterPedestrian type_info function
  AICharacterPedestrian::GetWalkToNav(void)
  AICharacterPedestrian::GetNumPeds(void)
  AICharacterPedestrian::GetPedestrian(int)
  AICharacterPedestrian::fgPedConvOffset
  AICharacterPedestrian::fgPedAwaitingBuddy
  AICharacterPedestrian virtual table
  AICharacterPedestrian::fgPedsEnabled
  AICharacterPedestrian::fgPedBase
  AICharacterPedestrian::fgNumPeds
  AICharacterPedestrian::fgNumSpecialPedPoints
  AICharacterPedestrian::fgForceSpecialPed
  AICharacterPedestrian::fgUseLastSpawnPos
  AICharacterPedestrian::fgChatAllowed
  AICharacterPedestrian::fgLastSpawnType
  AICharacterPedestrian::fgPedVoiceDelay
  AICharacterPedestrian::fgLastSeg
  AICharacterPedestrian::fgSpecialPedPoints
  AICharacterPedestrian::fgBondPos
  AICharacterPedestrian::fpPeds
  AICharacterPedestrian::fgLastInitPos
  AICharacterPedestrian::fgLastInitDir
  AICharacterPedestrian type_info node

Xbox methods treated as members (36 of 44; untyped ones count when ECX is read before it is written): CheckVehicleThreat, DoAiming, DoAvoiding, DoDead, DoDodging, DoDying, DoIdling, DoLeaning, DoNeutral, DoStartled, DoTurning, DoWalking, FaceTarget, FindClosestCar, FindPointAgainstWall, GetDegFromAng, HandleInterrupts, InvalidateFurthestPed, KillSelf, LoadPedTextures, MaintainHeading, PlayAnim, PreventFalling, ReachedTarget, Recycle, SetActor, SetNewTargetBasedOnThreat, SetTarget, TargetIsInRange, ThreatenedByVehicle, UpdateAnimSpeed, UpdateClosestPed, UpdateRotPos, UpdateThreatRelativeDirection, scalar_deleting_destructor, ~AICharacterPedestrian

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W [3: DoAiming@24910, FaceTarget@24c30, ~AICharacterPedestrian@23f50]
  +0x010  w- LEA addr-taken [1: UpdateRotPos@24870]
  +0x030  w[4] LEA/R addr-taken -> atan_turns [5: FaceTarget@24c30, PreventFalling@25d10, TargetIsInRange@24b30, UpdateClosestPed@23ce0, UpdateRotPos@24870]
  +0x038  w[4] R [3: FaceTarget@24c30, TargetIsInRange@24b30, UpdateRotPos@24870]
  +0x040  w[4] LEA/W float addr-taken [12: CheckVehicleThreat@24280, DoLeaning@263d0, FaceTarget@24c30, FindClosestCar@24ee0, FindPointAgainstWall@24f80, HandleInterrupts@27800…]
  +0x044  w[4] W float [1: CheckVehicleThreat@24280]
  +0x048  w[4] W float [3: FaceTarget@24c30, TargetIsInRange@24b30, UpdateThreatRelativeDirection@23e60]
  +0x05c  w[4] R/W [6: DoAvoiding@27240, DoWalking@270b0, HandleInterrupts@27800, KillSelf@24140, MaintainHeading@26620, UpdateRotPos@24870]
  +0x060  w[4] R/RW/W [12: DoAvoiding@27240, DoDead@23b70, DoDodging@274c0, DoDying@25fc0, DoIdling@25f40, DoLeaning@263d0…]
  +0x064  w[4] W [1: PlayAnim@253b0]
  +0x070  w[4] R [3: DoDodging@274c0, DoDying@25fc0, DoLeaning@263d0]
  +0x088  w[4] R [1: PlayAnim@253b0]
  +0x0a8  w[4] R/W [10: DoAiming@24910, DoDead@23b70, DoStartled@27150, DoWalking@270b0, FaceTarget@24c30, PlayAnim@253b0…]
  +0x0b0  w[4] W [1: PlayAnim@253b0]
  +0x140  w[4] R [5: FindPointAgainstWall@24f80, HandleInterrupts@27800, KillSelf@24140, Recycle@254c0, UpdateClosestPed@23ce0]
  +0x144  w[4] R -> WWorldPos::FindClosestFace [1: PreventFalling@25d10]
  +0x148  w[4] R/W -> WCollider::~WCollider [2: FindPointAgainstWall@24f80, ~AICharacterPedestrian@23f50]
  +0x14c  w[4] W [4: DoAvoiding@27240, DoWalking@270b0, HandleInterrupts@27800, KillSelf@24140]
  +0x150  w[1] R/W [5: DoDodging@274c0, DoWalking@270b0, HandleInterrupts@27800, KillSelf@24140, MaintainHeading@26620]
  +0x154  w[4] R/W [2: HandleInterrupts@27800, KillSelf@24140]
  +0x15c  w[4] R [3: DoDying@25fc0, DoNeutral@25ef0, PlayAnim@253b0]
  +0x160  w[4] R/W [1: UpdateClosestPed@23ce0]
  +0x164  w[4] R/W [1: UpdateClosestPed@23ce0]
  +0x168  w[4] W [1: UpdateClosestPed@23ce0]
  +0x16c  w[4] W [1: UpdateClosestPed@23ce0]
  +0x180  w- LEA addr-taken [1: DoWalking@270b0]
  +0x190  w[4] LEA/W float addr-taken [7: DoDodging@274c0, DoLeaning@263d0, FaceTarget@24c30, MaintainHeading@26620, PreventFalling@25d10, ReachedTarget@24e50…]
  +0x198  w[4] W float [1: FaceTarget@24c30]
  +0x19c  w[4] W [4: DoAvoiding@27240, DoWalking@270b0, HandleInterrupts@27800, KillSelf@24140]
  +0x1a0  w[4] W [1: PlayAnim@253b0]
  +0x1a4  w[4] W float [2: UpdateRotPos@24870, UpdateThreatRelativeDirection@23e60]
  +0x1a8  w[4] W float [1: UpdateRotPos@24870]
  +0x1ac  w[4] W float [3: HandleInterrupts@27800, KillSelf@24140, ThreatenedByVehicle@26760]
  +0x1b0  w[4] W [2: HandleInterrupts@27800, KillSelf@24140]
  +0x1b4  w[1] R [2: MaintainHeading@26620, UpdateClosestPed@23ce0]
  +0x1b8  w[4] R/W [4: HandleInterrupts@27800, KillSelf@24140, MaintainHeading@26620, UpdateClosestPed@23ce0]
  +0x1bc  w[4] R [1: UpdateThreatRelativeDirection@23e60]
  +0x1c0  w[4] R [1: MaintainHeading@26620]
  +0x1c4  w[4] W [1: PlayAnim@253b0]
  +0x1c8  w[4] W float [2: PlayAnim@253b0, UpdateAnimSpeed@24db0]
  +0x1cc  w[4] W [1: UpdateAnimSpeed@24db0]
  +0x1d0  w- LEA addr-taken [2: DoLeaning@263d0, FindPointAgainstWall@24f80]
  +0x1e0  w- LEA addr-taken [1: FindPointAgainstWall@24f80]
  +0x1e4  w[4] W [1: FindPointAgainstWall@24f80]
  +0x1e8  w[4] W float [1: FindPointAgainstWall@24f80]
  +0x1ec  w[4] W [1: DoLeaning@263d0]
  +0x1f0  w[4] W [1: ThreatenedByVehicle@26760]
  +0x1f4  w[4] R/W [1: ThreatenedByVehicle@26760]
  +0x1f8  w[4] R/W [1: ThreatenedByVehicle@26760]
  +0x1fc  w[4] R/W [2: ThreatenedByVehicle@26760, UpdateAnimSpeed@24db0]
  +0x204  w- LEA addr-taken [1: CheckVehicleThreat@24280]
  +0x208  w- LEA addr-taken [1: CheckVehicleThreat@24280]
  +0x20c  w[4] R/W float [4: CheckVehicleThreat@24280, DoDodging@274c0, ThreatenedByVehicle@26760, UpdateAnimSpeed@24db0]
  +0x210  w- LEA addr-taken [3: CheckVehicleThreat@24280, DoDodging@274c0, SetNewTargetBasedOnThreat@25e00]
  +0x220  w- LEA addr-taken [3: CheckVehicleThreat@24280, DoDodging@274c0, SetNewTargetBasedOnThreat@25e00]
  +0x230  w- LEA addr-taken [2: CheckVehicleThreat@24280, SetNewTargetBasedOnThreat@25e00]
  +0x23c  w[4] W [1: ThreatenedByVehicle@26760]
  +0x240  w[4] R/W [1: FindPointAgainstWall@24f80]
  +0x244  w[4] R/W [1: FindPointAgainstWall@24f80]
  +0x248  w[4] W float [1: FindPointAgainstWall@24f80]
  +0x24c  w[4] W [2: DoDodging@274c0, DoLeaning@263d0]
  +0x250  w[1] R/W [2: DoAiming@24910, TargetIsInRange@24b30]
  +0x260  w- LEA addr-taken [1: TargetIsInRange@24b30]
  +0x26c  w[4] R/W [1: DoAiming@24910]
  +0x270  w[4] R/W [1: DoAiming@24910]
  +0x274  w[4] W float [2: DoAiming@24910, TargetIsInRange@24b30]
  +0x278  w[4] R/W float [1: DoAiming@24910]
  +0x27c  w[4] R/W float [1: DoAiming@24910]
  +0x280  w[4] W float [1: DoAiming@24910]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[8] R [1: PedInRange@1269e8]
  +0x008  w[4] R [1: PedInRange@1269e8]
  +0x020  w[4, 8] R float [7: DoAvoiding@127a30, FaceDirection@129790, FaceTarget@129578, GetFacingDiff@1294b0, PreventFalling@1274f0, TargetIsInRange@128b28…]
  +0x028  w[4] R float [7: DoAvoiding@127a30, FaceDirection@129790, FaceTarget@129578, GetFacingDiff@1294b0, PreventFalling@1274f0, TargetIsInRange@128b28…]
  +0x030  w[4, 8] LEA/R float addr-taken [13: DoAvoiding@127a30, DoLeaning@1284e0, FaceTarget@129578, FindClosestCar@129c88, FindPointAgainstWall@12a4a0, GetFacingDiff@1294b0…]
  +0x034  w[4] R float [1: CheckVehicleThreat@129d80]
  +0x038  w[4] R float [13: DoAvoiding@127a30, DoLeaning@1284e0, FaceTarget@129578, FindClosestCar@129c88, FindPointAgainstWall@12a4a0, GetFacingDiff@1294b0…]
  +0x04c  w[4] R/W [5: HandleInterrupts@128cd8, KillSelf@126ad8, MaintainHeading@1290b8, SetNewState@12aa20, UpdateRotPos@128788]
  +0x050  w[4] R/W [10: DoAvoiding@127a30, DoDead@128488, DoDodging@127d58, DoDying@1280c8, DoIdling@127798, DoLeaning@1284e0…]
  +0x054  w[4] W [2: PlayAnim@12a8f0, PlayCrossFade@12a988]
  +0x060  w[4] R/W -> AICharacterPedestrian::GetRandomConvAnim, AICharacterPedestrian::GetSpecialPedInfo [8: DoAvoiding@127a30, DoDodging@127d58, DoDying@1280c8, DoIdling@127798, DoLeaning@1284e0, DoTurning@1282e0…]
  +0x078  w[4] R [2: PlayAnim@12a8f0, PlayCrossFade@12a988]
  +0x098  w[4] R/W [20: DoAiming@128860, DoAvoiding@127a30, DoDead@128488, DoDodging@127d58, DoDying@1280c8, DoIdling@127798…]
  +0x0a0  w[4] R/W [2: PlayAnim@12a8f0, PlayCrossFade@12a988]
  +0x140  w[4] R/W [6: DoAiming@128860, FaceDirection@129790, FaceTarget@129578, HandleInterrupts@128cd8, Recycle@126c98, ~AICharacterPedestrian@125b48]
  +0x150  w[4] R -> AICharacterPedestrian::SetBuddy [5: FindPointAgainstWall@12a4a0, HandleInterrupts@128cd8, KillSelf@126ad8, Recycle@126c98, UpdateClosestPed@1299d0]
  +0x154  w[4] R -> WWorldPos::FindClosestFace [1: PreventFalling@1274f0]
  +0x158  w[4] R/W -> WCollider::~WCollider [2: FindPointAgainstWall@12a4a0, ~AICharacterPedestrian@125b48]
  +0x15c  w[4] W [1: SetNewState@12aa20]
  +0x160  w[1] R/W [10: DoAvoiding@127a30, DoDodging@127d58, DoIdling@127798, DoLeaning@1284e0, DoStartled@127908, DoWalking@127840…]
  +0x161  w[1] R/W [1: DoDodging@127d58]
  +0x164  w[4] R/W [1: KillSelf@126ad8]
  +0x168  w[4] R/W [1: Recycle@126c98]
  +0x16c  w[4] R/W -> AICharacterPedestrian::PlayAnim [7: DoDying@1280c8, DoIdling@127798, DoNeutral@127708, HandleInterrupts@128cd8, PlayAnim@12a8f0, PlayCrossFade@12a988…]
  +0x170  w[4] R/W [2: HandleInterrupts@128cd8, UpdateClosestPed@1299d0]
  +0x174  w[4] R/W float [2: HandleInterrupts@128cd8, UpdateClosestPed@1299d0]
  +0x178  w[4] W [1: UpdateClosestPed@1299d0]
  +0x17c  w[4] R/W float [2: HandleInterrupts@128cd8, UpdateClosestPed@1299d0]
  +0x180  w[8] LEA/W addr-taken [5: DoAvoiding@127a30, DoDodging@127d58, DoLeaning@1284e0, DoStartled@127908, Recycle@126c98]
  +0x188  w[4] W [1: Recycle@126c98]
  +0x190  w[8] W [1: Recycle@126c98]
  +0x198  w[4] W [1: Recycle@126c98]
  +0x1a0  w[4, 8] LEA/R/W float addr-taken [3: FaceTarget@129578, ReachedTarget@129920, SetTarget@129098]
  +0x1a8  w[4] R/W float [3: FaceTarget@129578, ReachedTarget@129920, SetTarget@129098]
  +0x1ac  w[4] R/W [2: DoAvoiding@127a30, SetNewState@12aa20]
  +0x1b0  w[4] R/W [4: DoAvoiding@127a30, DoDodging@127d58, PlayAnim@12a8f0, PlayCrossFade@12a988]
  +0x1b4  w[4] R/W float [2: UpdateRotPos@128788, UpdateThreatRelativeDirection@12a398]
  +0x1b8  w[4] W float [1: UpdateRotPos@128788]
  +0x1bc  w[4] R/W float [3: DoLeaning@1284e0, KillSelf@126ad8, ThreatenedByVehicle@129bc8]
  +0x1c0  w[4] W float [1: KillSelf@126ad8]
  +0x1c4  w[4] R/W [3: MaintainHeading@1290b8, Recycle@126c98, UpdateClosestPed@1299d0]
  +0x1c8  w[4] R/W [4: KillSelf@126ad8, MaintainHeading@1290b8, Recycle@126c98, UpdateClosestPed@1299d0]
  +0x1cc  w[4] R [1: UpdateThreatRelativeDirection@12a398]
  +0x1d0  w[4] R -> WRoadNav::IncNavPosition, WRoadNetwork::Get [4: DoDodging@127d58, GetWalkToNav@12acc0, MaintainHeading@1290b8, Recycle@126c98]
  +0x1d4  w[4] R/W -> AnimationController::GetCurrentFrame [2: DoLeaning@1284e0, PlayAnim@12a8f0]
  +0x1d8  w[4] R float [2: PlayAnim@12a8f0, UpdateAnimSpeed@129868]
  +0x1dc  w[4] W float [1: UpdateAnimSpeed@129868]
  +0x1e0  w[8] W [1: FindPointAgainstWall@12a4a0]
  +0x1e8  w[4] W [1: FindPointAgainstWall@12a4a0]
  +0x1f0  w[4, 8] R/W float [2: DoLeaning@1284e0, FindPointAgainstWall@12a4a0]
  +0x1f4  w[4] W [1: FindPointAgainstWall@12a4a0]
  +0x1f8  w[4] R/W float [2: DoLeaning@1284e0, FindPointAgainstWall@12a4a0]
  +0x1fc  w[4] R/W [1: DoLeaning@1284e0]
  +0x200  w[4] W [1: ThreatenedByVehicle@129bc8]
  +0x204  w[4] R/W [2: Recycle@126c98, ThreatenedByVehicle@129bc8]
  +0x208  w[4] R/W [1: ThreatenedByVehicle@129bc8]
  +0x20c  w[4] R/W [4: DoTurning@1282e0, Recycle@126c98, ThreatenedByVehicle@129bc8, UpdateAnimSpeed@129868]
  +0x210  w[4] R/W [2: CheckVehicleThreat@129d80, DoDodging@127d58]
  +0x214  w[4] LEA/R float addr-taken [1: CheckVehicleThreat@129d80]
  +0x218  w[4] LEA/R float addr-taken [2: CheckVehicleThreat@129d80, DoStartled@127908]
  +0x21c  w[4] R/W float [4: CheckVehicleThreat@129d80, DoDodging@127d58, ThreatenedByVehicle@129bc8, UpdateAnimSpeed@129868]
  +0x220  w[8] R/W [2: CheckVehicleThreat@129d80, SetNewTargetBasedOnThreat@127610]
  +0x228  w[4] R/W [2: CheckVehicleThreat@129d80, SetNewTargetBasedOnThreat@127610]
  +0x230  w[4, 8] R/W float [2: CheckVehicleThreat@129d80, SetNewTargetBasedOnThreat@127610]
  +0x234  w[4] R/W float [1: CheckVehicleThreat@129d80]
  +0x238  w[4] R/W float [2: CheckVehicleThreat@129d80, SetNewTargetBasedOnThreat@127610]
  +0x240  w[8] R/W [2: CheckVehicleThreat@129d80, SetNewTargetBasedOnThreat@127610]
  +0x248  w[4] R/W [2: CheckVehicleThreat@129d80, SetNewTargetBasedOnThreat@127610]
  +0x24c  w[4] R/W float [2: DoDodging@127d58, ThreatenedByVehicle@129bc8]
  +0x250  w[4] R/W [1: FindPointAgainstWall@12a4a0]
  +0x254  w[4] R/W [1: FindPointAgainstWall@12a4a0]
  +0x258  w[4] R/W float [2: FindPointAgainstWall@12a4a0, Recycle@126c98]
  +0x25c  w[4] R/W [6: DoDodging@127d58, DoLeaning@1284e0, DoStartled@127908, DoTurning@1282e0, HandleInterrupts@128cd8, Recycle@126c98]
  +0x260  w[1] R/W [4: DoAiming@128860, HandleInterrupts@128cd8, Recycle@126c98, TargetIsInRange@128b28]
  +0x270  w[8] R/W [3: HandleInterrupts@128cd8, Recycle@126c98, TargetIsInRange@128b28]
  +0x278  w[4] R/W [3: HandleInterrupts@128cd8, Recycle@126c98, TargetIsInRange@128b28]
  +0x27c  w[4] R/W [2: DoAiming@128860, HandleInterrupts@128cd8]
  +0x280  w[4] R/W [2: DoAiming@128860, HandleInterrupts@128cd8]
  +0x284  w[4] R/W float [2: DoAiming@128860, TargetIsInRange@128b28]
  +0x288  w[4] R/W float [1: DoAiming@128860]
  +0x28c  w[4] LEA/R float addr-taken [1: DoAiming@128860]
  +0x290  w[4] LEA/W float addr-taken [1: DoAiming@128860]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
