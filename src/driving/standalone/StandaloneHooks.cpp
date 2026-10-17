// The devtools hooks game code calls that neither standalone build has: they need the original image (the static
// initialisers' table dump, the TRAP_* modes, the shadow tests, the XBE profiler) or are conveniences of the injected
// build (the file dumps). Each does nothing, as its devtools body does when its setting is off.

#include "../devtools/FileDump.h"
#include "../devtools/ShadowTicks.h"
#include "../devtools/StandaloneTraps.h"
#include "../devtools/StaticInitDump.h"
#include "../../common/xbeProfiler.h"

void FileDump_Save(const char *, const void *, size_t) {
}

bool StaticInitDump_RunOriginalTable(void) {
    return false;
}

void StaticInitDump_Write(void) {
}

void StandaloneTraps_BeforeInject(void) {
}

void StandaloneTraps_AfterInject(void) {
}

void StandaloneTraps_FirstTick(void) {
}

void StandaloneTraps_HeapCreated(void *) {
}

void Profiler_Frame(const char *) {
}

void ShadowTicks_FirstTick(void) {
}

void ShadowTicks_Tick(void) {
}
