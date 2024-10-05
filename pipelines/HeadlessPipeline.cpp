#include "../Tutorial.hpp"
#include "../helper/Helpers.hpp"
#include "../helper/VK.hpp"

static uint32_t shader_code[] =
#include "../spv/shaders/headless.comp.inl"
    ;

#define BUFFER_ELEMENTS 32

void Tutorial::HeadlessPipeline::create(RTG &rtg)
{
    shaderModule = rtg.helpers.create_shader_module(shader_code);

    { // the descriptorSetLayout layout holds world info in a storage buffer used in the shader:
        std::array<VkDescriptorSetLayoutBinding, 1> bindings{
            VkDescriptorSetLayoutBinding{
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT},
        };

        VkDescriptorSetLayoutCreateInfo create_info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = uint32_t(bindings.size()),
            .pBindings = bindings.data(),
        };

        VK(vkCreateDescriptorSetLayout(rtg.device, &create_info, nullptr, &descriptorSetLayout));
    }

    {
        // create pipeline layout:
        std::array<VkDescriptorSetLayout, 1> layouts{
            descriptorSetLayout,
        };

        VkPipelineLayoutCreateInfo create_info{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = uint32_t(layouts.size()),
            .pSetLayouts = layouts.data(),
            .pushConstantRangeCount = 0,
            .pPushConstantRanges = nullptr,
        };

        VK(vkCreatePipelineLayout(rtg.device, &create_info, nullptr, &pipelineLayout));
    }

    VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
    pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    VK(vkCreatePipelineCache(rtg.device, &pipelineCacheCreateInfo, nullptr, &pipelineCache));

    { // create pipeline:

        VkComputePipelineCreateInfo computePipelineCreateInfo{};
        computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        computePipelineCreateInfo.layout = pipelineLayout;
        computePipelineCreateInfo.flags = 0;

        // Pass SSBO size via specialization constant
        struct SpecializationData
        {
            uint32_t BUFFER_ELEMENT_COUNT = BUFFER_ELEMENTS;
        } specializationData;

        VkSpecializationMapEntry specializationMapEntry{};
        specializationMapEntry.constantID = 0;
        specializationMapEntry.offset = 0;
        specializationMapEntry.size = sizeof(uint32_t);

        VkSpecializationInfo specializationInfo;
        specializationInfo.mapEntryCount = 1;
        specializationInfo.pMapEntries = &specializationMapEntry;
        specializationInfo.dataSize = sizeof(SpecializationData);
        specializationInfo.pData = &specializationData;

        VkPipelineShaderStageCreateInfo shaderStage = {};
        shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStage.pName = "main";
        shaderStage.pSpecializationInfo = &specializationInfo;
        shaderStage.module = shaderModule;

        assert(shaderStage.module != VK_NULL_HANDLE);
        computePipelineCreateInfo.stage = shaderStage;
        VK(vkCreateComputePipelines(rtg.device, pipelineCache, 1, &computePipelineCreateInfo, nullptr, &pipeline));
    }
}

void Tutorial::HeadlessPipeline::destroy(RTG &rtg)
{
    vkDestroyShaderModule(rtg.device, shaderModule, nullptr);

    if (descriptorSetLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(rtg.device, descriptorSetLayout, nullptr);
        descriptorSetLayout = VK_NULL_HANDLE;
    }

    if (pipelineCache != VK_NULL_HANDLE)
    {
        vkDestroyPipelineCache(rtg.device, pipelineCache, nullptr);
        pipelineCache = VK_NULL_HANDLE;
    }

    if (pipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(rtg.device, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }

    if (pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(rtg.device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
}