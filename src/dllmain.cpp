#include <windows.h>
#include <stdio.h>

#include "inject.h"
#include "common/console.h"

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
  if (fdwReason == DLL_PROCESS_ATTACH) {
    // The loader is a console executable, so there is usually somewhere to print already, but not when it
    // was started detached. EnsureConsoleOutput tells those apart without throwing away a redirection - see
    // the comment there, and note that a valid stdout handle on its own does not mean anyone can see it.
    EnsureConsoleOutput();
    Inject();
  }
  return TRUE;
}
