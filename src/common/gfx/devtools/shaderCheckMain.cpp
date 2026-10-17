// shadercheck: the shader check (shaderCheck.h) without the game.
//
//   shadercheck [-v] [-d] <file>...
//
// Each file is a d3d9_shaders.log or d3d9_pixel_shaders.log that a run left behind (the backend dumps every
// program it translates there, with the declaration), or an XBE, whose vertex programs are found by their
// header and checked with every register four floats and with each declaration, of those in the logs read
// before it, that declares exactly the registers the program reads. A .bin file is a program's raw tokens, put to the runtime's validator. A .hlsl file is fxaa.hlsl, compiled as
// the game used to compile it and compared with the committed fxaaBytecode.h. -v prints every program's
// report, not only the ones that differ; -d puts both disassemblies in every report. The exit code is the number of programs that differ.

#include "shaderCheck.h"
#include "../nv2aPixelShader.h"
#include "../nv2aVertexShader.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3dcompiler.h>
#include "../fxaaBytecode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

namespace {

bool g_verbose = false;
int g_checked = 0, g_differ = 0;
std::vector<std::vector<uint32_t> > g_declarations;   // distinct ones from the logs read so far

void Report(const char *what, bool same, const char *report) {
    g_checked++;
    if (!same) g_differ++;
    if (!same || g_verbose) printf("%s: %s", what, report);
}

// The components and NORMPACKED3-ness of an Xbox declaration's types, as the backend reads them.
bool TypeInfo(uint32_t type, uint8_t *components, bool *normPacked) {
    *normPacked = false;
    *components = (uint8_t)((type >> 4) & 0xF);
    switch (type) {
        case 0x12: case 0x22: case 0x32: case 0x42: case 0x15: case 0x25: case 0x35: case 0x45:
        case 0x11: case 0x21: case 0x41: case 0x14:
            return true;
        case 0x40: *components = 4; return true;
        case 0x16: *components = 3; *normPacked = true; return true;
        default: return false;
    }
}

bool DeclarationInfo(const std::vector<uint32_t> &decl, uint32_t *normPacked, uint8_t components[16]) {
    *normPacked = 0;
    memset(components, 0, 16);
    for (size_t i = 0; i < decl.size() && decl[i] != 0xFFFFFFFFu; i++) {
        if ((decl[i] & 0xF0000000u) != 0x40000000u) continue;
        uint32_t type = (decl[i] >> 16) & 0xFF, reg = decl[i] & 0xF;
        uint8_t n;
        bool np;
        if (!TypeInfo(type, &n, &np)) return false;
        components[reg] = (uint8_t)(n == 0 || n > 4 ? 4 : n);
        if (np) *normPacked |= 1u << reg;
    }
    return true;
}

// A NULLREF device, on which the D3D9 runtime validates each program as CreateVertexShader / CreatePixelShader do
// in the game: bytecode that disassembles can still be refused.
IDirect3DDevice9 *ValidationDevice() {
    static IDirect3DDevice9 *device = NULL;
    static bool tried = false;
    if (tried) return device;
    tried = true;
    IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (d3d == NULL) return NULL;
    HWND window = CreateWindowA("STATIC", "shadercheck", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, NULL, NULL, NULL, NULL);
    D3DPRESENT_PARAMETERS pp;
    memset(&pp, 0, sizeof(pp));
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.hDeviceWindow = window;
    if (FAILED(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_NULLREF, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device)))
        device = NULL;
    if (device == NULL) fprintf(stderr, "no NULLREF device: programs are not validated\n");
    return device;
}

// Appends the runtime's verdict on our program to the report; false if it refuses it.
bool Validate(const uint32_t *code, bool pixel, std::string *report) {
    IDirect3DDevice9 *device = ValidationDevice();
    if (device == NULL) return true;
    HRESULT hr;
    if (pixel) {
        IDirect3DPixelShader9 *shader = NULL;
        hr = device->CreatePixelShader((const DWORD *)code, &shader);
        if (shader) shader->Release();
    } else {
        IDirect3DVertexShader9 *shader = NULL;
        hr = device->CreateVertexShader((const DWORD *)code, &shader);
        if (shader) shader->Release();
    }
    if (SUCCEEDED(hr)) return true;
    char line[128];
    snprintf(line, sizeof(line), "  REFUSED by the D3D9 runtime (0x%08lx)\n", (unsigned long)hr);
    *report += line;
    return false;
}

void CheckVertex(const char *what, const std::vector<uint8_t> &function, uint32_t normPacked, const uint8_t components[16]) {
    static char report[65536];
    bool same = ShaderCheck_Vertex(function.data(), normPacked, components, report, sizeof(report));
    std::string text = report;
    std::vector<uint32_t> code;
    Nv2aVertexShaderInfo info;
    char error[256];
    if (Nv2aVertexShader_Translate(function.data(), normPacked, components, &code, &info, error, sizeof(error)))
        same = Validate(code.data(), false, &text) && same;
    Report(what, same, text.c_str());
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

// d3d9_shaders.log: "==== shader N", "decl: ...", then "microcode (K instructions):" and K lines of three words.
void CheckVertexLog(const char *path, const std::string &text) {
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
        uint8_t components[16];
        if (!DeclarationInfo(decl, &normPacked, components)) continue;   // the backend refuses these too
        bool known = false;
        for (size_t d = 0; d < g_declarations.size() && !known; d++) known = g_declarations[d] == decl;
        if (!known) g_declarations.push_back(decl);
        char what[512];
        snprintf(what, sizeof(what), "%s vertex shader %d", path, index);
        CheckVertex(what, function, normPacked, components);
    }
}

