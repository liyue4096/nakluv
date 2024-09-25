#include "SceneVertex.hpp"

#include <cstddef>

std::pair<std::vector<VkVertexInputBindingDescription>, std::vector<VkVertexInputAttributeDescription>> SceneVertex::get_binding_and_attribute_descriptions(bool useColor)
{
    std::vector<VkVertexInputBindingDescription> bindings = {
        VkVertexInputBindingDescription{
            .binding = 0,
            .stride = sizeof(SceneVertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX}};

    std::vector<VkVertexInputAttributeDescription> attributes = {
        VkVertexInputAttributeDescription{
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(SceneVertex, Position)},
        VkVertexInputAttributeDescription{
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(SceneVertex, Normal),
        },
        VkVertexInputAttributeDescription{
            .location = 2,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset = offsetof(SceneVertex, Tangent),
        },
        VkVertexInputAttributeDescription{
            .location = 3,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(SceneVertex, TexCoord),
        },
    };

    if (useColor)
    {
        attributes.push_back(VkVertexInputAttributeDescription{
            .location = 4,
            .binding = 0,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .offset = offsetof(SceneVertex, color)});
    }

    return {bindings, attributes};
}

VkPipelineVertexInputStateCreateInfo SceneVertex::get_vertex_input_state(bool useColor)
{
    auto [bindings, attributes] = get_binding_and_attribute_descriptions(useColor);

    VkPipelineVertexInputStateCreateInfo vertex_input_info{};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_info.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
    vertex_input_info.pVertexBindingDescriptions = bindings.data();
    vertex_input_info.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertex_input_info.pVertexAttributeDescriptions = attributes.data();

    return vertex_input_info;
}