#ifndef COMMON_CONSOLE_H_
#define COMMON_CONSOLE_H_

#include <windows.h>
#include <stdio.h>

// ---------------------------------------------------------------------------------------------------------------
// Makes sure everything the game prints ends up somewhere a person can read.
//
// There are three ways this process can be started and they need different things done:
//
//  - from a terminal, or double-clicked (the standalone loader is a console executable, so Windows gives it
//    one): a console window already exists and stdout already points at it. Leave it alone.
//  - with stdout redirected to a file or a pipe, which is how tools/drive_game.ps1 captures a whole boot:
//    leave that alone too, or the output being collected would be thrown away.
//  - with no console at all. Both CXBX launchers are GUI-subsystem executables, so a DLL injected into that
//    world has nowhere to write; and a console executable started detached is in the same position, holding a
//    stdout handle that is valid but that nobody can see. This is the case that needs a console made.
//
// Testing the handle alone is not enough to tell the third case from the first, which is the mistake that
// lost the output: a detached process has a perfectly valid stdout handle attached to a console window that
// does not exist. Asking whether there is a console *window* is what actually distinguishes them.
// ---------------------------------------------------------------------------------------------------------------

static inline void EnsureConsoleOutput(void) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD type = (out != NULL && out != INVALID_HANDLE_VALUE) ? GetFileType(out) : FILE_TYPE_UNKNOWN;

    // Redirected somewhere deliberate - a file or a pipe. Whoever did that wants the output there.
    if (type == FILE_TYPE_DISK || type == FILE_TYPE_PIPE)
        return;

    // A console window already exists and stdout is a character device, so it is already being displayed.
    if (GetConsoleWindow() != NULL && type == FILE_TYPE_CHAR)
        return;

    if (!AllocConsole() && GetConsoleWindow() == NULL)
        return;   // nothing more to be done; better to carry on silently than to fail to start

    // Both streams, because a crash report is no use with half of it missing. freopen_s is used rather than
    // freopen only to keep the compiler quiet about the deprecated form.
    FILE *reopened = NULL;
    freopen_s(&reopened, "CONOUT$", "w", stdout);
    freopen_s(&reopened, "CONOUT$", "w", stderr);

    // Unbuffered, so that whatever was printed immediately before a fault has actually been written.
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
}

#endif // COMMON_CONSOLE_H_
