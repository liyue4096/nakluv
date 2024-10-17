#version 450

layout(set=0,binding=0,std140) uniform World {
	vec3 SKY_DIRECTION;
	vec3 SKY_ENERGY; //energy supplied by sky to a surface patch with normal = SKY_DIRECTION
	vec3 SUN_DIRECTION;
	vec3 SUN_ENERGY; //energy supplied by sun to a surface patch with normal = SUN_DIRECTION
	vec3 EYE; // Camera position in world space
};

layout(push_constant) uniform Push {
	int type;
} materialType;

layout(set=2, binding=0) uniform sampler2D TEXTURE;
layout(set=2, binding=1) uniform samplerCube TEXTURE_CUBEMAP;

layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec4 tangent;
layout(location=3) in vec2 texCoord;

#ifdef USE_COLOR
layout(location=4) in vec4 Color;
layout(location=4) out vec4 outColor;
#endif


layout(location=0) out vec4 outColor;


vec3 decodeRGBE(vec4 rgbe) {
    int exponent = int(rgbe.a * 255.0) - 128;  // Extract and scale the exponent
    vec3 color;
    color.r = ldexp(rgbe.r + 0.5 / 256.0, exponent);  // Add 0.5/256 to avoid quantization issues
    color.g = ldexp(rgbe.g + 0.5 / 256.0, exponent);
    color.b = ldexp(rgbe.b + 0.5 / 256.0, exponent);
    return color;
}

vec3 toneMapReinhard(vec3 color) {
    return color / (color + vec3(1.0));
}

const int PBR = 0;
const int LAMBERTIAN = 1;
const int MIRROR = 2;
const int ENVIRONMENT = 3;

void main() {
	vec3 n = normalize(normal);
	float alpha = 1.0;
	vec3 albedo;
	vec3 viewDir = normalize(EYE - position);

	vec3 reflectDir = reflect(-viewDir, n);

	vec3 energy = (SKY_ENERGY * (0.5 * dot(n, SKY_DIRECTION) + 0.5)
		+ SUN_ENERGY * max(0.0, dot(n, SUN_DIRECTION)))/ 3.14159 ;
	
	
	if (materialType.type == ENVIRONMENT) {
        // cubemap
        vec4 cubemapColor = texture(TEXTURE_CUBEMAP, n);
		albedo = decodeRGBE(cubemapColor);
		outColor = vec4(energy * albedo, 1.0);
		return;
		// Apply tone mapping
    	//albedo = toneMapReinhard(albedo);
    } 
	else if(materialType.type == MIRROR){
		reflectDir.y = -reflectDir.y; // Flip the Y-axis for correct reflections
    	vec4 reflectionColor = texture(TEXTURE_CUBEMAP, reflectDir);
    	albedo = decodeRGBE(reflectionColor);
		outColor = vec4(energy * albedo, 1.0);
		return;
		//albedo = toneMapReinhard(albedo);
	}
	else {
        // 2d texture
        albedo = texture(TEXTURE, texCoord).rgb;
		alpha = texture(TEXTURE, texCoord).a;  // Get the alpha channel from the texture
    }

    //outColor = vec4(texture(albedo, texCoord) * energy, 1.0);
	outColor = vec4(energy * albedo, alpha);
}