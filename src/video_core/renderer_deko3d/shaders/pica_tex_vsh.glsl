#version 460

layout (location = 0) in vec4 inPosition;
layout (location = 1) in vec4 inColor;
layout (location = 2) in vec2 inTexCoord0;
layout (location = 3) in vec2 inTexCoord1;
layout (location = 4) in vec2 inTexCoord2;
layout (location = 5) in float inTexCoord0W;
layout (location = 6) in vec4 inNormQuat;
layout (location = 7) in vec3 inView;

layout (location = 0) out vec4 outColor;
layout (location = 1) out vec2 outTexCoord0;
layout (location = 2) out vec2 outTexCoord1;
layout (location = 3) out vec2 outTexCoord2;
layout (location = 4) out float outTexCoord0W;
layout (location = 5) out vec4 outNormQuat;
layout (location = 6) out vec3 outView;

void main()
{
    gl_Position = inPosition;
    outColor = inColor;
    outTexCoord0 = inTexCoord0;
    outTexCoord1 = inTexCoord1;
    outTexCoord2 = inTexCoord2;
    outTexCoord0W = inTexCoord0W;
    outNormQuat = inNormQuat;
    outView = inView;
}
