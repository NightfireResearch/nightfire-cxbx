// The tuning databases. See Tuning.h.

#include "Tuning.h"

#include "DebugVarUntested.h"
#include "../engine/GameLoop.h"
#include "../engine/UFileLoader.h"
#include "../engine/UMemory.hpp"
#include "../../helpers.h"
#include "../../common/xbeOverload.h"
#include "../platform/FileSys.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define DTuningDBMgrVtable ((const void *const *)0x0018bebc)
#define FoundValue ((char *)0x001e22f0)          // FindItem's result buffer
#define FoundIndexedValue ((char *)0x001e2340)   // FindIndexedItem's

#define Sprintf ((int (__cdecl *)(char *, const char *, ...))0x00132767)
#define Sscanf ((int (__cdecl *)(const char *, const char *, ...))0x00133234)

namespace {

static const int kFilePriority = 100;   // FILESYS_ ... sync priority

static const char kTuningFolder[] = "data\\tuning\\";

// A key without its spaces (tuning files write names without them).
void RemoveSpaces(char *out, const char *in) {
    for (; *in != '\0'; ++in)
        if (*in != ' ')
            *out++ = *in;
    *out = '\0';
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------
// DTuningFile

// FUNC_AT(0x0003d6e0)
DTuningFile* DTuningFile::Construct(const char *name, const char *level, int retry, bool skipCheck) {
    char folder[256];
    unknown000 = 0;
    textFile = NULL;
    reader = NULL;
    mode = kNotOpen;
    path[0] = '\0';
    strcpy(folder, name);
    for (char *colon = strchr(folder, ':'); colon != NULL; colon = strchr(folder, ':'))
        *colon = '\\';
    if (level != NULL) {
        Sprintf(path, "%s%s\\%s.tun", kTuningFolder, folder, level);
        if (!skipCheck && !FILESYS_existssync(path, kFilePriority)) {
            if (retry) {
                Sprintf(path, "%s%s\\%s.tun", kTuningFolder, folder, level);
                if (FILESYS_existssync(path, kFilePriority))
                    return this;
            }
            Sprintf(path, "%s%s\\default.tun", kTuningFolder, folder);
        }
    }
    return this;
}

// FUNC_AT(0x0003d9c0)
DTuningFile::Reader* DTuningFile::OpenForRead() {
    Reader *made = (Reader *)OperatorNew(sizeof(Reader));
    if (made != NULL)
        made->text = path[0] != '\0' ? (const char *)UFileLoader::FileLoadz(path, 0) : NULL;
    reader = made;
    mode = made != NULL ? kLoaded : kNotOpen;
    return made;
}

// FUNC_AT(0x0003da50)
void DTuningFile::Destruct() {
    if (mode == kTextFile) {
        TextFile *file = textFile;
        if (file != NULL) {
            DEBUGVAR_UNTESTED("DTuningFile::Destruct (text file)");
            file->Destruct();
            OperatorDelete(file);
        }
    } else if (mode == kLoaded) {
        OperatorDelete(reader);   // the loaded text itself is not freed
    }
}

// FUNC_AT(0x0003d9a0)
void TextFile::Destruct() {
    if (open)
        FILESYS_closesync(slot, kFilePriority);
}

// FUNC_AT(0x0003d7f0)
char* DTuningFile::Reader::FindItem(const char *name) {
    const char *source = text;
    if (source == NULL)
        return NULL;
    char key[256];
    RemoveSpaces(key, name);
    OptionParser line;
    line.Construct(source, key);
    return line.GetFullString(FoundValue) ? FoundValue : NULL;
}

// FUNC_AT(0x0003d870)
char* DTuningFile::Reader::FindIndexedItem(const char *name, const char *index) {
    if (text == NULL)
        return NULL;
    char key[256];
    Sprintf(key, "%s{%s}", name, index);
    RemoveSpaces(key, key);
    OptionParser line;
    line.Construct(text, key);
    return line.GetFullString(FoundIndexedValue) ? FoundIndexedValue : NULL;
}

// FUNC_AT(0x0003d910)
void DTuningFile::ParseData(const char *text, float *values) {
    if (text != NULL)
        Sscanf(text, "%f,%f,%f,%f", &values[0], &values[1], &values[2], &values[3]);
}

// FUNC_AT(0x0003d940)
void DTuningFile::ParseData_Colour(const char *text, uint32_t *colour) {
    if (text == NULL)
        return;
    // The original scans alpha into its own argument slot, so an alpha the text lacks is the text pointer's low
    // bits; the other three it lacks are whatever its stack held (0 here).
    int alpha = int(uintptr_t(text));
    int red = 0, green = 0, blue = 0;
    Sscanf(text, "%d,%d,%d,%d", &alpha, &red, &green, &blue);
    *colour = ((uint32_t(alpha) << 8 | red) << 8 | green) << 8 | blue;
}

// ---------------------------------------------------------------------------------------------------------------
// DTuningDBMgr

// FUNC_AT(0x0003d610)
DTuningDBMgr* DTuningDBMgr::Construct() {
    vtable = DTuningDBMgrVtable;
    return this;
}

// FUNC_AT(0x00059630)
void DTuningDBMgr::InitSingleton() {
    DTuningDBMgr *made = (DTuningDBMgr *)OperatorNew(sizeof(DTuningDBMgr));
    TuningDBMgr = made != NULL ? made->Construct() : NULL;
}

// The singleton manager's kill at clean-up (the manager is registered at start-up, Bond_StartUpSystem).
//
// FUNC_AT(0x0003d620)
void DTuningDBMgr::DestroySingleton() {
    DTuningDBMgr *instance = TuningDBMgr;
    if (instance != NULL) {
        typedef void *(DTuningDBMgr::*DeleteMethod)(unsigned);
        (instance->*XbeVirtual<DeleteMethod>(instance, 0))(1);
    }
}

// FUNC_AT(0x0003d640)
void DTuningDBMgr::LoadDatabase(const char *databaseName, const char *level, int retry, bool skipCheck) {
    name = databaseName;
    DTuningFile *made = (DTuningFile *)OperatorNew(sizeof(DTuningFile));
    file = made != NULL ? made->Construct(databaseName, level, retry, skipCheck) : NULL;
    reader = file->OpenForRead();
}

// FUNC_AT(0x0003d6c0)
void DTuningDBMgr::CloseCurrent() {
    DTuningFile *open = file;
    if (open != NULL) {
        open->Destruct();
        OperatorDelete(open);
    }
}
