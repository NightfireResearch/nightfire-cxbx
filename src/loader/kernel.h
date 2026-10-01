#ifndef LOADER_KERNEL_H_
#define LOADER_KERNEL_H_

// The xboxkrnl imports the loader provides. See kernel.cpp for what is and is not implemented.

// Builds the stub trampolines. Must be called before Kernel_Resolve.
bool Kernel_Init(void);

// Returns the address to put in the XBE's thunk table for this ordinal, or NULL if it cannot be provided.
void *Kernel_Resolve(unsigned ordinal);

// The name of an ordinal, for diagnostics. "?" if unknown.
const char *Kernel_OrdinalName(unsigned ordinal);

#endif // LOADER_KERNEL_H_
