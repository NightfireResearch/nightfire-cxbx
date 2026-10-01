#pragma once

#include "../drivinghelpers.h"

// A list of file names the loader keeps for its debug dump (FileNameList::Dump, 0x00117160). Only ever used
// through pointers to the game's own instances, so it has no fields here yet - an overlay class with no
// layout to check (see src/common/xbeClass.h).
class FileNameList {
    public:
    // Records a name, as the loader does for every file it opens while its request logging is on (0x001174d0).
    // AUTOGEN
    void AddFile(char *name);
};
