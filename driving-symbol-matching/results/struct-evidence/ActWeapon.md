# ActWeapon

FastAlloc/constructed sizes under its tag: {'allocated': [368], 'constructed': []}
deleting destructor 0x1b1b0 frees/deletes with size 0x170 (call to UMemory::FastFree)
Xbox vtable 0x0018a440 (20 slots) stored by its constructor
PS2 sheet virtual table row: ['ActWeapon virtual table']
constructor 0x1abd0 first calls: ['RSceneObj::RSceneObj', 'UGroup::GroupLocateTag', 'RSceneObj::UseArticle']

Xbox methods (14):
  0x1ab20 undefined __stdcall LoadAttributes(void)
  0x1abd0 undefined4 * __thiscall ActWeapon(undefined4 * param_1_00, undefined4 param_2, undefined4 param_3)
  0x1acc0 undefined ~ActWeapon(void)
  0x1ad00 undefined SetTransformToWorldSpace(undefined4 param_1, undefined4 param_2)
  0x1ad70 undefined SetOwner(undefined4 param_1)
  0x1adc0 undefined SetEventDynamicData(void)
  0x1add0 undefined PlayEvent(undefined4 param_1)
  0x1adf0 undefined SpawnWeapon(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0x1ae30 undefined CurrentMuzzleFlashStrength(void)
  0x1ae70 undefined ManualRender(void)
  0x1b020 undefined RenderShellCasings(void)
  0x1b050 undefined TransformToWorldSpace(undefined4 param_1)
  0x1b1b0 undefined scalar_deleting_destructor(undefined1 param_1)
  0x1b270 undefined StartMuzzleFlash(undefined4 param_1)

PS2 methods (20):
  0x1161c8 ActWeapon::LoadAttributes
  0x1162a8 ActWeapon::ActWeapon
  0x116410 ActWeapon::~ActWeapon
  0x116498 ActWeapon::SetTransformToWorldSpace
  0x116608 ActWeapon::SetOwner
  0x116678 ActWeapon::SetEventDynamicData
  0x1166a8 ActWeapon::PlayEvent
  0x1166d0 ActWeapon::SpawnWeapon
  0x116720 ActWeapon::StartMuzzleFlash
  0x116948 ActWeapon::CurrentMuzzleFlashStrength
  0x116988 ActWeapon::Render
  0x1169c8 ActWeapon::ManualRender
  0x116c68 ActWeapon::RenderShellCasings
  0x116cb0 ActWeapon::TransformToWorldSpace
  0x116d10 ActWeapon::TransformPointToWorldSpace
  0x116df0 ActWeapon::TransformMatrixToWorldSpace
  0x116f58 ActWeapon::GetVelocity
  0x1184b8 ActWeapon::operator_new
  0x1184d8 ActWeapon::operator_delete
  0x118690 ActWeapon::LoadAttributes_global_ctors

Sheet rows:
  ActWeapon::LoadAttributes(void)
  ActWeapon::ActWeapon(ActWeaponAux *, unsigned int)
  ActWeapon::~ActWeapon(void)
  ActWeapon::SetTransformToWorldSpace(MATRIX4 &, MATRIX4 &)
  ActWeapon::SetOwner(PhysicsObject *)
  ActWeapon::SetEventDynamicData(void)
  ActWeapon::PlayEvent(unsigned int)
  ActWeapon::SpawnWeapon(COORD3 &, COORD3 &, COORD3 &, COORD3 &)
  ActWeapon::StartMuzzleFlash(COORD3 &)
  ActWeapon::CurrentMuzzleFlashStrength(void)
  ActWeapon::Render(void)
  ActWeapon::ManualRender(void)
  ActWeapon::RenderShellCasings(void)
  ActWeapon::TransformToWorldSpace(MATRIX4 &) const
  ActWeapon::TransformPointToWorldSpace(COORD4 &) const
  ActWeapon::TransformMatrixToWorldSpace(MATRIX4 &) const
  ActWeapon::GetVelocity(void) const
  ActWeapon type_info function
  ActWeapon::operator new(unsigned int)
  ActWeapon::operator delete(void *, unsigned int)
  ActWeapon::CurrentMuzzleFlashDirection(void)
  ActWeapon::SetHenchInfo(CARP::AIHenchInfo *)
  ActWeapon::GetOwner(void) const
  ActWeapon::IsUnderCharacterControl(void) const
  ActWeapon::IsBondWeapon(void) const
  ActWeapon::SetIsBlochWeapon(void)
  ActWeapon::IsBlochWeapon(void) const
  ActWeapon::GetHenchInfo(void) const
  ActWeapon::SetVelocity(COORD3 &)
  ActWeapon::SetTracerType(int)
  ActWeapon::GetTracerType(void)
  ActWeapon::NotTracer(int)
  ActWeapon::SetUnderCharacterControl(bool)
  ActWeapon virtual table
  ActWeapon type_info node

Xbox methods treated as members (13 of 14; untyped ones count when ECX is read before it is written): ActWeapon, CurrentMuzzleFlashStrength, ManualRender, PlayEvent, RenderShellCasings, SetEventDynamicData, SetOwner, SetTransformToWorldSpace, SpawnWeapon, StartMuzzleFlash, TransformToWorldSpace, scalar_deleting_destructor, ~ActWeapon

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: ActWeapon@1abd0, ~ActWeapon@1acc0]
  +0x02c  w[4] R -> RAnimEngine::Handle::ProcessStimuli [2: PlayEvent@1add0, SpawnWeapon@1adf0]
  +0x040  w- LEA addr-taken [1: ActWeapon@1abd0]
  +0x080  w[4] W [1: ActWeapon@1abd0]
  +0x08c  w- LEA addr-taken [1: ManualRender@1ae70]
  +0x090  w- LEA addr-taken [2: ActWeapon@1abd0, StartMuzzleFlash@1b270]
  +0x124  w- LEA addr-taken [1: StartMuzzleFlash@1b270]
  +0x134  w[4] R/W [3: ActWeapon@1abd0, CurrentMuzzleFlashStrength@1ae30, StartMuzzleFlash@1b270]
  +0x138  w[4] R/W [4: ActWeapon@1abd0, ManualRender@1ae70, RenderShellCasings@1b020, SetOwner@1ad70]
  +0x13c  w[4] R/W [4: ActWeapon@1abd0, SetTransformToWorldSpace@1ad00, TransformToWorldSpace@1b050, ~ActWeapon@1acc0]
  +0x140  w[4] R/W [4: ActWeapon@1abd0, SetTransformToWorldSpace@1ad00, TransformToWorldSpace@1b050, ~ActWeapon@1acc0]
  +0x144  w[4] W [2: ActWeapon@1abd0, SetOwner@1ad70]
  +0x150  w[4] W [1: ActWeapon@1abd0]
  +0x154  w[4] W [1: ActWeapon@1abd0]
  +0x158  w[4] W [1: ActWeapon@1abd0]
  +0x15c  w[4] W [1: ActWeapon@1abd0]
  +0x160  w[4] W [1: ActWeapon@1abd0]
  +0x164  w[4] W [1: ActWeapon@1abd0]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[8] W [1: ActWeapon@1162a8]
  +0x008  w[8] W [1: ActWeapon@1162a8]
  +0x010  w[8] W [1: ActWeapon@1162a8]
  +0x018  w[8] W [1: ActWeapon@1162a8]
  +0x020  w[8] W [1: ActWeapon@1162a8]
  +0x028  w[8] W [1: ActWeapon@1162a8]
  +0x030  w[8] W [1: ActWeapon@1162a8]
  +0x038  w[8] W [1: ActWeapon@1162a8]
  +0x040  w- LEA addr-taken [5: ActWeapon@1162a8, ManualRender@1169c8, SetEventDynamicData@116678, StartMuzzleFlash@116720, ~ActWeapon@116410]
  +0x068  w[4] R [1: SpawnWeapon@1166d0]
  +0x07c  w[4] R/W [3: ActWeapon@1162a8, Render@116988, ~ActWeapon@116410]
  +0x080  w[4] W [1: ActWeapon@1162a8]
  +0x090  w- LEA addr-taken [2: ActWeapon@1162a8, StartMuzzleFlash@116720]
  +0x124  w[8] W [1: StartMuzzleFlash@116720]
  +0x12c  w[8] W [1: StartMuzzleFlash@116720]
  +0x134  w[4] R/W [3: ActWeapon@1162a8, CurrentMuzzleFlashStrength@116948, StartMuzzleFlash@116720]
  +0x138  w[4] R/W [5: ActWeapon@1162a8, ManualRender@1169c8, Render@116988, RenderShellCasings@116c68, SetOwner@116608]
  +0x13c  w[4] R/W -> __builtin_delete [6: ActWeapon@1162a8, SetTransformToWorldSpace@116498, TransformMatrixToWorldSpace@116df0, TransformPointToWorldSpace@116d10, TransformToWorldSpace@116cb0, ~ActWeapon@116410]
  +0x140  w[4] R/W -> __builtin_delete [6: ActWeapon@1162a8, SetTransformToWorldSpace@116498, TransformMatrixToWorldSpace@116df0, TransformPointToWorldSpace@116d10, TransformToWorldSpace@116cb0, ~ActWeapon@116410]
  +0x144  w[4] W [2: ActWeapon@1162a8, SetOwner@116608]
  +0x150  w[4] LEA/W float addr-taken [2: ActWeapon@1162a8, GetVelocity@116f58]
  +0x154  w[4] W float [1: ActWeapon@1162a8]
  +0x158  w[4] W float [1: ActWeapon@1162a8]
  +0x15c  w[4] W [1: ActWeapon@1162a8]
  +0x160  w[4] W [1: ActWeapon@1162a8]
  +0x164  w[4] W [1: ActWeapon@1162a8]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  PlayEvent: R +0x2c w4
