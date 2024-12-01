#include "../Tutorial.hpp"
#include "../helper/Helpers.hpp"
#include "../helper/VK.hpp"

static uint32_t triangle_code[] =
#include "../spv/shaders/pterrain_triangle.comp.inl"
    ;

// static uint32_t frag_code[] =
// #include "../spv/shaders/shadow.frag.inl"
//     ;

void Tutorial::PTerrainTrianglePipeline::create(RTG &rtg)
{
    VkShaderModule compute_shader_module = rtg.helpers.create_shader_module(triangle_code);

    {
        // VK(vkCreateDescriptorSetLayout(rtg.device, &create_info, nullptr, &descriptor_set_layout));
        descriptor_set_layout = shared_descriptor_set_layout; // use shared layout
    }

    {
        // create pipeline layout:
        std::array<VkDescriptorSetLayout, 1> layouts{
            descriptor_set_layout,
        };

        VkPushConstantRange range{
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .offset = 0,
            .size = sizeof(PTerrainPipeline::Push),
        };

        VkPipelineLayoutCreateInfo create_info{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = uint32_t(layouts.size()),
            .pSetLayouts = layouts.data(),
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &range,
        };

        VK(vkCreatePipelineLayout(rtg.device, &create_info, nullptr, &layout));
    }

    { // create pipeline:

        // shader code for vertex and fragment pipeline stages:
        std::array<VkPipelineShaderStageCreateInfo, 1> stages{
            VkPipelineShaderStageCreateInfo{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = compute_shader_module,
                .pName = "main"},
        };

        // all of the above structures get bundled together into one very large create_info:
        VkComputePipelineCreateInfo create_info{
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = stages[0],
            .layout = layout,
        };

        VK(vkCreateComputePipelines(rtg.device, VK_NULL_HANDLE, 1, &create_info, nullptr, &handle));
    }

    // modules no longer needed now that pipeline is created:
    vkDestroyShaderModule(rtg.device, compute_shader_module, nullptr);
}

void Tutorial::PTerrainTrianglePipeline::destroy(RTG &rtg)
{
    // if (descriptor_set_layout != VK_NULL_HANDLE)
    // {
    //     vkDestroyDescriptorSetLayout(rtg.device, descriptor_set_layout, nullptr);
    //     descriptor_set_layout = VK_NULL_HANDLE;
    // }

    if (layout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(rtg.device, layout, nullptr);
        layout = VK_NULL_HANDLE;
    }

    if (handle != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(rtg.device, handle, nullptr);
        handle = VK_NULL_HANDLE;
    }
}