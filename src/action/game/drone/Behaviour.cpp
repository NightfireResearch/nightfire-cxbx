// The drone behaviour block: unpacking a placement's behaviour words and stats, and reading and writing single
// behaviour properties (docs/drone/behaviours/README.md section 2, behaviour_properties.md).

#include "Behaviour.h"
#include "DroneTables.h"   // DroneBehaviourLayouts, DroneBehaviourMasks (still the game's tables)

#include <string.h>

// A property's bits in the three behaviour words: DroneBehaviourLayouts[id] names the word (wordShift & 7), the
// shift (wordShift >> 3) and the mask (DroneBehaviourMasks[maskIndex], already in place - it is not shifted).
// The id is not range-checked, as in the original; every caller passes a constant below NUM_DRONE_PROPERTIES.
//
// AUTOINJECT
uint behaviour_util_getProperty(int id, uint *words) {
    const DroneBehaviourLayout *layout = &DroneBehaviourLayouts[id];
    return (words[layout->wordShift & 7] & DroneBehaviourMasks[layout->maskIndex]) >> (layout->wordShift >> 3);
}

// Clears the property's bits, then ORs in value << shift. The value is not masked: a value wider than the field
// spills into its neighbours (every caller passes 0 or 1, and only single-bit properties).
//
// AUTOINJECT
void behaviour_util_setProperty(int id, uint *words, int value) {
    const DroneBehaviourLayout *layout = &DroneBehaviourLayouts[id];
    uint *word = &words[layout->wordShift & 7];
    *word &= ~DroneBehaviourMasks[layout->maskIndex];
    *word |= (uint)value << (layout->wordShift >> 3);
}

// Record j of a behaviour class's stats. Only NDrone2_DoModeSettingsNEW calls it, always with j = 1 (the records
// 0 and 2, apparently the Easy and Hard variants, are decoded by behaviour_util_get and never read).
//
// AUTOINJECT
DroneStatsRecord* behaviour_util_getStats(int behaviourClass, int j) {
    return &drone_stats[behaviourClass][j];
}

// The widths of the eight fields of a packed stats record, LSB first (49 bits: none crosses a word boundary).
// The original builds this array on the stack on every call.
static const uchar kStatsFieldBits[DRONE_STATS_FIELDS] = {8, 3, 8, 8, 5, 8, 8, 1};

#define BEHAVIOUR_MAX_WORDS 3           // header lo16: 1..3 behaviour words
#define BEHAVIOUR_MAX_PROPERTIES 0x5b   // header hi16: 1..91 properties
#define STATS_HEADER_PRESENT 0x80000000 // the stats header's bit 31: there is a stats block
#define STATS_HEADER_TAG 0x80010000     // ...and its hi16 must be 0x8001; lo16 = total words, a third per record
#define STATS_RECORDS 3

// Unpacks a drone placement's behaviour block (keys 15 onwards, docs/drone/behaviours/README.md 2.1) into bs:
// class, then per behaviour an attack mode, a header (word count, property count) and the words (copied to
// bs->words1/words2 when those are set), then optionally the three packed stats records, which go straight into
// drone_stats[class] (last placement of a class wins).
//
// Quirks kept from the original:
// - The class is stored before it is checked; the behaviour-1 fields are stored before they are checked.
// - Behaviour 1's words are copied only once its header has passed; behaviour 2's are copied BEFORE its mode and
//   header are checked, so a bad count (up to 0xffff) would copy that many words first.
// - On any failure the defaults are written, and both word destinations are zeroed without a NULL check (the
//   only caller always passes both). drone_stats is left alone. Returns false (the caller ignores it).
// - A missing or untagged stats block is not a failure: the stats are left as they were and it returns true.
// - Only the low byte of the return value is set by the original (mov al,1 / xor al,al).
//
// AUTOINJECT
bool behaviour_util_get(BehaviourStruct *bs, uint *data) {
    bs->behaviourClass = data[0];
    uint *p = data + 1;
    if (bs->behaviourClass >= NUM_BEHAVIOUR_CLASSES)
        goto invalid;

    bs->mode1 = p[0];
    bs->count1 = (ushort)p[1];
    bs->numProps1 = (ushort)(p[1] >> 16);
    p += 2;
    if (bs->mode1 >= NUM_BEHAVIOUR_MODES || bs->count1 > BEHAVIOUR_MAX_WORDS || bs->count1 == 0 ||
        bs->numProps1 > BEHAVIOUR_MAX_PROPERTIES || bs->numProps1 == 0)
        goto invalid;
    if (bs->words1 != NULL)
        memcpy(bs->words1, p, bs->count1 * sizeof(uint));
    p += bs->count1;

    bs->mode2 = p[0];
    bs->count2 = (ushort)p[1];
    bs->numProps2 = (ushort)(p[1] >> 16);
    p += 2;
    if (bs->words2 != NULL)
        memcpy(bs->words2, p, bs->count2 * sizeof(uint)); // before the checks below, as the original
    p += bs->count2;
    if (bs->mode2 >= NUM_BEHAVIOUR_MODES || bs->count2 > BEHAVIOUR_MAX_WORDS || bs->count2 == 0 ||
        bs->numProps2 > BEHAVIOUR_MAX_PROPERTIES || bs->numProps2 == 0)
        goto invalid;

    {
        uint header = p[0];
        if ((header & STATS_HEADER_PRESENT) == 0)
            return true;
        uint *stats = p + 1;
        if ((header & 0xffff0000) != STATS_HEADER_TAG)
            return true;
        uint recordWords = (header & 0xffff) / STATS_RECORDS;

        for (int j = 0; j < STATS_RECORDS; j++) {
            DroneStatsRecord *record = &drone_stats[bs->behaviourClass][j];
            int bit = 0;
            for (int k = 0; k < DRONE_STATS_FIELDS; k++) {
                uint width = kStatsFieldBits[k];
                // The original extracts through a byte: (low byte of the shifted word) & (byte mask); with widths
                // of at most 8 that is the plain field value.
                uchar value = (uchar)(stats[bit >> 5] >> (bit & 31)) & (uchar)((1u << width) - 1);
                bit += width;
                record->field[k] = value;
            }
            stats += recordWords;
        }
    }
    return true;

invalid:
    NF_WARN("Invalid behaviour data!\n"); // GC check (0x8004e46c)
    bs->behaviourClass = 0;
    bs->mode1 = 0;
    bs->count1 = BEHAVIOUR_MAX_WORDS;
    bs->numProps1 = BEHAVIOUR_MAX_PROPERTIES;
    bs->words1[0] = 0;
    bs->words1[1] = 0;
    bs->words1[2] = 0;
    bs->numProps2 = BEHAVIOUR_MAX_PROPERTIES;
    bs->count2 = BEHAVIOUR_MAX_WORDS;
    bs->mode2 = 0;
    bs->words2[0] = 0;
    bs->words2[1] = 0;
    bs->words2[2] = 0;
    return false;
}
