#include "pcmCache.h"

#include "dsndSeam.h"   // DSound_BackendMissing
#include "../../common/sound/xadpcm.h"
#include "../../driving/platform/Host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Note the game can also overwrite a bound buffer's sample data in place through dsndWriteVoiceData - the streaming
// path - which the key cannot see; PcmCache_Written is how the backends hear of it.

struct PcmCacheEntry {
    const void *adpcm;
    uint32_t adpcmBytes;
    int channels;
    uint32_t fingerprint;   // see FingerprintAdpcm - guards against the address being reused
    int16_t *pcm;
    size_t pcmValues;       // total int16 values, i.e. samples * channels
    uint64_t lastUsed;
};

#define PCM_CACHE_MAX_ENTRIES 512
#define PCM_CACHE_MAX_BYTES   (96u * 1024u * 1024u)

static PcmCacheEntry g_pcmCache[PCM_CACHE_MAX_ENTRIES];
static int g_pcmCacheCount = 0;
static size_t g_pcmCacheBytes = 0;
static uint64_t g_pcmCacheClock = 0;

// A cheap content fingerprint, because (pointer, size) alone is not a safe identity for this data. The game
// loads sounds into a pool and reuses the same addresses for different samples, so the same key can name
// completely different audio later in a session - which sounds like the wrong effect playing. Sampling a
// couple of hundred bytes spread across the buffer distinguishes any two real samples; hashing all of a 3 MB
// bank would cost as much as decoding it.
static uint32_t FingerprintAdpcm(const void *data, uint32_t bytes) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t h = 2166136261u;
    h ^= bytes;
    h *= 16777619u;
    const uint32_t chunks = 16, chunkBytes = 16;
    for (uint32_t c = 0; c < chunks; c++) {
        uint64_t start = ((uint64_t)bytes * c) / chunks;
        for (uint32_t i = 0; i < chunkBytes && start + i < bytes; i++) {
            h ^= p[start + i];
            h *= 16777619u;
        }
    }
    return h;
}

static void ReleaseCacheEntry(PcmCacheEntry *e) {
    g_pcmCacheBytes -= e->pcmValues * sizeof(int16_t);
    free(e->pcm);
    memset(e, 0, sizeof(*e));
}

// Frees the least recently used entry that no live buffer is still playing from. Returns false if every entry
// is in use, which would need more than 512 buffers bound at once and so should not happen.
static bool EvictOneCacheEntry(void) {
    int oldest = -1;
    for (int i = 0; i < g_pcmCacheCount; i++) {
        if (g_pcmCache[i].pcm == NULL || PcmCache_InUse(g_pcmCache[i].pcm))
            continue;
        if (oldest < 0 || g_pcmCache[i].lastUsed < g_pcmCache[oldest].lastUsed)
            oldest = i;
    }
    if (oldest < 0)
        return false;
    ReleaseCacheEntry(&g_pcmCache[oldest]);
    g_pcmCache[oldest] = g_pcmCache[--g_pcmCacheCount];
    memset(&g_pcmCache[g_pcmCacheCount], 0, sizeof(g_pcmCache[g_pcmCacheCount]));
    return true;
}

