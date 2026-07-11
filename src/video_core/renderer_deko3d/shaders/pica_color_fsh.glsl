#version 460

layout (location = 0) in vec4 inColor;
layout (location = 0) out vec4 outColor;

layout (std140, binding = 0) uniform PicaFragmentState {
    int alpha_test_enabled;
    int alpha_test_func;
    float alpha_test_ref;
    int texture_enable_mask;
    float depth_scale;
    float depth_offset;
    int depthmap_w_buffer;
    int pad0;
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

float PicaDepth() {
    float z_over_w = -gl_FragCoord.z;
    float depth = z_over_w * pica.depth_scale + pica.depth_offset;
    if (pica.depthmap_w_buffer != 0) {
        depth /= gl_FragCoord.w;
    }
    return depth;
}

void main()
{
    outColor = inColor;
    if (!AlphaTestPassed(outColor.a)) {
        discard;
    }
    gl_FragDepth = PicaDepth();
}
