#ifndef DRIVING_DEVTOOLS_STATICINITDUMP_H_
#define DRIVING_DEVTOOLS_STATICINITDUMP_H_

// NIGHTFIRE_STATICDUMP / NIGHTFIRE_STATICORIG: the static initialisers' results, ours or the XBE's, dumped for an
// offline comparison (StaticInitDump.cpp).

// Runs the XBE's own C++ initialiser table when NIGHTFIRE_STATICORIG=1, and says whether it did.
bool StaticInitDump_RunOriginalTable(void);

// When NIGHTFIRE_STATICDUMP names a file: writes .data, .bss and the atexit list there, and exits.
void StaticInitDump_Write(void);

#endif // DRIVING_DEVTOOLS_STATICINITDUMP_H_
