// Offline check of the audio backend's Xbox ADPCM decoder (src/action/sound/xadpcm.cpp). The equivalent of
// tools/vsh_translate_test.cpp for audio - run it after any change to the decoder; see tools/xadpcm_test.ps1
// for the build. Needs nothing from the game to run.
//
//   xadpcm_test.exe                              run the self-checks
//   xadpcm_test.exe <in.adpcm> <out.wav> [chans] also decode a real ADPCM blob to a playable .wav
//
// The second form is there for the day a real sound bank can be extracted: decoding it and listening is the
// only thing that can settle XADPCM_SAMPLES_PER_BLOCK (see the comment in xadpcm.h), which no offline test can.
//
// What the self-checks cover, and why each one is here:
//  - hand-computed vectors, worked out from the IMA algorithm by hand rather than from any implementation.
//    These pin down the things a round-trip cannot: nibble ordering within a group, header endianness, that
//    the header predictor is state rather than an emitted sample, sign handling and saturation. A round-trip
//    through our own encoder would agree with the decoder even if both had, say, the nibble order backwards.
//  - an encode/decode round-trip over a sine sweep, which catches structural mistakes end to end and gives a
//    signal-to-noise figure to compare against after a change. The encoder lives only in this test.
//  - stereo interleave and channel independence.
//  - the size and byte-offset/sample-index arithmetic the backend will use for loop regions and play cursors.
#include "../src/action/sound/xadpcm.cpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int g_failures = 0;

