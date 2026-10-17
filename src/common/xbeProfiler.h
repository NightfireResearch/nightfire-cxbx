#ifndef XBEPROFILER_H_
#define XBEPROFILER_H_

// ---------------------------------------------------------------------------------------------------------------
// A sampling profiler for the game thread.
//
// The XBE is a stripped 2002 binary running under our loader, so none of the usual tools can see into it: a
// Windows profiler attaches to the process and reports "driving.exe + 0x14c9f0" at best, and more often
// nothing at all, because the image is mapped by hand rather than loaded. But that address is exactly what is
// wanted - it is the address Ghidra shows - so this samples it directly.
//
// Profiler_Start() takes a handle to the thread that runs the game and a second thread suspends it a thousand
// times a second, reads EIP, and resumes it. Profiler_Report() prints where the samples landed, hottest
// first, so a stall can be looked up in the disassembly by address.
// ---------------------------------------------------------------------------------------------------------------

// Called once a frame from the graphics seam. It does nothing at all unless settings.ini has
//
//     [Settings]
//     Profile=on
//
// in which case the first call starts sampling and every 250th prints what has been seen since.
void Profiler_Frame(const char *what);

#endif // XBEPROFILER_H_
