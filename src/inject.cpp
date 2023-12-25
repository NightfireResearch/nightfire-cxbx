#include "inject.h"

#include "cxbx/cxbxbinding.h"

int __cdecl psiFileOpen(int param_1);
int ** __cdecl psiFileLoad(char *filename, unsigned short allocType, int *sizeOut);

void WriteMemory(size_t offset, void *data, size_t size)
{
  memcpy((void *) offset, data, size);
}

void WriteByte(size_t offset, unsigned char byte)
{
  WriteMemory(offset, &byte, 1);
}

void WriteBytes(size_t offset, unsigned char byte, size_t count)
{
  for (size_t i = 0; i < count; i++) {
  	WriteByte(offset + i, byte);
  }
}

void WriteJmpRet(size_t from, size_t to)
{
  size_t relative = to - (from + 5);

  WriteByte(from, 0xE9);
  WriteMemory(from + 1, &relative, sizeof(relative));
}

void Inject()
{

  // Experiments with increasing resolution beyond original limits
  
  // 640x480: Default
  // 800x600: Stable, UI elements misaligned
  // 1024x768: Various graphics are broken entirely, videos fail to play, will crash if cameras are scaled
  // 1280x720: crashes at Mem_Init
  // 1920x1080: crashes at Mem_Init
  int width = 640;
  int height = 480;
  float fWidth = (float)width;
  float fHeight = (float)height;

  // This is in the params to D3DCreateDevice
  WriteMemory(0x000e6efc, &width, 4);
  WriteMemory(0x000e6f04, &height, 4);

  // This is D3DDevice_SetViewport(&local_98), and a global, slightly later on
  WriteMemory(0x000e6ceb, &width, 4);
  WriteMemory(0x000e6cd7, &height, 4);

  // This is psiPostDraw
  WriteMemory(0x000dd8e6, &width, 4);
  WriteMemory(0x000dd8e1, &height, 4);

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


  //  FUN_0008fdd0 - Sprite related?
  WriteMemory(0x008fdd8, &width, 4);
  WriteMemory(0x008fde5, &height, 4);

  // Around 00093ef1 in FUN_00093960 - menu-related?
  WriteMemory(0x00093ef2, &width, 4);
  WriteMemory(0x00093efe, &height, 4);

  // Around 000e7212 in FUN_000e7130 - call to XGSetTextureHeader - buffer for full-screen blur effect?
  WriteMemory(0x000e7218, &width, 4);
  WriteMemory(0x000e7213, &height, 4);

  // Around FUN_000e8a90 - FMV related?
  // Early bit - set up?
  // WriteMemory(0x000e8adc, &width, 4);
  // WriteMemory(0x000e8ad7, &height, 4);
  // Later bit - compare to something else?
  // TODO: This


  

  // Patch filesystem handling, to enable studying / extraction / patching of assets  
  WriteJmpRet(0x000e04a0, (size_t)&psiFileOpen);
  WriteJmpRet(0x000dca30, (size_t)&psiFileLoad);
}