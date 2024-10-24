#version 450

layout(push_constant) uniform PushConstants {
    bool useColor;
} pushConstants;

struct Transform {
	mat4 CLIP_FROM_LOCAL;
	mat4 WORLD_FROM_LOCAL;
	mat4 WORLD_FROM_LOCAL_NORMAL;
    mat4 WORLD_FROM_LOCAL_TANGENT;
};

layout(set=1, binding=0, std140) readonly buffer Transforms {
	Transform TRANSFORMS[];
};

layout(set=2, binding=3) uniform sampler2D DISPLACEMENT_MAP;

layout(location=0) in vec3 Position;
layout(location=1) in vec3 Normal;
layout(location=2) in vec4 Tangent;
layout(location=3) in vec2 TexCoord;

#ifdef USE_COLOR
layout(location=4) in vec4 Color;
layout(location=4) out vec4 outColor;
#endif

layout(location=0) out vec3 position;
layout(location=1) out vec3 normal;
layout(location=2) out vec4 tangent;
layout(location=3) out vec2 texCoord;
layout(location=4) out mat3 TBN;  // Output the TBN matrix to fragment shader

// Constants
const float maxDisplacement = 0.035;

void main() {
    float displacement = texture(DISPLACEMENT_MAP, TexCoord).r;
    vec3 displacedPosition = Position + Normal * (displacement - 1.0) * maxDisplacement;

    // Position transformation
    vec4 localPosition = vec4(Position, 1.0);
    localPosition.xyz += Normal * (displacement * maxDisplacement);


    // Position transformation
    gl_Position = TRANSFORMS[gl_InstanceIndex].CLIP_FROM_LOCAL * localPosition;
    position = mat4x3(TRANSFORMS[gl_InstanceIndex].WORLD_FROM_LOCAL) * localPosition;
    
    // Normal transformation
    normal = mat3(TRANSFORMS[gl_InstanceIndex].WORLD_FROM_LOCAL_NORMAL) * Normal;
    
    // Tangent transformation and bitangent calculation
    tangent = vec4(mat3(TRANSFORMS[gl_InstanceIndex].WORLD_FROM_LOCAL_TANGENT) * Tangent.xyz, Tangent.w);
    vec3 worldTangent = mat3(TRANSFORMS[gl_InstanceIndex].WORLD_FROM_LOCAL_TANGENT) * Tangent.xyz;
    vec3 worldBitangent = cross(normal, worldTangent) * Tangent.w;

    // Construct TBN matrix and pass it to fragment shader
    TBN = mat3(worldTangent, worldBitangent, normal);
    
    // Pass the texture coordinates
    texCoord = TexCoord;


    #ifdef USE_COLOR
    if (pushConstants.useColor) {
        outColor = Color;
    } else {
        outColor = vec4(1.0); // Default to white if color is not used
    }
    #endif
}