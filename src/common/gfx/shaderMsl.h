#ifndef COMMON_GFX_SHADERMSL_H_
#define COMMON_GFX_SHADERMSL_H_

#include <stddef.h>
#include <stdint.h>
#include <string>

// ---------------------------------------------------------------------------------------------------------------
// SPIR-V to Metal Shading Language, for SDL's Metal driver, which takes MSL and not SPIR-V (shaderMsl.cpp, SPIRV-Cross).
//
// Every resource keeps the slot it has for SDL's SPIR-V drivers: a uniform block at binding n of the stage's uniform
// set is [[buffer(n)]], a sampler at binding n of its sampler set is [[texture(n)]] and [[sampler(n)]] - SDL's MSL
// order for a program with uniform buffers and sampled textures only (SDL_CreateGPUShader). Vertex inputs are
// [[attribute(location)]] through [[stage_in]]; SDL puts the vertex buffers from [[buffer(14)]] on.
//
// The source starts with "#pragma METAL fp math_mode(safe)" (macOS 15): SDL compiles MSL with Metal's default
// options, whose math mode is fast. SPIRV-Cross writes a NoContraction add, subtract or multiply as a call of a
// helper that does it as one fma, which nothing can fuse further; the multiply's adds -0, not +0, so that a product
// of -0 stays -0 as the SPIR-V drivers give it.
// ---------------------------------------------------------------------------------------------------------------

// The MSL of a module of shaderSpirv.h's interface, and the name its entry point has there (SPIRV-Cross renames
// "main"). False with a message on failure.
bool ShaderMsl_Translate(const uint32_t *words, size_t wordCount, std::string *source, std::string *entryPoint,
                         std::string *error);

#endif // COMMON_GFX_SHADERMSL_H_
