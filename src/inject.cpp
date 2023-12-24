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
  // 800x600: Stable but does not fill screen
  // 1024x768: Various graphics are broken entirely, videos fail to play
  // 1280x720: crashes at Mem_Init
  // 1920x1080: crashes at Mem_Init
  int width = 640;
  int height = 480;

  // This is in the params to D3DCreateDevice
  WriteMemory(0x000e6efc, &width, 4);
  WriteMemory(0x000e6f04, &height, 4);

  // This is D3DDevice_SetViewport(&local_98), and a global, slightly later on
  WriteMemory(0x000e6ceb, &width, 4);
  WriteMemory(0x000e6cd7, &height, 4);


  // Patch filesystem handling, to enable studying / extraction / patching of assets  
  WriteJmpRet(0x000e04a0, (size_t)&psiFileOpen);
  WriteJmpRet(0x000dca30, (size_t)&psiFileLoad);
}