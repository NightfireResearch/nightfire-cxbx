# AMix

FastAlloc/constructed sizes under its tag: {'allocated': [80], 'constructed': []}
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none

Xbox methods (12):
  0x11c980 undefined SetTransition(undefined4 param_1, undefined4 param_2)
  0x11c9e0 undefined Revert(undefined4 param_1)
  0x11ca10 float10 * __thiscall GetVolume(AMix * this, float10 * __return_storage_ptr__)
  0x11ca90 undefined GetPresetVolume(void)
  0x11cb80 void __cdecl Reset(int param_1)
  0x11d6a0 AMix.conflict * __cdecl Add(char * param_1)
  0x11d720 undefined4 __cdecl Get(char * param_1)
  0x11d750 undefined Remove(void)
  0x11d790 undefined __cdecl Load(char * param_1)
  0x11d980 undefined __stdcall Clear(void)
  0x11da20 undefined ClearMaster(void)
  0x11da60 undefined SetMaster(undefined4 param_1)

PS2 methods (21):
  0x2f2cb0 AMix::Begin
  0x2f2ce0 AMix::End
  0x2f2d08 AMix::AMix
  0x2f2d50 AMix::Add
  0x2f2dc8 AMix::Get
  0x2f2e18 AMix::Remove
  0x2f2e60 AMix::StartFrame
  0x2f2e68 AMix::EndFrame
  0x2f2e70 AMix::Save
  0x2f3058 AMix::Load
  0x2f32b0 AMix::Clear
  0x2f3328 AMix::ClearMaster
  0x2f3360 AMix::SetMaster
  0x2f33a0 AMix::SetTransition
  0x2f3410 AMix::Revert
  0x2f3450 AMix::Reset
  0x2f34d0 AMix::GetVolume
  0x2f3578 AMix::GetPresetVolume
  0x2f3580 AMix::~AMix
  0x2f4740 AMix::gList_global_ctors
  0x2f4760 AMix::gList_global_dtors

Sheet rows:
  AMix::Begin(void)
  AMix::End(void)
  AMix::AMix(void)
  AMix::Add(char *)
  AMix::Get(char *)
  AMix::Remove(void)
  AMix::StartFrame(void)
  AMix::EndFrame(void)
  AMix::Save(char *)
  AMix::Load(char *)
  AMix::Clear(void)
  AMix::ClearMaster(void)
  AMix::SetMaster(char *)
  AMix::SetTransition(float, int)
  AMix::Revert(int)
  AMix::Reset(int)
  AMix::GetVolume(void)
  AMix::GetPresetVolume(void)
  AMix::~AMix(void)
  AMix::gList

Xbox methods treated as members (8 of 12; untyped ones count when ECX is read before it is written): Clear, ClearMaster, GetPresetVolume, GetVolume, Remove, Revert, SetMaster, SetTransition

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W -> AMix::GetVolume, AMix::Remove [4: ClearMaster@11da20, GetVolume@11ca10, Remove@11d750, SetMaster@11da60]
  +0x004  w[4] R/W float [3: GetVolume@11ca10, Revert@11c9e0, SetTransition@11c980]
  +0x008  w[4] W float [2: Revert@11c9e0, SetTransition@11c980]
  +0x010  w[4] W float [1: GetPresetVolume@11ca90]
  +0x014  w[4] R/W float [3: GetVolume@11ca10, Revert@11c9e0, SetTransition@11c980]
  +0x018  w[4] W float [3: GetVolume@11ca10, Revert@11c9e0, SetTransition@11c980]
  +0x01c  w[1] R/W [3: GetVolume@11ca10, Revert@11c9e0, SetTransition@11c980]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] R/W -> AMix::Remove [4: AMix@2f2d08, ClearMaster@2f3328, GetVolume@2f34d0, SetMaster@2f3360]
  +0x004  w[4] R/W float [4: AMix@2f2d08, GetVolume@2f34d0, Revert@2f3410, SetTransition@2f33a0]
  +0x008  w[4] R/W float [3: AMix@2f2d08, Revert@2f3410, SetTransition@2f33a0]
  +0x00c  w[4] W float [1: AMix@2f2d08]
  +0x010  w[4] R/W float [2: AMix@2f2d08, GetPresetVolume@2f3578]
  +0x014  w[4] R/W float [4: AMix@2f2d08, GetVolume@2f34d0, Revert@2f3410, SetTransition@2f33a0]
  +0x018  w[4] R/W float [4: AMix@2f2d08, GetVolume@2f34d0, Revert@2f3410, SetTransition@2f33a0]
  +0x01c  w[4] R/W [4: AMix@2f2d08, GetVolume@2f34d0, Revert@2f3410, SetTransition@2f33a0]
  +0x024  w[4] W [1: AMix@2f2d08]
  +0x028  w[4] W [1: AMix@2f2d08]
  +0x02c  w[4] W float [1: AMix@2f2d08]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  GetPresetVolume: W +0x10 w4 float
