#include "StringToNumber.hpp"

#include <stdlib.h>
#include <string.h>

#include "UMemory.hpp"

#define kStringToNumberTag ((const char *)0x0018d93c)   // "StringToNumber"

static const int kSortFrom = 31;   // tables this long or longer are sorted and binary searched

// The two qsort comparators (0x00119ef0 and 0x00119f40 in the game), used only by the constructor.
static int __cdecl CompareByString(const void *a, const void *b) {
    int c = strcmp(((const StringToNumberEntry *)a)->string, ((const StringToNumberEntry *)b)->string);
    return c < 0 ? -1 : c > 0 ? 1 : 0;
}

static int __cdecl CompareByNumber(const void *a, const void *b) {
    return ((const StringToNumberEntry *)a)->number - ((const StringToNumberEntry *)b)->number;
}

// FUNC_AT(0x00119f50)
StringToNumber* StringToNumber::Construct(StringToNumberEntry *entries) {
    int n = 0;
    while (entries[n].string != nullptr)
        n++;
    count = n;
    table = entries;
    if (n < kSortFrom) {
        sortedByString = nullptr;
        sortedByNumber = nullptr;
        return this;
    }
    StringToNumberEntry *block = (StringToNumberEntry *)UMemory::FastAlloc(n * 16, kStringToNumberTag);
    sortedByNumber = block + n;
    sortedByString = block;
    for (int i = 0; i < count; i++) {
        sortedByString[i] = table[i];
        sortedByNumber[i] = table[i];
    }
    // No table has two entries with one key, so qsort's order is the original's whatever its algorithm.
    qsort(sortedByString, count, sizeof(StringToNumberEntry), CompareByString);
    qsort(sortedByNumber, count, sizeof(StringToNumberEntry), CompareByNumber);
    return this;
}

// FUNC_AT(0x0011a020)
void StringToNumber::Destruct() {
    if (sortedByString != nullptr)
        UMemory::FastFree(sortedByString, count * 16);
}

// AUTOINJECT
int StringToNumber::ConvertStringToNumber(char *name) {
    if (sortedByString != nullptr) {
        int i = BinarySearch(name);
        return i == -1 ? -1 : sortedByString[i].number;
    }
    for (int i = 0; i < count; i++)
        if (strcmp(name, table[i].string) == 0)
            return table[i].number;
    return -1;
}

// AUTOINJECT
int StringToNumber::BinarySearch(char *name) {
    int lo = 0, hi = count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        int c = strcmp(name, sortedByString[mid].string);
        if (c > 0)
            lo = mid + 1;
        else if (c < 0)
            hi = mid - 1;
        else
            return mid;
    }
    return -1;
}
