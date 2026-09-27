# RRenderHigh

FastAlloc/constructed sizes under its tag: None
Xbox vtable 0x00191aa8 (5 slots) stored by its constructor
PS2 sheet virtual table row: none
constructor 0x8c210 first calls: ['MEM_fill', 'RRenderHigh::PushPlayerView', 'RViewCamera::SetZBufferRange']

Xbox methods (11):
  0x8bac0 undefined __stdcall KillTrackRenderPostSim(void)
  0x8bad0 void __thiscall ReceiveCameraInput(RRenderHigh * this)
  0x8bb10 undefined RearrangeSplitScreens(void)
  0x8bf30 void __thiscall Render(RRenderHigh * this)
  0x8c090 void __thiscall ~RRenderHigh(RRenderHigh * this)
  0x8c130 undefined PushPlayerView(void)
  0x8c210 int __thiscall RRenderHigh(RRenderHigh * this)
  0x8c3d0 void __stdcall InitGameRender(void)
  0x8c740 undefined __stdcall KillGameRender(void)
  0x8c800 undefined InitTrackRenderPostSim(void)
  0x8c840 undefined __stdcall RestartTrackRender(void)

PS2 methods (21):
  0x1c3c10 RRenderHigh::Render
  0x1c3d28 RRenderHigh::StartFrameRender
  0x1c3d58 RRenderHigh::StopFrameRender
  0x1c3db0 RRenderHigh::RRenderHigh
  0x1c3ee8 RRenderHigh::~RRenderHigh
  0x1c3fc8 RRenderHigh::InitGlobalRender
  0x1c3fe8 RRenderHigh::KillGlobalRender
  0x1c4008 RRenderHigh::InitGameRender
  0x1c4580 RRenderHigh::KillGameRender
  0x1c4688 RRenderHigh::InitFrontEndRender
  0x1c4808 RRenderHigh::KillFrontEndRender
  0x1c4840 RRenderHigh::InitTrackRenderPostSim
  0x1c48c0 RRenderHigh::KillTrackRenderPostSim
  0x1c48e8 RRenderHigh::RestartTrackRender
  0x1c49e8 RRenderHigh::ReceiveCameraInput
  0x1c4a68 RRenderHigh::PushPlayerView
  0x1c4ad8 RRenderHigh::PushPlayerView
  0x1c4b48 RRenderHigh::PopPlayerView
  0x1c4c10 RRenderHigh::RearrangeSplitScreens
  0x1c5000 RRenderHigh::fgThis_global_ctors
  0x1c5020 RRenderHigh::fgThis_global_dtors

Sheet rows:
  RRenderHigh::Render(void)
  RRenderHigh::StartFrameRender(void)
  RRenderHigh::StopFrameRender(void)
  RRenderHigh::RRenderHigh(void)
  RRenderHigh::~RRenderHigh(void)
  RRenderHigh::InitGlobalRender(void)
  RRenderHigh::KillGlobalRender(void)
  RRenderHigh::InitGameRender(void)
  RRenderHigh::KillGameRender(void)
  RRenderHigh::InitFrontEndRender(void)
  RRenderHigh::KillFrontEndRender(void)
  RRenderHigh::InitTrackRenderPostSim(void)
  RRenderHigh::KillTrackRenderPostSim(void)
  RRenderHigh::RestartTrackRender(void)
  RRenderHigh::ReceiveCameraInput(void)
  RRenderHigh::PushPlayerView(void)
  RRenderHigh::PushPlayerView(RViewCamera *, RPlayerCamera *)
  RRenderHigh::PopPlayerView(void)
  RRenderHigh::RearrangeSplitScreens(void)
  RRenderHigh::fgThis

Xbox methods treated as members (6 of 11; untyped ones count when ECX is read before it is written): PushPlayerView, RRenderHigh, RearrangeSplitScreens, ReceiveCameraInput, Render, ~RRenderHigh

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x004  w- LEA addr-taken [1: ReceiveCameraInput@8bad0]
  +0x020  w[4] R/W [5: PushPlayerView@8c130, RRenderHigh@8c210, RearrangeSplitScreens@8bb10, ReceiveCameraInput@8bad0, ~RRenderHigh@8c090]
  +0x024  w[4] R/W [2: RRenderHigh@8c210, ~RRenderHigh@8c090]
  +0x028  w[4] W [1: RRenderHigh@8c210]
  +0x030  w[4] R/W [2: RRenderHigh@8c210, ~RRenderHigh@8c090]
  +0x034  w[4] R/W [2: RRenderHigh@8c210, ~RRenderHigh@8c090]
  +0x038  w[4] R/W [2: RRenderHigh@8c210, ~RRenderHigh@8c090]
  +0x03c  w[4] W [1: RRenderHigh@8c210]
  +0x040  w[4] W [1: RRenderHigh@8c210]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] R -> GHud::Render, RViewCamera::Render [1: Render@1c3c10]
  +0x004  w- LEA addr-taken [2: PopPlayerView@1c4b48, ReceiveCameraInput@1c49e8]
  +0x020  w[4] R/W [6: PopPlayerView@1c4b48, PushPlayerView@1c4ad8, RRenderHigh@1c3db0, RearrangeSplitScreens@1c4c10, ReceiveCameraInput@1c49e8, Render@1c3c10]
  +0x024  w[4] R/W [2: RRenderHigh@1c3db0, ~RRenderHigh@1c3ee8]
  +0x028  w[4] W [1: RRenderHigh@1c3db0]
  +0x030  w[4] R/W -> RViewCamera::Render [3: RRenderHigh@1c3db0, Render@1c3c10, ~RRenderHigh@1c3ee8]
  +0x034  w[4] R/W -> RRenderHigh::StopFrameRender [3: RRenderHigh@1c3db0, Render@1c3c10, ~RRenderHigh@1c3ee8]
  +0x038  w[4] R/W [3: RRenderHigh@1c3db0, Render@1c3c10, ~RRenderHigh@1c3ee8]
  +0x040  w[8] R/W -> __floatdisf [2: RRenderHigh@1c3db0, Render@1c3c10]
  +0x048  w[4] R/W float [2: RRenderHigh@1c3db0, Render@1c3c10]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
