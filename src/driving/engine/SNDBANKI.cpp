
typedef struct {
    uint32_t magic;
    ushort someThing;
    ushort numPatches;
    uint32_t maybeHeaderSizeBytes;
    uint32_t maybeSampleSizeBytes;
    uint8_t unknown[4];
    uint32_t offsets[1]; // Quantity is variable - numPatches
} BANKHEADER;

// AUTOINJECT
void* SNDBANKI_getppatch(void *data, int patchNum) {

    BANKHEADER *bankData = (BANKHEADER*)data;

    if ((patchNum < bankData->numPatches) && (bankData->offsets[patchNum] != 0)) {
        void* sampleDataStart = bankData->offsets + (patchNum * sizeof(bankData->offsets[0]));
        return sampleDataStart + bankData->offsets[patchNum];
    }

    return NULL;
}