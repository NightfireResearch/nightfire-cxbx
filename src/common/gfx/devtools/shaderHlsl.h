#ifndef COMMON_GFX_DEVTOOLS_SHADERHLSL_H_
#define COMMON_GFX_DEVTOOLS_SHADERHLSL_H_

#include <stddef.h>
#include <stdint.h>

struct Nv2aPixelShaderInfo;

// The translators' HLSL, as it was compiled before they wrote bytecode (see shaderCheck.h).

// An XDK vertex shader function to HLSL. inputsUsed/outputsWritten come back as register bitmasks.
bool ShaderHlsl_Vertex(const uint8_t *function, const uint32_t *normPackedInputs, const uint8_t *inputComponents,
                       char *out, size_t outSize, uint32_t *inputsUsed, uint32_t *outputsWritten, int *instructionCountOut);

// A pixel shader definition to HLSL.
bool ShaderHlsl_Pixel(const uint32_t def[60], char *hlsl, size_t hlslSize, Nv2aPixelShaderInfo *info);

#endif // COMMON_GFX_DEVTOOLS_SHADERHLSL_H_
