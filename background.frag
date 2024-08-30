#version 450

layout(location = 0) out vec4 outColor;

layout(location = 0) in vec2 position;

layout(push_constant) uniform Push {
	float time;
} pushData;

void main(){
    //outColor = vec4( sin((gl_FragCoord.x * gl_FragCoord.x + 3 * gl_FragCoord.y * gl_FragCoord.y) / 150), cos(gl_FragCoord.y / 100), 0.1, 1.0 );

    float noise = sin(position.x * 10.0 + pushData.time * 5.0) *
                  cos(position.y * 10.0 + pushData.time * 5.0);

    float discharge = abs(sin(position.x * 50.0 + position.y * 50.0 + pushData.time * 10.0)) *
                      exp(-length(position) * 1.2);

    float color = discharge * noise;

    color = pow(color, 3.0);

    outColor = vec4(0.0, 0.5 * color, 1.0 * color, 1.0);

    //outColor = vec4(fract(position.x + pushData.time), position.y, 0.0, 1.0);
}