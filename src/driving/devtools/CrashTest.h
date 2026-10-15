#ifndef DRIVING_DEVTOOLS_CRASHTEST_H_
#define DRIVING_DEVTOOLS_CRASHTEST_H_

// NIGHTFIRE_CRASHTEST=<ms>: a deliberate fault that many milliseconds after start, for checking the standalone
// executable's crash reporter (standalone/CrashReport.h). driving_standalone_test only.

// From the standalone executable's main, after the crash reporter is installed.
void CrashTest_Start(void);

#endif // DRIVING_DEVTOOLS_CRASHTEST_H_
