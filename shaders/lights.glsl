struct SunLight
{
    float angle;
    float strength;
};

struct SphereLight
{
    float radius;
    float power;
    float limit;
    float padding;
};

struct SpotLight
{
    float radius;
    float power;
    float fov;
    float blend;
    float limit;
    float padding;
};

struct LightObject
{
    vec3 tint;
    float padding;
    int type;
    int shadow;
    float padding0[2];
   /* union{
        SunLight sun;
        SphereLight sphere;
        SpotLight spot;
    } data;
    */
    float data[6];
    float padding1[2];
};

struct Light
{
    LightObject light_obj;
    // mat4 transform;
    vec4 position;
    vec4 quaternion;
};

vec3 get_default_env_light(){
    vec3 sky_direction = vec3(0.0, 0.0, 1.0);
    vec3 sun_direction = normalize(vec3(6.0 / 23.0, 13.0 / 23.0, 18.0 / 23.0));
    float intensity = 0.8;

    // RGB environment color (can add variation across the cubemap)
    vec3 sky_energy = vec3(1.0, 1.0, 1.0) * intensity;

    // Calculate lighting color based on direction
    vec3 color = mix(sky_energy, sun_direction * intensity, 0.5);

    return color;
}

float get_attenuation(float distance, float radius) {
    return radius / max(distance * distance, radius);
}

vec3 rotateVectorByQuaternion(vec3 v, vec4 q) {
    vec3 q_xyz = q.xyz;
    float q_w = q.w;
    
    vec3 t = 2.0 * cross(q_xyz, v);
    vec3 rotatedVector = v + q_w * t + cross(q_xyz, t);
    return rotatedVector;
}

vec3 compute_diffuse(Light light, vec3 fragPos, vec3 normal) {
    vec3 lightDir;
    float attenuation = 1.0;
    vec3 diffuseColor = vec3(0.0);

    if (light.light_obj.type == 0) { // SunLight
        lightDir = normalize((light.position * vec4(0.0, 0.0, -1.0, 0.0)).xyz);
        float diff = max(dot(normal, -lightDir), 0.0);
        float cosAngle = cos(light.light_obj.data[0]);
        diff = smoothstep(cosAngle, 1.0, diff);

        diffuseColor = light.light_obj.tint * light.light_obj.data[1] * diff; // data[1] is sun strength
    }
    else if (light.light_obj.type == 1) { // SphereLight
        vec3 lightPos = light.position.xyz; // Fourth column for position in world space
        lightDir = normalize(lightPos - fragPos);
        float distance = length(lightPos - fragPos);
        float limit = light.light_obj.data[2];    // data[2] is sphere.limit
        if(limit > 0.00001 && distance >= limit)
            return diffuseColor;
        if(limit < 0.00001)
            limit = 1e10;
            
        float physical_fadeoff = 1.0;
        if(limit > 0.00001)
            physical_fadeoff = max(0.0, 1 - ldexp(distance/limit, 4));
        attenuation = get_attenuation(distance, light.light_obj.data[0]) * physical_fadeoff;    // data[0] is sphere.radius
        float diff = max(dot(normal, lightDir), 0.0);
        diffuseColor = light.light_obj.tint * light.light_obj.data[1] * attenuation * diff;     // data[1] is power
    }
    else if (light.light_obj.type == 2) { // SpotLight
        float radius = light.light_obj.data[0];
        float power = light.light_obj.data[1];
        float fov = light.light_obj.data[2];
        float blend = light.light_obj.data[3];
        float limit = light.light_obj.data[4];
        if(limit < 0.00001)
            limit = 1e10;

        vec3 lightPos = light.position.xyz;
        vec3 lightDir = rotateVectorByQuaternion(vec3(0.0, 0.0, -1.0), light.quaternion);

        vec3 fragToLight = lightPos - fragPos;
        float distance = length(fragToLight);
        vec3 lightDirection = normalize(fragToLight);

        float theta = dot(lightDirection, -lightDir);
        float innerAngle = cos((fov * (1.0 - blend)) / 2.0);
        float outerAngle = cos(fov / 2.0);

        float intensity = 0.0;
        if (theta > innerAngle) {
            // Fully illuminated area
            intensity = 1.0;
        } else if (theta > outerAngle) {
            // Linear falloff based on blend factor
            float edgeRatio = (theta - outerAngle) / (innerAngle - outerAngle);
            intensity = smoothstep(0.0, 1.0, edgeRatio);
        }

        float attenuation = 1.0 / (distance * distance);

        float limitFalloff = max(0.0, 1.0 - pow(distance / limit, 4.0));
        attenuation *= limitFalloff;

        float diff = max(dot(normal, lightDirection), 0.0);
        diffuseColor = light.light_obj.tint * power * diff * intensity * attenuation;  
    }

    return diffuseColor;
}