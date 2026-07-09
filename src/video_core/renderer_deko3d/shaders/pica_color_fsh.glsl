#version 460

layout (location = 0) in vec4 inColor;
layout (location = 0) out vec4 outColor;

layout (std140, binding = 0) uniform PicaFragmentState {
    int alpha_test_enabled;
    int alpha_test_func;
    float alpha_test_ref;
    float alpha_test_pad;
} pica;

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
    outColor = inColor;
    if (!AlphaTestPassed(outColor.a)) {
        discard;
    }
}
