#ifndef DRIVING_DEVTOOLS_LOADERSHADOW_H_
#define DRIVING_DEVTOOLS_LOADERSHADOW_H_

// NIGHTFIRE_LDSHADOW=1: EAGL's loader and name pools against the originals on every ELF object in the archives.
// See LoaderShadow.cpp.
void LoaderShadow_Run(void);

#endif // DRIVING_DEVTOOLS_LOADERSHADOW_H_
