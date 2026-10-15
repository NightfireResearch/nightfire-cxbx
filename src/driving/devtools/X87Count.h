#ifndef DRIVING_DEVTOOLS_X87COUNT_H_
#define DRIVING_DEVTOOLS_X87COUNT_H_

// NIGHTFIRE_X87COUNT=1: counts the calls of each x87 assembly function (docs/driving/64-bit.md 4.1) and keeps a
// few of their arguments. See X87Count.cpp.

// From Inject(), after the injection table, before the game starts.
void X87Count_Install(void);

// From Teleport_Tick, once per simulation tick on the game thread.
void X87Count_Tick(void);

#endif // DRIVING_DEVTOOLS_X87COUNT_H_