static void Check(bool ok, const char *what) {
    printf("%-58s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok)
        g_failures++;
}

static void CheckSamples(const int16_t *got, const int16_t *want, int count, const char *what) {
    for (int i = 0; i < count; i++) {
        if (got[i] != want[i]) {
            printf("%-58s FAILED (sample %d: got %d, want %d)\n", what, i, got[i], want[i]);
            g_failures++;
            return;
        }
    }
    printf("%-58s ok\n", what);
}

// Builds one mono block: 4-byte header plus up to 32 bytes of nibble data (rest zero-filled).
static void MakeMonoBlock(uint8_t *block, int16_t predictor, int16_t stepIndex,
                          const uint8_t *nibbleBytes, size_t nibbleByteCount) {
    memset(block, 0, XADPCM_BLOCK_BYTES_PER_CHANNEL);
    block[0] = (uint8_t)(predictor & 0xff);
    block[1] = (uint8_t)((predictor >> 8) & 0xff);
    block[2] = (uint8_t)(stepIndex & 0xff);
    block[3] = (uint8_t)((stepIndex >> 8) & 0xff);
    if (nibbleBytes != NULL && nibbleByteCount > 0)
        memcpy(block + 4, nibbleBytes, nibbleByteCount > 32 ? 32 : nibbleByteCount);
}

// ---------------------------------------------------------------------------------------------------------------
// Hand-computed vectors
// ---------------------------------------------------------------------------------------------------------------

static void TestHandVectors(void) {
    uint8_t block[XADPCM_BLOCK_BYTES_PER_CHANNEL * 2];
    int16_t out[XADPCM_SAMPLES_PER_BLOCK * 2];

    // Vector 1 - nibble order and "header predictor is state, not a sample".
    // predictor 0, index 0 (step 7), first data byte 0x21, everything else zero.
    //   nibble 0 = 1 (the LOW nibble of 0x21): delta = 7>>3 + 7>>2 = 0 + 1 = 1  -> predictor 1
    //   nibble 1 = 2 (the HIGH nibble):        delta = 7>>3 + 7>>1 = 0 + 3 = 3  -> predictor 4
    //   nibble 2+ = 0:                         delta = 7>>3 = 0                 -> predictor stays 4
    // If the nibble order were reversed the first two samples would be 3 and 4 instead; if the header
    // predictor were emitted as a sample they would be 0 and 1.
    {
        const uint8_t data[4] = { 0x21, 0x00, 0x00, 0x00 };
        MakeMonoBlock(block, 0, 0, data, sizeof(data));
        size_t n = XAdpcm_Decode(block, XADPCM_BLOCK_BYTES_PER_CHANNEL, 1, out, sizeof(out) / sizeof(out[0]));
        Check(n == XADPCM_SAMPLES_PER_BLOCK, "mono block decodes to 64 samples");
        const int16_t want[8] = { 1, 4, 4, 4, 4, 4, 4, 4 };
        CheckSamples(out, want, 8, "vector 1: nibble order, header is state only");
    }

    // Vector 2 - header endianness and the step index walking back down the table.
    // predictor bytes 10 27 = 0x2710 = 10000, index bytes 05 00 = 5 (step 12), all data nibbles 0.
    //   each code 0 adds step>>3 and moves the index down one: steps 12,11,10,9,8,7 -> +1 five times, then +0.
    // A byte-swapped header would read the predictor as 0x1027 = 4135 instead.
    {
        MakeMonoBlock(block, 10000, 5, NULL, 0);
        XAdpcm_Decode(block, XADPCM_BLOCK_BYTES_PER_CHANNEL, 1, out, sizeof(out) / sizeof(out[0]));
        const int16_t want[7] = { 10001, 10002, 10003, 10004, 10005, 10005, 10005 };
        CheckSamples(out, want, 7, "vector 2: header endianness, step index decay");
    }

    // Vector 3 - the sign bit is bit 3 of the code, applied to the whole magnitude.
    // predictor 0, index 0 (step 7), first data byte 0x09: low nibble 9 = sign + bit0.
    //   magnitude = 7>>3 + 7>>2 = 1, negated -> predictor -1.
    {
        const uint8_t data[1] = { 0x09 };
        MakeMonoBlock(block, 0, 0, data, sizeof(data));
        XAdpcm_Decode(block, XADPCM_BLOCK_BYTES_PER_CHANNEL, 1, out, sizeof(out) / sizeof(out[0]));
        const int16_t want[2] = { -1, -1 };
        CheckSamples(out, want, 2, "vector 3: sign bit");
    }

    // Vector 4 - saturation of both the predictor and the step index.
    // predictor 32767, index 88 (step 32767), first nibble 7 = the largest positive delta.
    //   delta = 4095 + 32767 + 16383 + 8191 = 61436, so the predictor must clamp to 32767 rather than wrap,
    //   and the index must clamp to 88 rather than run off the end of the step table.
    {
        const uint8_t data[1] = { 0x07 };
        MakeMonoBlock(block, 32767, 88, data, sizeof(data));
        XAdpcm_Decode(block, XADPCM_BLOCK_BYTES_PER_CHANNEL, 1, out, sizeof(out) / sizeof(out[0]));
        Check(out[0] == 32767, "vector 4: predictor saturates at +32767");
    }

    // ...and the negative end, with an out-of-range step index in the header for good measure.
    {
        const uint8_t data[1] = { 0x0f };
        MakeMonoBlock(block, -32768, 200, data, sizeof(data)); // index 200 must clamp to 88
        XAdpcm_Decode(block, XADPCM_BLOCK_BYTES_PER_CHANNEL, 1, out, sizeof(out) / sizeof(out[0]));
        Check(out[0] == -32768, "vector 5: predictor saturates at -32768, bad index clamped");
    }

    // Vector 6 - stereo: two 4-byte headers first, then nibble groups interleaved per channel, and the two
    // channels' decoder states must not interfere. Channel 0 gets vector 1's data, channel 1 gets silence at
    // predictor 1000.
    {
        uint8_t stereo[XADPCM_BLOCK_BYTES_PER_CHANNEL * 2];
        memset(stereo, 0, sizeof(stereo));
        stereo[0] = 0; stereo[1] = 0; stereo[2] = 0; stereo[3] = 0;          // ch0: predictor 0, index 0
        stereo[4] = 0xe8; stereo[5] = 0x03; stereo[6] = 0; stereo[7] = 0;    // ch1: predictor 1000, index 0
        stereo[8] = 0x21;                                                     // ch0 group 0, first byte
        // ch1 group 0 is stereo[12..15], left as zeros.
        size_t n = XAdpcm_Decode(stereo, sizeof(stereo), 2, out, sizeof(out) / sizeof(out[0]));
        Check(n == XADPCM_SAMPLES_PER_BLOCK * 2, "stereo block decodes to 64 frames");
        const int16_t want[6] = { 1, 1000, 4, 1000, 4, 1000 };
        CheckSamples(out, want, 6, "vector 6: stereo interleave, independent states");
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Sizes and position arithmetic
// ---------------------------------------------------------------------------------------------------------------

static void TestArithmetic(void) {
    Check(XAdpcm_BlockBytes(1) == 36 && XAdpcm_BlockBytes(2) == 72, "block size is 36 bytes per channel");
    Check(XAdpcm_BlockCount(36 * 10, 1) == 10, "block count, mono");
    Check(XAdpcm_BlockCount(72 * 10, 2) == 10, "block count, stereo");
    Check(XAdpcm_BlockCount(36 * 10 + 20, 1) == 10, "trailing partial block ignored");
    Check(XAdpcm_DecodedSamplesPerChannel(36 * 10, 1) == 640, "decoded samples per channel, mono");
    Check(XAdpcm_DecodedValueCount(72 * 10, 2) == 1280, "decoded value count, stereo");

    // What the backend needs these for: a loop point the game gives in ADPCM bytes has to become a PCM sample
    // index, and a PCM play cursor has to come back as an ADPCM byte offset for psiStreamGetPlayPos.
    Check(XAdpcm_ByteOffsetToSample(36 * 3, 1) == 64 * 3, "byte offset -> sample, mono");
    Check(XAdpcm_ByteOffsetToSample(72 * 3, 2) == 64 * 3, "byte offset -> sample, stereo");
    Check(XAdpcm_SampleToByteOffset(64 * 3, 1) == 36 * 3, "sample -> byte offset, mono");
    Check(XAdpcm_SampleToByteOffset(64 * 3, 2) == 72 * 3, "sample -> byte offset, stereo");
    Check(XAdpcm_ByteOffsetToSample(36 * 3 + 35, 1) == 64 * 3, "byte offset rounds down to a block");
    Check(XAdpcm_SampleToByteOffset(64 * 3 + 63, 1) == 36 * 3, "sample rounds down to a block");

    bool roundTrips = true;
    for (int channels = 1; channels <= 2; channels++) {
        for (size_t block = 0; block < 100; block++) {
            size_t bytes = block * XAdpcm_BlockBytes(channels);
            if (XAdpcm_SampleToByteOffset(XAdpcm_ByteOffsetToSample(bytes, channels), channels) != bytes)
                roundTrips = false;
        }
    }
    Check(roundTrips, "byte offset <-> sample index round-trips on block bounds");

    // Degenerate inputs should be refused rather than crash - the game does bind zero-length buffers
    // (maybeSoundShutdown unbinds every buffer with SetBufferData(NULL, 0)).
    int16_t out[8];
    Check(XAdpcm_Decode(NULL, 36, 1, out, 8) == 0, "NULL source refused");
    Check(XAdpcm_Decode(out, 0, 1, out, 8) == 0, "zero-length source decodes to nothing");
    uint8_t block[36] = { 0 };
    Check(XAdpcm_Decode(block, 36, 6, out, 8) == 0, "unsupported channel count refused");
    Check(XAdpcm_Decode(block, 36, 1, out, 4) == 4, "output capacity respected");
}

// ---------------------------------------------------------------------------------------------------------------
// Round-trip. The encoder here exists only to generate test material - it is not part of the backend.
// ---------------------------------------------------------------------------------------------------------------

// Encodes samples into Xbox ADPCM blocks. Mirrors the decoder's state machine exactly, which is what an IMA
// encoder has to do: it picks the code whose decoded delta is closest below the real difference, then runs the
// decoder's own update so encoder and decoder stay in step.
static size_t EncodeMono(const int16_t *samples, size_t sampleCount, uint8_t *dst) {
    XAdpcmState state;
    state.predictor = 0;
    state.stepIndex = 0;

    size_t written = 0;
    size_t blocks = sampleCount / XADPCM_SAMPLES_PER_BLOCK;
    for (size_t block = 0; block < blocks; block++) {
        uint8_t *out = dst + written;
        memset(out, 0, XADPCM_BLOCK_BYTES_PER_CHANNEL);
        // The header carries the state the first nibble of this block is applied to.
        out[0] = (uint8_t)(state.predictor & 0xff);
        out[1] = (uint8_t)((state.predictor >> 8) & 0xff);
        out[2] = (uint8_t)(state.stepIndex & 0xff);
        out[3] = 0;

        for (int i = 0; i < XADPCM_SAMPLES_PER_BLOCK; i++) {
            int target = samples[block * XADPCM_SAMPLES_PER_BLOCK + i];
            int diff = target - state.predictor;
            unsigned code = 0;
            if (diff < 0) {
                code = 8;
                diff = -diff;
            }
            int step = kStepTable[state.stepIndex];
            if (diff >= step) { code |= 4; diff -= step; }
            if (diff >= step >> 1) { code |= 2; diff -= step >> 1; }
            if (diff >= step >> 2) { code |= 1; }
            DecodeNibble(code, &state); // keep the encoder's state identical to the decoder's

            // Nibble i goes in byte 4 + i/2, low nibble first.
            out[4 + i / 2] |= (uint8_t)(code << ((i & 1) * 4));
        }
        written += XADPCM_BLOCK_BYTES_PER_CHANNEL;
    }
    return written;
}

static void TestRoundTrip(void) {
    const int sampleCount = XADPCM_SAMPLES_PER_BLOCK * 64; // 4096 samples
    static int16_t original[sampleCount];
    static int16_t decoded[sampleCount];
    static uint8_t encoded[(sampleCount / XADPCM_SAMPLES_PER_BLOCK) * XADPCM_BLOCK_BYTES_PER_CHANNEL];

    // A sweep from about 100 Hz to 5 kHz at 44032 Hz, at -3 dBFS: smooth enough for ADPCM to track, and wide
    // enough in frequency that a block-layout mistake shows up as a large error rather than a small one.
    for (int i = 0; i < sampleCount; i++) {
        double t = (double)i / 44032.0;
        double freq = 100.0 + (5000.0 - 100.0) * ((double)i / (double)sampleCount);
        original[i] = (int16_t)(23000.0 * sin(2.0 * 3.14159265358979 * freq * t));
    }

    size_t encodedBytes = EncodeMono(original, sampleCount, encoded);
    Check(encodedBytes == sizeof(encoded), "round-trip: encoder filled every block");

    size_t got = XAdpcm_Decode(encoded, encodedBytes, 1, decoded, sampleCount);
    Check(got == (size_t)sampleCount, "round-trip: decoder produced every sample");

    double signal = 0.0, noise = 0.0, peakError = 0.0;
    for (int i = 0; i < sampleCount; i++) {
        double s = (double)original[i];
        double e = (double)original[i] - (double)decoded[i];
        signal += s * s;
        noise += e * e;
        if (fabs(e) > peakError)
            peakError = fabs(e);
    }
    double snr = (noise > 0.0) ? 10.0 * log10(signal / noise) : 999.0;
    printf("%-58s %.1f dB (peak error %.0f)\n", "round-trip: signal-to-noise", snr, peakError);
    // IMA ADPCM on a smooth sweep comfortably clears 20 dB. Anything near or below 0 dB means the decoder is
    // not reconstructing the waveform at all, which is what a block-layout or state-tracking bug looks like.
    Check(snr > 20.0, "round-trip: SNR above 20 dB");
}

// ---------------------------------------------------------------------------------------------------------------
// Optional: decode a real blob to a .wav so it can be listened to
// ---------------------------------------------------------------------------------------------------------------

static bool WriteWav(const char *path, const int16_t *samples, size_t valueCount, int channels, uint32_t rate) {
    FILE *f = fopen(path, "wb");
    if (f == NULL)
        return false;
    uint32_t dataBytes = (uint32_t)(valueCount * sizeof(int16_t));
    uint32_t byteRate = rate * (uint32_t)channels * 2u;
    uint16_t blockAlign = (uint16_t)(channels * 2);
    uint32_t riffSize = 36 + dataBytes;
    uint32_t fmtSize = 16;
    uint16_t pcm = 1, chans = (uint16_t)channels, bits = 16;
    fwrite("RIFF", 1, 4, f);          fwrite(&riffSize, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);      fwrite(&fmtSize, 4, 1, f);
    fwrite(&pcm, 2, 1, f);            fwrite(&chans, 2, 1, f);
    fwrite(&rate, 4, 1, f);           fwrite(&byteRate, 4, 1, f);
    fwrite(&blockAlign, 2, 1, f);     fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f);          fwrite(&dataBytes, 4, 1, f);
    fwrite(samples, sizeof(int16_t), valueCount, f);
    fclose(f);
    return true;
}

static int DecodeFile(const char *inPath, const char *outPath, int channels) {
    FILE *f = fopen(inPath, "rb");
    if (f == NULL) { printf("cannot open %s\n", inPath); return 2; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *src = (uint8_t *)malloc((size_t)size);
    if (src == NULL) { fclose(f); printf("out of memory\n"); return 2; }
    if (fread(src, 1, (size_t)size, f) != (size_t)size) { fclose(f); free(src); printf("short read\n"); return 2; }
    fclose(f);

    size_t values = XAdpcm_DecodedValueCount((size_t)size, channels);
    int16_t *pcm = (int16_t *)malloc(values * sizeof(int16_t));
    if (pcm == NULL) { free(src); printf("out of memory\n"); return 2; }
    size_t got = XAdpcm_Decode(src, (size_t)size, channels, pcm, values);
    printf("%ld bytes of %d-channel ADPCM (%zu blocks) -> %zu samples\n",
           size, channels, XAdpcm_BlockCount((size_t)size, channels), got / (size_t)channels);
    int rc = WriteWav(outPath, pcm, got, channels, 44032) ? 0 : 2;
    printf(rc == 0 ? "wrote %s\n" : "could not write %s\n", outPath);
    free(pcm);
    free(src);
    return rc;
}

int main(int argc, char **argv) {
    if (argc >= 3)
        return DecodeFile(argv[1], argv[2], argc >= 4 ? atoi(argv[3]) : 1);

    printf("Xbox ADPCM decoder self-checks (%d samples per %d-byte block)\n\n",
           XADPCM_SAMPLES_PER_BLOCK, XADPCM_BLOCK_BYTES_PER_CHANNEL);
    TestHandVectors();
    printf("\n");
    TestArithmetic();
    printf("\n");
    TestRoundTrip();
    printf("\n%s\n", g_failures == 0 ? "all checks passed" : "THERE WERE FAILURES");
    return g_failures == 0 ? 0 : 1;
}
