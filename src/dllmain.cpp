#include <windows.h>
#include <stdio.h>

#include "inject.h"

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
  if (fdwReason == DLL_PROCESS_ATTACH) {
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    Inject();
  }
  return TRUE;
}
