#include "Ps2Convert.h"
#include "FS_ps2.h"
#include "Ps2Mesh.h"
#include "Ps2Texture.h"
#include "Ps2Sound.h"
#include "../../common/sound/xadpcm.h"
#include "../memory.h"

#include <stdio.h>
#include <string.h>
#include <algorithm>

// The game's own allocation types for these (MemInfo's names)
#define MALLOC_PS2_OBJECT   ((MallocFlags)0x2804)
#define MALLOC_PS2_TEXTURE  ((MallocFlags)0x2b04)

void Ps2_ConvertMapTexture(bmpHeaderEntry *header, uint32_t hashcode, const uint8_t *palette, uint32_t paletteBytes) {
    const uint8_t *texels = (const uint8_t *)(TextureInfo *)header->textureInfo;
    int colours = Ps2Texture_PaletteColours(paletteBytes);
    if (texels == NULL || colours == 0) {
        printf("[ps2] texture 0x%08x: a palette of %u bytes, not converted\n", hashcode, paletteBytes);
        header->textureInfo = NULL;
        return;
    }
    int width = header->widthMinusOne + 1;
    int height = header->heightMinusOne + 1;
    int frames = header->animFrames != 0 ? header->animFrames : 1;
    uint32_t texelBytes = (*(const uint32_t *)(texels - 4) & 0xffffff) - 4;    // from block 0x16's header word
    if (!Ps2Texture_FitSize(&width, &height, frames, colours, texelBytes)) {
        printf("[ps2] texture 0x%08x: %u bytes of texels, not converted\n", hashcode, texelBytes);
        header->textureInfo = NULL;
        return;
    }
    Ps2TextureFormat format = Ps2Texture_ChooseFormat(palette, colours, width, height);
    // The pause screen's film is drawn into one texture from another, pixel by pixel (Woman.cpp): those stay linear
    // A8R8G8B8, as the Xbox's are
    if (hashcode == HT_Texture_Fire2 || hashcode == HT_Texture_WomanBack)
        format = PS2TEX_LIN_A8R8G8B8;
    TextureInfo *tex = (TextureInfo *)Mem_Malloc(Ps2Texture_KxtBytes(width, height, frames, format),
                                                 MALLOC_PS2_TEXTURE, 0);
    Ps2Texture_BuildKxt(tex, hashcode, width, height, frames, header->someDivisor, texels, palette, colours, format);
    header->textureInfo = tex;
}

// The last block 0x2b, for the mesh after it
static const uint8_t *MorphData;
static uint32_t MorphDataBytes;

void Ps2_MorphData(const uint8_t *block, uint32_t blockBytes) {
    MorphData = block;
    MorphDataBytes = blockBytes;
}

ModelData *Ps2_ConvertModel(const uint8_t *block, uint32_t blockBytes) {
    std::vector<uint8_t> kxe;
    Ps2MeshStats stats;
    bool converted = Ps2Mesh_ConvertToKxe(block, blockBytes, MorphData, MorphDataBytes, kxe, &stats);
    MorphData = NULL;
    if (!converted)
        return NULL;
    ModelData *model = (ModelData *)Mem_Malloc(kxe.size(), MALLOC_PS2_OBJECT, 0);
    memcpy(model, kxe.data(), kxe.size());
    return model;
}

// The streamed sounds' rate and their samples' header as the Xbox's banks have them (mono, 4-bit ADPCM)
#define STREAM_FREQUENCY 22050
#define STREAM_CHANNELS 1
#define STREAM_BITS_PER_SAMPLE 4

// A streamed sound's SPU ADPCM out of the language's STREAMS.BIN, through STREAMS.LUT (SFXStream.cpp's
// StreamLookupFileDetails); empty when it cannot be read
static std::vector<uint8_t> ReadStream(const std::vector<StreamLookupFileDetails> &lookup, int stream) {
    std::vector<uint8_t> audio;
    if (stream < 0 || stream >= (int)lookup.size() || lookup[stream].AudioSize <= 0)
        return audio;
    char name[64];
    snprintf(name, sizeof(name), "%s/STREAMS/STREAMS.BIN", SFXLanguageName);
    audio.resize((size_t)lookup[stream].AudioSize);
    if (!Ps2Fs_ReadFile(name, (uint32_t)lookup[stream].AudioOffset, (uint32_t)audio.size(), audio.data()))
        audio.clear();
    return audio;
}

static std::vector<StreamLookupFileDetails> ReadStreamLookup(void) {
    char name[64];
    snprintf(name, sizeof(name), "%s/STREAMS/STREAMS.LUT", SFXLanguageName);
    std::vector<StreamLookupFileDetails> lookup(Ps2Fs_FileSize(name) / sizeof(StreamLookupFileDetails));
    if (!lookup.empty() && !Ps2Fs_ReadFile(name, 0, (uint32_t)(lookup.size() * sizeof(StreamLookupFileDetails)),
                                           lookup.data()))
        lookup.clear();
    return lookup;
}

