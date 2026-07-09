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
    switch (pica.alpha_test_func) {
        case 0:  return a == pica.alpha_test_ref; // Equal
        case 1:  return a != pica.alpha_test_ref; // NotEqual
        case 2:  return a <  pica.alpha_test_ref; // Less
        case 3:  return a <= pica.alpha_test_ref; // LessEqual
        case 4:  return a >  pica.alpha_test_ref; // Greater
        case 5:  return a >= pica.alpha_test_ref; // GreaterEqual
        case 6:  return true;                       // Always
        default: return false;                      // Never and unknown
    }
}

void main()
{
    outColor = texture(texture0, inTexCoord0) * inColor;
    if (!AlphaTestPassed(outColor.a)) {
        discard;
    }
}
