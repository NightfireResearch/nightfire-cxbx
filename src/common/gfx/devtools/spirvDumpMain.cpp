// spirvdump: the translators' SPIR-V (shaderSpirv.h) without the game, for spirv-val.
//
//   spirvdump <folder> <file>...
//
// Each file is a d3d9_shaders.log or d3d9_pixel_shaders.log from a run, or an XBE whose vertex programs are found
// by their header, read as shadercheck (shaderCheckMain.cpp) reads them. Every program is written to the folder as
// vs_<n>.spv or ps_<n>.spv, and one line per program gives a hash of its vs_2_0/ps_2_0 tokens, so that two builds'
// D3D9 lowerings can be compared by their output. Then "spirv-val --target-env vulkan1.0 <folder>/*.spv".

#include "../nv2aPixelShader.h"
#include "../nv2aVertexShader.h"
#include "../shaderSpirv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

namespace {

const char *g_folder;
int g_programs = 0, g_failed = 0;

std::string ReadFile(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return std::string();
    std::string s;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}

std::vector<uint32_t> Words(const char *text) {
    std::vector<uint32_t> words;
    char *end;
    for (const char *p = text;; p = end) {
        while (*p == ' ' || *p == '\t') p++;
        unsigned long v = strtoul(p, &end, 16);
        if (end == p) break;
        words.push_back((uint32_t)v);
    }
    return words;
}

uint32_t Hash(const std::vector<uint32_t> &words) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < words.size(); i++)
        for (int b = 0; b < 4; b++) h = (h ^ ((words[i] >> (8 * b)) & 0xFF)) * 16777619u;
    return h;
}

void Write(const char *name, const std::vector<uint32_t> &words) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.spv", g_folder, name);
    FILE *f = fopen(path, "wb");
    if (f == NULL) { fprintf(stderr, "cannot write %s\n", path); return; }
    fwrite(words.data(), 4, words.size(), f);
    fclose(f);
}

// An Xbox declaration's components, NORMPACKED3 registers and SPIR-V input kinds, as the SDL GPU backend reads them.
bool DeclarationInfo(const std::vector<uint32_t> &decl, uint32_t *normPacked, uint8_t components[16], uint8_t kinds[16]) {
    *normPacked = 0;
    memset(components, 0, 16);
    memset(kinds, kSpirvFloat, 16);
    for (size_t i = 0; i < decl.size() && decl[i] != 0xFFFFFFFFu; i++) {
        if ((decl[i] & 0xF0000000u) != 0x40000000u) continue;
        uint32_t type = (decl[i] >> 16) & 0xFF, reg = decl[i] & 0xF;
        uint8_t n = (uint8_t)((type >> 4) & 0xF), kind = kSpirvFloat;
        switch (type) {
            case 0x12: case 0x22: case 0x32: case 0x42: case 0x11: case 0x21: case 0x41: break;
            case 0x15: case 0x25: case 0x35: case 0x45: kind = kSpirvSigned; break;
            case 0x14: kind = kSpirvUnsigned; break;
            case 0x40: n = 4; kind = kSpirvColour; break;
            case 0x16: n = 3; kind = kSpirvUnsigned; *normPacked |= 1u << reg; break;
            default: return false;
        }
        components[reg] = (uint8_t)(n == 0 || n > 4 ? 4 : n);
        kinds[reg] = kind;
    }
    return true;
}

void Vertex(const char *what, const std::vector<uint8_t> &function, uint32_t normPacked, const uint8_t components[16],
            const uint8_t kinds[16]) {
    std::vector<uint32_t> tokens, words;
    Nv2aVertexShaderInfo info;
    ShaderSpirvInfo spirvInfo;
    char error[256];
    bool tokensOk = Nv2aVertexShader_Translate(function.data(), normPacked, components, &tokens, &info, error, sizeof(error));
    ShaderSpirvOptions options;
    memset(&options, 0, sizeof(options));
    memcpy(options.inputKind, kinds, 16);
    char name[64];
    for (int sprite = 0; sprite < 2; sprite++) {
        options.pointSprite = sprite != 0;
        bool ok = Nv2aVertexShader_TranslateSpirv(function.data(), normPacked, components, options, &words, &info,
                                                  &spirvInfo, error, sizeof(error));
        snprintf(name, sizeof(name), "vs_%d%s", g_programs, sprite ? "_sprite" : "");
        if (ok) Write(name, words);
        else { g_failed++; printf("%s: no SPIR-V: %s\n", what, error); }
    }
    printf("vs_%d d3d9 %08x %s\n", g_programs, tokensOk ? Hash(tokens) : 0, what);
    g_programs++;
}

