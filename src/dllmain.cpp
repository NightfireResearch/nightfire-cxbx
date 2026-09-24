#include <windows.h>
#include <stdio.h>

#include "inject.h"
#include "common/console.h"

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
  if (fdwReason == DLL_PROCESS_ATTACH) {
    // Injected into a CXBX-hosted process there is nowhere to print, because both launchers are GUI
    // executables; under the standalone loader there usually is already. EnsureConsoleOutput tells those
    // apart without throwing away a redirection - see the comment there, and note that a valid stdout handle
    // on its own does not mean anyone can see it.
    EnsureConsoleOutput();
    Inject();
  }
  return TRUE;
}
