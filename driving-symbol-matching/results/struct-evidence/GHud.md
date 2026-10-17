# GHud

FastAlloc/constructed sizes under its tag: {'allocated': [], 'constructed': [1264]}
Xbox vtable 0x0019ff84 (5 slots) stored by its constructor
Xbox vtable 0x001a06c0 (1 slots) stored by its constructor
PS2 sheet virtual table row: ['GHud virtual table']
constructor 0xdab50 first calls: ['UMemory::FastAlloc', 'RViewCamera::RViewCamera', 'UMemory::FastAlloc']

Xbox methods (73):
  0xd7c10 GHud * __stdcall TheApp(void)
  0xd7c20 undefined SetLevelLoadText(void)
  0xd7c30 void __thiscall EnableLavaDamageEffect(int param_1_00, char param_2)
  0xd7c60 undefined LavaDamageEffectEnabled(void)
  0xd7c70 undefined FadeInFromBlack(undefined4 param_1)
  0xd7cd0 undefined FadeOutToBlack(undefined4 param_1)
  0xd7d40 undefined GetBlackFadeAnimating(void)
  0xd7d50 float __thiscall GetBlackFadePct(GHud * this)
  0xd7d60 undefined __thiscall Stylin(GHud * this)
  0xd7d90 undefined RadarRadiusInMeters(void)
  0xd7da0 void __thiscall Reset(GHud * this)
  0xd8170 undefined IsTargetPointOnScreen(undefined4 param_1)
  0xd81d0 undefined DrawHealth(void)
  0xd87a0 undefined TriggerDamageFlash(undefined4 param_1)
  0xd87f0 undefined TriggerBloodCurtain(void)
  0xd8800 undefined TriggerTimerOn(undefined4 param_1)
  0xd8810 undefined TriggerTimerOff(void)
  0xd8820 undefined DrawScreenHits(void)
  0xd8970 undefined DrawScreenFades(void)
  0xd8ae0 undefined DrawFlyByWire(void)
  0xd8c00 undefined DrawZoomingCrossHairs(undefined4 param_1)
  0xd8ea0 undefined DrawFadingCrossHairs(undefined4 param_1)
  0xd9040 undefined SetAutoDrive(undefined1 param_1)
  0xd9050 void __thiscall PauseOff(GHud * this)
  0xd90e0 undefined CheckCheatInput(undefined4 param_1)
  0xd9140 undefined CheatMsgHandler(undefined4 param_1)
  0xd9370 undefined GetCenter(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0xd93d0 undefined DrawPauseErrors(void)
  0xd9570 undefined DrawBlinkingArrow(undefined4 param_1, undefined4 param_2)
  0xd98d0 undefined DrawFontAndShadow(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, un
  0xd9970 undefined DrawFontCentered(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, und
  0xd9a50 undefined DrawFont(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, undefined4 
  0xd9ad0 undefined DrawFont(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, undefined4 
  0xd9b30 undefined LetterBoxOn(undefined1 param_1)
  0xd9b70 undefined LetterBoxOff(undefined1 param_1)
  0xd9b90 undefined GetLetterBoxMode(void)
  0xd9ba0 undefined AimOn(void)
  0xd9bd0 undefined AimOff(void)
  0xd9bf0 undefined GadgetOn(undefined4 param_1, undefined4 param_2)
  0xd9c30 undefined GadgetOff(void)
  0xd9c50 undefined DrawGadget(void)
  0xd9e30 undefined DrawLetterBox(void)
  0xd9f50 undefined SetTarget(undefined4 param_1)
  0xd9fb0 undefined SetTargetLockState(undefined4 param_1)
  0xda030 void __thiscall TriggerMissionMsg(GHud * this, int param_1_00, int param_2, int param_3)
  0xda0a0 undefined TriggerObjectiveMsg(undefined4 param_1, undefined4 param_2, undefined1 param_3)
  0xda180 undefined TriggerButtonMsg(undefined4 param_1, undefined param_2, undefined param_3, undefined4 param_4, undef
  0xda1d0 undefined GetButton(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0xda260 undefined KillButtonMessage(void)
  0xda280 undefined dbMessage(undefined4 param_1, undefined4 param_2)
  0xda2e0 undefined DrawMessage(void)
  0xda540 undefined DrawStylin(void)
  0xda8e0 undefined dbDrawTarget(undefined4 param_1, undefined4 param_2, undefined param_3, undefined4 param_4, undefine
  0xdab50 undefined4 * __thiscall GHud(GHud * this)
  0xdc420 void __thiscall ~GHud(GHud * this)
  0xdc4a0 undefined GetScreenCoords(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0xdc590 undefined DrawBloodCurtain(void)
  0xdc7d0 undefined DrawTimer(void)
  0xdc990 void __thiscall DrawWeapon(GHud * this)
  0xdcae0 undefined PauseOn(void)
  0xdcc10 undefined PauseMenuMsgHandler(undefined4 param_1)
  0xdd340 undefined DrawPauseMenu(void)
  0xdee80 undefined SetMissionPassed(void)
  0xdeee0 undefined SetMissionFailed(undefined1 param_1)
  0xdef90 undefined DrawButtonMessage(void)
  0xdf2e0 undefined DrawMissionResult(void)
  0xdfc30 undefined DrawObjectiveMessage(void)
  0xe00d0 undefined scalar_deleting_destructor(undefined1 param_1)
  0xe00f0 undefined SetControllerUnplugged(undefined1 param_1)
  0xe0140 undefined4 __thiscall Render(GHud * this)
  0xe0440 void __thiscall DrawCrossHairs(GHud * this)
  0xe1970 void __thiscall Draw(GHud * this)
  0xe1a90 undefined UpdateTargets(void)

PS2 methods (103):
  0x23ae30 GHud::GHud
  0x23c560 GHud::~GHud
  0x23c618 GHud::TheApp
  0x23c628 GHud::StartRadarStencil
  0x23c6a8 GHud::EndRadarStencil
  0x23c6c8 GHud::SetLevelLoadText
  0x23c6f8 GHud::EnableLavaDamageEffect
  0x23c718 GHud::LavaDamageEffectEnabled
  0x23c728 GHud::FadeInFromBlack
  0x23c780 GHud::FadeOutToBlack
  0x23c810 GHud::GetBlackFadeAnimating
  0x23c818 GHud::GetBlackFadePct
  0x23c820 GHud::Stylin
  0x23c848 GHud::RadarRadiusInMeters
  0x23c858 GHud::Reset
  0x23cb58 GHud::SetControllerUnplugged
  0x23cbc0 GHud::Render
  0x23cf88 GHud::Draw
  0x23d118 GHud::DrawRadarIsOn
  0x23d148 GHud::DrawQRadar
  0x23d1c8 GHud::DrawQRadarBlips
  0x23d210 GHud::DrawQRadarPulse
  0x23d288 GHud::DrawQRadarArrow
  0x23d568 GHud::GetScreenCoords
  0x23d6a8 GHud::UpdateTargets
  0x23d9c8 GHud::IsTargetPointOnScreen
  0x23da30 GHud::DrawHealth
  0x23e270 GHud::TriggerDamageFlash
  0x23e2c0 GHud::DrawBloodCurtain
  0x23e558 GHud::TriggerBloodCurtain
  0x23e568 GHud::TriggerTimerOn
  0x23e570 GHud::TriggerTimerOff
  0x23e580 GHud::DrawTimer
  0x23e7b8 GHud::DrawScreenHits
  0x23e930 GHud::DrawScreenFades
  0x23eab0 GHud::IsFlyByWireOn
  0x23ead8 GHud::DrawFlyByWire
  0x23ec08 GHud::DrawZoomingCrossHairs
  0x23f080 GHud::DrawFadingCrossHairs
  0x23f328 GHud::DrawCrossHairs
  0x240c98 GHud::DrawWeapon
  0x240e00 GHud::GetBondVehicleOrientation0to1
  0x240f60 GHud::GetObjectiveOrientation0to1
  0x240fe0 GHud::GetDistanceToObjective
  0x241040 GHud::SetAutoDrive
  0x241048 GHud::PauseOn
  0x2411a0 GHud::PauseOff
  0x241200 GHud::CanToggleInvert
  0x241210 GHud::PauseMenuMsgHandler
  0x241b38 GHud::CheckCheatInput
  0x241bb8 GHud::CheatMsgHandler
  0x241ea8 GHud::GetCenter
  0x241f40 GHud::DrawPauseErrors
  0x2422b8 GHud::DrawBlinkingArrow
  0x2426a0 GHud::DrawPauseMenu
  0x244248 GHud::DrawFontGlowing
  0x244398 GHud::DrawFontAndShadow
  0x244490 GHud::DrawFontCentered
  0x244600 GHud::DrawFont
  0x2446d8 GHud::DrawFont
  0x244790 GHud::GetHelpIconAndTextSpacing
  0x244828 GHud::DrawRadar2DBlips
  0x244c00 GHud::DrawRadar2DPulse
  0x244c08 GHud::DrawRadar2D
  0x244c10 GHud::DrawRadarArrow2D
  0x244d80 GHud::DrawMapIndicator
  0x244ea0 GHud::LetterBoxOn
  0x244ec8 GHud::LetterBoxOff
  0x244ee8 GHud::GetLetterBoxMode
  0x244ef0 GHud::AimOn
  0x244f30 GHud::AimOff
  0x244f48 GHud::GadgetOn
  0x244f70 GHud::GadgetOff
  0x244f88 GHud::DrawGadget
  0x245190 GHud::DrawLetterBox
  0x2452e8 GHud::ComputeOrientation
  0x2454f0 GHud::ComputeAngle0to1
  0x2456c0 GHud::SetMissionPassed
  0x245708 GHud::SetMissionFailed
  0x2457a0 GHud::SetTarget
  0x245800 GHud::SetTargetLockState
  0x245890 GHud::TriggerMissionMsg
  0x245910 GHud::TriggerObjectiveMsg
  0x2459d0 GHud::TriggerButtonMsg
  0x2459f8 GHud::GetButton
  0x245ab8 GHud::DrawButtonMessage
  0x245eb0 GHud::KillButtonMessage
  0x245ec0 GHud::ClearObjectiveMessage
  0x245ee0 GHud::dbMessage
  0x245f68 GHud::DrawMissionResult
  0x2469b8 GHud::DrawMessage
  0x246c48 GHud::DrawObjectiveMessage
  0x2470f0 GHud::DrawDebugMessage
  0x2470f8 GHud::DrawEnemyTarget
  0x2471e0 GHud::DrawPowerupTarget
  0x2472b0 GHud::DrawStylin
  0x247618 GHud::DrawObjectiveTarget
  0x2476b8 GHud::DrawTarget
  0x2477a0 GHud::dbDrawTarget
  0x247d80 GHud::GetGallery
  0x247d88 GHud::IsPaused
  0x247d90 GHud::IsAimOn
  0x247e78 GHud::fActionQueue_global_ctors

Sheet rows:
  GHud::GHud(void)
  GHud::~GHud(void)
  GHud::TheApp(void)
  GHud::StartRadarStencil(void)
  GHud::EndRadarStencil(void)
  GHud::SetLevelLoadText(void)
  GHud::ClearLevelLoadText(void)
  GHud::DrawLevelLoadText(void)
  GHud::ChargeEMP(void)
  GHud::EnableLavaDamageEffect(bool)
  GHud::LavaDamageEffectEnabled(void)
  GHud::FadeInFromBlack(int)
  GHud::FadeOutToBlack(int)
  GHud::GetBlackFadeAnimating(void)
  GHud::GetBlackFadePct(void)
  GHud::Stylin(void)
  GHud::RadarRadiusInMeters(void)
  GHud::Reset(void)
  GHud::SetControllerUnplugged(bool)
  GHud::Render(void)
  GHud::Draw(void)
  GHud::DrawRadarIsOn(void)
  GHud::DrawQRadar(void)
  GHud::DrawQRadarBlips(MATRIX4 *, MATRIX4 *)
  GHud::DrawQRadarPulse(MATRIX4 *, MATRIX4 *)
  GHud::DrawQRadarArrow(void)
  GHud::GetScreenCoords(COORD3 *, bool *, COORD2 *)
  GHud::UpdateTargets(void)
  GHud::IsTargetPointOnScreen(COORD2 &)
  GHud::DrawHealth(void)
  GHud::TriggerDamageFlash(float)
  GHud::DrawBloodCurtain(void)
  GHud::TriggerBloodCurtain(void)
  GHud::TriggerTimerOn(int)
  GHud::TriggerTimerOff(int)
  GHud::DrawTimer(void)
  GHud::DrawScreenHits(void)
  GHud::DrawScreenFades(void)
  GHud::IsFlyByWireOn(void)
  GHud::DrawFlyByWire(void)
  GHud::DrawZoomingCrossHairs(GGallery::Keys *)
  GHud::DrawFadingCrossHairs(GGallery::Keys *)
  GHud::DrawCrossHairs(void)
  GHud::DrawWeapon(void)
  GHud::GetBondVehicleOrientation0to1(void)
  GHud::GetObjectiveOrientation0to1(COORD3 *)
  GHud::GetDistanceToObjective(void)
  GHud::SetAutoDrive(bool)
  GHud::PauseOn(void)
  GHud::PauseOff(void)
  GHud::CanToggleInvert(void)
  GHud::PauseMenuMsgHandler(int)
  GHud::CheckCheatInput(GHud::CheatCode &)
  GHud::CheatMsgHandler(int)
  GHud::GetCenter(GGallery::Keys *, float, char *)
  GHud::DrawPauseErrors(void)
  GHud::DrawBlinkingArrow(float, float)
  GHud::DrawPauseMenu(void)
  GHud::DrawFontGlowing(GGallery::Keys *, float, float, unsigned
  GHud::DrawFontAndShadow(GGallery::Keys *, float, float, unsigne
  GHud::DrawFontCentered(GGallery::Keys *, float, float, float, f
  GHud::DrawFont(GGallery::Keys *, float, float, unsigned int, fl
  GHud::DrawFont(GGallery::Keys *, float, float, unsigned int, fl
  GHud::GetHelpIconAndTextSpacing(int, float, float, float, float
  GHud::DrawRadar2DBlips(COORD2 &, bool)
  GHud::DrawRadar2DPulse(COORD2 &)
  GHud::DrawRadar2D(COORD2 &, bool, bool)
  GHud::DrawRadarArrow2D(void)
  GHud::DrawMapIndicator(COORD2 &, GGallery::Keys *, COORD3 *, un
  GHud::LetterBoxOn(bool)
  GHud::LetterBoxOff(bool)
  GHud::GetLetterBoxMode(void)
  GHud::AimOn(void)
  GHud::AimOff(void)
  GHud::GadgetOn(int, float)
  GHud::GadgetOff(void)
  GHud::DrawGadget(void)
  GHud::DrawLetterBox(void)
  GHud::ComputeOrientation(COORD3 *, COORD3 *, COORD3 *, COORD4 *
  GHud::ComputeAngle0to1(COORD3 *, COORD3 *, COORD3 *)

Xbox methods treated as members (71 of 73; untyped ones count when ECX is read before it is written): AimOff, AimOn, CheatMsgHandler, CheckCheatInput, Draw, DrawBlinkingArrow, DrawBloodCurtain, DrawButtonMessage, DrawCrossHairs, DrawFadingCrossHairs, DrawFlyByWire, DrawFont, DrawFontAndShadow, DrawFontCentered, DrawGadget, DrawHealth, DrawLetterBox, DrawMessage, DrawMissionResult, DrawObjectiveMessage, DrawPauseErrors, DrawPauseMenu, DrawScreenFades, DrawScreenHits, DrawStylin, DrawTimer, DrawWeapon, DrawZoomingCrossHairs, EnableLavaDamageEffect, FadeInFromBlack, FadeOutToBlack, GHud, GadgetOff, GadgetOn, GetBlackFadeAnimating, GetBlackFadePct, GetCenter, GetLetterBoxMode, GetScreenCoords, IsTargetPointOnScreen, KillButtonMessage, LavaDamageEffectEnabled, LetterBoxOff, LetterBoxOn, PauseMenuMsgHandler, PauseOff, PauseOn, RadarRadiusInMeters, Render, Reset, SetAutoDrive, SetControllerUnplugged, SetLevelLoadText, SetMissionFailed, SetMissionPassed, SetTarget, SetTargetLockState, Stylin, TheApp, TriggerBloodCurtain, TriggerButtonMsg, TriggerDamageFlash, TriggerMissionMsg, TriggerObjectiveMsg, TriggerTimerOff, TriggerTimerOn, UpdateTargets, dbDrawTarget, scalar_deleting_destructor, ~GHud

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: GHud@dab50, ~GHud@dc420]
  +0x010  w[4] R/W -> EAGL::TAR::~TAR [3: DrawBloodCurtain@dc590, GHud@dab50, ~GHud@dc420]
  +0x018  w[4] R/W -> GGallery::BITMAP_Draw2D, GGallery::FONT_Draw, GGallery::FONT_Height, GGallery::FONT_Load, GGallery::FONT_SetScale, GGall [17: Draw@e1970, DrawFlyByWire@d8ae0, DrawFont@d9a50, DrawFont@d9ad0, DrawFontAndShadow@d98d0, DrawFontCentered@d9970…]
  +0x01c  w[4] R/W [1: GHud@dab50]
  +0x020  w[4] R/W [1: GHud@dab50]
  +0x024  w[4] R/W [2: DrawWeapon@dc990, GHud@dab50]
  +0x028  w[4] R/W [2: DrawWeapon@dc990, GHud@dab50]
  +0x02c  w- LEA addr-taken [1: GHud@dab50]
  +0x030  w[4] R [1: GHud@dab50]
  +0x034  w[4] R [1: GHud@dab50]
  +0x038  w[4] R [1: GHud@dab50]
  +0x03c  w[4] R [1: GHud@dab50]
  +0x040  w[4] R [1: GHud@dab50]
  +0x044  w[4] R [1: GHud@dab50]
  +0x048  w[4] R/W [1: GHud@dab50]
  +0x04c  w- LEA addr-taken [1: GHud@dab50]
  +0x050  w[4] R [1: GHud@dab50]
  +0x054  w[4] R [1: GHud@dab50]
  +0x058  w[4] R [1: GHud@dab50]
  +0x05c  w[4] R [1: GHud@dab50]
  +0x060  w[4] R [1: GHud@dab50]
  +0x064  w[4] R [1: GHud@dab50]
  +0x068  w[4] R/W [1: GHud@dab50]
  +0x084  w[4] R/W [2: DrawStylin@da540, GHud@dab50]
  +0x088  w[4] LEA/R addr-taken [1: GHud@dab50]
  +0x08c  w[4] R [1: GHud@dab50]
  +0x090  w[4] R [1: GHud@dab50]
  +0x094  w[4] R [2: DrawGadget@d9c50, GHud@dab50]
  +0x098  w[4] R [1: GHud@dab50]
  +0x09c  w[4] R [1: GHud@dab50]
  +0x0a0  w[4] W [1: GHud@dab50]
  +0x0a4  w[4] W [1: GHud@dab50]
  +0x0a8  w[4] W [1: GHud@dab50]
  +0x0ac  w[4] R/W [2: DrawMissionResult@df2e0, GHud@dab50]
  +0x0b0  w[4] R/W [2: DrawMissionResult@df2e0, GHud@dab50]
  +0x0b4  w[4] R/W [1: GHud@dab50]
  +0x0b8  w[4] R/W [1: GHud@dab50]
  +0x0bc  w[4] R/W [1: GHud@dab50]
  +0x0c0  w[4] R/W [1: GHud@dab50]
  +0x0c4  w[4] R/W [1: GHud@dab50]
  +0x0c8  w[4] R/W [1: GHud@dab50]
  +0x0cc  w[4] R/W [1: GHud@dab50]
  +0x0d0  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x0d4  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x0d8  w[4] W [1: GHud@dab50]
  +0x0dc  w[4] W [1: GHud@dab50]
  +0x0e0  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x0e4  w[4] R/W [1: GHud@dab50]
  +0x0e8  w[4] R/W [2: DrawPauseErrors@d93d0, GHud@dab50]
  +0x0ec  w[4] R/W [2: DrawPauseErrors@d93d0, GHud@dab50]
  +0x0f0  w[4] R/W [2: DrawPauseErrors@d93d0, GHud@dab50]
  +0x0f4  w[4] W [1: GHud@dab50]
  +0x0f8  w[4] R/W [2: DrawPauseErrors@d93d0, GHud@dab50]
  +0x0fc  w[4] W [1: GHud@dab50]
  +0x100  w[4] R/W [1: GHud@dab50]
  +0x104  w[4] LEA/W addr-taken [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x108  w[4] W [1: GHud@dab50]
  +0x10c  w[4] W [1: GHud@dab50]
  +0x110  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x114  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x118  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x11c  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x120  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x128  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x12c  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x130  w[4] W [1: GHud@dab50]
  +0x134  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x138  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x13c  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x140  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x144  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x148  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x14c  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x150  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x154  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x168  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x16c  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x170  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x174  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x178  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x17c  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x180  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x184  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x188  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x18c  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x190  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x194  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x198  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x19c  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x1a0  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x1c4  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x1c8  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x1cc  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x1d0  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x1d4  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x1d8  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x1dc  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x1e0  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x1e4  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x1e8  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x1ec  w[4] R/W -> GSystem::LOCALE_Symbol [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x1f0  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x1f4  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x1f8  w[4] R/W -> GSystem::LOCALE_Symbol [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x1fc  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x200  w[4] R/W [3: DrawPauseMenu@dd340, DrawTimer@dc7d0, GHud@dab50]
  +0x204  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x208  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x20c  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x210  w[4] R/W [2: DrawPauseMenu@dd340, GHud@dab50]
  +0x214  w[4] R/W -> __ftol2 [2: GHud@dab50, dbDrawTarget@da8e0]
  +0x218  w[4] W [1: GHud@dab50]
  +0x21c  w[4] W [1: GHud@dab50]
  +0x220  w[4] W [1: GHud@dab50]
  +0x224  w[4] W [1: GHud@dab50]
  +0x228  w[4] R/W [1: GHud@dab50]
  +0x22c  w[4] W [1: GHud@dab50]
  +0x230  w[4] W [1: GHud@dab50]
  +0x234  w[4] W [1: GHud@dab50]
  +0x238  w[4] W [1: GHud@dab50]
  +0x23c  w[4] W [1: GHud@dab50]
  +0x240  w[4] W [1: GHud@dab50]
  +0x244  w[4] W [1: GHud@dab50]
  +0x248  w[4] R/W [2: DrawFlyByWire@d8ae0, GHud@dab50]
  +0x24c  w[4] R/W [2: DrawFlyByWire@d8ae0, GHud@dab50]
  +0x250  w[4] R/W [2: DrawFlyByWire@d8ae0, GHud@dab50]
  +0x254  w[4] R/W [2: DrawFlyByWire@d8ae0, GHud@dab50]
  +0x258  w[4] R/W [2: DrawFlyByWire@d8ae0, GHud@dab50]
  +0x25c  w[4] R/W [2: DrawFlyByWire@d8ae0, GHud@dab50]
  +0x260  w[4] W [1: GHud@dab50]
  +0x264  w[4] W [1: GHud@dab50]
  +0x268  w[4] R/W [1: GHud@dab50]
  +0x26c  w[4] W [1: GHud@dab50]
  +0x270  w[4] W [1: GHud@dab50]
  +0x274  w[4] W [1: GHud@dab50]
  +0x278  w[4] W [1: GHud@dab50]
  +0x27c  w[4] W [1: GHud@dab50]
  +0x280  w[4] W [1: GHud@dab50]
  +0x284  w[4] W [1: GHud@dab50]
  +0x288  w[4] W [1: GHud@dab50]
  +0x28c  w[4] R/W [3: DrawFadingCrossHairs@d8ea0, DrawZoomingCrossHairs@d8c00, GHud@dab50]
  +0x290  w[4] W [1: GHud@dab50]
  +0x294  w[4] W [1: GHud@dab50]
  +0x298  w[4] W [1: GHud@dab50]
  +0x29c  w[4] R/W [2: DrawScreenHits@d8820, GHud@dab50]
  +0x2a4  w[4] W [1: GHud@dab50]
  +0x2a8  w[4] W [1: GHud@dab50]
  +0x2ac  w[4] W [1: GHud@dab50]
  +0x2b0  w[1] R/W [10: Draw@e1970, DrawFlyByWire@d8ae0, DrawMissionResult@df2e0, DrawObjectiveMessage@dfc30, DrawScreenFades@d8970, PauseOff@d9050…]
  +0x2b1  w[1] R/W [2: Reset@d7da0, SetControllerUnplugged@e00f0]
  +0x2b2  w[1] R/W -> InputConfigManager::Get, InputConfigManager::GetNumConfigs [6: DrawPauseMenu@dd340, PauseMenuMsgHandler@dcc10, PauseOff@d9050, PauseOn@dcae0, Reset@d7da0, SetAutoDrive@d9040]
  +0x2b3  w[1] W [1: Reset@d7da0]
  +0x2b4  w[4] R/W [7: Draw@e1970, DrawLetterBox@d9e30, DrawScreenHits@d8820, GetLetterBoxMode@d9b90, LetterBoxOff@d9b70, LetterBoxOn@d9b30…]
  +0x2b8  w[4] R/W float [3: DrawLetterBox@d9e30, LetterBoxOn@d9b30, Reset@d7da0]
  +0x2bc  w[4] W [1: PauseOn@dcae0]
  +0x2c8  w[4] W [1: PauseOn@dcae0]
  +0x2cc  w[4] R/W [2: DrawTimer@dc7d0, Reset@d7da0]
  +0x2d0  w[4] W [2: PauseOn@dcae0, Reset@d7da0]
  +0x2d4  w[4] W [1: Reset@d7da0]
  +0x2d8  w[4] W [1: Reset@d7da0]
  +0x2dc  w[4] W [3: PauseOn@dcae0, Render@e0140, Reset@d7da0]
  +0x2e0  w[4] W [2: Render@e0140, Reset@d7da0]
  +0x2e4  w[4] W [2: Render@e0140, Reset@d7da0]
  +0x2e8  w[4] W [2: Render@e0140, Reset@d7da0]
  +0x2ec  w[4] W float [3: DrawStylin@da540, Reset@d7da0, Stylin@d7d60]
  +0x2f0  w[4] R/W [3: DrawStylin@da540, Reset@d7da0, Stylin@d7d60]
  +0x2f8  w[4] W [2: Reset@d7da0, Stylin@d7d60]
  +0x2fc  w[4] W [3: KillButtonMessage@da260, Reset@d7da0, TriggerButtonMsg@da180]
  +0x300  w[4] W [1: TriggerButtonMsg@da180]
  +0x304  w[4] W float [5: DrawButtonMessage@def90, KillButtonMessage@da260, Reset@d7da0, TriggerButtonMsg@da180, TriggerObjectiveMsg@da0a0]
  +0x308  w[4] W float [4: DrawButtonMessage@def90, KillButtonMessage@da260, Reset@d7da0, TriggerButtonMsg@da180]
  +0x30c  w[1] W [2: Reset@d7da0, TriggerObjectiveMsg@da0a0]
  +0x30d  w[1] R/W [2: DrawZoomingCrossHairs@d8c00, Reset@d7da0]
  +0x310  w[4] R/W [7: DrawPauseMenu@dd340, PauseMenuMsgHandler@dcc10, PauseOn@dcae0, Render@e0140, Reset@d7da0, SetMissionFailed@deee0…]
  +0x314  w[4] R/W [9: Draw@e1970, DrawBloodCurtain@dc590, DrawMissionResult@df2e0, Render@e0140, Reset@d7da0, SetMissionFailed@deee0…]
  +0x318  w[4] W float [4: DrawMissionResult@df2e0, Reset@d7da0, SetMissionFailed@deee0, SetMissionPassed@dee80]
  +0x31c  w[4] W [1: Reset@d7da0]
  +0x320  w[4] W [1: Reset@d7da0]
  +0x324  w[4] W float [4: Render@e0140, Reset@d7da0, SetMissionFailed@deee0, SetMissionPassed@dee80]
  +0x328  w[4] R/W [3: DrawScreenHits@d8820, Reset@d7da0, TriggerDamageFlash@d87a0]
  +0x32c  w[4] R/W [3: DrawScreenHits@d8820, Reset@d7da0, TriggerDamageFlash@d87a0]
  +0x330  w[4] R/W [3: EnableLavaDamageEffect@d7c30, LavaDamageEffectEnabled@d7c60, Reset@d7da0]
  +0x334  w[4] W [1: Reset@d7da0]
  +0x338  w[4] W [1: Reset@d7da0]
  +0x33c  w[4] W [1: Reset@d7da0]
  +0x340  w[4] W [1: Reset@d7da0]
  +0x344  w[4] W [1: Reset@d7da0]
  +0x348  w[4] W [1: Reset@d7da0]
  +0x34c  w[4] W float [5: DrawScreenFades@d8970, FadeInFromBlack@d7c70, FadeOutToBlack@d7cd0, GetBlackFadePct@d7d50, Reset@d7da0]
  +0x350  w[1] R/W [5: DrawScreenFades@d8970, FadeInFromBlack@d7c70, FadeOutToBlack@d7cd0, GetBlackFadeAnimating@d7d40, Reset@d7da0]
  +0x354  w[4] W [3: FadeInFromBlack@d7c70, FadeOutToBlack@d7cd0, Reset@d7da0]
  +0x358  w[4] W [3: FadeInFromBlack@d7c70, FadeOutToBlack@d7cd0, Reset@d7da0]
  +0x35c  w[4] W float [3: FadeInFromBlack@d7c70, FadeOutToBlack@d7cd0, Reset@d7da0]
  +0x360  w[4] R/W [3: Render@e0140, Reset@d7da0, TriggerMissionMsg@da030]
  +0x364  w[4] W float [1: Render@e0140]
  +0x368  w[4] W float [3: DrawBloodCurtain@dc590, DrawObjectiveMessage@dfc30, Render@e0140]
  +0x36c  w[4] W [1: Reset@d7da0]
  +0x374  w[4] W [1: Reset@d7da0]
  +0x378  w[4] W [1: Reset@d7da0]
  +0x380  w[4] W float [2: Reset@d7da0, SetTarget@d9f50]
  +0x384  w[4] W float [2: Reset@d7da0, SetTarget@d9f50]
  +0x388  w[4] R/W [2: Reset@d7da0, SetTargetLockState@d9fb0]
  +0x38c  w[4] R/W float [4: DrawFadingCrossHairs@d8ea0, DrawZoomingCrossHairs@d8c00, Reset@d7da0, SetTargetLockState@d9fb0]
  +0x390  w[4] R/W float [4: DrawFadingCrossHairs@d8ea0, DrawZoomingCrossHairs@d8c00, GHud@dab50, Reset@d7da0]
  +0x394  w[4] W float [2: DrawZoomingCrossHairs@d8c00, SetTargetLockState@d9fb0]
  +0x398  w[4] W float [2: DrawZoomingCrossHairs@d8c00, SetTargetLockState@d9fb0]
  +0x39c  w[4] R/W [3: GHud@dab50, Reset@d7da0, SetTargetLockState@d9fb0]
  +0x3a0  w[4] W [2: Reset@d7da0, SetTargetLockState@d9fb0]
  +0x3a4  w[4] W [1: Reset@d7da0]
  +0x3a8  w[4] W [5: Reset@d7da0, SetMissionFailed@deee0, SetMissionPassed@dee80, TriggerButtonMsg@da180, TriggerObjectiveMsg@da0a0]
  +0x3ac  w[4] W [5: Reset@d7da0, SetMissionFailed@deee0, SetMissionPassed@dee80, TriggerButtonMsg@da180, TriggerObjectiveMsg@da0a0]
  +0x3b0  w[4] W float [6: DrawObjectiveMessage@dfc30, Reset@d7da0, SetMissionFailed@deee0, SetMissionPassed@dee80, TriggerButtonMsg@da180, TriggerObjectiveMsg@da0a0]
  +0x3b4  w[4] R/W [4: Reset@d7da0, SetMissionFailed@deee0, SetMissionPassed@dee80, TriggerObjectiveMsg@da0a0]
  +0x3b8  w[4] R/W [5: DrawObjectiveMessage@dfc30, Reset@d7da0, SetMissionFailed@deee0, SetMissionPassed@dee80, TriggerObjectiveMsg@da0a0]
  +0x3bc  w[4] R/W [3: DrawObjectiveMessage@dfc30, Reset@d7da0, TriggerObjectiveMsg@da0a0]
  +0x3c0  w[4] W [4: Reset@d7da0, SetMissionFailed@deee0, SetMissionPassed@dee80, TriggerObjectiveMsg@da0a0]
  +0x3c4  w[1] W [2: Reset@d7da0, UpdateTargets@e1a90]
  +0x3c8  w[4] W [1: UpdateTargets@e1a90]
  +0x3d0  w[4] W [1: UpdateTargets@e1a90]
  +0x3d4  w[4] W [1: UpdateTargets@e1a90]
  +0x3d8  w[4] W [1: Reset@d7da0]
  +0x3dc  w[1] W [2: Reset@d7da0, UpdateTargets@e1a90]
  +0x3e0  w[4] W [1: UpdateTargets@e1a90]
  +0x3f0  w[4] W [1: UpdateTargets@e1a90]
  +0x3f4  w[4] W [1: UpdateTargets@e1a90]
  +0x3fc  w[4] W [1: Reset@d7da0]
  +0x400  w[4] W [1: Reset@d7da0]
  +0x404  w[4] R/W [3: DrawMessage@da2e0, Reset@d7da0, TriggerMissionMsg@da030]
  +0x408  w[4] W float [3: DrawMessage@da2e0, Reset@d7da0, TriggerMissionMsg@da030]
  +0x40c  w[4] W float [1: DrawMessage@da2e0]
  +0x410  w[1] R/W [4: DrawHealth@d81d0, DrawWeapon@dc990, Reset@d7da0, TriggerMissionMsg@da030]
  +0x414  w[4] W [1: Reset@d7da0]
  +0x418  w[1] R/W [7: AimOff@d9bd0, AimOn@d9ba0, Draw@e1970, LetterBoxOn@d9b30, Reset@d7da0, SetMissionFailed@deee0…]
  +0x41c  w[4] R/W [2: DrawGadget@d9c50, GadgetOn@d9bf0]
  +0x420  w[4] W float [4: DrawGadget@d9c50, GadgetOff@d9c30, GadgetOn@d9bf0, Reset@d7da0]
  +0x424  w[4] W [3: GadgetOff@d9c30, GadgetOn@d9bf0, Reset@d7da0]
  +0x428  w[4] W float [4: DrawGadget@d9c50, GadgetOff@d9c30, GadgetOn@d9bf0, Reset@d7da0]
  +0x42c  w[4] W [3: GadgetOff@d9c30, GadgetOn@d9bf0, Reset@d7da0]
  +0x430  w[1] W [2: Reset@d7da0, SetLevelLoadText@d7c20]
  +0x431  w[1] W [1: Reset@d7da0]
  +0x432  w[1] W [1: Reset@d7da0]
  +0x433  w[1] W [3: PauseMenuMsgHandler@dcc10, Render@e0140, Reset@d7da0]
  +0x434  w[1] W [4: CheatMsgHandler@d9140, PauseMenuMsgHandler@dcc10, Render@e0140, Reset@d7da0]
  +0x438  w[4] R/W [4: DrawPauseMenu@dd340, PauseMenuMsgHandler@dcc10, PauseOff@d9050, PauseOn@dcae0]
  +0x43c  w[4] R/W float [4: DrawBloodCurtain@dc590, Reset@d7da0, SetMissionFailed@deee0, TriggerBloodCurtain@d87f0]
  +0x440  w[4] R/W float [8: DrawBloodCurtain@dc590, DrawLetterBox@d9e30, DrawPauseMenu@dd340, DrawScreenHits@d8820, DrawTimer@dc7d0, GetScreenCoords@dc4a0…]
  +0x444  w[4] R/W float [6: DrawBloodCurtain@dc590, DrawLetterBox@d9e30, DrawScreenHits@d8820, GetScreenCoords@dc4a0, IsTargetPointOnScreen@d8170, Reset@d7da0]
  +0x448  w[4] R/W float [5: DrawPauseMenu@dd340, DrawStylin@da540, GetCenter@d9370, Reset@d7da0, dbDrawTarget@da8e0]
  +0x44c  w[4] W float [3: DrawPauseMenu@dd340, Reset@d7da0, dbDrawTarget@da8e0]
  +0x450  w[4] W [1: Reset@d7da0]
  +0x454  w[1] R/W [6: AimOn@d9ba0, DrawHealth@d81d0, DrawPauseMenu@dd340, DrawWeapon@dc990, PauseMenuMsgHandler@dcc10, Reset@d7da0]
  +0x458  w[4] R/W [4: DrawTimer@dc7d0, Reset@d7da0, TriggerTimerOff@d8810, TriggerTimerOn@d8800]
  +0x45c  w[4] W float [3: DrawZoomingCrossHairs@d8c00, Reset@d7da0, SetTarget@d9f50]
  +0x460  w[4] W float [3: DrawZoomingCrossHairs@d8c00, Reset@d7da0, SetTarget@d9f50]
  +0x464  w[4] W float [3: DrawZoomingCrossHairs@d8c00, Reset@d7da0, SetTarget@d9f50]
  +0x468  w[4] W float [3: DrawZoomingCrossHairs@d8c00, Reset@d7da0, SetTarget@d9f50]
  +0x470  w[4] R/W [6: DrawPauseMenu@dd340, PauseMenuMsgHandler@dcc10, PauseOn@dcae0, Render@e0140, Reset@d7da0, SetMissionFailed@deee0]
  +0x474  w[4] R/W [5: DrawPauseMenu@dd340, PauseMenuMsgHandler@dcc10, PauseOn@dcae0, Render@e0140, Reset@d7da0]
  +0x478  w[4] R/W [4: DrawPauseMenu@dd340, PauseMenuMsgHandler@dcc10, PauseOn@dcae0, Reset@d7da0]
  +0x47c  w[4] W [3: PauseOn@dcae0, Reset@d7da0, SetMissionFailed@deee0]
  +0x484  w[4] R/W [4: DrawPauseMenu@dd340, PauseMenuMsgHandler@dcc10, PauseOn@dcae0, Reset@d7da0]
  +0x488  w[4] R/W [4: DrawPauseMenu@dd340, PauseMenuMsgHandler@dcc10, PauseOff@d9050, Reset@d7da0]
  +0x48c  w[4] R/W [3: Draw@e1970, Reset@d7da0, SetControllerUnplugged@e00f0]
  +0x494  w[4] W [1: Reset@d7da0]
  +0x498  w[4] W [1: Reset@d7da0]
  +0x49c  w- LEA addr-taken [3: CheatMsgHandler@d9140, PauseOn@dcae0, Reset@d7da0]
  +0x4a0  w- LEA addr-taken [1: CheatMsgHandler@d9140]
  +0x4d8  w[4] W [1: CheatMsgHandler@d9140]
  +0x4e0  w[4] W float [3: DrawStylin@da540, GHud@dab50, Stylin@d7d60]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] R/W -> EAGL::TAR::~TAR [2: GHud@23ae30, ~GHud@23c560]
  +0x008  w[4] R/W -> ActionQueue::~ActionQueue, Draw::SetModelMatrix, GGallery::BITMAP_Draw2D, GGallery::BITMAP_SetState, GGallery::FONT_Draw [35: Draw@23cf88, DrawBlinkingArrow@2422b8, DrawButtonMessage@245ab8, DrawCrossHairs@23f328, DrawFadingCrossHairs@23f080, DrawFlyByWire@23ead8…]
  +0x00c  w[4] R/W [1: GHud@23ae30]
  +0x010  w[4] R/W [1: GHud@23ae30]
  +0x014  w[4] R/W [1: GHud@23ae30]
  +0x018  w[4] R/W [1: GHud@23ae30]
  +0x01c  w- LEA addr-taken [2: DrawHealth@23da30, GHud@23ae30]
  +0x038  w[4] LEA/W addr-taken [2: DrawHealth@23da30, GHud@23ae30]
  +0x058  w[4] R/W [2: DrawHealth@23da30, GHud@23ae30]
  +0x074  w[4] R/W [2: DrawStylin@2472b0, GHud@23ae30]
  +0x078  w- LEA addr-taken [1: GHud@23ae30]
  +0x084  w[4] R [1: DrawGadget@244f88]
  +0x090  w[4] R/W [2: DrawCrossHairs@23f328, GHud@23ae30]
  +0x094  w[4] R/W [2: DrawCrossHairs@23f328, GHud@23ae30]
  +0x098  w[4] R/W [2: DrawMissionResult@245f68, GHud@23ae30]
  +0x09c  w[4] LEA/R/W addr-taken [2: DrawMissionResult@245f68, GHud@23ae30]
  +0x0a0  w[4] R/W [2: DrawMissionResult@245f68, GHud@23ae30]
  +0x0a4  w[4] R/W [2: DrawMissionResult@245f68, GHud@23ae30]
  +0x0a8  w[4] LEA/R/W addr-taken [2: DrawMissionResult@245f68, GHud@23ae30]
  +0x0ac  w[4] R/W [2: DrawMissionResult@245f68, GHud@23ae30]
  +0x0b0  w[4] R/W [2: DrawMissionResult@245f68, GHud@23ae30]
  +0x0b4  w[4] LEA/R/W addr-taken [2: DrawMissionResult@245f68, GHud@23ae30]
  +0x0b8  w[4] R/W [2: DrawMissionResult@245f68, GHud@23ae30]
  +0x0bc  w[4] R/W [2: DrawMissionResult@245f68, GHud@23ae30]
  +0x0c0  w[4] R/W [3: DrawButtonMessage@245ab8, DrawPauseMenu@2426a0, GHud@23ae30]
  +0x0c4  w[4] R/W [3: DrawButtonMessage@245ab8, DrawPauseMenu@2426a0, GHud@23ae30]
  +0x0c8  w[4] R/W [2: DrawButtonMessage@245ab8, GHud@23ae30]
  +0x0cc  w[4] R/W [2: DrawButtonMessage@245ab8, GHud@23ae30]
  +0x0d0  w[4] R/W [3: DrawButtonMessage@245ab8, DrawPauseMenu@2426a0, GHud@23ae30]
  +0x0d4  w[4] R/W [1: GHud@23ae30]
  +0x0d8  w[4] R/W [2: DrawPauseErrors@241f40, GHud@23ae30]
  +0x0dc  w[4] R/W [2: DrawPauseErrors@241f40, GHud@23ae30]
  +0x0e0  w[4] R/W [2: DrawPauseErrors@241f40, GHud@23ae30]
  +0x0e4  w[4] R/W [2: DrawPauseErrors@241f40, GHud@23ae30]
  +0x0e8  w[4] R/W [2: DrawPauseErrors@241f40, GHud@23ae30]
  +0x0ec  w[4] R/W [2: DrawPauseErrors@241f40, GHud@23ae30]
  +0x0f0  w[4] R/W [1: GHud@23ae30]
  +0x0f4  w[4] LEA/W addr-taken [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x0f8  w[4] W [1: GHud@23ae30]
  +0x0fc  w[4] W [1: GHud@23ae30]
  +0x100  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x104  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x108  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x10c  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x110  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x118  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x11c  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x120  w[4] R/W [2: DrawBlinkingArrow@2422b8, GHud@23ae30]
  +0x124  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x128  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x12c  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x130  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x134  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x138  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x13c  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x140  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x144  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x158  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x15c  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x160  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x164  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x168  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x16c  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x170  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x174  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x178  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x17c  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x180  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x184  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x188  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x18c  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x190  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x1b4  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x1b8  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x1bc  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x1c0  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x1c4  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x1c8  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x1cc  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x1d0  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x1d4  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x1d8  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x1dc  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x1e0  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x1e4  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x1e8  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x1ec  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x1f0  w[4] R/W [3: DrawPauseMenu@2426a0, DrawTimer@23e580, GHud@23ae30]
  +0x1f4  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x1f8  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x1fc  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x200  w[4] R/W [2: DrawPauseMenu@2426a0, GHud@23ae30]
  +0x204  w[4] R/W [2: GHud@23ae30, dbDrawTarget@2477a0]
  +0x208  w[4] R/W [2: DrawRadar2DBlips@244828, GHud@23ae30]
  +0x20c  w[4] R/W [2: DrawRadar2DBlips@244828, GHud@23ae30]
  +0x210  w[4] R/W [2: DrawRadar2DBlips@244828, GHud@23ae30]
  +0x214  w[4] R/W [2: DrawRadar2DBlips@244828, GHud@23ae30]
  +0x218  w[4] R/W [2: DrawCrossHairs@23f328, GHud@23ae30]
  +0x21c  w[4] R/W [3: DrawQRadarArrow@23d288, DrawRadarArrow2D@244c10, GHud@23ae30]
  +0x220  w[4] R/W [2: DrawRadarArrow2D@244c10, GHud@23ae30]
  +0x224  w[4] R/W [2: DrawRadarArrow2D@244c10, GHud@23ae30]
  +0x228  w[4] W [1: GHud@23ae30]
  +0x22c  w[4] W [1: GHud@23ae30]
  +0x230  w[4] R/W [2: DrawQRadar@23d148, GHud@23ae30]
  +0x234  w[4] R/W [2: GHud@23ae30, StartRadarStencil@23c628]
  +0x238  w[4] R/W [2: DrawFlyByWire@23ead8, GHud@23ae30]
  +0x23c  w[4] R/W [2: DrawFlyByWire@23ead8, GHud@23ae30]
  +0x240  w[4] R/W [2: DrawFlyByWire@23ead8, GHud@23ae30]
  +0x244  w[4] R/W [2: DrawFlyByWire@23ead8, GHud@23ae30]
  +0x248  w[4] R/W [2: DrawFlyByWire@23ead8, GHud@23ae30]
  +0x24c  w[4] R/W [2: DrawFlyByWire@23ead8, GHud@23ae30]
  +0x250  w[4] R/W [2: DrawCrossHairs@23f328, GHud@23ae30]
  +0x254  w[4] R/W [2: DrawCrossHairs@23f328, GHud@23ae30]
  +0x258  w[4] R/W [1: GHud@23ae30]
  +0x25c  w[4] R/W [2: DrawMessage@2469b8, GHud@23ae30]
  +0x260  w[4] W [1: GHud@23ae30]
  +0x264  w[4] W [1: GHud@23ae30]
  +0x268  w[4] R/W [2: DrawObjectiveMessage@246c48, GHud@23ae30]
  +0x26c  w[4] R/W [2: DrawObjectiveMessage@246c48, GHud@23ae30]
  +0x270  w[4] R/W [2: DrawCrossHairs@23f328, GHud@23ae30]
  +0x274  w[4] R/W [2: DrawCrossHairs@23f328, GHud@23ae30]
  +0x278  w[4] R/W [2: DrawCrossHairs@23f328, GHud@23ae30]
  +0x27c  w[4] R/W [4: DrawCrossHairs@23f328, DrawFadingCrossHairs@23f080, DrawZoomingCrossHairs@23ec08, GHud@23ae30]
  +0x280  w[4] R/W [2: DrawCrossHairs@23f328, GHud@23ae30]
  +0x284  w[4] R/W [2: DrawCrossHairs@23f328, GHud@23ae30]
  +0x288  w[4] R/W [3: DrawCrossHairs@23f328, DrawFadingCrossHairs@23f080, GHud@23ae30]
  +0x28c  w[4] R/W [2: DrawScreenHits@23e7b8, GHud@23ae30]
  +0x294  w[4] W [1: GHud@23ae30]
  +0x298  w[4] R/W [2: DrawObjectiveTarget@247618, GHud@23ae30]
  +0x29c  w[4] W [1: GHud@23ae30]
  +0x2a0  w[4] R/W [12: Draw@23cf88, DrawFlyByWire@23ead8, DrawMissionResult@245f68, DrawObjectiveMessage@246c48, DrawRadarIsOn@23d118, DrawScreenFades@23e930…]
  +0x2a4  w[4] R/W [2: Reset@23c858, SetControllerUnplugged@23cb58]
  +0x2a8  w[4] R/W -> RPlayerCamera::GetZoomPercent [11: CanToggleInvert@241200, DrawCrossHairs@23f328, DrawHealth@23da30, DrawPauseMenu@2426a0, DrawRadarIsOn@23d118, GetObjectiveOrientation0to1@240f60…]
  +0x2ac  w[4] W [1: Reset@23c858]
  +0x2b0  w[4] R/W [8: Draw@23cf88, DrawLetterBox@245190, DrawRadarIsOn@23d118, DrawScreenHits@23e7b8, GetLetterBoxMode@244ee8, LetterBoxOff@244ec8…]
  +0x2b4  w[4] R/W float [3: DrawLetterBox@245190, LetterBoxOn@244ea0, Reset@23c858]
  +0x2b8  w[4] R/W float [2: DrawBlinkingArrow@2422b8, PauseOn@241048]
  +0x2bc  w[4] R/W float [1: DrawBlinkingArrow@2422b8]
  +0x2c0  w[4] R/W float [1: DrawBlinkingArrow@2422b8]
  +0x2c4  w[4] R/W [2: DrawBlinkingArrow@2422b8, PauseOn@241048]
  +0x2c8  w[4] R/W [2: DrawTimer@23e580, Reset@23c858]
  +0x2cc  w[4] R/W float [3: DrawHealth@23da30, PauseOn@241048, Reset@23c858]
  +0x2d0  w[4] R/W float [2: DrawHealth@23da30, Reset@23c858]
  +0x2d4  w[4] R/W float [2: DrawHealth@23da30, Reset@23c858]
  +0x2d8  w[4] W float [3: PauseOn@241048, Render@23cbc0, Reset@23c858]
  +0x2dc  w[4] W [2: Render@23cbc0, Reset@23c858]
  +0x2e0  w[4] W [2: Render@23cbc0, Reset@23c858]
  +0x2e4  w[4] W float [2: Render@23cbc0, Reset@23c858]
  +0x2e8  w[4] R/W float [3: DrawStylin@2472b0, Reset@23c858, Stylin@23c820]
  +0x2ec  w[4] R/W [3: DrawStylin@2472b0, Reset@23c858, Stylin@23c820]
  +0x2f0  w[4] R/W float [1: DrawStylin@2472b0]
  +0x2f4  w[4] R/W float [3: DrawStylin@2472b0, Reset@23c858, Stylin@23c820]
  +0x2f8  w[4] R/W [4: DrawButtonMessage@245ab8, KillButtonMessage@245eb0, Reset@23c858, TriggerButtonMsg@2459d0]
  +0x2fc  w[4] R/W -> GSystem::LOCALE_Symbol [2: DrawButtonMessage@245ab8, TriggerButtonMsg@2459d0]
  +0x300  w[4] R/W float [5: DrawButtonMessage@245ab8, KillButtonMessage@245eb0, Reset@23c858, TriggerButtonMsg@2459d0, TriggerObjectiveMsg@245910]
  +0x304  w[4] R/W float [4: DrawButtonMessage@245ab8, KillButtonMessage@245eb0, Reset@23c858, TriggerButtonMsg@2459d0]
  +0x308  w[4] W [3: DrawObjectiveMessage@246c48, Reset@23c858, TriggerObjectiveMsg@245910]
  +0x30c  w[4] R/W [3: DrawCrossHairs@23f328, DrawZoomingCrossHairs@23ec08, Reset@23c858]
  +0x310  w[4] R/W [7: DrawMissionResult@245f68, DrawPauseMenu@2426a0, PauseMenuMsgHandler@241210, PauseOn@241048, Render@23cbc0, Reset@23c858…]
  +0x314  w[4] R/W [8: Draw@23cf88, DrawBloodCurtain@23e2c0, DrawMissionResult@245f68, Render@23cbc0, Reset@23c858, SetMissionFailed@245708…]
  +0x318  w[4] R/W float [3: DrawMissionResult@245f68, Reset@23c858, SetMissionFailed@245708]
  +0x31c  w[4] R/W float [2: DrawMissionResult@245f68, Reset@23c858]
  +0x320  w[4] R/W float [2: DrawMissionResult@245f68, Reset@23c858]
  +0x324  w[4] R/W float [4: DrawMissionResult@245f68, Render@23cbc0, Reset@23c858, SetMissionFailed@245708]
  +0x328  w[4] R/W [3: DrawScreenHits@23e7b8, Reset@23c858, TriggerDamageFlash@23e270]
  +0x32c  w[4] R/W [3: DrawScreenHits@23e7b8, Reset@23c858, TriggerDamageFlash@23e270]
  +0x330  w[4] R/W [3: EnableLavaDamageEffect@23c6f8, LavaDamageEffectEnabled@23c718, Reset@23c858]
  +0x334  w[4] W [1: Reset@23c858]
  +0x338  w[4] R/W float [2: DrawRadarArrow2D@244c10, Reset@23c858]
  +0x33c  w[4] R/W [2: DrawRadarArrow2D@244c10, Reset@23c858]
  +0x340  w[4] LEA/W addr-taken [2: DrawQRadarPulse@23d210, Reset@23c858]
  +0x344  w[4] W [1: Reset@23c858]
  +0x348  w[4] W [1: Reset@23c858]
  +0x34c  w[4] R/W float [5: DrawScreenFades@23e930, FadeInFromBlack@23c728, FadeOutToBlack@23c780, GetBlackFadePct@23c818, Reset@23c858]
  +0x350  w[4] R/W [5: DrawScreenFades@23e930, FadeInFromBlack@23c728, FadeOutToBlack@23c780, GetBlackFadeAnimating@23c810, Reset@23c858]
  +0x354  w[4] R/W [4: DrawScreenFades@23e930, FadeInFromBlack@23c728, FadeOutToBlack@23c780, Reset@23c858]
  +0x358  w[4] R/W float [4: DrawScreenFades@23e930, FadeInFromBlack@23c728, FadeOutToBlack@23c780, Reset@23c858]
  +0x35c  w[4] R/W float [3: DrawScreenFades@23e930, FadeInFromBlack@23c728, Reset@23c858]
  +0x360  w[4] R/W [3: Render@23cbc0, Reset@23c858, TriggerMissionMsg@245890]
  +0x364  w[4] R/W [2: DrawStylin@2472b0, Render@23cbc0]
  +0x368  w[4] R/W float [8: DrawButtonMessage@245ab8, DrawGadget@244f88, DrawHealth@23da30, DrawMessage@2469b8, DrawMissionResult@245f68, DrawObjectiveMessage@246c48…]
  +0x36c  w[4] R/W float [2: DrawHealth@23da30, Reset@23c858]
  +0x370  w[4] R/W float [1: DrawHealth@23da30]
  +0x374  w[4] W [1: Reset@23c858]
  +0x378  w[4] W [1: Reset@23c858]
  +0x380  w[4, 8] R/W float [4: DrawCrossHairs@23f328, DrawEnemyTarget@2470f8, Reset@23c858, SetTarget@2457a0]
  +0x384  w[4] R/W float [4: DrawCrossHairs@23f328, DrawEnemyTarget@2470f8, Reset@23c858, SetTarget@2457a0]
  +0x388  w[4] R/W [4: DrawCrossHairs@23f328, DrawEnemyTarget@2470f8, Reset@23c858, SetTargetLockState@245800]
  +0x38c  w[4] R/W [4: DrawFadingCrossHairs@23f080, DrawZoomingCrossHairs@23ec08, Reset@23c858, SetTargetLockState@245800]
  +0x390  w[4] R/W [4: DrawFadingCrossHairs@23f080, DrawZoomingCrossHairs@23ec08, GHud@23ae30, Reset@23c858]
  +0x394  w[4] R/W float [3: DrawFadingCrossHairs@23f080, DrawZoomingCrossHairs@23ec08, SetTargetLockState@245800]
  +0x398  w[4] R/W float [3: DrawFadingCrossHairs@23f080, DrawZoomingCrossHairs@23ec08, SetTargetLockState@245800]
  +0x39c  w[4] R/W -> ATargeting::SetState [3: GHud@23ae30, Reset@23c858, SetTargetLockState@245800]
  +0x3a0  w[4] R/W [3: DrawEnemyTarget@2470f8, Reset@23c858, SetTargetLockState@245800]
  +0x3a4  w[4] R/W [3: DrawObjectiveTarget@247618, DrawRadar2DBlips@244828, Reset@23c858]
  +0x3a8  w[4] R/W float [5: ClearObjectiveMessage@245ec0, DrawObjectiveMessage@246c48, Reset@23c858, TriggerButtonMsg@2459d0, TriggerObjectiveMsg@245910]
  +0x3ac  w[4] R/W float [5: ClearObjectiveMessage@245ec0, DrawObjectiveMessage@246c48, Reset@23c858, TriggerButtonMsg@2459d0, TriggerObjectiveMsg@245910]
  +0x3b0  w[4] R/W float [5: ClearObjectiveMessage@245ec0, DrawObjectiveMessage@246c48, Reset@23c858, TriggerButtonMsg@2459d0, TriggerObjectiveMsg@245910]
  +0x3b4  w[4] R/W -> GSystem::LOCALE_Symbol [4: ClearObjectiveMessage@245ec0, DrawObjectiveMessage@246c48, Reset@23c858, TriggerObjectiveMsg@245910]
  +0x3b8  w[4] R/W [4: ClearObjectiveMessage@245ec0, DrawObjectiveMessage@246c48, Reset@23c858, TriggerObjectiveMsg@245910]
  +0x3bc  w[4] R/W [3: DrawObjectiveMessage@246c48, Reset@23c858, TriggerObjectiveMsg@245910]
  +0x3c0  w[4] R/W [4: ClearObjectiveMessage@245ec0, DrawObjectiveMessage@246c48, Reset@23c858, TriggerObjectiveMsg@245910]
  +0x3c4  w[4] R/W [3: DrawPowerupTarget@2471e0, Reset@23c858, UpdateTargets@23d6a8]
  +0x3c8  w[4] R/W [2: DrawPowerupTarget@2471e0, UpdateTargets@23d6a8]
  +0x3d0  w[8] W [1: UpdateTargets@23d6a8]
  +0x3d8  w[4] W [1: Reset@23c858]
  +0x3dc  w[4] R/W [3: DrawObjectiveTarget@247618, Reset@23c858, UpdateTargets@23d6a8]
  +0x3e0  w[4] W [1: UpdateTargets@23d6a8]
  +0x3f0  w[8] LEA/W addr-taken [2: DrawObjectiveTarget@247618, UpdateTargets@23d6a8]
  +0x3f8  w[4] R [1: DrawObjectiveTarget@247618]
  +0x3fc  w[4] W [1: Reset@23c858]
  +0x400  w[4] W [1: Reset@23c858]
  +0x404  w[4] R/W [3: DrawMessage@2469b8, Reset@23c858, TriggerMissionMsg@245890]
  +0x408  w[4] R/W float [3: DrawMessage@2469b8, Reset@23c858, TriggerMissionMsg@245890]
  +0x40c  w[4] R/W float [1: DrawMessage@2469b8]
  +0x410  w[4] R/W [5: DrawHealth@23da30, DrawMessage@2469b8, DrawWeapon@240c98, Reset@23c858, TriggerMissionMsg@245890]
  +0x414  w[4] W [1: Reset@23c858]
  +0x418  w[4] R/W [7: AimOff@244f30, AimOn@244ef0, Draw@23cf88, DrawCrossHairs@23f328, IsAimOn@247d90, LetterBoxOn@244ea0…]
  +0x41c  w[4] R/W [2: DrawGadget@244f88, GadgetOn@244f48]
  +0x420  w[4] R/W float [4: DrawGadget@244f88, GadgetOff@244f70, GadgetOn@244f48, Reset@23c858]
  +0x424  w[4] R/W float [4: DrawGadget@244f88, GadgetOff@244f70, GadgetOn@244f48, Reset@23c858]
  +0x428  w[4] R/W float [4: DrawGadget@244f88, GadgetOff@244f70, GadgetOn@244f48, Reset@23c858]
  +0x42c  w[4] R/W [4: DrawGadget@244f88, GadgetOff@244f70, GadgetOn@244f48, Reset@23c858]
  +0x430  w[4] W [2: Reset@23c858, SetLevelLoadText@23c6c8]
  +0x434  w[4] R/W [2: DrawHealth@23da30, Reset@23c858]
  +0x438  w[4] R/W [2: DrawCrossHairs@23f328, Reset@23c858]
  +0x43c  w[4] W [3: PauseMenuMsgHandler@241210, Render@23cbc0, Reset@23c858]
  +0x440  w[4] W [4: CheatMsgHandler@241bb8, PauseMenuMsgHandler@241210, Render@23cbc0, Reset@23c858]
  +0x444  w[4] R/W [4: DrawPauseMenu@2426a0, PauseMenuMsgHandler@241210, PauseOff@2411a0, PauseOn@241048]
  +0x448  w[4] R/W float [4: DrawMissionResult@245f68, Reset@23c858, SetMissionFailed@245708, TriggerBloodCurtain@23e558]
  +0x44c  w[4] R/W float [9: DrawCrossHairs@23f328, DrawLetterBox@245190, DrawPauseMenu@2426a0, DrawScreenFades@23e930, DrawScreenHits@23e7b8, DrawTimer@23e580…]
  +0x450  w[4] R/W float [6: DrawLetterBox@245190, DrawScreenFades@23e930, DrawScreenHits@23e7b8, GetScreenCoords@23d568, IsTargetPointOnScreen@23d9c8, Reset@23c858]
  +0x454  w[4] R/W float [8: DrawCrossHairs@23f328, DrawGadget@244f88, DrawObjectiveMessage@246c48, DrawPauseMenu@2426a0, DrawStylin@2472b0, GetCenter@241ea8…]
  +0x458  w[4] R/W float [5: DrawCrossHairs@23f328, DrawGadget@244f88, DrawPauseMenu@2426a0, Reset@23c858, dbDrawTarget@2477a0]
  +0x45c  w[4] R/W float [2: DrawCrossHairs@23f328, Reset@23c858]
  +0x460  w[4] R/W [7: AimOn@244ef0, DrawCrossHairs@23f328, DrawHealth@23da30, DrawPauseMenu@2426a0, DrawWeapon@240c98, PauseMenuMsgHandler@241210…]
  +0x464  w[4] R/W [4: DrawTimer@23e580, Reset@23c858, TriggerTimerOff@23e570, TriggerTimerOn@23e568]
  +0x468  w[4] R/W float [4: DrawCrossHairs@23f328, DrawZoomingCrossHairs@23ec08, Reset@23c858, SetTarget@2457a0]
  +0x46c  w[4] R/W float [4: DrawCrossHairs@23f328, DrawZoomingCrossHairs@23ec08, Reset@23c858, SetTarget@2457a0]
  +0x470  w[4] R/W float [4: DrawCrossHairs@23f328, DrawZoomingCrossHairs@23ec08, Reset@23c858, SetTarget@2457a0]
  +0x474  w[4] R/W float [4: DrawCrossHairs@23f328, DrawZoomingCrossHairs@23ec08, Reset@23c858, SetTarget@2457a0]
  +0x480  w[4] R/W [6: DrawPauseMenu@2426a0, PauseMenuMsgHandler@241210, PauseOn@241048, Render@23cbc0, Reset@23c858, SetMissionFailed@245708]
  +0x484  w[4] R/W [5: DrawPauseMenu@2426a0, PauseMenuMsgHandler@241210, PauseOn@241048, Render@23cbc0, Reset@23c858]
  +0x488  w[4] R/W [4: DrawPauseMenu@2426a0, PauseMenuMsgHandler@241210, PauseOn@241048, Reset@23c858]
  +0x48c  w[4] W [3: PauseOn@241048, Reset@23c858, SetMissionFailed@245708]
  +0x494  w[4] R/W [4: DrawPauseMenu@2426a0, PauseMenuMsgHandler@241210, PauseOn@241048, Reset@23c858]
  +0x498  w[4] R/W [4: DrawPauseMenu@2426a0, PauseMenuMsgHandler@241210, PauseOff@2411a0, Reset@23c858]
  +0x49c  w[4] R/W [3: Draw@23cf88, Reset@23c858, SetControllerUnplugged@23cb58]
  +0x4a4  w[4] W [1: Reset@23c858]
  +0x4a8  w[4] W [1: Reset@23c858]
  +0x4ac  w- LEA addr-taken [1: CheckCheatInput@241b38]
  +0x4e8  w[4] LEA/W addr-taken [3: CheatMsgHandler@241bb8, PauseOn@241048, Reset@23c858]
  +0x4f0  w[4] R/W float [3: DrawStylin@2472b0, GHud@23ae30, Stylin@23c820]
  +0x4f4  w[4] W [2: GHud@23ae30, ~GHud@23c560]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  SetLevelLoadText: W +0x430 w1
  LavaDamageEffectEnabled: R +0x330 w4
  GetBlackFadeAnimating: R +0x350 w1
  GetBlackFadePct: W +0x34c w4 float
  TriggerBloodCurtain: W +0x43c w4
  TriggerTimerOn: W +0x458 w4
  TriggerTimerOff: W +0x458 w4
  SetAutoDrive: W +0x2b2 w1
  LetterBoxOff: W +0x2b4 w4
  GetLetterBoxMode: R +0x2b4 w4
  AimOff: W +0x418 w1
