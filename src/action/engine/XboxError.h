#ifndef XBOXERROR_H_
#define XBOXERROR_H_

// The Xbox layer's two error screens (0x000e22b0-0x000e24a0, 0x000e9190): a 40-line text log drawn in the debug
// font, and the "disc is dirty or damaged" screen a failed disc read ends on.

void logError(const char *format, ...);       // adds a line to the error log
void ShowFatalErrorScreen(bool doSwap);       // draws the log; with doSwap, as a whole frame of its own
void FS_FatalErrorHandler(void);              // the disc error screen, forever
void xboxInitTextures(void);                  // the debug font the error log is drawn in

// Ours: the end of a fatal error the original stopped at with INT3. Says why, then exits.
void XboxError_Halt(const char *what, const char *detail);

#endif // XBOXERROR_H_
