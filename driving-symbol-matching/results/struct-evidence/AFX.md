# AFX

FastAlloc/constructed sizes under its tag: None
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none

Xbox methods (8):
  0x124790 undefined Init(void)
  0x1247d0 undefined __cdecl SetMode(int mode, int region, int param_3)
  0x124810 undefined * __stdcall GetCurrentModeName(void)
  0x124830 undefined1 __stdcall IsAllVoices(void)
  0x124850 undefined __stdcall Pause(void)
  0x124870 undefined __stdcall Resume(void)
  0x124880 undefined __stdcall Shutdown(void)
  0x1248b0 undefined __stdcall Update(void)

PS2 methods (12):
  0x2f7ca0 AFX::Init
  0x2f7d18 AFX::SetMode
  0x2f7d68 AFX::TestMode
  0x2f7de8 AFX::GetCurrentMix
  0x2f7e08 AFX::GetCurrentModeName
  0x2f7e48 AFX::IsAllVoices
  0x2f7e80 AFX::GetName
  0x2f7ea0 AFX::GetSettings
  0x2f7eb8 AFX::Update
  0x2f8248 AFX::Pause
  0x2f8280 AFX::Resume
  0x2f8290 AFX::Shutdown

Sheet rows:
  AFX::Init(void)
  AFX::SetMode(AFX::ModeType, AFX::RegionType, int)
  AFX::TestMode(void)
  AFX::GetCurrentMix(void)
  AFX::GetCurrentModeName(void)
  AFX::IsAllVoices(void)
  AFX::GetName(int)
  AFX::GetSettings(int)
  AFX::Update(void)
  AFX::Pause(void)
  AFX::Resume(void)
  AFX::Shutdown(void)
  AFX::fgRegion

Xbox methods treated as members (4 of 8; untyped ones count when ECX is read before it is written): GetCurrentModeName, IsAllVoices, Resume, Update

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):

PS2 this-relative accesses (PS2 offsets):

Short single-field methods (accessor candidates; check they touch this, not a pointee):
