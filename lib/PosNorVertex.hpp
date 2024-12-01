#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>

struct PosNorVertex
{
    struct
    {
        float x, y, z;
    } Position;

    struct
    {
        float x, y, z;
    } Normal;

    // a pipeline vertex input state that works with a buffer holding a PosColVertex[] array:
    static const VkPipelineVertexInputStateCreateInfo array_input_state;
};

static_assert(sizeof(PosNorVertex) == 3 * 4 + 3 * 4, "PosNorVertex is packed.");