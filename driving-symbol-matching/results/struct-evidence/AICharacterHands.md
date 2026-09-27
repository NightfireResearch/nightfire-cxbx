# AICharacterHands

FastAlloc/constructed sizes under its tag: None
deleting destructor 0x228b0 frees/deletes with size 0x160 (call to UMemory::FastFree)
Xbox vtable 0x0018ad28 (36 slots) stored by its constructor
PS2 sheet virtual table row: ['AICharacterHands virtual table']

Xbox methods (22):
  0x227a0 undefined GetAimFraction(void)
  0x227c0 undefined __thiscall Shutdown(AICharacterHands * this)
  0x22820 undefined ~AICharacterHands(void)
  0x22830 undefined AbleToFire(void)
  0x22860 undefined GetWeapon(void)
  0x228b0 undefined scalar_deleting_destructor(undefined1 param_1)
  0x228e0 undefined GetRotPos(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0x22930 undefined __stdcall Init(void)
  0x22c30 undefined ShowHands(void)
  0x22c50 undefined HideHands(void)
  0x22cc0 undefined ChangeWeapon(undefined4 param_1)
  0x22dd0 undefined Fire(void)
  0x22e20 undefined Reload(void)
  0x22e70 undefined DoNeutral(void)
  0x22f10 undefined DoIdling(void)
  0x22f70 undefined DoArming(void)
  0x22fb0 undefined DoArmed(void)
  0x230a0 undefined DoAiming(void)
  0x23170 undefined DoFiring(void)
  0x23230 undefined DoReloading(void)
  0x23280 undefined DoUnarming(void)
  0x232b0 undefined UpdateRotPos(void)

PS2 methods (28):
  0x1237d0 AICharacterHands::GetAimFraction
  0x1237f0 AICharacterHands::GetRotPos
  0x123878 AICharacterHands::GetRotPos
  0x123950 AICharacterHands::Init
  0x123d90 AICharacterHands::Shutdown
  0x123e28 AICharacterHands::AICharacterHands
  0x123ea0 AICharacterHands::~AICharacterHands
  0x123ef8 AICharacterHands::ShowHands
  0x123f10 AICharacterHands::HideHands
  0x123f90 AICharacterHands::ChangeWeapon
  0x1240c8 AICharacterHands::Fire
  0x124170 AICharacterHands::Reload
  0x1241e0 AICharacterHands::AbleToFire
  0x124230 AICharacterHands::GetWeapon
  0x124238 AICharacterHands::DoInitial
  0x124240 AICharacterHands::DoNeutral
  0x124348 AICharacterHands::DoIdling
  0x1243f0 AICharacterHands::DoArming
  0x124440 AICharacterHands::DoArmed
  0x124570 AICharacterHands::DoAiming
  0x124650 AICharacterHands::DoFiring
  0x124770 AICharacterHands::DoReloading
  0x124800 AICharacterHands::DoUnarming
  0x124848 AICharacterHands::UpdateRotPos
  0x124f30 AICharacterHands::SetZoomButton
  0x124f40 AICharacterHands::Get
  0x124f50 AICharacterHands::AreVisible
  0x124f58 AICharacterHands::fgHandObj_global_ctors

Sheet rows:
  AICharacterHands::GetAimFraction(void)
  AICharacterHands::GetRotPos(int, MATRIX4 &)
  AICharacterHands::GetRotPos(int, MATRIX4 &, COORD3 &)
  AICharacterHands::Init(void)
  AICharacterHands::Shutdown(void)
  AICharacterHands::AICharacterHands(int)
  AICharacterHands::~AICharacterHands(void)
  AICharacterHands::ShowHands(void)
  AICharacterHands::HideHands(void)
  AICharacterHands::ChangeWeapon(int)
  AICharacterHands::Fire(void)
  AICharacterHands::Reload(void)
  AICharacterHands::AbleToFire(void)
  AICharacterHands::GetWeapon(void)
  AICharacterHands::DoInitial(void)
  AICharacterHands::DoNeutral(void)
  AICharacterHands::DoIdling(void)
  AICharacterHands::DoArming(void)
  AICharacterHands::DoArmed(void)
  AICharacterHands::DoAiming(void)
  AICharacterHands::DoFiring(void)
  AICharacterHands::DoReloading(void)
  AICharacterHands::DoUnarming(void)
  AICharacterHands::UpdateRotPos(void)
  AICharacterHands type_info function
  AICharacterHands::SetZoomButton(bool)
  AICharacterHands::Get(void)
  AICharacterHands::AreVisible(void)
  AICharacterHands::fgHandObj
  AICharacterHands::fgHumanObj
  AICharacterHands::fgInitRotPos
  AICharacterHands::fgActor
  AICharacterHands::fgZoomButton
  AICharacterHands::fgIsZoomed
  AICharacterHands virtual table
  AICharacterHands::fgWeaponSide
  AICharacterHands type_info node

Xbox methods treated as members (20 of 22; untyped ones count when ECX is read before it is written): AbleToFire, ChangeWeapon, DoAiming, DoArmed, DoArming, DoFiring, DoIdling, DoNeutral, DoReloading, DoUnarming, Fire, GetAimFraction, GetWeapon, HideHands, Reload, ShowHands, Shutdown, UpdateRotPos, scalar_deleting_destructor, ~AICharacterHands

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W [4: DoFiring@23170, DoNeutral@22e70, DoReloading@23230, ~AICharacterHands@22820]
  +0x05c  w[4] R/W [10: AbleToFire@22830, ChangeWeapon@22cc0, DoAiming@230a0, DoArmed@22fb0, DoArming@22f70, DoNeutral@22e70…]
  +0x060  w[4] R/W [3: ChangeWeapon@22cc0, DoNeutral@22e70, HideHands@22c50]
  +0x064  w[4] W [5: ChangeWeapon@22cc0, DoArmed@22fb0, DoNeutral@22e70, Fire@22dd0, Reload@22e20]
  +0x068  w[4] R/W [10: ChangeWeapon@22cc0, DoArmed@22fb0, DoFiring@23170, DoNeutral@22e70, DoReloading@23230, DoUnarming@23280…]
  +0x088  w[4] R/W [5: ChangeWeapon@22cc0, DoArmed@22fb0, DoNeutral@22e70, Fire@22dd0, Reload@22e20]
  +0x0a8  w[4] R/W [10: ChangeWeapon@22cc0, DoAiming@230a0, DoArmed@22fb0, DoArming@22f70, DoNeutral@22e70, DoUnarming@23280…]
  +0x0ac  w[4] R/W -> AnimationController::GetCurrentFrame, AnimationController::GetRemainingTime, AnimationController::GetTotalTime [4: DoAiming@230a0, DoArmed@22fb0, DoFiring@23170, Fire@22dd0]
  +0x140  w[1] R/W [3: AbleToFire@22830, DoIdling@22f10, Reload@22e20]
  +0x141  w[1] R [2: DoNeutral@22e70, HideHands@22c50]
  +0x144  w[4] R/W [4: ChangeWeapon@22cc0, DoArming@22f70, DoUnarming@23280, HideHands@22c50]
  +0x14c  w[4] W float [1: DoAiming@230a0]
  +0x150  w[1] R/W [4: DoArmed@22fb0, DoNeutral@22e70, HideHands@22c50, ShowHands@22c30]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[8] R/W [3: GetRotPos@1237f0, GetRotPos@123878, UpdateRotPos@124848]
  +0x008  w[8] R/W [3: GetRotPos@1237f0, GetRotPos@123878, UpdateRotPos@124848]
  +0x010  w[8] R/W [3: GetRotPos@1237f0, GetRotPos@123878, UpdateRotPos@124848]
  +0x018  w[8] R/W [3: GetRotPos@1237f0, GetRotPos@123878, UpdateRotPos@124848]
  +0x020  w[8] R/W [3: GetRotPos@1237f0, GetRotPos@123878, UpdateRotPos@124848]
  +0x028  w[8] R/W [3: GetRotPos@1237f0, GetRotPos@123878, UpdateRotPos@124848]
  +0x030  w[4, 8] LEA/R/W float addr-taken [3: GetRotPos@1237f0, GetRotPos@123878, UpdateRotPos@124848]
  +0x038  w[4, 8] R/W [3: GetRotPos@1237f0, GetRotPos@123878, UpdateRotPos@124848]
  +0x04c  w[4] R/W [13: AbleToFire@1241e0, ChangeWeapon@123f90, DoAiming@124570, DoArmed@124440, DoArming@1243f0, DoFiring@124650…]
  +0x050  w[4] R/W [3: ChangeWeapon@123f90, DoNeutral@124240, HideHands@123f10]
  +0x054  w[4] W [6: ChangeWeapon@123f90, DoArmed@124440, DoIdling@124348, DoNeutral@124240, Fire@1240c8, Reload@124170]
  +0x058  w[4] R/W [13: AICharacterHands@123e28, ChangeWeapon@123f90, DoArmed@124440, DoFiring@124650, DoIdling@124348, DoNeutral@124240…]
  +0x078  w[4] R/W [7: AICharacterHands@123e28, ChangeWeapon@123f90, DoArmed@124440, DoIdling@124348, DoNeutral@124240, Fire@1240c8…]
  +0x098  w[4] R/W [13: ChangeWeapon@123f90, DoAiming@124570, DoArmed@124440, DoArming@1243f0, DoFiring@124650, DoIdling@124348…]
  +0x09c  w[4] R/W -> AnimationController::GetTotalTime [4: DoAiming@124570, DoArmed@124440, DoFiring@124650, Fire@1240c8]
  +0x140  w[4] R/W [6: AICharacterHands@123e28, DoFiring@124650, DoIdling@124348, DoNeutral@124240, DoReloading@124770, ~AICharacterHands@123ea0]
  +0x150  w[4] R/W [5: AICharacterHands@123e28, AbleToFire@1241e0, DoFiring@124650, DoIdling@124348, Reload@124170]
  +0x154  w[4] R/W [4: AICharacterHands@123e28, ChangeWeapon@123f90, DoNeutral@124240, HideHands@123f10]
  +0x158  w[4] R/W [5: AICharacterHands@123e28, ChangeWeapon@123f90, DoArming@1243f0, DoUnarming@124800, HideHands@123f10]
  +0x15c  w[4] R/W float [2: AICharacterHands@123e28, UpdateRotPos@124848]
  +0x160  w[4] W float [2: AICharacterHands@123e28, DoAiming@124570]
  +0x164  w[4] R/W [7: AICharacterHands@123e28, AreVisible@124f50, ChangeWeapon@123f90, DoArmed@124440, DoNeutral@124240, HideHands@123f10…]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  ~AICharacterHands: W +0x0 w4
  GetWeapon: R +0x68 w4
