// The debug variables: dbindex/dbattrib_, the tuning reads, and the index variable's methods. See
// DebugVariables.h.

#pragma fp_contract(off)

#include "DebugVariables.h"

#include "DebugVarUntested.h"
#include "StdStreams.h"
#include "Tuning.h"
#include "../engine/UMemory.hpp"
#include "../../helpers.h"
#include "../../common/xbeOverload.h"
#include "../platform/X87.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

using namespace GameStd;

#define CurrentIndexer (*(DebugUIntVariable **)0x001e22c4)   // the index dbindex set, until dbendindex
#define DebugUIntVariableVtable ((const void *const *)0x0018bd00)

#define OStrStream_Construct ((OStrStream *(__fastcall *)(OStrStream *, int, char *, int, int, int))0x00131b27)
#define OStrStream_Destruct ((void (__fastcall *)(BasicIos *, int))0x00131bbb)

namespace {

static const int kSlotAsString = 9;   // DebugData's vtable

static const int kOpenOut = 2;        // ios_base::out

} // namespace

// ---------------------------------------------------------------------------------------------------------------
// DebugData

// FUNC_AT(0x00038160)
void* DebugData::GetDeindexedPtr(void *base) {
    if (indexer == NULL)
        return base;
    uint32_t index = *(uint32_t *)indexer->GetDeindexedPtr(indexer->data);
    return (char *)base + stride * index;
}

// FUNC_AT(0x00037b00)
uint32_t* DebugData::GetValuePtr() {
    if (indexer == NULL)
        return (uint32_t *)data;
    uint32_t index = *indexer->GetValuePtr();
    return (uint32_t *)((char *)data + stride * index);
}

// FUNC_AT(0x000380e0)
char* DebugData::AsString(char *out) {
    char text[64];
    strcpy(text, "");
    strcpy(out, text);
    return out;
}

// The original returns its local copy of "", a pointer into its own finished stack frame; this one keeps the
// empty string where it stays valid.
//
// FUNC_AT(0x00038130)
char* DebugData::Axis2AsString() {
    DEBUGVAR_UNTESTED("DebugData::Axis2AsString");
    static char text[64];
    strcpy(text, "");
    return text;
}

// ---------------------------------------------------------------------------------------------------------------
// DebugVariable<unsigned int>

// FUNC_AT(0x00037b50)
int DebugUIntVariable::Debounce() {
    return step <= 0.0f;
}

// One press moves the value by |step| of the range times the scale, at least 1; past the maximum it wraps to the
// minimum when the step is negative or the scale 0, and otherwise stops at the maximum.
//
// FUNC_AT(0x00037b70)
void DebugUIntVariable::Increase(float scale) {
    uint32_t top = maximum;
    uint32_t bottom = minimum;
    uint32_t amount = Ftol(double(fabsf(step)) * double(top - bottom) * scale);
    if (amount == 0)
        amount = 1;
    if (top - *GetValuePtr() < amount) {
        if (step < 0.0f || scale == 0.0f)
            *GetValuePtr() = bottom;
        else
            *GetValuePtr() = top;
    } else {
        *GetValuePtr() += amount;
    }
}

// FUNC_AT(0x00037cb0)
void DebugUIntVariable::Decrease(float scale) {
    uint32_t top = maximum;
    uint32_t bottom = minimum;
    uint32_t amount = Ftol(double(fabsf(step)) * double(top - bottom) * scale);
    if (amount == 0)
        amount = 1;
    if (*GetValuePtr() - bottom < amount) {
        if (step < 0.0f || scale == 0.0f)
            *GetValuePtr() = top;
        else
            *GetValuePtr() = bottom;
    } else {
        *GetValuePtr() -= amount;
    }
}

// FUNC_AT(0x00038490)
void DebugUIntVariable::SetToMin() {
    *GetValuePtr() = minimum;
}

// FUNC_AT(0x000384c0)
void DebugUIntVariable::SetToMax() {
    *GetValuePtr() = maximum;
}

// The value's name, or the number as ostrstream << value prints it.
//
// FUNC_AT(0x000396d0)
char* DebugUIntVariable::AsString(char *out) {
    if (names != NULL) {
        strcpy(out, names[*GetValuePtr()]);
        return out;
    }
    char text[64];
    OStrStream stream;
    OStrStream_Construct(&stream, 0, text, 0x20, kOpenOut, 1);
    stream.InsertUInt(*GetValuePtr())->Put('\0');   // << value << ends
    strcpy(out, text);
    OStrStream_Destruct(&stream.ios, 0);
    stream.ios.Destruct();
    return out;
}

// FUNC_AT(0x0003a3f0)
void DebugUIntVariable::SetFromString(const char *text) {
    StringToUInt(text, GetValuePtr());
}

// ---------------------------------------------------------------------------------------------------------------
// dbindex