// d3d9_pixel_shaders.log: "==== pixel shader N at ..." then the 60 definition words.
void CheckPixelLog(const char *path, const std::string &text) {
    size_t at = 0;
    while ((at = text.find("==== pixel shader ", at)) != std::string::npos) {
        int index = atoi(text.c_str() + at + 18);
        size_t start = text.find('\n', at);
        size_t end = text.find("==== ", at + 1);
        at++;
        if (start == std::string::npos) continue;
        std::string block = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        for (size_t i = 0; i < block.size(); i++) if (block[i] == '\n' || block[i] == '\r') block[i] = ' ';
        std::vector<uint32_t> def = Words(block.c_str());   // the words stop at the line after the definition
        if (def.size() < 60) continue;
        def.resize(60);
        static char report[65536];
        bool same = ShaderCheck_Pixel(def.data(), report, sizeof(report));
        std::string text = report;
        std::vector<uint32_t> code;
        Nv2aPixelShaderInfo info;
        char error[256];
        if (Nv2aPixelShader_Translate(def.data(), &code, &info, error, sizeof(error)))
            same = Validate(code.data(), true, &text) && same;
        char what[512];
        snprintf(what, sizeof(what), "%s pixel shader %d", path, index);
        Report(what, same, text.c_str());
    }
}

// An XBE's vertex programs: {0x78, 0x20, count, 0}, four bytes, then count 16-byte instructions of which
// only the last has the final bit.
void CheckXbe(const char *path, const std::string &data) {
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
        // Every register as four floats, then the declarations the game was seen to use.
        uint8_t components[16];
        memset(components, 4, sizeof(components));
        char what[512];
        snprintf(what, sizeof(what), "%s program at 0x%zx, all float4", path, i);
        CheckVertex(what, function, 0, components);
        std::vector<uint32_t> code;
        Nv2aVertexShaderInfo info;
        char error[256];
        uint32_t reads = Nv2aVertexShader_Translate(function.data(), 0, components, &code, &info, error, sizeof(error)) ? info.inputsUsed : 0;
        for (size_t d = 0; d < g_declarations.size(); d++) {
            uint32_t normPacked, declared = 0;
            if (!DeclarationInfo(g_declarations[d], &normPacked, components)) continue;
            for (int r = 0; r < 16; r++) if (components[r]) declared |= 1u << r;
            if (declared != reads) continue;   // the game pairs a program with a declaration of what it reads
            snprintf(what, sizeof(what), "%s program at 0x%zx, declaration %zu", path, i, d);
            CheckVertex(what, function, normPacked, components);
        }
        i += 7 + 16 * (size_t)count;
    }
}

// fxaa.hlsl against fxaaBytecode.h: the blob must be what D3DCompile makes of the source, as fxaa.cpp once ran it.
void CheckFxaa(const char *path, const std::string &text) {
    struct Entry { const char *entry, *profile; const DWORD *blob; size_t size; };
    const Entry entries[2] = { { "vsmain", "vs_3_0", g_fxaaVertexShader, sizeof(g_fxaaVertexShader) },
                               { "psmain", "ps_3_0", g_fxaaPixelShader, sizeof(g_fxaaPixelShader) } };
    std::string source;
    for (size_t i = 0; i < text.size(); i++) if (text[i] != '\r') source += text[i];
    for (int i = 0; i < 2; i++) {
        ID3DBlob *code = NULL, *errors = NULL;
        HRESULT hr = D3DCompile(source.data(), source.size(), "fxaa", NULL, NULL, entries[i].entry, entries[i].profile,
                                D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
        bool same = SUCCEEDED(hr) && code->GetBufferSize() == entries[i].size &&
                    memcmp(code->GetBufferPointer(), entries[i].blob, entries[i].size) == 0;
        char what[512], report[256];
        snprintf(what, sizeof(what), "%s %s", path, entries[i].entry);
        snprintf(report, sizeof(report), "%s\n", same ? "same as fxaaBytecode.h" : "DIFFERENT from fxaaBytecode.h: run tools/gfx/fxaa_bytecode.py");
        Report(what, same, report);
        if (code) code->Release();
        if (errors) errors->Release();
    }
}

} // namespace

int main(int argc, char **argv) {
    for (int a = 1; a < argc; a++) {
        if (strcmp(argv[a], "-v") == 0) { g_verbose = true; continue; }
        if (strcmp(argv[a], "-d") == 0) { ShaderCheck_ShowCode(true); continue; }
        std::string data = ReadFile(argv[a]);
        if (data.empty()) { fprintf(stderr, "cannot read %s\n", argv[a]); continue; }
        size_t len = strlen(argv[a]);
        if (len > 4 && (strcmp(argv[a] + len - 4, ".xbe") == 0 || strcmp(argv[a] + len - 4, ".XBE") == 0))
            CheckXbe(argv[a], data);
        else if (len > 4 && strcmp(argv[a] + len - 4, ".bin") == 0) {   // raw tokens, validated only
            std::string t;
            bool ok = Validate((const uint32_t *)data.data(), (data[2] & 0xFF) == 0xFF && (unsigned char)data[3] == 0xFF, &t);
            printf("%s: %s\n", argv[a], ok ? "accepted" : t.c_str());
        }
        else if (len > 5 && strcmp(argv[a] + len - 5, ".hlsl") == 0)
            CheckFxaa(argv[a], data);
        else if (data.find("==== pixel shader ") != std::string::npos)
            CheckPixelLog(argv[a], data);
        else
            CheckVertexLog(argv[a], data);
    }
    printf("[shadercheck] %d programs, %d differ\n", g_checked, g_differ);
    return g_differ;
}
