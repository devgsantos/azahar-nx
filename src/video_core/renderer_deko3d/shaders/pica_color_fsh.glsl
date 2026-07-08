#version 460

layout (location = 0) in vec4 inColor;
layout (location = 1) in vec2 inTexCoord0;
layout (location = 2) in vec2 inTexCoord1;
layout (location = 3) in vec2 inTexCoord2;
layout (location = 0) out vec4 outColor;

layout (binding = 0) uniform sampler2D texture0;
layout (binding = 1) uniform sampler2D texture1;
layout (binding = 2) uniform sampler2D texture2;

layout (std140, binding = 0) uniform FragmentUniforms {
    uvec4 stages[6];
    vec4 constants[6];
    vec4 initialCombinerBuffer;
    uvec4 control;
    vec4 alphaData;
} uniforms;

vec4 sampleTexture(uint unit)
{
    if ((uniforms.control.z & (1u << unit)) == 0u) {
        return vec4(1.0);
    }
    if (unit == 0u) {
        return texture(texture0, inTexCoord0);
    }
    if (unit == 1u) {
        return texture(texture1, inTexCoord1);
    }
    return texture(texture2, inTexCoord2);
}

vec4 sourceValue(uint source, vec4 previous, vec4 previousBuffer, vec4 constantColor)
{
    switch (source) {
    case 0u:
    case 1u:
    case 2u:
        return inColor;
    case 3u:
        return sampleTexture(0u);
    case 4u:
        return sampleTexture(1u);
    case 5u:
    case 6u:
        return sampleTexture(2u);
    case 13u:
        return previousBuffer;
    case 14u:
        return constantColor;
    case 15u:
        return previous;
    default:
        return vec4(0.0);
    }
}

vec3 colorModifier(vec4 value, uint modifier)
{
    switch (modifier) {
    case 0u: return value.rgb;
    case 1u: return vec3(1.0) - value.rgb;
    case 2u: return vec3(value.a);
    case 3u: return vec3(1.0 - value.a);
    case 4u: return vec3(value.r);
    case 5u: return vec3(1.0 - value.r);
    case 8u: return vec3(value.g);
    case 9u: return vec3(1.0 - value.g);
    case 12u: return vec3(value.b);
    case 13u: return vec3(1.0 - value.b);
    default: return value.rgb;
    }
}

float alphaModifier(vec4 value, uint modifier)
{
    switch (modifier) {
    case 0u: return value.a;
    case 1u: return 1.0 - value.a;
    case 2u: return value.r;
    case 3u: return 1.0 - value.r;
    case 4u: return value.g;
    case 5u: return 1.0 - value.g;
    case 6u: return value.b;
    case 7u: return 1.0 - value.b;
    default: return value.a;
    }
}

vec3 colorOperation(uint operation, vec3 a, vec3 b, vec3 c)
{
    switch (operation) {
    case 0u: return a;
    case 1u: return a * b;
    case 2u: return min(a + b, vec3(1.0));
    case 3u: return clamp(a + b - vec3(0.5), vec3(0.0), vec3(1.0));
    case 4u: return mix(b, a, c);
    case 5u: return max(a - b, vec3(0.0));
    case 6u:
    case 7u: {
        float value = clamp(dot(a * 2.0 - 1.0, b * 2.0 - 1.0), 0.0, 1.0);
        return vec3(value);
    }
    case 8u: return min(a * b + c, vec3(1.0));
    case 9u: return min((a + b) * c, vec3(1.0));
    default: return a;
    }
}

float alphaOperation(uint operation, float a, float b, float c)
{
    switch (operation) {
    case 0u: return a;
    case 1u: return a * b;
    case 2u: return min(a + b, 1.0);
    case 3u: return clamp(a + b - 0.5, 0.0, 1.0);
    case 4u: return mix(b, a, c);
    case 5u: return max(a - b, 0.0);
    case 8u: return min(a * b + c, 1.0);
    case 9u: return min((a + b) * c, 1.0);
    default: return a;
    }
}

bool alphaTestPass(float alpha)
{
    uint function = uniforms.control.w;
    float reference = uniforms.alphaData.x;
    switch (function) {
    // Function 0 is also the power-on/default value while alpha testing is disabled. Treat it as
    // pass in the native uber-shader so an unset alpha-test register cannot erase the whole frame.
    case 0u: return true;
    case 1u: return true;
    case 2u: return abs(alpha - reference) <= (1.0 / 255.0);
    case 3u: return abs(alpha - reference) > (1.0 / 255.0);
    case 4u: return alpha < reference;
    case 5u: return alpha <= reference;
    case 6u: return alpha > reference;
    case 7u: return alpha >= reference;
    default: return true;
    }
}

void main()
{
    vec4 previous = inColor;
    vec4 previousBuffer = uniforms.initialCombinerBuffer;

    for (uint stage = 0u; stage < 6u; ++stage) {
        uvec4 packed = uniforms.stages[stage];
        uint sources = packed.x;
        uint modifiers = packed.y;
        uint operations = packed.z;
        uint scales = packed.w;

        vec4 source0 = sourceValue((sources >> 0u) & 0xFu, previous, previousBuffer,
                                   uniforms.constants[stage]);
        vec4 source1 = sourceValue((sources >> 4u) & 0xFu, previous, previousBuffer,
                                   uniforms.constants[stage]);
        vec4 source2 = sourceValue((sources >> 8u) & 0xFu, previous, previousBuffer,
                                   uniforms.constants[stage]);
        vec4 alphaSource0 = sourceValue((sources >> 16u) & 0xFu, previous, previousBuffer,
                                        uniforms.constants[stage]);
        vec4 alphaSource1 = sourceValue((sources >> 20u) & 0xFu, previous, previousBuffer,
                                        uniforms.constants[stage]);
        vec4 alphaSource2 = sourceValue((sources >> 24u) & 0xFu, previous, previousBuffer,
                                        uniforms.constants[stage]);

        vec3 color0 = colorModifier(source0, (modifiers >> 0u) & 0xFu);
        vec3 color1 = colorModifier(source1, (modifiers >> 4u) & 0xFu);
        vec3 color2 = colorModifier(source2, (modifiers >> 8u) & 0xFu);
        float alpha0 = alphaModifier(alphaSource0, (modifiers >> 12u) & 0x7u);
        float alpha1 = alphaModifier(alphaSource1, (modifiers >> 16u) & 0x7u);
        float alpha2 = alphaModifier(alphaSource2, (modifiers >> 20u) & 0x7u);

        uint colorOperationCode = (operations >> 0u) & 0xFu;
        uint alphaOperationCode = (operations >> 16u) & 0xFu;
        float colorScale = float(1u << min((scales >> 0u) & 0x3u, 2u));
        float alphaScale = float(1u << min((scales >> 16u) & 0x3u, 2u));
        vec4 stageResult;
        stageResult.rgb = clamp(colorOperation(colorOperationCode, color0, color1, color2) *
                                    colorScale,
                                vec3(0.0), vec3(1.0));
        stageResult.a = clamp(alphaOperation(alphaOperationCode, alpha0, alpha1, alpha2) *
                                  alphaScale,
                              0.0, 1.0);

        if ((uniforms.control.x & (1u << stage)) != 0u) {
            previousBuffer.rgb = stageResult.rgb;
        }
        if ((uniforms.control.y & (1u << stage)) != 0u) {
            previousBuffer.a = stageResult.a;
        }
        previous = stageResult;
    }

    if (!alphaTestPass(previous.a)) {
        discard;
    }
    outColor = previous;
}