const int16_t *PcmCache_Decode(const void *adpcm, uint32_t adpcmBytes, int channels, size_t *values) {
    uint32_t fingerprint = FingerprintAdpcm(adpcm, adpcmBytes);
    for (int i = 0; i < g_pcmCacheCount; i++) {
        if (g_pcmCache[i].adpcm == adpcm && g_pcmCache[i].adpcmBytes == adpcmBytes &&
            g_pcmCache[i].channels == channels) {
            if (g_pcmCache[i].fingerprint == fingerprint) {
                g_pcmCache[i].lastUsed = ++g_pcmCacheClock;
                *values = g_pcmCache[i].pcmValues;
                return g_pcmCache[i].pcm;
            }
            // Same address and size, different audio: the pool slot has been reused. Drop the stale decode
            // (unless something is still playing it, in which case leave it be and decode a second copy).
            if (!PcmCache_InUse(g_pcmCache[i].pcm)) {
                ReleaseCacheEntry(&g_pcmCache[i]);
                g_pcmCache[i] = g_pcmCache[--g_pcmCacheCount];
                memset(&g_pcmCache[g_pcmCacheCount], 0, sizeof(g_pcmCache[g_pcmCacheCount]));
            }
            break;
        }
    }

    size_t wantBytes = XAdpcm_DecodedValueCount(adpcmBytes, channels) * sizeof(int16_t);
    while ((g_pcmCacheCount >= PCM_CACHE_MAX_ENTRIES || g_pcmCacheBytes + wantBytes > PCM_CACHE_MAX_BYTES) &&
           EvictOneCacheEntry()) {
        // keep evicting until there is room
    }
    if (g_pcmCacheCount >= PCM_CACHE_MAX_ENTRIES) {
        DSound_BackendMissing("decoded PCM cache full (every entry still in use)");
        return NULL;
    }

    size_t count = XAdpcm_DecodedValueCount(adpcmBytes, channels);
    if (count == 0)
        return NULL;

    int16_t *pcm = (int16_t *)malloc(count * sizeof(int16_t));
    if (pcm == NULL) {
        DSound_BackendMissing("out of memory decoding ADPCM");
        return NULL;
    }
    int64_t before, after, freq;
    HostQueryPerformanceFrequency(&freq);
    HostQueryPerformanceCounter(&before);
    size_t got = XAdpcm_Decode(adpcm, adpcmBytes, channels, pcm, count);
    HostQueryPerformanceCounter(&after);
    if (got == 0) {
        free(pcm);
        return NULL;
    }
    // Decoding happens on the game thread, so a big bank stalls the frame. Timed because a stutter as a level
    // loads is exactly what that would sound like, and guessing at it is worse than measuring it.
    double decodeMs = 1000.0 * (double)(after - before) / (double)freq;
    if (decodeMs > 2.0)
        printf("[sound] SLOW decode: %.1f ms for %u bytes\n", decodeMs, adpcmBytes);

    PcmCacheEntry *e = &g_pcmCache[g_pcmCacheCount++];
    e->adpcm = adpcm;
    e->adpcmBytes = adpcmBytes;
    e->channels = channels;
    e->fingerprint = fingerprint;
    e->pcm = pcm;
    e->pcmValues = got;
    e->lastUsed = ++g_pcmCacheClock;
    g_pcmCacheBytes += got * sizeof(int16_t);
    printf("[sound] decoded %u bytes of %d-channel ADPCM at 0x%08x -> %u samples (cache now %u entries, %.1f MB)\n",
           adpcmBytes, channels, (unsigned)(uintptr_t)adpcm, (unsigned)(got / (size_t)channels), g_pcmCacheCount,
           g_pcmCacheBytes / 1048576.0);
    *values = got;
    return pcm;
}

// Every ADPCM block carries its own predictor and step index, so a block range can be decoded in isolation and the
// result is bit-identical to decoding the whole buffer - which is what makes patching the cache correct rather than
// approximate.
//
// The write is rounded out to whole blocks: SFXUpdateStreams works in multiples of 0x48 (one stereo block) and
// caps each transfer at 0x2400, but the last chunk of a file read need not be aligned.
void PcmCache_Written(const void *adpcm, uint32_t offset, uint32_t length) {
    if (adpcm == NULL || length == 0)
        return;

    for (int i = 0; i < g_pcmCacheCount; i++) {
        PcmCacheEntry *e = &g_pcmCache[i];
        if (e->adpcm != adpcm)
            continue;

        size_t blockBytes = XAdpcm_BlockBytes(e->channels);
        size_t totalBlocks = XAdpcm_BlockCount(e->adpcmBytes, e->channels);
        size_t firstBlock = (size_t)offset / blockBytes;
        size_t lastBlock = ((size_t)offset + (size_t)length + blockBytes - 1) / blockBytes; // exclusive
        if (lastBlock > totalBlocks)
            lastBlock = totalBlocks;
        if (firstBlock >= lastBlock)
            continue;

        const uint8_t *src = (const uint8_t *)e->adpcm + firstBlock * blockBytes;
        size_t srcBytes = (lastBlock - firstBlock) * blockBytes;
        size_t dstValue = firstBlock * XADPCM_SAMPLES_PER_BLOCK * (size_t)e->channels;
        if (dstValue >= e->pcmValues)
            continue;
        XAdpcm_Decode(src, srcBytes, e->channels, e->pcm + dstValue, e->pcmValues - dstValue);
    }
}
