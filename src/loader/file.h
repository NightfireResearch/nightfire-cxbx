#ifndef LOADER_FILE_H_
#define LOADER_FILE_H_

// The file-system half of the kernel, kept apart from kernel.cpp because it is the one part with real
// semantics rather than a translation of one call onto another. See file.cpp.

struct KernelFileExport {
    unsigned ordinal;
    void    *implementation;
};

// The table kernel.cpp folds into its own when it resolves an import.
const KernelFileExport *Kernel_FileExports(unsigned *count);

#endif // LOADER_FILE_H_
