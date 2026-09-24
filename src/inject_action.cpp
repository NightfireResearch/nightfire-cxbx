#include "inject.h"

#include "action/math/math.h"
#include "action/game.h"
#include "action/input.h"
#include "action/util/crc.h"
#include "action/memory.h"
#include "action/sound/music.h"
#include "action/ui/ui.h"
#include "action/engine/psiFile.h"
#include "action/engine/XboxFile.h"
#include "action/engine/XboxStartup.h"
#include "action/engine/psiSave.h"
#include "action/engine/Direct3D/d3dSeam.h"
#include "action/sound/dsndSeam.h"
#include "action/sound/dsndStream.h"
#include "action/game/view.h"
#include "action/game/obj/car.h"
#include "action/game/obj/Light.h"
#include "action/game/obj/Switch.h"
#include "action/game/obj/ScriptPlayer.h"
#include "action/game/mp/multiplayer.h"

#include "common/launchInfo.h"

#include "cxbx/cxbxbinding.h"

void WriteMemory(size_t offset, void *data, size_t size)
{
  memcpy((void *) offset, data, size);
}

void WriteByte(size_t offset, unsigned char byte)
{
  WriteMemory(offset, &byte, 1);
}

void FillBytes(size_t offset, unsigned char byte, size_t count)
{
  memset((void*)offset, byte, count);
}

void WriteJmpTo(size_t from, size_t to)
{
  size_t relative = to - (from + 5);

  WriteByte(from, 0xE9);
  WriteMemory(from + 1, &relative, sizeof(relative));
}

