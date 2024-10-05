#version 450

layout(set=0,binding=0,std140) uniform World {
	vec3 SKY_DIRECTION;
	vec3 SKY_ENERGY; //energy supplied by sky to a surface patch with normal = SKY_DIRECTION
	vec3 SUN_DIRECTION;
	vec3 SUN_ENERGY; //energy supplied by sun to a surface patch with normal = SUN_DIRECTION
};

layout(set=2,binding=0) uniform sampler2D TEXTURE;

layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec4 tangent;
layout(location=3) in vec2 texCoord;

#ifdef USE_COLOR
layout(location=4) in vec4 Color;
layout(location=4) out vec4 outColor;
#endif


layout(location=0) out vec4 outColor;

void main() {
	vec3 n = normalize(normal);
	//vec3 l = vec3(0.0, 0.0, 1.0);

	//vec3 albedo = vec3(1.0, 1.0, 1.0);
	float alpha = 1.0;
	vec3 albedo = texture(TEXTURE, texCoord).rgb;
	//float alpha = texture(TEXTURE, texCoord).a;  // Get the alpha channel from the texture

	vec3 energy = (SKY_ENERGY * (0.5 * dot(n,SKY_DIRECTION) + 0.5)
	       + SUN_ENERGY * max(0.0, dot(n,SUN_DIRECTION)))/ 3.14159 ;

    //outColor = vec4(texture(albedo, texCoord) * energy, 1.0);
	outColor = vec4(energy * albedo, alpha);
}