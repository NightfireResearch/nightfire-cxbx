// Shadow test for Inflate_huffman (engine/Inflate.cpp), the EDL blocks' Huffman decoder: the original and ours on
// the same made-up blocks, encoded here - stored runs, and literals and matches under random canonical trees
// (some blocks keep the last trees), matches reaching back before the block. The output, the decoder's tables
// (the game's, which last from block to block) and the error are compared. Run at start with MenuShadowTests=on
// (MenuProbe.cpp); NIGHTFIRE_SHADOW_ONLY=inflate runs it alone.

#include "InflateShadow.h"
#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"
#include "../engine/Inflate.h"

#include <stdio.h>
#include <string.h>
#include <vector>

namespace {

const unsigned kInflateHuffman = 0x000d3a80;
#define HuffTablesAt ((uint8_t *)0x0029b2d0)
const size_t HUFF_TABLES_SIZE = 0x1688 + 4 * (288 + 288 + 512);

const int LEN_BASE[29] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 20, 24, 28, 32, 40, 48, 56, 64, 80, 96, 112, 128,
                          160, 192, 224, 255};
const int LEN_EXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const int DIST_BASE[30] = {0, 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536,
                           2048, 3072, 4096, 6144, 8192, 12288, 16384, 24576};
const int DIST_EXTRA[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12,
                            13, 13};

const int HISTORY = 0x8000;     // bytes before the block's output, which matches may reach
const int OUT_SIZE = HISTORY + 0x40000;
const int IN_SIZE = 0x10000;

struct World {
    uint8_t out[OUT_SIZE];
    uint8_t in[IN_SIZE];
    maybeEDLDecompressorState state;
};
World world;
ShadowRegions regions;

// The stream, least significant bit first
struct BitWriter {
    std::vector<uint32_t> words;
    int used = 32;
    void Put(uint32_t v, int n) {
        for (int i = 0; i < n; i++) {
            if (used == 32) {
                words.push_back(0);
                used = 0;
            }
            words.back() |= ((v >> i) & 1u) << used;
            used++;
        }
    }
};

// A canonical code: lengths by symbol and each symbol's code
struct Tree {
    std::vector<int> lengths;
    std::vector<uint32_t> codes;
};

// Lengths for the symbols in use, from a random binary tree no deeper than 15 with as many leaves
void MakeLengths(Tree &t, int count, const std::vector<int> &used) {
    t.lengths.assign(count, 0);
    std::vector<int> leaves(1, 0);
    while (leaves.size() < used.size()) {
        int i = (int)ShadowRand((uint32_t)leaves.size());
        if (ShadowRand(3) == 0)
            i = (int)leaves.size() - 1;
        if (leaves[i] >= 15) {
            // find one that can split
            int j = 0;
            while (j < (int)leaves.size() && leaves[j] >= 15)
                j++;
            if (j == (int)leaves.size())
                break;
            i = j;
        }
        int d = leaves[i] + 1;
        leaves[i] = d;
        leaves.push_back(d);
    }
    if (leaves.size() == 1)
        leaves[0] = 1;   // one symbol: a code of one bit
    for (size_t k = 0; k < used.size() && k < leaves.size(); k++)
        t.lengths[used[k]] = leaves[k];
    // Canonical codes, as the decoder makes them
    t.codes.assign(count, 0);
    uint32_t code = 0;
    for (int l = 1; l <= 15; l++) {
        for (int s = 0; s < count; s++)
            if (t.lengths[s] == l)
                t.codes[s] = code++;
        code <<= 1;
    }
}

void PutSymbol(BitWriter &w, const Tree &t, int s) {
    int l = t.lengths[s];
    for (int i = l - 1; i >= 0; i--)
        w.Put((t.codes[s] >> i) & 1, 1);
}

void PutLengths(BitWriter &w, const Tree &t, int &last) {
    w.Put((uint32_t)t.lengths.size(), 9);
    for (int l : t.lengths) {
        if (l == last && ShadowRand(4)) {
            w.Put(0, 1);
        } else {
            w.Put(1, 1);
            w.Put((uint32_t)l, 4);
            last = l;
        }
    }
}

std::vector<int> Shuffled(int from, int to, int keep) {
    std::vector<int> v;
    for (int i = from; i < to; i++)
        v.push_back(i);
    for (int i = (int)v.size() - 1; i > 0; i--) {
        int j = (int)ShadowRand((uint32_t)i + 1);
        int t = v[i];
        v[i] = v[j];
        v[j] = t;
    }
    if (keep < (int)v.size())
        v.resize(keep);
    return v;
}

