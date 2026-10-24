# SWeaponManager

FastAlloc/constructed sizes under its tag: {'allocated': [], 'constructed': [384]}
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0xbb220 first calls: ['__builtin_vec_new', 'UMemory::FastAlloc', 'ActionQueue::ActionQueue']

Xbox methods (20):
  0xba7f0 undefined SortWeaponList(void)
  0xba990 undefined SetPlayerCar(undefined4 param_1)
  0xba9f0 void __thiscall LoadWeapon(SWeaponManager * this, int param_1, int param_2, int param_3)
  0xbaa70 undefined UnloadWeapon(undefined4 param_1)
  0xbaab0 undefined SetSecondary(undefined4 param_1)
  0xbaad0 undefined GetPreviousSecondaryType(void)
  0xbab20 undefined GetNextSecondaryType(void)
  0xbab80 undefined DoWeaponAnim(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0xbabb0 undefined GetWeaponFromSystem(undefined4 param_1)
  0xbabf0 undefined UserReloading(void)
  0xbad00 undefined StartNuclearSequence(undefined4 param_1, undefined4 param_2)
  0xbad40 void __thiscall ~SWeaponManager(SWeaponManager * this)
  0xbad70 void __thiscall Reset(SWeaponManager * this)
  0xbae50 undefined SetSpecialWeapon(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0xbaee0 undefined SelectSecondary(undefined1 param_1)
  0xbaf60 undefined UpdateTurret(void)
  0xbb220 SWeaponManager * __thiscall SWeaponManager(SWeaponManager * this)
  0xbb2b0 undefined FireWeapon(undefined4 param_1)
  0xbb9b0 undefined HandleNuclearSequence(void)
  0xbbdc0 void __thiscall Update(SWeaponManager * this)

PS2 methods (34):
  0x20bb00 SWeaponManager::SWeaponManager
  0x20bb78 SWeaponManager::~SWeaponManager
  0x20bbe0 SWeaponManager::Reset
  0x20bd80 SWeaponManager::CalcWeaponPriorities
  0x20bdf8 SWeaponManager::SortWeaponList
  0x20bf20 SWeaponManager::FindSortedSecondary
  0x20bf80 SWeaponManager::SetPlayerCar
  0x20bff0 SWeaponManager::SetSpecialWeapon
  0x20c078 SWeaponManager::Update
  0x20d378 SWeaponManager::LoadWeapon
  0x20d420 SWeaponManager::UnloadWeapon
  0x20d480 SWeaponManager::SetPrimary
  0x20d488 SWeaponManager::SetSecondary
  0x20d4b8 SWeaponManager::EnableAuto
  0x20d4c0 SWeaponManager::SelectSecondary
  0x20d598 SWeaponManager::GetPreviousSecondaryType
  0x20d608 SWeaponManager::GetNextSecondaryType
  0x20d678 SWeaponManager::FirePrimary
  0x20d6b0 SWeaponManager::FireSecondary
  0x20d700 SWeaponManager::FireSpecial
  0x20d740 SWeaponManager::DoWeaponAnim
  0x20d788 SWeaponManager::FireEMP
  0x20d980 SWeaponManager::UseRadarDish
  0x20d9a8 SWeaponManager::UpdateRadarDish
  0x20d9f0 SWeaponManager::UpdateTurret
  0x20de20 SWeaponManager::FireWeapon
  0x20e678 SWeaponManager::GetWeaponFromSystem
  0x20e6f0 SWeaponManager::GetSystemFromWeapon
  0x20e710 SWeaponManager::GetAnimHandle
  0x20e868 SWeaponManager::UserReloading
  0x20e978 SWeaponManager::HandleClipUpdate
  0x20ea20 SWeaponManager::StartNuclearSequence
  0x20ea60 SWeaponManager::HandleNuclearSequence
  0x210460 SWeaponManager::fWeaponSortOrder_global_ctors

Sheet rows:
  SWeaponManager::SWeaponManager(void)
  SWeaponManager::~SWeaponManager(void)
  SWeaponManager::Reset(void)
  SWeaponManager::CalcWeaponPriorities(void)
  SWeaponManager::SortWeaponList(void)
  SWeaponManager::FindSortedSecondary(void)
  SWeaponManager::SetPlayerCar(RSceneObj *)
  SWeaponManager::SetSpecialWeapon(int, CARP::Trigger *, float)
  SWeaponManager::Update(void)
  SWeaponManager::LoadWeapon(WeaponType, int, int, int)
  SWeaponManager::UnloadWeapon(WeaponType)
  SWeaponManager::SetPrimary(WeaponType)
  SWeaponManager::SetSecondary(WeaponType)
  SWeaponManager::EnableAuto(WeaponAuto)
  SWeaponManager::SelectSecondary(bool)
  SWeaponManager::GetPreviousSecondaryType(void)
  SWeaponManager::GetNextSecondaryType(void)
  SWeaponManager::FirePrimary(void)
  SWeaponManager::FireSecondary(void)
  SWeaponManager::FireSpecial(void)
  SWeaponManager::DoWeaponAnim(WeaponType, int, unsigned int)
  SWeaponManager::FireEMP(float)
  SWeaponManager::UseRadarDish(void)
  SWeaponManager::UpdateRadarDish(void)
  SWeaponManager::UpdateTurret(void)
  SWeaponManager::FireWeapon(WeaponType)
  SWeaponManager::GetWeaponFromSystem(int)
  SWeaponManager::GetSystemFromWeapon(WeaponType, int) const
  SWeaponManager::GetAnimHandle(void) const
  SWeaponManager::UserReloading(void)
  SWeaponManager::HandleClipUpdate(void)
  SWeaponManager::StartNuclearSequence(COORD3 &, float)
  SWeaponManager::HandleNuclearSequence(void)
  SWeaponManager::fWeaponSortOrder

Xbox methods treated as members (19 of 20; untyped ones count when ECX is read before it is written): DoWeaponAnim, FireWeapon, GetNextSecondaryType, GetPreviousSecondaryType, HandleNuclearSequence, LoadWeapon, Reset, SWeaponManager, SelectSecondary, SetPlayerCar, SetSecondary, SetSpecialWeapon, SortWeaponList, StartNuclearSequence, UnloadWeapon, Update, UpdateTurret, UserReloading, ~SWeaponManager

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W [2: Reset@bad70, Update@bbdc0]
  +0x004  w[4] R/W [8: Reset@bad70, SelectSecondary@baee0, SetSecondary@baab0, SetSpecialWeapon@bae50, SortWeaponList@ba7f0, UnloadWeapon@baa70…]
  +0x008  w[4] R/W [5: Reset@bad70, SetSpecialWeapon@bae50, SortWeaponList@ba7f0, UnloadWeapon@baa70, Update@bbdc0]
  +0x00c  w[4] R/W [2: Reset@bad70, Update@bbdc0]
  +0x010  w[4] R/W [12: FireWeapon@bb2b0, LoadWeapon@ba9f0, Reset@bad70, SWeaponManager@bb220, SelectSecondary@baee0, SetPlayerCar@ba990…]
  +0x014  w[4] R/W [5: DoWeaponAnim@bab80, FireWeapon@bb2b0, SWeaponManager@bb220, SetPlayerCar@ba990, Update@bbdc0]
  +0x018  w[4] W [2: FireWeapon@bb2b0, Reset@bad70]
  +0x01c  w[4] W [2: FireWeapon@bb2b0, Reset@bad70]
  +0x020  w[4] R/W -> ActionQueue::Flush, ActionQueue::GetAction, ActionQueue::IsEmpty, ActionQueue::PopAction, ActionQueue::~ActionQueue [4: Reset@bad70, SWeaponManager@bb220, Update@bbdc0, ~SWeaponManager@bad40]
  +0x024  w[1] R/W [2: Reset@bad70, Update@bbdc0]
  +0x025  w[1] R/W [2: Reset@bad70, Update@bbdc0]
  +0x026  w[1] R/W [2: Reset@bad70, Update@bbdc0]
  +0x027  w[1] W [3: FireWeapon@bb2b0, Reset@bad70, Update@bbdc0]
  +0x028  w[1] R/W [4: Reset@bad70, SelectSecondary@baee0, Update@bbdc0, UserReloading@babf0]
  +0x029  w[1] W [1: Reset@bad70]
  +0x030  w[1] W [1: Reset@bad70]
  +0x034  w[4] W [2: FireWeapon@bb2b0, Reset@bad70]
  +0x038  w[4] W [1: Reset@bad70]
  +0x03c  w[1] R/W [2: FireWeapon@bb2b0, Reset@bad70]
  +0x040  w[4] R/W [3: FireWeapon@bb2b0, Reset@bad70, Update@bbdc0]
  +0x044  w[4] R/W [3: HandleNuclearSequence@bb9b0, Reset@bad70, StartNuclearSequence@bad00]
  +0x050  w- LEA addr-taken [2: HandleNuclearSequence@bb9b0, StartNuclearSequence@bad00]
  +0x05c  w[4] R/W [3: HandleNuclearSequence@bb9b0, Reset@bad70, StartNuclearSequence@bad00]
  +0x060  w[4] W float [3: HandleNuclearSequence@bb9b0, Reset@bad70, StartNuclearSequence@bad00]
  +0x064  w[4] W [1: Reset@bad70]
  +0x068  w[4] W [1: Reset@bad70]
  +0x070  w[4] R/W [3: Reset@bad70, SetSpecialWeapon@bae50, Update@bbdc0]
  +0x074  w[4] W float [3: Reset@bad70, SetSpecialWeapon@bae50, Update@bbdc0]
  +0x078  w- LEA addr-taken [1: Reset@bad70]
  +0x0f8  w- LEA addr-taken [1: SortWeaponList@ba7f0]
  +0x178  w[4] R/W [3: GetNextSecondaryType@bab20, GetPreviousSecondaryType@baad0, SortWeaponList@ba7f0]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4, 8] R/W [4: HandleNuclearSequence@20ea60, Reset@20bbe0, SetPrimary@20d480, Update@20c078]
  +0x004  w[4] R/W [12: FindSortedSecondary@20bf20, FireSecondary@20d6b0, HandleClipUpdate@20e978, Reset@20bbe0, SelectSecondary@20d4c0, SetSecondary@20d488…]
  +0x008  w[4] R/W [7: FireSpecial@20d700, HandleNuclearSequence@20ea60, Reset@20bbe0, SetSpecialWeapon@20bff0, SortWeaponList@20bdf8, UnloadWeapon@20d420…]
  +0x00c  w[4] R/W [2: Reset@20bbe0, Update@20c078]
  +0x010  w[4] R/W -> __builtin_vec_delete [13: FireWeapon@20de20, HandleClipUpdate@20e978, LoadWeapon@20d378, Reset@20bbe0, SWeaponManager@20bb00, SelectSecondary@20d4c0…]
  +0x014  w[4] R/W [8: DoWeaponAnim@20d740, FireWeapon@20de20, GetAnimHandle@20e710, SWeaponManager@20bb00, SetPlayerCar@20bf80, Update@20c078…]
  +0x018  w[4] R/W [3: FireWeapon@20de20, Reset@20bbe0, Update@20c078]
  +0x01c  w[4] W [2: FireWeapon@20de20, Reset@20bbe0]
  +0x020  w[4] R/W -> ActionQueue::IsEmpty, ActionQueue::~ActionQueue [4: Reset@20bbe0, SWeaponManager@20bb00, Update@20c078, ~SWeaponManager@20bb78]
  +0x024  w[4] R/W [2: Reset@20bbe0, Update@20c078]
  +0x028  w[4] R/W [2: Reset@20bbe0, Update@20c078]
  +0x02c  w[4] R/W [2: Reset@20bbe0, Update@20c078]
  +0x030  w[4] W [3: FireWeapon@20de20, Reset@20bbe0, Update@20c078]
  +0x034  w[4] R/W [5: HandleClipUpdate@20e978, Reset@20bbe0, SelectSecondary@20d4c0, Update@20c078, UserReloading@20e868]
  +0x038  w[4] W [1: Reset@20bbe0]
  +0x040  w[4] W [2: FireEMP@20d788, Reset@20bbe0]
  +0x044  w[4] R/W float [3: FireWeapon@20de20, Reset@20bbe0, Update@20c078]
  +0x048  w[4] W [1: Reset@20bbe0]
  +0x04c  w[4] R/W [2: FireWeapon@20de20, Reset@20bbe0]
  +0x050  w[4] R/W [4: FireWeapon@20de20, Reset@20bbe0, SelectSecondary@20d4c0, Update@20c078]
  +0x054  w[4] R/W [3: HandleNuclearSequence@20ea60, Reset@20bbe0, StartNuclearSequence@20ea20]
  +0x060  w[4, 8] LEA/R/W float addr-taken [2: HandleNuclearSequence@20ea60, StartNuclearSequence@20ea20]
  +0x068  w[4] W [1: StartNuclearSequence@20ea20]
  +0x06c  w[4] R/W [3: HandleNuclearSequence@20ea60, Reset@20bbe0, StartNuclearSequence@20ea20]
  +0x070  w[4] R/W float [3: HandleNuclearSequence@20ea60, Reset@20bbe0, StartNuclearSequence@20ea20]
  +0x074  w[4] R/W [2: Reset@20bbe0, Update@20c078]
  +0x078  w[4] R/W float [2: Reset@20bbe0, UpdateTurret@20d9f0]
  +0x07c  w[1] W [1: UpdateTurret@20d9f0]
  +0x080  w[4] R/W [3: Reset@20bbe0, SetSpecialWeapon@20bff0, Update@20c078]
  +0x084  w[4] R/W float [3: Reset@20bbe0, SetSpecialWeapon@20bff0, Update@20c078]
  +0x088  w- LEA addr-taken [2: CalcWeaponPriorities@20bd80, SortWeaponList@20bdf8]
  +0x108  w[4] LEA/R addr-taken [2: FindSortedSecondary@20bf20, SortWeaponList@20bdf8]
  +0x188  w[4] R/W [4: FindSortedSecondary@20bf20, GetNextSecondaryType@20d608, GetPreviousSecondaryType@20d598, SortWeaponList@20bdf8]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  SetSecondary: W +0x4 w4
