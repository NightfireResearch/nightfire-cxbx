#ifndef COMMON_GFX_DEVTOOLS_SHADERCHECK_H_
#define COMMON_GFX_DEVTOOLS_SHADERCHECK_H_

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------------------------------------------
// The shader check: the translators' bytecode against the HLSL they used to produce, compiled by D3DCompile.
//
// Both programs run in a small vs_2_0/ps_2_0 interpreter on the same random inputs and constants (with the
// fog modes, depth flags and the like drawn from small integers often enough to take every select), and every
// output component they write is compared bit for bit (+0 and -0, and any two NaNs, count as equal). The
// interpreter rounds each instruction once - a mad is fused, an lrp is t * (b - a) + a with a fused mad, a dp is
// summed left to right - so two programs agree only if they do the same arithmetic in the same order: an
// instruction the wrong way round, a mul and add where the compiler fused a mad, a constant a bit off, all show.
// Texture reads return a hash of the coordinates' bits, so a coordinate one bit off shows too.
//
// NIGHTFIRE_SHADERCHECK=1 runs it on every shader the game translates (d3d9Backend.cpp), logging one line
// per shader and the disassembly of both where they differ; tools/gfx/shadercheck (shaderCheckMain.cpp) runs
// it over shader logs and the XBEs' programs without the game.
// ---------------------------------------------------------------------------------------------------------------

// Each returns true when ours computes what the HLSL reference computes (or both fail to build), and
// writes a report: one line, followed by both disassemblies when they differ.
bool ShaderCheck_Vertex(const uint8_t *function, uint32_t normPackedInputs, const uint8_t inputComponents[16],
                        char *report, size_t reportSize);
bool ShaderCheck_Pixel(const uint32_t def[60], char *report, size_t reportSize);

// Runs two programs of one kind on the same inputs. Returns the number of output components that differ in
// any trial (or -1 if either cannot be run) and describes the differences.
int ShaderCheck_Compare(const uint32_t *ours, const uint32_t *reference, char *report, size_t reportSize);

// Whether every report carries both disassemblies, not only those that differ.
void ShaderCheck_ShowCode(bool show);

// Instruction counts of a program: arithmetic and texture.
void ShaderCheck_Count(const uint32_t *code, int *arithmetic, int *texture);

#endif // COMMON_GFX_DEVTOOLS_SHADERCHECK_H_