// A block's stream: a few sections, each stored or coded
void MakeBlock(std::vector<uint32_t> &words) {
    BitWriter w;
    Tree lit, dist;
    bool haveTrees = false;
    int last = 0;
    long produced = 0;
    int sections = 1 + (int)ShadowRand(4);
    for (int sct = 0; sct < sections; sct++) {
        if (ShadowRand(4) == 0) {
            w.Put(0, 1);
            int n = ShadowRand(5) == 0 ? 0 : (int)ShadowRand(300);
            w.Put((uint32_t)n, 15);
            for (int i = 0; i < n; i++)
                w.Put(ShadowRand(0x100), 8);
            produced += n;
        } else {
            w.Put(1, 1);
            bool keep = haveTrees && ShadowRand(3) == 0;
            if (keep) {
                w.Put(0, 9);
                w.Put(0, 9);
            } else {
                // The literals in use, the end, some lengths; a count past the last now and then
                int litCount = 257 + (int)ShadowRand(29);
                std::vector<int> used = Shuffled(0, 256, 1 + (int)ShadowRand(256));
                used.push_back(256);
                std::vector<int> lengths = Shuffled(257, litCount, (int)ShadowRand(litCount - 256));
                used.insert(used.end(), lengths.begin(), lengths.end());
                std::vector<int> order = Shuffled(0, (int)used.size(), (int)used.size());
                std::vector<int> mixed;
                for (int i : order)
                    mixed.push_back(used[i]);
                MakeLengths(lit, litCount + (ShadowRand(6) == 0 ? (int)ShadowRand(20) : 0), mixed);
                int distCount = 1 + (int)ShadowRand(30);
                std::vector<int> dused = Shuffled(0, distCount, 1 + (int)ShadowRand(distCount));
                MakeLengths(dist, distCount, dused);
                PutLengths(w, lit, last);
                PutLengths(w, dist, last);
                haveTrees = true;
            }
            int symbols = (int)ShadowRand(200);
            for (int i = 0; i < symbols; i++) {
                bool match = ShadowRand(3) == 0;
                if (match) {
                    // A length symbol and a distance the history allows
                    std::vector<int> lengthSyms, distSyms;
                    for (int s = 257; s < (int)lit.lengths.size() && s < 286; s++)
                        if (lit.lengths[s])
                            lengthSyms.push_back(s);
                    for (int s = 0; s < (int)dist.lengths.size(); s++)
                        if (dist.lengths[s] && DIST_BASE[s] + (1 << DIST_EXTRA[s]) <= HISTORY + produced)
                            distSyms.push_back(s);
                    if (!lengthSyms.empty() && !distSyms.empty()) {
                        int s = lengthSyms[ShadowRand((uint32_t)lengthSyms.size())];
                        int k = s - 257;
                        PutSymbol(w, lit, s);
                        uint32_t extra = LEN_EXTRA[k] ? ShadowRand(1u << LEN_EXTRA[k]) : 0;
                        if (LEN_EXTRA[k])
                            w.Put(extra, LEN_EXTRA[k]);
                        int d = distSyms[ShadowRand((uint32_t)distSyms.size())];
                        PutSymbol(w, dist, d);
                        if (DIST_EXTRA[d])
                            w.Put(ShadowRand(1u << DIST_EXTRA[d]), DIST_EXTRA[d]);
                        produced += LEN_BASE[k] + extra + 3;
                        continue;
                    }
                }
                std::vector<int> lits;
                for (int s = 0; s < 256; s++)
                    if (lit.lengths[s])
                        lits.push_back(s);
                PutSymbol(w, lit, lits[ShadowRand((uint32_t)lits.size())]);
                produced++;
            }
            PutSymbol(w, lit, 256);
        }
        w.Put(sct == sections - 1 ? 1 : 0, 1);
    }
    w.Put(0, 32);
    w.Put(0, 32);
    words = w.words;
}

uint32_t Run(bool original) {
    world.state.errNum = 0;
    if (original) {
        XbeOriginalScope o(kInflateHuffman);
        ((void (__cdecl *)(maybeEDLDecompressorState *))kInflateHuffman)(&world.state);
    } else {
        Inflate_huffman(&world.state);
    }
    return (uint32_t)world.state.errNum;
}
uint32_t RunTheirs(void) { return Run(true); }
uint32_t RunOurs(void) { return Run(false); }

} // namespace

void InflateShadow_Run(void) {
    std::vector<uint8_t> savedTables(HuffTablesAt, HuffTablesAt + HUFF_TABLES_SIZE);
    regions.Clear();
    regions.Add(&world, sizeof(world));
    regions.Add(HuffTablesAt, HUFF_TABLES_SIZE);
    int cases = 0, differ = 0;
    std::vector<uint32_t> words;
    for (int trial = 0; trial < 3000; trial++) {
        MakeBlock(words);
        if (words.size() * 4 + sizeof(EDLHeader) > IN_SIZE)
            continue;
        for (int i = 0; i < OUT_SIZE; i++)
            world.out[i] = (uint8_t)ShadowRand(0x100);
        memset(world.in, 0, sizeof(world.in));
        memcpy(world.in + sizeof(EDLHeader), words.data(), words.size() * 4);
        memset(&world.state, 0, sizeof(world.state));
        world.state.src = (char *)world.in;
        world.state.dst = (char *)world.out + HISTORY;
        cases++;
        if (!ShadowCompare(regions, "inflate", "Inflate_huffman", trial, RunTheirs, RunOurs))
            differ++;
    }
    memcpy(HuffTablesAt, savedTables.data(), savedTables.size());
    printf("[inflate] Inflate_huffman: %d cases, %d differ\n", cases, differ);
    fflush(stdout);
}