// The bank's streamed sounds pointed at new samples after the bank's own, one a stream: their streams' numbers
static std::vector<int> TakeStreams(SoundBankHeader *bank) {
    std::vector<int> streams;
    for (int k = 0; k < bank->SFXCount; k++) {
        SFXParameters *sound = (SFXParameters *)((char *)bank->SFXBuffer + bank->SFXHeaderData[k].Offset);
        SFXSamplePoolFiles *pool = (SFXSamplePoolFiles *)(sound + 1);
        for (int i = 0; i < sound->SampleCount; i++) {
            if (pool[i].FileRef >= 0)
                continue;
            int stream = -1 - pool[i].FileRef;
            size_t at = std::find(streams.begin(), streams.end(), stream) - streams.begin();
            if (at == streams.size())
                streams.push_back(stream);
            pool[i].FileRef = bank->SampleCount + (int)at;
        }
    }
    return streams;
}

void Ps2_ConvertSoundBank(SoundBankHeader *bank) {
    int count = bank->SampleCount;
    SampleHeaderData *samples = bank->SampleHeader;
    if (count <= 0)
        return;
    std::vector<int> streams = TakeStreams(bank);
    std::vector<StreamLookupFileDetails> lookup;
    if (!streams.empty())
        lookup = ReadStreamLookup();

    // Every sample's sound decoded: the bank's own from the .SBF, then the streams'
    std::vector<std::vector<int16_t>> pcm(count + streams.size());
    std::vector<long> loopStarts(count + streams.size(), -1);
    size_t bytes = 0;
    for (int i = 0; i < count; i++) {
        pcm[i].resize((size_t)samples[i].Size / SPU_BLOCK_BYTES * SPU_BLOCK_SAMPLES);
        pcm[i].resize(Ps2Spu_Decode((const uint8_t *)(void *)samples[i].Address, samples[i].Size, pcm[i].data(),
                                    &loopStarts[i]));
        bytes += Ps2Spu_XAdpcmBytes(pcm[i].size());
    }
    int missing = 0;
    for (size_t s = 0; s < streams.size(); s++) {
        std::vector<uint8_t> audio = ReadStream(lookup, streams[s]);
        std::vector<int16_t> &out = pcm[count + s];
        out.resize(audio.size() / SPU_BLOCK_BYTES * SPU_BLOCK_SAMPLES);
        long loopStart;
        out.resize(Ps2Spu_Decode(audio.data(), audio.size(), out.data(), &loopStart));
        missing += out.empty();
        bytes += Ps2Spu_XAdpcmBytes(out.size());
    }
    if (missing != 0)
        printf("[ps2] a sound bank's streamed sounds: %d of %d not in STREAMS.BIN\n", missing, (int)streams.size());

    // The headers, the bank's and the streams', in memory of their own when there are streams (the .SHF's count first)
    if (!streams.empty()) {
        int total = count + (int)streams.size();
        int *headers = (int *)Mem_Malloc(sizeof(int) + total * sizeof(SampleHeaderData), MALLOC_XBOX, 0);
        headers[0] = total;
        SampleHeaderData *all = (SampleHeaderData *)(headers + 1);
        memcpy(all, samples, count * sizeof(SampleHeaderData));
        for (int s = count; s < total; s++) {
            memset(&all[s], 0, sizeof(SampleHeaderData));
            all[s].Frequency = STREAM_FREQUENCY;
            all[s].NumberOfChannels = STREAM_CHANNELS;
            all[s].BitsPerSample = STREAM_BITS_PER_SAMPLE;
        }
        psiSFXFreeFileMem(bank->SampleHeaderBuffer);
        bank->SampleHeaderBuffer = headers;
        bank->SampleHeader = all;
        bank->SampleCount = total;
        samples = all;
    }

    void *spu = samples[0].Address;     // the .SBF file, the samples one after another
    uint8_t *out = (uint8_t *)Mem_Malloc(bytes, MALLOC_XBOX, 0);
    for (int i = 0; i < bank->SampleCount; i++) {
        SampleHeaderData *sample = &samples[i];
        int written = (int)Ps2Spu_EncodeXAdpcm(pcm[i].data(), pcm[i].size(), out);
        sample->Address = out;
        sample->Size = written;
        sample->RealSize = written;
        if (i < count) {
            sample->Frequency = (int)Ps2Spu_PitchToHz(sample->Frequency);
            // The loop's start (the block the SPU flags) as the Xbox's: the byte offset of its block
            sample->LoopOffset = loopStarts[i] > 0 ? (int)(loopStarts[i] / XADPCM_SAMPLES_PER_BLOCK *
                                                           XADPCM_BLOCK_BYTES_PER_CHANNEL)
                                                   : 0;
        }
        out += written;
    }
    Mem_Free(&spu);
}