void VertexLog(const char *path, const std::string &text) {
    size_t at = 0;
    while ((at = text.find("==== shader ", at)) != std::string::npos) {
        int index = atoi(text.c_str() + at + 12);
        size_t declAt = text.find("\ndecl:", at), codeAt = text.find("microcode (", at);
        size_t next = text.find("==== shader ", at + 1);
        at++;
        if (declAt == std::string::npos || codeAt == std::string::npos || (next != std::string::npos && codeAt > next)) continue;
        std::vector<uint32_t> decl = Words(text.c_str() + declAt + 6);
        int count = atoi(text.c_str() + codeAt + 11);
        std::vector<uint8_t> function(8 + 16 * count, 0);
        function[0] = 0x78; function[1] = 0x20; function[2] = (uint8_t)count;
        size_t line = text.find('\n', codeAt);
        for (int i = 0; i < count && line != std::string::npos; i++) {
            size_t colon = text.find(':', line);
            std::vector<uint32_t> w = Words(text.c_str() + colon + 1);
            for (int k = 0; k < 3 && k < (int)w.size(); k++) memcpy(&function[8 + 16 * i + 4 * k], &w[k], 4);
            line = text.find('\n', line + 1);
        }
        uint32_t normPacked;
        uint8_t components[16], kinds[16];
        if (!DeclarationInfo(decl, &normPacked, components, kinds)) continue;
        char what[512];
        snprintf(what, sizeof(what), "%s vertex shader %d", path, index);
        Vertex(what, function, normPacked, components, kinds);
    }
}

void PixelLog(const char *path, const std::string &text) {
    size_t at = 0;
    while ((at = text.find("==== pixel shader ", at)) != std::string::npos) {
        int index = atoi(text.c_str() + at + 18);
        size_t start = text.find('\n', at);
        size_t end = text.find("==== ", at + 1);
        at++;
        if (start == std::string::npos) continue;
        std::string block = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        for (size_t i = 0; i < block.size(); i++) if (block[i] == '\n' || block[i] == '\r') block[i] = ' ';
        std::vector<uint32_t> def = Words(block.c_str());
        if (def.size() < 60) continue;
        def.resize(60);
        std::vector<uint32_t> tokens, words;
        Nv2aPixelShaderInfo info;
        ShaderSpirvInfo spirvInfo;
        char error[256], name[64];
        bool tokensOk = Nv2aPixelShader_Translate(def.data(), &tokens, &info, error, sizeof(error));
        ShaderSpirvOptions options;
        memset(&options, 0, sizeof(options));
        options.hostPixelEnd = true;
        snprintf(name, sizeof(name), "ps_%d", g_programs);
        if (Nv2aPixelShader_TranslateSpirv(def.data(), options, &words, &info, &spirvInfo, error, sizeof(error)))
            Write(name, words);
        else { g_failed++; printf("%s pixel shader %d: no SPIR-V: %s\n", path, index, error); }
        printf("ps_%d d3d9 %08x %s pixel shader %d\n", g_programs, tokensOk ? Hash(tokens) : 0, path, index);
        g_programs++;
    }
}

// Every vertex program in an XBE, with every register a float4.
void Xbe(const char *path, const std::string &data) {
    for (size_t i = 0; i + 8 < data.size(); i++) {
        const uint8_t *p = (const uint8_t *)data.data() + i;
        if (p[0] != 0x78 || p[1] != 0x20 || p[3] != 0 || p[2] == 0 || i + 8 + 16 * (size_t)p[2] > data.size()) continue;
        int count = p[2];
        bool valid = true;
        for (int k = 0; k < count && valid; k++) {
            uint32_t w2;
            memcpy(&w2, p + 8 + 16 * k + 8, 4);
            valid = ((w2 & 1) != 0) == (k == count - 1);
        }
        if (!valid) continue;
        std::vector<uint8_t> function(p, p + 8 + 16 * count);
        uint8_t components[16], kinds[16];
        memset(components, 4, sizeof(components));
        memset(kinds, kSpirvFloat, sizeof(kinds));
        char what[512];
        snprintf(what, sizeof(what), "%s program at 0x%zx", path, i);
        Vertex(what, function, 0, components, kinds);
        i += 7 + 16 * (size_t)count;
    }
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "spirvdump <folder> <d3d9_shaders.log | d3d9_pixel_shaders.log | .xbe>...\n");
        return 2;
    }
    g_folder = argv[1];
    for (int a = 2; a < argc; a++) {
        std::string data = ReadFile(argv[a]);
        if (data.empty()) { fprintf(stderr, "cannot read %s\n", argv[a]); continue; }
        size_t len = strlen(argv[a]);
        if (len > 4 && (strcmp(argv[a] + len - 4, ".xbe") == 0 || strcmp(argv[a] + len - 4, ".XBE") == 0))
            Xbe(argv[a], data);
        else if (data.find("==== pixel shader ") != std::string::npos)
            PixelLog(argv[a], data);
        else
            VertexLog(argv[a], data);
    }
    printf("[spirvdump] %d programs, %d without SPIR-V\n", g_programs, g_failed);
    return g_failed;
}