void Inject()
{
  // Instruction-level patches that are not whole-function replacements. Done first, so that nothing the rest
  // of this function sets up can run against un-patched XAPI.
  Inject_XboxStartup();

  // The DirectSound stream entry points, but only when no emulator is hosting this process - see
  // DSoundStream_InstallHooks. Under CXBX these addresses already carry CXBX's own patches.
  DSoundStream_InstallHooks();


  // Resolution beyond the original 640x480.
  //
  // The notes that were here described CXBX's behaviour and are no longer true. Under the standalone loader
  // and the D3D9 backend, 1920x1080 was measured in September 2026 as running the whole way: the device is
  // created at that size, Mem_Init - which the old notes named as the crash point for anything above
  // 1024x768 - completes, levels load, shaders translate, background movies play, and the frame rate is
  // unchanged at 50 fps and about 0.9 ms of work per frame. The cost is nil because the limit here is draw
  // call submission on the CPU, not fill rate. Widescreen=1 in settings.ini alongside it also runs clean,
  // which is the right pairing for a 16:9 display since the game has its own 16:9 projection.
  //
  // How it looks is the remaining problem, and it is 2D, not 3D. Observed at 1920x1080: the 3D views are
  // correct and sharp, and the window is now created at the render resolution (SizeWindowToBackBuffer in
  // Direct3D/d3d9Backend.cpp). But the main menus draw at their 640x480 pixel size in the top-left corner,
  // the movie letterboxing does the same, and HUD elements are a mixture - the ones rewritten since (the
  // crosshairs, for instance) follow the resolution, while the rest are drawn at the wrong scale. The cause
  // is in the sprite tables in ui/HUD.cpp, which mix entries derived from SCREEN_WIDTH/SCREEN_HEIGHT with
  // entries carrying hard-coded 640x480 coordinates; there is a "TODO: Change from 640x480 to generic"
  // sitting above one of them. That is the "UI elements misaligned" the old notes mention at 800x600, and
  // it is the remaining work rather than a crash.
  //
  // So this is left at 640x480 by default, and raising it is two lines in action/actionhelpers.h.
  int width = SCREEN_WIDTH;
  int height = SCREEN_HEIGHT;
  float fWidth = (float)width;
  float fHeight = (float)height;

  // The D3DCreateDevice backbuffer size used to be patched into xboxInitGraphics's immediates here
  // (0x000e6efc/0x000e6f04); that function is now reimplemented in d3dSeam.cpp and reads SCREEN_WIDTH/
  // SCREEN_HEIGHT directly, so those two patches are gone.

  // Likewise d3dSetup's viewport size (0x000e6ceb/0x000e6cd7) - reimplemented in d3dSeam.cpp, reads
  // SCREEN_WIDTH/SCREEN_HEIGHT directly.

  // This is psiPostDraw
  WriteMemory(0x000dd8e6, &width, 4);
  WriteMemory(0x000dd8e1, &height, 4);

  // psiFadeView - full screen sprite for fade-out
  WriteMemory(0x000de594, &fWidth, 4);
  WriteMemory(0x000de59c, &fHeight, 4);

  // Camera_CreateCameras has multiple, covering camera dimensions and positioning of MP windows
  WriteMemory(0x0025d23, &width, 4); // Camera 0 create
  WriteMemory(0x0025d1c, &height, 4);
  WriteMemory(0x0025d45, &width, 4); // Camera 5 create
  WriteMemory(0x0025d40, &height, 4);
  WriteMemory(0x0025d62, &width, 4); // Camera 7 create
  WriteMemory(0x0025d5d, &height, 4);
  WriteMemory(0x0025d82, &width, 4); // Camera 4 create
  WriteMemory(0x0025d7d, &height, 4);
  WriteMemory(0x0025d97, &width, 4); // Camera 6 create
  WriteMemory(0x0025d92, &height, 4);
  WriteMemory(0x0025daf, &width, 4); // Camera 8 create
  WriteMemory(0x0025daa, &height, 4);
  WriteMemory(0x0025dc4, &width, 4); // Camera 10 create
  WriteMemory(0x0025dbf, &height, 4);
  WriteMemory(0x0025e11, &width, 4); // Camera 9 create
  WriteMemory(0x0025e0c, &height, 4);
  WriteMemory(0x0025e51, &width, 4); // Player first-person camera, and multiplayer cameras
  WriteMemory(0x0025e4c, &height, 4);
  WriteMemory(0x0025e87, &fWidth, 4); // Camera to screen pixel bounds (single player only)
  WriteMemory(0x0025e82, &fHeight, 4);
  // TODO: Multiplayer scaling
  // TODO: Label_Init
  // TODO: Page_Init
  // TODO: HUD_CreateRadar


  //  FUN_0008fdd0 - Sprite related?
  WriteMemory(0x008fdd8, &width, 4);
  WriteMemory(0x008fde5, &height, 4);

  // Around 00093ef1 in FUN_00093960 - menu-related?
  WriteMemory(0x00093ef2, &width, 4);
  WriteMemory(0x00093efe, &height, 4);

  // Around 000e7212 in FUN_000e7130 - call to XGSetTextureHeader - buffer for full-screen blur effect?
  WriteMemory(0x000e7218, &width, 4);
  WriteMemory(0x000e7213, &height, 4);
  WriteMemory(0x000e7306, &fWidth, 4);
  WriteMemory(0x000e7301, &fHeight, 4);
  WriteMemory(0x000e7312, &fWidth, 4);
  WriteMemory(0x000e730d, &fHeight, 4);

  // Around FUN_000e8a90 - FMV related?
  // Early bit - set up?
  // WriteMemory(0x000e8adc, &width, 4);
  // WriteMemory(0x000e8ad7, &height, 4);
  // Later bit - compare to something else?
  // TODO: This


  /*
   * Function Patching
   */
#include "action/autogenerated_injections.inc"


  // Patched to enable Action and Driving to pass messages
  // These are common to both, so we can't use autogeneration
  WriteJmpTo(0x000eb0fb, (size_t)&XLaunchNewImageA);
  WriteJmpTo(0x000eb050, (size_t)&XGetLaunchInfo);


  // Special case for weapon stats
  // This has the limitation that the DLL must be injected before the constructor is called otherwise it will have no effect
  void *ptrCtorWeaponDefinitionTable = &ctor_WeaponDefinitionTable;
  WriteMemory(0x0016313c, &ptrCtorWeaponDefinitionTable, 4);

}