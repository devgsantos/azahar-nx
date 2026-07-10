#version 460

layout (location = 0) in vec4 inColor;
layout (location = 1) in vec2 inTexCoord0;

layout (location = 0) out vec4 outColor;

layout (std140, binding = 0) uniform PicaFragmentState {
    int alpha_test_enabled;
    int alpha_test_func;
    float alpha_test_ref;
    float alpha_test_pad;
} pica;

layout (binding = 0) uniform sampler2D texture0;

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
    outColor = texture(texture0, inTexCoord0) * inColor;
    if (!AlphaTestPassed(outColor.a)) {
        discard;
    }
}
