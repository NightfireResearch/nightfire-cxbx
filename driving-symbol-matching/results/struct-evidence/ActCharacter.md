# ActCharacter

FastAlloc/constructed sizes under its tag: {'allocated': [132], 'constructed': []}
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0x150a0 first calls: ['__builtin_new', 'ActCharacter::ChangeCharacter', 'ActCharacter::ChangeCharacter']

Xbox methods (21):
  0x14490 undefined LoadCharacter(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0x14580 undefined ShowWeapon(undefined4 param_1)
  0x145b0 undefined HideWeapon(undefined4 param_1)
  0x145e0 undefined SetWeaponsTransformToWorldSpace(undefined4 param_1, undefined4 param_2)
  0x14610 undefined SetWeaponVelocity(undefined4 param_1)
  0x14680 undefined StopUsingResources(void)
  0x14700 undefined ~ActCharacter(void)
  0x14740 undefined PlayEvent(undefined4 param_1, undefined4 param_2)
  0x14760 undefined StartShadow(void)
  0x147d0 undefined EndShadow(void)
  0x147f0 undefined SpawnWeapon(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, undefine
  0x14820 undefined DrawWeapon(undefined4 param_1)
  0x14850 undefined SetWeaponBone(undefined4 param_1, undefined4 param_2)
  0x14870 undefined GetShadowTriangle(void)
  0x14a50 undefined ChangeCharacter(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0x14bf0 undefined SetAlpha(undefined4 param_1)
  0x14c20 void __thiscall GetScaleFactors(ActCharacter * this, float * param_1, float * param_2)
  0x14c50 undefined Draw(undefined4 param_1, undefined4 param_2)
  0x15010 undefined CalculateMuzzleFlashIntensity(undefined4 param_1)
  0x150a0 undefined ActCharacter(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined1 param_4, undefin
  0x15180 undefined InheritWeaponLightingFromCar(void)

PS2 methods (24):
  0x10cb88 ActCharacter::LoadCharacter
  0x10cc60 ActCharacter::ActCharacter
  0x10cd78 ActCharacter::ShowWeapon
  0x10cdc8 ActCharacter::HideWeapon
  0x10ce20 ActCharacter::ChangeCharacter
  0x10cfe8 ActCharacter::SetWeaponsTransformToWorldSpace
  0x10d048 ActCharacter::SetWeaponVelocity
  0x10d0d0 ActCharacter::StopUsingResources
  0x10d188 ActCharacter::~ActCharacter
  0x10d1f0 ActCharacter::SetAlpha
  0x10d238 ActCharacter::GetAlpha
  0x10d258 ActCharacter::PlayEvent
  0x10d288 ActCharacter::StartShadow
  0x10d290 ActCharacter::EndShadow
  0x10d298 ActCharacter::GetScaleFactors
  0x10d308 ActCharacter::GetBaseScaleFactor
  0x10d320 ActCharacter::Draw
  0x10da98 ActCharacter::SpawnWeapon
  0x10dad0 ActCharacter::DrawWeapon
  0x10db30 ActCharacter::CalculateMuzzleFlashIntensity
  0x10dc00 ActCharacter::SetWeaponBone
  0x10dc30 ActCharacter::InheritWeaponLightingFromCar
  0x10dd00 ActCharacter::GetShadowTriangle
  0x10de78 ActCharacter::LoadCharacter_global_ctors

Sheet rows:
  ActCharacter::LoadCharacter(char *, char *, char *)
  ActCharacter::ActCharacter(ActModelDatabase *, ActTextureDataba
  ActCharacter::ShowWeapon(int)
  ActCharacter::HideWeapon(int)
  ActCharacter::ChangeCharacter(char *, char *, char *, unsigned
  ActCharacter::SetWeaponsTransformToWorldSpace(MATRIX4 &, MATRIX
  ActCharacter::SetWeaponVelocity(COORD3 &)
  ActCharacter::StopUsingResources(void)
  ActCharacter::~ActCharacter(void)
  ActCharacter::SetAlpha(float)
  ActCharacter::GetAlpha(void)
  ActCharacter::PlayEvent(int, unsigned int)
  ActCharacter::StartShadow(void)
  ActCharacter::EndShadow(void)
  ActCharacter::GetScaleFactors(float &, float &)
  ActCharacter::GetBaseScaleFactor(void)
  ActCharacter::Draw(MATRIX4 &, MATRIX4 &)
  ActCharacter::SpawnWeapon(int, COORD3 &, COORD3 &, COORD3 &, CO
  ActCharacter::DrawWeapon(int, EAGL::ViewPort *)
  ActCharacter::CalculateMuzzleFlashIntensity(int)
  ActCharacter::SetWeaponBone(int, MATRIX4 &)
  ActCharacter::InheritWeaponLightingFromCar(void)
  ActCharacter::GetShadowTriangle(void)

Xbox methods treated as members (18 of 21; untyped ones count when ECX is read before it is written): ActCharacter, CalculateMuzzleFlashIntensity, ChangeCharacter, Draw, DrawWeapon, GetScaleFactors, GetShadowTriangle, HideWeapon, InheritWeaponLightingFromCar, PlayEvent, SetAlpha, SetWeaponBone, SetWeaponVelocity, SetWeaponsTransformToWorldSpace, ShowWeapon, SpawnWeapon, StopUsingResources, ~ActCharacter

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R -> ActModelDatabase::UseModel, dummyNullFunction [2: ChangeCharacter@14a50, StopUsingResources@14680]
  +0x004  w[4] R -> ActTextureDatabase::UseTexture, dummyNullFunction [2: ChangeCharacter@14a50, StopUsingResources@14680]
  +0x008  w[4] R -> ActWeaponDatabase::StopUsingWeapon, ActWeaponDatabase::UseWeapon [2: ChangeCharacter@14a50, StopUsingResources@14680]
  +0x00c  w[4] R/W -> ActModel::SetTexture [2: ChangeCharacter@14a50, Draw@14c50]
  +0x010  w[4] R/W [2: ChangeCharacter@14a50, Draw@14c50]
  +0x014  w[4] R/W [2: ChangeCharacter@14a50, Draw@14c50]
  +0x018  w[4] R/W [2: ChangeCharacter@14a50, Draw@14c50]
  +0x01c  w[4] R/W [2: ChangeCharacter@14a50, Draw@14c50]
  +0x020  w[4] R/W -> ActCharacterInfo::~ActCharacterInfo [5: ActCharacter@150a0, ChangeCharacter@14a50, GetScaleFactors@14c20, StopUsingResources@14680, ~ActCharacter@14700]
  +0x024  w[4] R/W -> ActWeapon::RenderShellCasings [6: ChangeCharacter@14a50, DrawWeapon@14820, InheritWeaponLightingFromCar@15180, SetWeaponVelocity@14610, SetWeaponsTransformToWorldSpace@145e0, StopUsingResources@14680]
  +0x028  w[4] R/W -> ActWeapon::SetTransformToWorldSpace [5: ChangeCharacter@14a50, InheritWeaponLightingFromCar@15180, SetWeaponVelocity@14610, SetWeaponsTransformToWorldSpace@145e0, StopUsingResources@14680]
  +0x02c  w- LEA addr-taken [2: ChangeCharacter@14a50, StopUsingResources@14680]
  +0x04a  w- LEA addr-taken -> ActWeaponDatabase::StopUsingWeapon [2: ChangeCharacter@14a50, StopUsingResources@14680]
  +0x068  w[4] R [4: Draw@14c50, GetShadowTriangle@14870, SetAlpha@14bf0, ~ActCharacter@14700]
  +0x06c  w[1] R [1: Draw@14c50]
  +0x070  w[4] LEA/R/W float addr-taken [2: CalculateMuzzleFlashIntensity@15010, Draw@14c50]
  +0x074  w- LEA addr-taken [1: Draw@14c50]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] R/W -> ActModelDatabase::UseModel [3: ActCharacter@10cc60, ChangeCharacter@10ce20, StopUsingResources@10d0d0]
  +0x004  w[4] R/W -> ActTextureDatabase::StopUsingTexture, ActTextureDatabase::UseTexture [3: ActCharacter@10cc60, ChangeCharacter@10ce20, StopUsingResources@10d0d0]
  +0x008  w[4] R/W -> ActWeaponDatabase::StopUsingWeapon, ActWeaponDatabase::UseWeapon [3: ActCharacter@10cc60, ChangeCharacter@10ce20, StopUsingResources@10d0d0]
  +0x00c  w[4] R/W -> ActModel::SetTexture [2: ChangeCharacter@10ce20, Draw@10d320]
  +0x010  w[4] R/W [2: ChangeCharacter@10ce20, Draw@10d320]
  +0x014  w[4] R/W [2: ChangeCharacter@10ce20, Draw@10d320]
  +0x018  w[4] R/W [2: ChangeCharacter@10ce20, Draw@10d320]
  +0x01c  w[4] R/W [2: ChangeCharacter@10ce20, Draw@10d320]
  +0x020  w[4] R/W -> ActCharacterInfo::~ActCharacterInfo [5: ActCharacter@10cc60, ChangeCharacter@10ce20, GetScaleFactors@10d298, StopUsingResources@10d0d0, ~ActCharacter@10d188]
  +0x024  w[4] LEA/R/W addr-taken -> ActWeapon::SetTransformToWorldSpace [7: CalculateMuzzleFlashIntensity@10db30, ChangeCharacter@10ce20, DrawWeapon@10dad0, InheritWeaponLightingFromCar@10dc30, SetWeaponVelocity@10d048, SetWeaponsTransformToWorldSpace@10cfe8…]
  +0x028  w[4] R/W -> ActWeapon::SetTransformToWorldSpace [4: ChangeCharacter@10ce20, SetWeaponVelocity@10d048, SetWeaponsTransformToWorldSpace@10cfe8, StopUsingResources@10d0d0]
  +0x02c  w[8] LEA/W addr-taken [2: ChangeCharacter@10ce20, StopUsingResources@10d0d0]
  +0x034  w[2] W [1: ChangeCharacter@10ce20]
  +0x036  w[1] W [1: ChangeCharacter@10ce20]
  +0x04a  w[8] LEA/W addr-taken [2: ChangeCharacter@10ce20, StopUsingResources@10d0d0]
  +0x052  w[2] W [1: ChangeCharacter@10ce20]
  +0x054  w[1] W [1: ChangeCharacter@10ce20]
  +0x068  w[4] R/W -> ActCharacter::StopUsingResources [6: ActCharacter@10cc60, Draw@10d320, GetAlpha@10d238, GetShadowTriangle@10dd00, SetAlpha@10d1f0, ~ActCharacter@10d188]
  +0x06c  w[4] R/W [2: ActCharacter@10cc60, Draw@10d320]
  +0x070  w[4] LEA/R/W float addr-taken [2: CalculateMuzzleFlashIntensity@10db30, Draw@10d320]
  +0x074  w[4, 8] LEA/R/W float addr-taken [2: CalculateMuzzleFlashIntensity@10db30, Draw@10d320]
  +0x07c  w[8] W [1: CalculateMuzzleFlashIntensity@10db30]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  GetShadowTriangle: R +0x68 w4
