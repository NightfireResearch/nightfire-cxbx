#pragma once

#include "../drivinghelpers.h"

class UFileLoader {
public:
    static bool LookupAbsolutePath(char *pathOut, char *pathIn);
    static void* FileLoadDirectFromDisk(char* param_1, int param_2, bool z_variant);
    static void* AttemptBigFileLoad(char *param_1, undefined4 param_2);
    static void AddFileToRequestList(char* fname);
    static void* FileLoad(char *rawPath, int param_2, bool param_3);
};


