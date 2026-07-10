#version 460

layout (location = 0) in vec4 inColor;
layout (location = 1) in vec2 inTexCoord0;
layout (location = 2) in vec2 inTexCoord1;
layout (location = 3) in vec2 inTexCoord2;

layout (location = 0) out vec4 outColor;

layout (std140, binding = 0) uniform PicaFragmentState {
    int alpha_test_enabled;
    int alpha_test_func;
    float alpha_test_ref;
    int texture_enable_mask;
    int pad0;
    int pad1;
    int pad2;
    int pad3;
} pica;

layout (binding = 0) uniform sampler2D texture0;
layout (binding = 1) uniform sampler2D texture1;
layout (binding = 2) uniform sampler2D texture2;

bool AlphaTestPassed(float a) {
    if (pica.alpha_test_enabled == 0) {
        return true;
    }
    int func = clamp(pica.alpha_test_func, 0, 7);
    if (func == 0) return a == pica.alpha_test_ref;
    if (func == 1) return a != pica.alpha_test_ref;
    if (func == 2) return a <  pica.alpha_test_ref;
    if (func == 3) return a <= pica.alpha_test_ref;
    if (func == 4) return a >  pica.alpha_test_ref;
    if (func == 5) return a >= pica.alpha_test_ref;
    if (func == 6) return true;
    return false;
}

void main()
{
    vec4 color = inColor;
    if ((pica.texture_enable_mask & 1) != 0) {
        color *= texture(texture0, inTexCoord0);
    }
    if ((pica.texture_enable_mask & 2) != 0) {
        color *= texture(texture1, inTexCoord1);
    }
    if ((pica.texture_enable_mask & 4) != 0) {
        color *= texture(texture2, inTexCoord2);
    }
    outColor = color;
    if (!AlphaTestPassed(outColor.a)) {
        discard;
    }
}
