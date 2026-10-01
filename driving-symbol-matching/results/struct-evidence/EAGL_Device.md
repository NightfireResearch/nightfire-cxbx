# EAGL::Device

FastAlloc/constructed sizes under its tag: {'allocated': [28], 'constructed': []}
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0xe50c0 first calls: []

Xbox methods (10):
  0xe4f70 undefined Init(void)
  0xe50c0 undefined8 __thiscall Device(Device * this)
  0xe50e0 undefined ~Device(void)
  0xe8900 void __fastcall NewRenderContext(void * eaglDevice)
  0xe89e0 undefined4 __stdcall GetCurrentRenderContext(void)
  0xe89f0 undefined4 __stdcall GetCurrentTextureRenderContext(void)
  0xe8a20 undefined SetNewOverride(undefined4 param_1)
  0xe8a30 undefined SetDeleteOverride(undefined4 param_1)
  0xe8a40 undefined4 __stdcall Get(void)
  0xe8a50 undefined DeleteRenderContext(undefined4 param_1)

PS2 methods (10):
  0x286c98 EAGL::Device::~Device
  0x286e18 EAGL::Device::Init
  0x287230 EAGL::Device::Device
  0x287548 EAGL::Device::NewRenderContext
  0x287608 EAGL::Device::DeleteRenderContext
  0x287728 EAGL::Device::GetCurrentRenderContext
  0x287738 EAGL::Device::GetCurrentTextureRenderContext
  0x287768 EAGL::Device::SetNewOverride
  0x287778 EAGL::Device::SetDeleteOverride
  0x287788 EAGL::Device::Get

Sheet rows:
  EAGL::Device::~Device(void)
  EAGL::Device::Init(void)
  EAGL::Device::Device(void)
  EAGL::Device::NewRenderContext(void)
  EAGL::Device::DeleteRenderContext(EAGL::RenderContext *)
  EAGL::Device::GetCurrentRenderContext(void) const
  EAGL::Device::GetCurrentTextureRenderContext(void) const
  EAGL::Device::SetNewOverride(void *(*)(unsigned int, char *))
  EAGL::Device::SetDeleteOverride(void (*)(void *, unsigned int))
  EAGL::Device::Get(void)
  EAGL::Device::gGameLink

Xbox methods treated as members (10 of 10; untyped ones count when ECX is read before it is written): DeleteRenderContext, Device, Get, GetCurrentRenderContext, GetCurrentTextureRenderContext, Init, NewRenderContext, SetDeleteOverride, SetNewOverride, ~Device

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [1: Device@e50c0]
  +0x004  w[4] W [1: Device@e50c0]
  +0x010  w[4] R/W [4: DeleteRenderContext@e8a50, Device@e50c0, NewRenderContext@e8900, ~Device@e50e0]
  +0x014  w[4] R/W [2: Device@e50c0, ~Device@e50e0]
  +0x018  w[1] W [2: Device@e50c0, Init@e4f70]

PS2 this-relative accesses (PS2 offsets):
  +0x004  w[4] LEA/R/W addr-taken [4: DeleteRenderContext@287608, Device@287230, NewRenderContext@287548, ~Device@286c98]
  +0x008  w[4] R [1: ~Device@286c98]
  +0x00c  w[4] W [2: Init@286e18, ~Device@286c98]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
