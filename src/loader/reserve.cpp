// ---------------------------------------------------------------------------------------------------------------
// Claims the XBE's address range for this executable's own image.
//
// The XBE is linked to base 0x00010000 and is full of absolute addresses, so it has to be mapped exactly
// there. That address is allocatable in principle - the system minimum is 0x00010000 - but it is never
// actually free: the kernel has already put something there by the time the process exists, and a suspended
// process is no better, because this happens at creation rather than at startup. VirtualAlloc cannot win.
//
// What does win is being the image. The kernel maps the executable at its preferred base before anything else
// in the process exists, so linking this loader at 0x00010000 with an array big enough to span the XBE makes
// the range ours by construction. The XBE is then copied over the top of it.
//
// Two things this depends on, both checked at runtime by Xbe_Map rather than assumed:
//
//  - the array has to come before the loader's own code in the image, or copying the XBE in would overwrite
//    the code doing the copying. It is placed in .text and this file is listed first in the target's sources,
//    which puts its contribution at the start of that section;
//  - the image has to span the whole XBE. The array is sized for that with room to spare.
//
// It lives in .text rather than plain uninitialised data because .bss is placed after the code, which is the
// wrong side. The cost is that the array is stored in the file, so the executable is as large as the array.
// ---------------------------------------------------------------------------------------------------------------

#pragma section(".text")

// Nightfire's action XBE needs 0x2fb660; the driving one is smaller. Rounded up with headroom.
__declspec(allocate(".text")) unsigned char XBE_ADDRESS_SPACE[0x340000];

// Where the reservation actually landed, so the loader can check its assumptions instead of trusting them.
extern "C" const unsigned char *Loader_ReservationStart(void) { return XBE_ADDRESS_SPACE; }
extern "C" unsigned long Loader_ReservationSize(void) { return (unsigned long)sizeof(XBE_ADDRESS_SPACE); }
