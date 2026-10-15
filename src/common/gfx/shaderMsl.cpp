#include "shaderMsl.h"

#include <spirv_msl.hpp>

// SDL's descriptor sets for SPIR-V (SDL_CreateGPUShader): a vertex program's samplers in set 0 and uniform buffers in
// set 1, a pixel program's in sets 2 and 3.
static uint32_t SamplerSet(spv::ExecutionModel stage) { return stage == spv::ExecutionModelVertex ? 0 : 2; }
static uint32_t UniformSet(spv::ExecutionModel stage) { return stage == spv::ExecutionModelVertex ? 1 : 3; }

bool ShaderMsl_Translate(const uint32_t *words, size_t wordCount, std::string *source, std::string *entryPoint,
                         std::string *error) {
    try {
        spirv_cross::CompilerMSL compiler(words, wordCount);
        spirv_cross::CompilerMSL::Options options;
        options.platform = spirv_cross::CompilerMSL::Options::macOS;
        options.set_msl_version(2, 1);
        compiler.set_msl_options(options);

        spv::ExecutionModel stage = compiler.get_execution_model();
        spirv_cross::ShaderResources resources = compiler.get_shader_resources();
        for (const spirv_cross::Resource &buffer : resources.uniform_buffers) {
            uint32_t set = compiler.get_decoration(buffer.id, spv::DecorationDescriptorSet);
            uint32_t binding = compiler.get_decoration(buffer.id, spv::DecorationBinding);
            if (set != UniformSet(stage)) {
                *error = "uniform buffer " + buffer.name + " in set " + std::to_string(set);
                return false;
            }
            spirv_cross::MSLResourceBinding slot;
            slot.stage = stage;
            slot.desc_set = set;
            slot.binding = binding;
            slot.msl_buffer = binding;
            compiler.add_msl_resource_binding(slot);
        }
        for (const spirv_cross::Resource &sampler : resources.sampled_images) {
            uint32_t set = compiler.get_decoration(sampler.id, spv::DecorationDescriptorSet);
            uint32_t binding = compiler.get_decoration(sampler.id, spv::DecorationBinding);
            if (set != SamplerSet(stage)) {
                *error = "sampler " + sampler.name + " in set " + std::to_string(set);
                return false;
            }
            spirv_cross::MSLResourceBinding slot;
            slot.stage = stage;
            slot.desc_set = set;
            slot.binding = binding;
            slot.msl_texture = binding;
            slot.msl_sampler = binding;
            compiler.add_msl_resource_binding(slot);
        }
        if (!resources.separate_images.empty() || !resources.separate_samplers.empty() ||
            !resources.storage_buffers.empty() || !resources.storage_images.empty()) {
            *error = "resources other than uniform buffers and combined samplers";
            return false;
        }

        std::string msl = compiler.compile();
        // The multiply's helper adds +0, which turns a product of -0 into +0
        static const char multiply[] = "return fma(l, r, T(0));";
        size_t at = msl.find(multiply);
        if (at != std::string::npos)
            msl.replace(at, sizeof(multiply) - 1, "return fma(l, r, -T(0));");
        else if (msl.find("spvFMul(") != std::string::npos) {
            *error = "spvFMul's body is not the one this expects";
            return false;
        }
        *source = "#pragma METAL fp math_mode(safe)\n" + msl;
        *entryPoint = compiler.get_cleansed_entry_point_name(compiler.get_entry_points_and_stages()[0].name, stage);
        return true;
    } catch (const spirv_cross::CompilerError &e) {
        *error = e.what();
        return false;
    }
}
