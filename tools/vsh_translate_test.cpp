// Offline check of the D3D9 backend's NV2A -> HLSL vertex shader translator: translates every shader blob
// dumped by tools/vsh_dump.py and compiles the HLSL with D3DCompile, exactly as the backend does at runtime.
// Run it after any change to the translator (TranslateVshToHlsl in d3d9Backend.cpp); see
// tools/vsh_translate_test.ps1 for the build. Includes the backend source directly so the static translator
// functions are reachable; the two settings accessors the backend links against are stubbed below.
//
// Usage: vsh_translate_test.exe <dump dir from vsh_dump.py>
// Writes <dir>/vsNNN.hlsl for a few representative shaders (and for any that fail to compile).
#include "../src/action/engine/Direct3D/d3d9Backend.cpp"

int Settings_GetFPSOverride(void) { return 0; }
int Settings_GetGraphicsBackend(void) { return 0; }

int main(int argc, char **argv) {
    if (argc < 2) { printf("usage: vsh_translate_test <dump dir>\n"); return 2; }
    const char *dir = argv[1];
    static char hlsl[65536];
    int ok = 0, bad = 0;
    for (int i = 0; i < 130; i++) {
        char path[512];
        if (i < 128) snprintf(path, sizeof(path), "%s/vs%03d.bin", dir, i);
        else snprintf(path, sizeof(path), "%s/%s.bin", dir, i == 128 ? "immediate" : "overlay");
        FILE *f = fopen(path, "rb");
        if (!f) { printf("missing %s\n", path); bad++; continue; }
        static uint8_t blob[8192];
        fread(blob, 1, sizeof(blob), f);
        fclose(f);
        uint32_t normPacked = (i < 128) ? 2u : 0u; // v1 is NORMPACKED3 in the mesh declarations
        uint32_t inputs, outputs; int count;
        if (!TranslateVshToHlsl(blob, &normPacked, hlsl, sizeof(hlsl), &inputs, &outputs, &count)) {
            printf("vs%03d: translate FAILED\n", i); bad++; continue;
        }
        ID3DBlob *code = NULL, *err = NULL;
        HRESULT hr = D3DCompile(hlsl, strlen(hlsl), NULL, NULL, NULL, "main", "vs_2_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err);
        bool dump = FAILED(hr) || i == 0 || i == 3 || i == 127 || i == 129;
        if (FAILED(hr)) { printf("vs%03d: compile FAILED: %s\n", i, err ? (const char*)err->GetBufferPointer() : "?"); bad++; }
        else ok++;
        if (dump) { snprintf(path, sizeof(path), "%s/vs%03d.hlsl", dir, i); FILE *o = fopen(path, "w"); if (o) { fputs(hlsl, o); fclose(o); } }
        if (code) code->Release();
        if (err) err->Release();
    }
    printf("ok %d bad %d\n", ok, bad);
    return bad == 0 ? 0 : 1;
}
