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
	int src_albedo;
	int src_roughness;
	int src_metalness;
	vec3 albedo;
} materialType;

layout(set=2, binding=0) uniform sampler2D TEXTURE;
layout(set=2, binding=1) uniform samplerCube TEXTURE_CUBEMAP;
layout(set=2, binding=2) uniform sampler2D NORMAL_MAP;

layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec4 tangent;
layout(location=3) in vec2 texCoord;
layout(location=4) in mat3 TBN;

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
	
    if (materialType.type == LAMBERTIAN || materialType.type == PBR) {
        // Sample and decode the normal map
        vec3 normal_tangent = texture(NORMAL_MAP, texCoord).rgb;
        normal_tangent = normal_tangent * 2.0 - 1.0;  // Convert from [0, 1] to [-1, 1]

        // Transform the normal from tangent space to world space using the TBN matrix
        n = normalize(TBN * normal_tangent);
    } else {
        // Use interpolated normal if no normal map is used
        n = normalize(TBN[2]);  // TBN[2] is the interpolated normal
    }

	vec3 envColor = decodeRGBE(texture(TEXTURE_CUBEMAP, n));

	if (materialType.type == ENVIRONMENT) {
        // cubemap
        vec4 cubemapColor = texture(TEXTURE_CUBEMAP, n);
		albedo = decodeRGBE(cubemapColor);
		// Apply tone mapping
    	// albedo = toneMapReinhard(albedo);
		outColor = vec4(energy * albedo, 1.0);
		return;
    } 
	else if(materialType.type == MIRROR){
		reflectDir.z = -reflectDir.z; // Flip the Z-axis for correct reflections
    	vec4 reflectionColor = texture(TEXTURE_CUBEMAP, reflectDir);
    	albedo = decodeRGBE(reflectionColor);
		// albedo = toneMapReinhard(albedo);
		outColor = vec4(energy * albedo, 1.0);
		return;
	}
	else if(materialType.type == LAMBERTIAN){
		if(materialType.src_albedo == 0){
			albedo = vec3(materialType.albedo.r, materialType.albedo.g, materialType.albedo.b);
			vec3 diffuseLight = envColor * albedo;
			outColor = vec4(diffuseLight, alpha);
			return;
		}
		else{
			albedo = texture(TEXTURE, texCoord).rgb;
			alpha = texture(TEXTURE, texCoord).a;
			vec3 diffuseLight = envColor * albedo;
			outColor = vec4(diffuseLight, alpha);
			//outColor = vec4(energy * albedo, alpha);
			return;
		}
	}
	else {
        if(materialType.src_albedo == 0){
			albedo = vec3(materialType.albedo.r, materialType.albedo.g, materialType.albedo.b);
			vec3 diffuseLight = envColor * albedo;
			outColor = vec4(diffuseLight, alpha);
			return;
		}
		else{
			albedo = texture(TEXTURE, texCoord).rgb;
			alpha = texture(TEXTURE, texCoord).a;
			vec3 diffuseLight = envColor * albedo;
			outColor = vec4(diffuseLight, alpha);
			return;
		}
    }

	outColor = vec4(energy * albedo, alpha);
}