#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <optional>
#include <vector>

struct Color
{
    uint8_t r, g, b, a;
};

struct SceneVertex
{
    struct
    {
        float x, y, z;
    } Position;

    struct
    {
        float x, y, z;
    } Normal;

    struct
    {
        float x, y, z, w;
    } Tangent;

    struct
    {
        float s, t;
    } TexCoord;

    std::optional<struct Color> color;

    static VkPipelineVertexInputStateCreateInfo get_vertex_input_state(bool useColor);

    static std::pair<std::vector<VkVertexInputBindingDescription>,
                     std::vector<VkVertexInputAttributeDescription>>
    get_binding_and_attribute_descriptions(bool useColor);
};
