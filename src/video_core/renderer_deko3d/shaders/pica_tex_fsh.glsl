#version 460

layout (location = 0) in vec4 inColor;
layout (location = 1) in vec2 inTexCoord0;
layout (location = 2) in vec2 inTexCoord1;
layout (location = 3) in vec2 inTexCoord2;

layout (location = 0) out vec4 outColor;

struct TevStage {
    ivec4 source0;
    ivec4 source1_mod0;
    ivec4 mod1;
    ivec4 ops;
    vec4 const_color;
};

layout (std140, binding = 0) uniform PicaFragmentState {
    int alpha_test_enabled;
    int alpha_test_func;
    float alpha_test_ref;
    int texture_enable_mask;
    float depth_scale;
    float depth_offset;
    int depthmap_w_buffer;
    int combiner_update_rgb_mask;
    int combiner_update_alpha_mask;
    int pad2;
    int pad3;
    int pad4;
    vec4 combiner_buffer_color;
    TevStage tev_stages[6];
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

vec4 ByteRound(vec4 value)
{
    return round(clamp(value, vec4(0.0), vec4(1.0)) * 255.0) / 255.0;
}

float ByteRound1(float value)
{
    return round(clamp(value, 0.0, 1.0) * 255.0) / 255.0;
}

vec4 TextureSource(int source)
{
    if (source == 3 && (pica.texture_enable_mask & 1) != 0) {
        return texture(texture0, inTexCoord0);
    }
    if (source == 4 && (pica.texture_enable_mask & 2) != 0) {
        return texture(texture1, inTexCoord1);
    }
    if (source == 5 && (pica.texture_enable_mask & 4) != 0) {
        return texture(texture2, inTexCoord2);
    }
    return vec4(1.0);
}

vec4 SourceValue(int source, int stage_index, vec4 primary_color, vec4 primary_fragment_color,
                 vec4 secondary_fragment_color, vec4 combiner_buffer, vec4 combiner_output)
{
    if (source == 0) {
        return primary_color;
    }
    if (source == 1) {
        return primary_fragment_color;
    }
    if (source == 2) {
        return secondary_fragment_color;
    }
    if (source >= 3 && source <= 5) {
        return TextureSource(source);
    }
    if (source == 13) {
        return combiner_buffer;
    }
    if (source == 14) {
        return pica.tev_stages[stage_index].const_color;
    }
    if (source == 15) {
        return combiner_output;
    }
    return vec4(0.0);
}

vec3 ApplyColorModifier(vec4 value, int modifier)
{
    if (modifier == 0) return value.rgb;
    if (modifier == 1) return vec3(1.0) - value.rgb;
    if (modifier == 2) return value.aaa;
    if (modifier == 3) return vec3(1.0) - value.aaa;
    if (modifier == 4) return value.rrr;
    if (modifier == 5) return vec3(1.0) - value.rrr;
    if (modifier == 8) return value.ggg;
    if (modifier == 9) return vec3(1.0) - value.ggg;
    if (modifier == 12) return value.bbb;
    if (modifier == 13) return vec3(1.0) - value.bbb;
    return vec3(0.0);
}

float ApplyAlphaModifier(vec4 value, int modifier)
{
    if (modifier == 0) return value.a;
    if (modifier == 1) return 1.0 - value.a;
    if (modifier == 2) return value.r;
    if (modifier == 3) return 1.0 - value.r;
    if (modifier == 4) return value.g;
    if (modifier == 5) return 1.0 - value.g;
    if (modifier == 6) return value.b;
    if (modifier == 7) return 1.0 - value.b;
    return 0.0;
}

vec3 CombineColor(int op, vec3 c1, vec3 c2, vec3 c3)
{
    if (op == 0) return c1;
    if (op == 1) return c1 * c2;
    if (op == 2) return c1 + c2;
    if (op == 3) return c1 + c2 - vec3(0.5);
    if (op == 4) return mix(c2, c1, c3);
    if (op == 5) return c1 - c2;
    if (op == 6 || op == 7) return vec3(dot(c1 - vec3(0.5), c2 - vec3(0.5)) * 4.0);
    if (op == 8) return fma(c1, c2, c3);
    if (op == 9) return min(c1 + c2, vec3(1.0)) * c3;
    return vec3(0.0);
}

float CombineAlpha(int op, float a1, float a2, float a3)
{
    if (op == 0) return a1;
    if (op == 1) return a1 * a2;
    if (op == 2) return a1 + a2;
    if (op == 3) return a1 + a2 - 0.5;
    if (op == 4) return mix(a2, a1, a3);
    if (op == 5) return a1 - a2;
    if (op == 8) return fma(a1, a2, a3);
    if (op == 9) return min(a1 + a2, 1.0) * a3;
    return 0.0;
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
    vec4 primary_color = ByteRound(inColor);
    vec4 primary_fragment_color = vec4(0.0);
    vec4 secondary_fragment_color = vec4(0.0);
    vec4 combiner_buffer = vec4(0.0);
    vec4 next_combiner_buffer = pica.combiner_buffer_color;
    vec4 combiner_output = vec4(0.0);

    for (int i = 0; i < 6; ++i) {
        TevStage stage = pica.tev_stages[i];
        int color_source1 = stage.source0.x;
        int color_source2 = stage.source0.y;
        int color_source3 = stage.source0.z;
        int alpha_source1 = stage.source0.w;
        int alpha_source2 = stage.source1_mod0.x;
        int alpha_source3 = stage.source1_mod0.y;

        if (i == 0 && color_source1 == 15) color_source1 = color_source3;
        if (i == 0 && color_source2 == 15) color_source2 = color_source3;
        if (i == 0 && color_source3 == 15) color_source3 = pica.tev_stages[i].source0.z;
        if (i == 0 && alpha_source1 == 15) alpha_source1 = alpha_source3;
        if (i == 0 && alpha_source2 == 15) alpha_source2 = alpha_source3;
        if (i == 0 && alpha_source3 == 15) alpha_source3 = pica.tev_stages[i].source1_mod0.y;

        vec3 color_results_1 =
            ApplyColorModifier(SourceValue(color_source1, i, primary_color,
                                           primary_fragment_color, secondary_fragment_color,
                                           combiner_buffer, combiner_output),
                               stage.source1_mod0.z);
        vec3 color_results_2 =
            ApplyColorModifier(SourceValue(color_source2, i, primary_color,
                                           primary_fragment_color, secondary_fragment_color,
                                           combiner_buffer, combiner_output),
                               stage.source1_mod0.w);
        vec3 color_results_3 =
            ApplyColorModifier(SourceValue(color_source3, i, primary_color,
                                           primary_fragment_color, secondary_fragment_color,
                                           combiner_buffer, combiner_output),
                               stage.mod1.x);
        vec3 color_output = ByteRound(vec4(CombineColor(stage.ops.x, color_results_1,
                                                        color_results_2, color_results_3),
                                           1.0)).rgb;

        float alpha_output;
        if (stage.ops.x == 7) {
            alpha_output = color_output.r;
        } else {
            float alpha_results_1 =
                ApplyAlphaModifier(SourceValue(alpha_source1, i, primary_color,
                                               primary_fragment_color, secondary_fragment_color,
                                               combiner_buffer, combiner_output),
                                   stage.mod1.y);
            float alpha_results_2 =
                ApplyAlphaModifier(SourceValue(alpha_source2, i, primary_color,
                                               primary_fragment_color, secondary_fragment_color,
                                               combiner_buffer, combiner_output),
                                   stage.mod1.z);
            float alpha_results_3 =
                ApplyAlphaModifier(SourceValue(alpha_source3, i, primary_color,
                                               primary_fragment_color, secondary_fragment_color,
                                               combiner_buffer, combiner_output),
                                   stage.mod1.w);
            alpha_output = ByteRound1(CombineAlpha(stage.ops.y, alpha_results_1,
                                                   alpha_results_2, alpha_results_3));
        }

        combiner_output = vec4(clamp(color_output * float(stage.ops.z), vec3(0.0), vec3(1.0)),
                               clamp(alpha_output * float(stage.ops.w), 0.0, 1.0));
        combiner_buffer = next_combiner_buffer;
        if (i < 4 && (pica.combiner_update_rgb_mask & (1 << i)) != 0) {
            next_combiner_buffer.rgb = combiner_output.rgb;
        }
        if (i < 4 && (pica.combiner_update_alpha_mask & (1 << i)) != 0) {
            next_combiner_buffer.a = combiner_output.a;
        }
    }

    outColor = combiner_output;
    if (!AlphaTestPassed(outColor.a)) {
        discard;
    }
    gl_FragDepth = PicaDepth();
}
