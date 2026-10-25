#ifndef LAUNCHER_SELFTEST_H_
#define LAUNCHER_SELFTEST_H_

// launcher --selftest: first-time setup, a first install, updates from a fake releases API (good, bad signature,
// bad hash, no network, minLauncher), undo, and the game started for a while - each step's PASS or FAIL printed,
// and the exit code 0 only if all pass. SelfTest.cpp lists the arguments.

int SelfTest_Run(int argc, char **argv);

#endif // LAUNCHER_SELFTEST_H_
