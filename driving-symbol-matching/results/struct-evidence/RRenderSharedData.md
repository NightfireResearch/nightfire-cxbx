# RRenderSharedData

FastAlloc/constructed sizes under its tag: None
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none

Xbox methods (8):
  0x7e190 undefined Init(void)
  0x7e1a0 undefined SetVehiclesAllowed(undefined1 param_1)
  0x7e1b0 undefined MarkAsDirty(void)
  0x7e1c0 undefined SendPerViewPort(void)
  0x7e1f0 undefined __stdcall SendPerWorldInstance(void)
  0x7e200 undefined GetShadowBrightness(undefined4 param_1, undefined param_2, undefined4 param_3, undefined4 param_4)
  0x7e2b0 undefined SendPerObjectInstance(undefined param_1, undefined4 param_2)
  0x7e310 undefined SendPerCarInstance(undefined param_1, undefined4 param_2)

PS2 methods (10):
  0x1ada20 RRenderSharedData::Init
  0x1adbd0 RRenderSharedData::Shutdown
  0x1adc48 RRenderSharedData::SetVehiclesAllowed
  0x1adc58 RRenderSharedData::MarkAsDirty
  0x1adc68 RRenderSharedData::SendPerViewPort
  0x1adcb0 RRenderSharedData::GetDynamicObjectLightingProps
  0x1adcc0 RRenderSharedData::SendPerWorldInstance
  0x1adce8 RRenderSharedData::GetShadowBrightness
  0x1ade10 RRenderSharedData::SendPerObjectInstance
  0x1adea8 RRenderSharedData::SendPerCarInstance

Sheet rows:
  RRenderSharedData::Init(void)
  RRenderSharedData::Shutdown(void)
  RRenderSharedData::SetVehiclesAllowed(bool)
  RRenderSharedData::MarkAsDirty(void)
  RRenderSharedData::SendPerViewPort(void)
  RRenderSharedData::GetDynamicObjectLightingProps(void)
  RRenderSharedData::SendPerWorldInstance(CARP::Instance &, void
  RRenderSharedData::GetShadowBrightness(COORD3, unsigned int)
  RRenderSharedData::SendPerObjectInstance(CARP::Instance &, void
  RRenderSharedData::SendPerCarInstance(CARP::Instance &, void *)

Xbox methods treated as members (4 of 8; untyped ones count when ECX is read before it is written): GetShadowBrightness, Init, MarkAsDirty, SetVehiclesAllowed

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[8] R [1: GetShadowBrightness@1adce8]
  +0x008  w[4] R [1: GetShadowBrightness@1adce8]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
