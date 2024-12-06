#version 450

layout(set=0,binding=0,std140) uniform World {
	vec3 SKY_DIRECTION;
	vec3 SKY_ENERGY; //energy supplied by sky to a surface patch with normal = SKY_DIRECTION
	vec3 SUN_DIRECTION;
	vec3 SUN_ENERGY; //energy supplied by sun to a surface patch with normal = SUN_DIRECTION
};

layout(set=2, binding=0) uniform sampler2D TEXTURE;
layout(set=2, binding=1) uniform samplerCube TEXTURE_CUBEMAP;
layout(set=2, binding=2) uniform sampler2D NORMAL_MAP;
layout(set=2, binding=3) uniform sampler2D DISPLACEMENT_MAP;

layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 texCoord;

layout(location=0) out vec4 outColor;

void main() {
	vec3 n = normalize(normal);
	
	if (any(isnan(n))) {
    	n = vec3(0.6, 0.6, 0.6); // Default normal
	}	 
	//vec3 l = vec3(0.0, 0.0, 1.0);

	vec3 albedo = texture(TEXTURE, texCoord).rgb;
	float alpha = texture(TEXTURE, texCoord).a;  // Get the alpha channel from the texture

	//hemisphere lighting from direction l:
	//vec3 e = vec3(0.5 * dot(n,l) + 0.5);
	//hemisphere sky + directional sun:
	vec3 e = SKY_ENERGY * (0.5 * dot(n,SKY_DIRECTION) + 0.05)
	       + SUN_ENERGY * max(0.0, dot(n,SUN_DIRECTION)) ;

	outColor = vec4(e * albedo, alpha);
}