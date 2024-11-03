#version 450

layout(push_constant) uniform PushConstants {
    mat4 CLIP_FROM_LIGHT;
};

struct Transform {
	mat4 CLIP_FROM_LOCAL;
	mat4 WORLD_FROM_LOCAL;
    mat4 WORLD_FROM_LOCAL_NORMAL;
	mat4 WORLD_FROM_LOCAL_TANGENT;
};

layout(set = 0, binding = 0, std140) uniform vp_ubo {
    mat4 ViewProjection;
};

layout(set = 1, binding = 0, std140) readonly buffer Transforms {
	Transform TRANSFORMS[];
};

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec4 Tangent;
layout(location = 3) in vec2 TexCoord;

void main()
{
   vec4 pos = vec4(in_position, 1.0);
   vec4 world_pos = TRANSFORMS[gl_InstanceIndex].WORLD_FROM_LOCAL * pos;
   gl_Position = CLIP_FROM_LIGHT * world_pos;
}