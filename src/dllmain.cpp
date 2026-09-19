#include <windows.h>
#include <stdio.h>

#include "inject.h"

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
  if (fdwReason == DLL_PROCESS_ATTACH) {
    // Only make a console if the process has not already got somewhere to write. Injected into a CXBX-hosted
    // process there is nowhere, so one is needed. Under nfloader, though, stdout is already the loader's -
    // possibly redirected to a file - and taking it over would silently discard everything printed from here
    // on, including the loader's own diagnostics.
    HANDLE existing = GetStdHandle(STD_OUTPUT_HANDLE);
    if (existing == NULL || existing == INVALID_HANDLE_VALUE) {
      AllocConsole();
      freopen("CONOUT$", "w", stdout);
    }
    Inject();
  }
  return TRUE;
}
