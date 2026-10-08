#ifndef DRIVING_DEVTOOLS_SNDCALLTRACE_H_
#define DRIVING_DEVTOOLS_SNDCALLTRACE_H_

// NIGHTFIRE_SNDTRACE=1: a log of every call into the sound library's API, one line per call. See SndCallTrace.cpp.

// From Inject(), after the injection table (it moves the jumps the table wrote) and after the shadow tests.
void SndCallTrace_Install(void);

// From Scheduler.cpp's RunTick, first thing: a simulation tick begins, and the last one's calls are written out.
void SndCallTrace_Tick(void);

#endif // DRIVING_DEVTOOLS_SNDCALLTRACE_H_
