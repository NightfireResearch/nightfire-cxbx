# GFX

FastAlloc/constructed sizes under its tag: None
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none

Xbox methods (14):
  0xd32f0 undefined __thiscall Trigger(void * this, undefined4 param_1, undefined1 param_2, undefined4 param_3, undefine
  0xd33f0 undefined __stdcall Init(void)
  0xd3450 undefined CULL_GetNextCanvas(void)
  0xd3490 undefined PurgeSceneObjEffects(undefined4 param_1)
  0xd34b0 undefined PurgeAllEffects(void)
  0xd34c0 undefined default Update(void)
  0xd34e0 int __cdecl Trigger(ulong giottoRef, undefined4 param_2, undefined4 param_3, ulong * param_4, uint param_5, ul
  0xd3520 undefined TriggerIncidental(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, un
  0xd3560 undefined Purge(undefined4 param_1)
  0xd3590 undefined SuppressEffect(undefined4 param_1)
  0xd35b0 undefined SetIntensity(undefined4 param_1, undefined4 param_2)
  0xd35d0 undefined Shutdown(void)
  0xd35f0 undefined LookupReference(undefined4 param_1)
  0xd3610 undefined __thiscall Trigger(void * this, undefined4 param_1, undefined4 param_2, undefined4 param_3, undefine

PS2 methods (28):
  0x232ce0 GFX::Init
  0x232d28 GFX::Shutdown
  0x232d50 GFX::Debug
  0x232d58 GFX::CULL_GetNextCanvas
  0x232d98 GFX::CULL_SetCanvas
  0x232dc8 GFX::PurgeSceneObjEffects
  0x232df0 GFX::PurgeAllEffects
  0x232e18 GFX::Locate
  0x232e80 GFX::Update
  0x232eb8 GFX::GetNumEffects
  0x232ec8 GFX::GetFirst
  0x232ed0 GFX::GetNext
  0x232ed8 GFX::IsEnd
  0x232ef0 GFX::GetSceneObj
  0x232f18 GFX::SetCulling
  0x232f48 GFX::LookupReference
  0x232f68 GFX::UGroupToReference
  0x232f70 GFX::Trigger
  0x233010 GFX::TriggerIncidental
  0x2330b0 GFX::Trigger
  0x233108 GFX::TriggerIncidental
  0x233160 GFX::Purge
  0x233188 GFX::Draw
  0x233190 GFX::Draw
  0x233198 GFX::Gallery
  0x2331c0 GFX::SuppressEffect
  0x2331e8 GFX::SetIntensity
  0x233440 GFX::fGallery_global_ctors

Sheet rows:
  GFX::Init(void)
  GFX::Shutdown(void)
  GFX::Debug(void)
  GFX::CULL_GetNextCanvas(COORD4 &, float &, bool &, float &)
  GFX::CULL_SetCanvas(int, bool, float)
  GFX::PurgeSceneObjEffects(RSceneObj *)
  GFX::PurgeAllEffects(void)
  GFX::Locate(char *)
  GFX::Update(void)
  GFX::GetNumEffects(void)
  GFX::GetFirst(void)
  GFX::GetNext(AnimRef *)
  GFX::IsEnd(AnimRef *)
  GFX::GetSceneObj(AnimRef *)
  GFX::SetCulling(AnimRef *, bool)
  GFX::LookupReference(char *)
  GFX::UGroupToReference(UGroup *)
  GFX::Trigger(char *, COORD3 &, COORD4 &, COORD3 &, COORD4 &, RS
  GFX::TriggerIncidental(char *, COORD3 &, COORD4 &, COORD3 &, CO
  GFX::Trigger(GiottoRef *, COORD3 &, COORD4 &, COORD3 &, COORD4
  GFX::TriggerIncidental(GiottoRef *, COORD3 &, COORD4 &, COORD3
  GFX::Purge(GiottoHandle *)
  GFX::Draw(GiottoHandle *)
  GFX::Draw(GiottoHandle *, COORD4 &, COORD4 &)
  GFX::Gallery(void)
  GFX::SuppressEffect(GiottoHandle *)
  GFX::SetIntensity(GiottoHandle *, float)
  GFX::fGallery

Xbox methods treated as members (3 of 14; untyped ones count when ECX is read before it is written): Init, Trigger

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):

PS2 this-relative accesses (PS2 offsets):
  +0x001  w- LEA addr-taken [1: GetNext@232ed0]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