// FUNC_AT(0x00038190)
void dbindex(const char *name, uint32_t *data, uint32_t minimum, uint32_t maximum, const char *const *names) {
    DebugUIntVariable *index = (DebugUIntVariable *)OperatorNew(sizeof(DebugUIntVariable));
    if (index != NULL) {
        index->name = name;
        index->nameLength = uint32_t(strlen(name));
        index->data = data;
        index->maximum = maximum;
        index->unknown0c = 0;
        index->indexer = NULL;
        index->stride = 0;
        index->minimum = minimum;
        index->step = -1.0f;
        index->names = names;
        index->vtable = DebugUIntVariableVtable;
    }
    CurrentIndexer = index;
}

// FUNC_AT(0x00037ad0)
void dbendindex() {
    CurrentIndexer = NULL;
}

// ---------------------------------------------------------------------------------------------------------------
// The value parsers

namespace {

// istrstream stream(text); then the extraction; then ~istrstream.
template <class Extract> void ParseThroughStream(const char *text, Extract extract) {
    if (text == NULL)
        return;
    IStrStream stream;
    stream.Construct(text, 1);
    extract(&stream);
    stream.DestructAll();
}

} // namespace

// FUNC_AT(0x00038f10)
void StringToChar(const char *text, char *value) {
    ParseThroughStream(text, [value](IStrStream *stream) { ExtractChar(stream, value); });
}

// FUNC_AT(0x00039db0)
void StringToInt(const char *text, int *value) {
    ParseThroughStream(text, [value](IStrStream *stream) { stream->ExtractInt(value); });
}

// FUNC_AT(0x00039e40)
void StringToUInt(const char *text, unsigned *value) {
    ParseThroughStream(text, [value](IStrStream *stream) { stream->ExtractUInt(value); });
}

// FUNC_AT(0x00039ed0)
void StringToFloat(const char *text, float *value) {
    ParseThroughStream(text, [value](IStrStream *stream) { stream->ExtractFloat(value); });
}

// FUNC_AT(0x00039f60)
void StringToBool(const char *text, bool *value) {
    ParseThroughStream(text, [value](IStrStream *stream) { stream->ExtractBool(value); });
}

// ---------------------------------------------------------------------------------------------------------------
// dbattrib_

namespace {

// The template behind every dbattrib_: the value of `name` in the open tuning file, or with an index running,
// the value of "name{index}" for each index into data + index * stride (the index variable put back after).
// The float-vector and colour reads find their index value through GetDeindexedPtr(data), the others through
// GetValuePtr(), its inlined form; the result is the same.
template <class T>
void ReadTuned(const char *name, T *data, uint32_t stride, void (*parse)(const char *, T *), bool viaDeindex) {
    DebugUIntVariable *index = CurrentIndexer;
    if (index == NULL) {
        parse(TuningDBMgr->reader->FindItem(name), data);
        return;
    }
    uint32_t *value = viaDeindex ? (uint32_t *)index->GetDeindexedPtr(index->data) : index->GetValuePtr();
    uint32_t saved = *value;
    for (uint32_t i = index->minimum; i <= index->maximum; i++) {
        value = viaDeindex ? (uint32_t *)index->GetDeindexedPtr(index->data) : index->GetValuePtr();
        *value = i;
        char indexText[64];
        typedef char *(DebugUIntVariable::*AsStringMethod)(char *);
        const char *indexName = (index->*XbeVirtual<AsStringMethod>(index, kSlotAsString))(indexText);
        parse(TuningDBMgr->reader->FindIndexedItem(name, indexName), (T *)((char *)data + i * stride));
    }
    value = viaDeindex ? (uint32_t *)index->GetDeindexedPtr(index->data) : index->GetValuePtr();
    *value = saved;
}

} // namespace

// FUNC_AT(0x000395d0)
void dbattrib_f(const char *name, char *data, int, int, uint32_t stride, float, const char *const *) {
    ReadTuned(name, data, stride, &StringToChar, false);
}

// FUNC_AT(0x00039ff0)
void dbattrib_s8(const char *name, int *data, int, int, uint32_t stride, float, const char *const *) {
    ReadTuned(name, data, stride, &StringToInt, false);
}

// FUNC_AT(0x0003a0f0)
void dbattrib_u8(const char *name, unsigned *data, unsigned, unsigned, uint32_t stride, float, const char *const *) {
    ReadTuned(name, data, stride, &StringToUInt, false);
}

// FUNC_AT(0x0003a1f0)
void dbattrib_float(const char *name, float *data, float, float, uint32_t stride, float, const char *const *) {
    ReadTuned(name, data, stride, &StringToFloat, false);
}

// FUNC_AT(0x0003a2f0)
void dbattrib_bool(const char *name, bool *data, int, int, uint32_t stride, float, const char *const *) {
    ReadTuned(name, data, stride, &StringToBool, false);
}

// FUNC_AT(0x0003d470)
void dbattrib_floatrgb(const char *name, float *data, uint8_t *, uint32_t stride) {
    ReadTuned(name, data, stride, &DTuningFile::ParseData, true);
}

// FUNC_AT(0x0003d530)
void dbattrib_argb(const char *name, uint32_t *data, uint8_t *, uint32_t stride) {
    ReadTuned(name, data, stride, &DTuningFile::ParseData_Colour, true);
}
