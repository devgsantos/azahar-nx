// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "video_core/renderer_deko3d/deko3d_rasterizer.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <functional>
#include <optional>
#include <unordered_set>

#include "common/assert.h"
#include "common/logging/log.h"
#include "video_core/pica/pica_core.h"
#include "video_core/renderer_deko3d/deko3d_shader.h"
#include "video_core/renderer_deko3d/deko3d_stats.h"
#include "video_core/renderer_deko3d/deko3d_texture_cache.h"

namespace VideoCore::Deko3D {
namespace {

#ifdef __SWITCH__
u32 AlignUp(u32 value, u32 alignment) {
    return alignment == 0 ? value : ((value + alignment - 1) / alignment) * alignment;
}

template <typename T>
void HashCombine(std::size_t& seed, T value) {
    seed ^= std::hash<T>{}(value) + 0x9e3779b9U + (seed << 6) + (seed >> 2);
}

DkBlendOp MapBlendEquation(Pica::FramebufferRegs::BlendEquation equation) {
    using BlendEquation = Pica::FramebufferRegs::BlendEquation;
    switch (equation) {
    case BlendEquation::Add:
        return DkBlendOp_Add;
    case BlendEquation::Subtract:
        return DkBlendOp_Sub;
    case BlendEquation::ReverseSubtract:
        return DkBlendOp_RevSub;
    case BlendEquation::Min:
        return DkBlendOp_Min;
    case BlendEquation::Max:
        return DkBlendOp_Max;
    }
    return DkBlendOp_Add;
}

std::optional<DkBlendFactor> MapBlendFactor(Pica::FramebufferRegs::BlendFactor factor) {
    using BlendFactor = Pica::FramebufferRegs::BlendFactor;
    switch (factor) {
    case BlendFactor::Zero:
        return DkBlendFactor_Zero;
    case BlendFactor::One:
        return DkBlendFactor_One;
    case BlendFactor::SourceColor:
        return DkBlendFactor_SrcColor;
    case BlendFactor::OneMinusSourceColor:
        return DkBlendFactor_InvSrcColor;
    case BlendFactor::DestColor:
        return DkBlendFactor_DstColor;
    case BlendFactor::OneMinusDestColor:
        return DkBlendFactor_InvDstColor;
    case BlendFactor::SourceAlpha:
        return DkBlendFactor_SrcAlpha;
    case BlendFactor::OneMinusSourceAlpha:
        return DkBlendFactor_InvSrcAlpha;
    case BlendFactor::DestAlpha:
        return DkBlendFactor_DstAlpha;
    case BlendFactor::OneMinusDestAlpha:
        return DkBlendFactor_InvDstAlpha;
    case BlendFactor::ConstantColor:
        return DkBlendFactor_ConstColor;
    case BlendFactor::OneMinusConstantColor:
        return DkBlendFactor_InvConstColor;
    case BlendFactor::ConstantAlpha:
        return DkBlendFactor_ConstAlpha;
    case BlendFactor::OneMinusConstantAlpha:
        return DkBlendFactor_InvConstAlpha;
    case BlendFactor::SourceAlphaSaturate:
        return DkBlendFactor_SrcAlphaSaturate;
    }
    return std::nullopt;
}

DkCompareOp MapCompare(Pica::FramebufferRegs::CompareFunc func) {
    using CompareFunc = Pica::FramebufferRegs::CompareFunc;
    switch (func) {
    case CompareFunc::Never:
        return DkCompareOp_Never;
    case CompareFunc::Always:
        return DkCompareOp_Always;
    case CompareFunc::Equal:
        return DkCompareOp_Equal;
    case CompareFunc::NotEqual:
        return DkCompareOp_NotEqual;
    case CompareFunc::LessThan:
        return DkCompareOp_Less;
    case CompareFunc::LessThanOrEqual:
        return DkCompareOp_Lequal;
    case CompareFunc::GreaterThan:
        return DkCompareOp_Greater;
    case CompareFunc::GreaterThanOrEqual:
        return DkCompareOp_Gequal;
    }
    return DkCompareOp_Always;
}

struct alignas(16) PicaFragmentState {
    s32 alpha_test_enabled;
    s32 alpha_test_func;
    float alpha_test_ref;
    float alpha_test_pad;
};

static_assert(sizeof(PicaFragmentState) == 16, "PicaFragmentState must match std140 layout");

s32 MapAlphaTestFunc(Pica::FramebufferRegs::CompareFunc func) {
    using CompareFunc = Pica::FramebufferRegs::CompareFunc;
    switch (func) {
    case CompareFunc::Equal:
        return 0;
    case CompareFunc::NotEqual:
        return 1;
    case CompareFunc::LessThan:
        return 2;
    case CompareFunc::LessThanOrEqual:
        return 3;
    case CompareFunc::GreaterThan:
        return 4;
    case CompareFunc::GreaterThanOrEqual:
        return 5;
    case CompareFunc::Always:
        return 6;
    case CompareFunc::Never:
        return 7;
    }
    return 7;
}

u32 ColorWriteMask(const Pica::FramebufferRegs& regs) {
    u32 mask = 0;
    if (regs.framebuffer.allow_color_write != 0 && regs.output_merger.red_enable != 0) {
        mask |= DkColorMask_R;
    }
    if (regs.framebuffer.allow_color_write != 0 && regs.output_merger.green_enable != 0) {
        mask |= DkColorMask_G;
    }
    if (regs.framebuffer.allow_color_write != 0 && regs.output_merger.blue_enable != 0) {
        mask |= DkColorMask_B;
    }
    if (regs.framebuffer.allow_color_write != 0 && regs.output_merger.alpha_enable != 0) {
        mask |= DkColorMask_A;
    }
    return mask;
}

std::size_t TransformedStateSignature(const Pica::PicaCore& pica,
                                      const Pica::RegsInternal& regs) {
    const auto& fb = regs.framebuffer;
    const auto viewport = regs.rasterizer.GetViewportRect();
    std::size_t seed = 0;
    HashCombine(seed, fb.framebuffer.GetColorBufferPhysicalAddress());
    HashCombine(seed, fb.framebuffer.GetDepthBufferPhysicalAddress());
    HashCombine(seed, fb.framebuffer.GetWidth());
    HashCombine(seed, fb.framebuffer.GetHeight());
    HashCombine(seed, static_cast<u32>(fb.framebuffer.color_format.Value()));
    HashCombine(seed, static_cast<u32>(fb.framebuffer.depth_format.Value()));
    HashCombine(seed, static_cast<u32>(viewport.left));
    HashCombine(seed, static_cast<u32>(viewport.top));
    HashCombine(seed, static_cast<u32>(viewport.GetWidth()));
    HashCombine(seed, static_cast<u32>(viewport.GetHeight()));
    HashCombine(seed, regs.rasterizer.scissor_test.x1.Value());
    HashCombine(seed, regs.rasterizer.scissor_test.y1.Value());
    HashCombine(seed, regs.rasterizer.scissor_test.x2.Value());
    HashCombine(seed, regs.rasterizer.scissor_test.y2.Value());
    HashCombine(seed, fb.output_merger.alphablend_enable.Value());
    HashCombine(seed, static_cast<u32>(fb.output_merger.alpha_blending.blend_equation_rgb.Value()));
    HashCombine(seed, static_cast<u32>(fb.output_merger.alpha_blending.blend_equation_a.Value()));
    HashCombine(seed, static_cast<u32>(fb.output_merger.alpha_blending.factor_source_rgb.Value()));
    HashCombine(seed, static_cast<u32>(fb.output_merger.alpha_blending.factor_dest_rgb.Value()));
    HashCombine(seed, static_cast<u32>(fb.output_merger.alpha_blending.factor_source_a.Value()));
    HashCombine(seed, static_cast<u32>(fb.output_merger.alpha_blending.factor_dest_a.Value()));
    HashCombine(seed, fb.output_merger.depth_test_enable.Value());
    HashCombine(seed, fb.output_merger.depth_write_enable.Value());
    HashCombine(seed, static_cast<u32>(fb.output_merger.depth_test_func.Value()));
    HashCombine(seed, fb.output_merger.alpha_test.enable.Value());
    HashCombine(seed, static_cast<u32>(fb.output_merger.alpha_test.func.Value()));
    HashCombine(seed, fb.output_merger.alpha_test.ref.Value());

    u32 texture_mask = 0;
    const auto textures = regs.texturing.GetTextures();
    for (u32 index = 0; index < textures.size(); ++index) {
        if (textures[index].enabled != 0) {
            texture_mask |= 1U << index;
        }
    }
    HashCombine(seed, texture_mask);
    (void)pica;
    return seed;
}
#endif

void LogSoftwareBridgeOnce() {
    static bool logged = false;
    if (!logged) {
        LOG_INFO(Render,
                 "Deko3D rasterizer: using software PICA rasterization with native Deko3D "
                 "presentation");
        logged = true;
    }
}

} // namespace

Rasterizer::Rasterizer(Memory::MemorySystem& memory, Pica::PicaCore& pica, State& state_,
                       TextureCache& texture_cache_, ShaderCache& shader_cache_)
    : RasterizerAccelerated{memory, pica}, state{state_}, texture_cache{texture_cache_},
      shader_cache{shader_cache_}, software_fallback{memory, pica} {}

bool Rasterizer::Initialize() {
    if (!state.IsInitialized() || !texture_cache.IsInitialized() || !shader_cache.IsInitialized()) {
        LOG_ERROR(Render,
                  "Deko3D rasterizer initialization requested before dependent renderer state");
        return false;
    }
#ifdef __SWITCH__
    if (!InitializeGpuResources()) {
        ShutdownGpuResources();
        return false;
    }
#endif
    initialized = true;
    LOG_INFO(Render, "Deko3D rasterizer initialized with software compatibility fallback");
    return true;
}

void Rasterizer::Shutdown() {
#ifdef __SWITCH__
    ShutdownGpuResources();
#endif
    initialized = false;
}

#ifdef __SWITCH__
bool Rasterizer::InitializeGpuResources() {
    device = state.GetDevice();
    queue = state.GetQueue();
    if (!device || !queue) {
        LOG_ERROR(Render, "Deko3D rasterizer cannot initialize without device and queue");
        return false;
    }

    DkMemBlockMaker command_mem_maker;
    dkMemBlockMakerDefaults(&command_mem_maker, device,
                            AlignUp(RasterCommandMemorySize, DK_MEMBLOCK_ALIGNMENT));
    command_mem_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    command_mem_block = dkMemBlockCreate(&command_mem_maker);
    if (!command_mem_block) {
        LOG_ERROR(Render, "Deko3D rasterizer command memory allocation failed");
        return false;
    }

    DkCmdBufMaker command_buffer_maker;
    dkCmdBufMakerDefaults(&command_buffer_maker, device);
    command_buffer = dkCmdBufCreate(&command_buffer_maker);
    if (!command_buffer) {
        LOG_ERROR(Render, "Deko3D rasterizer command buffer creation failed");
        return false;
    }
    const u32 command_slice_size = RasterCommandMemorySize / FrameSliceCount;
    DkMemBlockMaker vertex_mem_maker;
    dkMemBlockMakerDefaults(&vertex_mem_maker, device,
                            AlignUp(VertexBufferSize, DK_MEMBLOCK_ALIGNMENT));
    vertex_mem_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    vertex_mem_block = dkMemBlockCreate(&vertex_mem_maker);
    if (!vertex_mem_block) {
        LOG_ERROR(Render, "Deko3D rasterizer vertex memory allocation failed");
        return false;
    }
    vertex_cpu_buffer = dkMemBlockGetCpuAddr(vertex_mem_block);
    vertex_gpu_addr = dkMemBlockGetGpuAddr(vertex_mem_block);
    if (!vertex_cpu_buffer || vertex_gpu_addr == 0) {
        LOG_ERROR(Render, "Deko3D rasterizer vertex memory mapping failed");
        return false;
    }

    DkMemBlockMaker uniform_mem_maker;
    dkMemBlockMakerDefaults(&uniform_mem_maker, device,
                            AlignUp(UniformBufferSize, DK_MEMBLOCK_ALIGNMENT));
    uniform_mem_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    uniform_mem_block = dkMemBlockCreate(&uniform_mem_maker);
    if (!uniform_mem_block) {
        LOG_ERROR(Render, "Deko3D rasterizer uniform memory allocation failed");
        return false;
    }
    uniform_cpu_buffer = dkMemBlockGetCpuAddr(uniform_mem_block);
    uniform_gpu_addr = dkMemBlockGetGpuAddr(uniform_mem_block);
    if (!uniform_cpu_buffer || uniform_gpu_addr == 0) {
        LOG_ERROR(Render, "Deko3D rasterizer uniform memory mapping failed");
        return false;
    }

    DkMemBlockMaker descriptor_mem_maker;
    dkMemBlockMakerDefaults(&descriptor_mem_maker, device,
                            AlignUp(DescriptorBufferSize, DK_MEMBLOCK_ALIGNMENT));
    descriptor_mem_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    descriptor_mem_block = dkMemBlockCreate(&descriptor_mem_maker);
    if (!descriptor_mem_block) {
        LOG_ERROR(Render, "Deko3D rasterizer descriptor memory allocation failed");
        return false;
    }
    descriptor_cpu_buffer = dkMemBlockGetCpuAddr(descriptor_mem_block);
    descriptor_gpu_addr = dkMemBlockGetGpuAddr(descriptor_mem_block);
    if (!descriptor_cpu_buffer || descriptor_gpu_addr == 0) {
        LOG_ERROR(Render, "Deko3D rasterizer descriptor memory mapping failed");
        return false;
    }

    const u32 vertex_slice_size = VertexBufferSize / FrameSliceCount;
    const u32 uniform_slice_size = UniformBufferSize / FrameSliceCount;
    for (u32 index = 0; index < FrameSliceCount; ++index) {
        auto& slice = frame_slices[index];
        slice.command_offset = index * command_slice_size;
        slice.command_size =
            index == FrameSliceCount - 1 ? RasterCommandMemorySize - slice.command_offset
                                         : command_slice_size;
        slice.vertex_offset = index * vertex_slice_size;
        slice.vertex_size =
            index == FrameSliceCount - 1 ? VertexBufferSize - slice.vertex_offset
                                         : vertex_slice_size;
        slice.uniform_offset = index * uniform_slice_size;
        slice.uniform_size =
            index == FrameSliceCount - 1 ? UniformBufferSize - slice.uniform_offset
                                         : uniform_slice_size;
        slice.fence = {};
        slice.fence_pending = false;
    }
    current_frame_slice = 0;

    LOG_INFO(Render,
             "Deko3D rasterizer GPU resources created: vertex={} uniform={} descriptor={} "
             "slices={}",
             VertexBufferSize, UniformBufferSize, DescriptorBufferSize, FrameSliceCount);
    return true;
}

void Rasterizer::ShutdownGpuResources() {
    if (device || queue || command_buffer || command_mem_block || vertex_mem_block ||
        uniform_mem_block || descriptor_mem_block) {
        state.WaitIdle();
    }

    if (command_buffer) {
        dkCmdBufDestroy(command_buffer);
        command_buffer = nullptr;
    }
    if (command_mem_block) {
        dkMemBlockDestroy(command_mem_block);
        command_mem_block = nullptr;
    }
    if (vertex_mem_block) {
        dkMemBlockDestroy(vertex_mem_block);
        vertex_mem_block = nullptr;
    }
    vertex_cpu_buffer = nullptr;
    vertex_gpu_addr = 0;

    if (uniform_mem_block) {
        dkMemBlockDestroy(uniform_mem_block);
        uniform_mem_block = nullptr;
    }
    uniform_cpu_buffer = nullptr;
    uniform_gpu_addr = 0;

    if (descriptor_mem_block) {
        dkMemBlockDestroy(descriptor_mem_block);
        descriptor_mem_block = nullptr;
    }
    descriptor_cpu_buffer = nullptr;
    descriptor_gpu_addr = 0;
    if (depth_mem_block) {
        dkMemBlockDestroy(depth_mem_block);
        depth_mem_block = nullptr;
    }
    depth_image = {};
    depth_view = {};
    depth_width = 0;
    depth_height = 0;
    depth_format = 0;

    frame_slices = {};
    current_frame_slice = 0;
    device = {};
    queue = {};
}

Rasterizer::FrameSlice& Rasterizer::CurrentFrameSlice() {
    auto& slice = frame_slices[current_frame_slice];
    current_frame_slice = (current_frame_slice + 1) % FrameSliceCount;
    return slice;
}

bool Rasterizer::WaitForFrameSlice(FrameSlice& slice) {
    if (!slice.fence_pending) {
        return true;
    }

    if (QueueHasError("before frame-slice fence wait")) {
        return false;
    }

    FlushQueue();
    RecordRasterFencePoll();
    const DkResult poll_result = dkFenceWait(&slice.fence, 0);
    if (poll_result == DkResult_Success) {
        RecordFencePollSuccess();
        RecordRasterFencePollSuccess();
        RecordHardwareDrawCompleted(slice.pending_vertices / 3);
        RecordTransformedBatchCompleted(slice.pending_vertices);
        slice.fence_pending = false;
        slice.pending_vertices = 0;
        return true;
    }

    RecordRingWait();
    RecordFenceWait();
    RecordRasterFenceWait();
    constexpr s64 FenceWaitTimeoutNs = 1'000'000'000LL;
    const auto wait_start = std::chrono::steady_clock::now();
    const DkResult result = dkFenceWait(&slice.fence, FenceWaitTimeoutNs);
    const auto wait_end = std::chrono::steady_clock::now();
    const auto wait_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(wait_end - wait_start).count();
    RecordFenceWaitDurationMs(static_cast<std::uint64_t>(std::max<s64>(wait_ms, 0)));
    RecordRasterFenceWaitDurationUs(
        static_cast<std::uint64_t>(std::max<s64>(wait_ms, 0)) * 1000);
    if (result != DkResult_Success) {
        if (result == DkResult_Timeout) {
            RecordFenceTimeout();
            RecordRasterFenceTimeout();
        }
        LOG_WARNING(Render, "Deko3D rasterizer frame-slice fence wait failed result={} wait_ms={}",
                    static_cast<int>(result), wait_ms);
        return false;
    }
    RecordHardwareDrawCompleted(slice.pending_vertices / 3);
    RecordTransformedBatchCompleted(slice.pending_vertices);
    slice.fence_pending = false;
    slice.pending_vertices = 0;
    return true;
}

Rasterizer::HardwareEligibility Rasterizer::EvaluateTransformedBatchEligibility() const {
    using ColorFormat = Pica::FramebufferRegs::ColorFormat;
    using LogicOp = Pica::FramebufferRegs::LogicOp;

    const auto& fb = regs.framebuffer;
    u32 blockers = None;
    const bool batch_valid = vertex_batch.size() >= 3 && (vertex_batch.size() % 3) == 0 &&
                             fallback_vertex_batch.size() == vertex_batch.size();
    if (!batch_valid) {
        blockers |= InvalidBatch;
    }
    if (!initialized || !device || !queue || !command_buffer || !vertex_cpu_buffer ||
        vertex_gpu_addr == 0) {
        blockers |= MissingGpuResources;
    }
    if (!shader_cache.GetColorVertexShader() || !shader_cache.GetColorFragmentShader()) {
        blockers |= ShaderUnavailable;
    }
    if (fb.IsShadowRendering()) {
        blockers |= ShadowRendering;
    }
    if (fb.framebuffer.color_format != ColorFormat::RGBA8) {
        blockers |= FramebufferFormat;
    }
    if (fb.framebuffer.GetColorBufferPhysicalAddress() == 0 || fb.framebuffer.GetWidth() == 0 ||
        fb.framebuffer.GetHeight() == 0) {
        blockers |= FramebufferDimensions;
    }
    // Depth and blending are fully wired to Deko3D in SubmitHardwareChunk; only D24S8
    // depth targets remain unsupported because GetOrCreateDepthTarget cannot create that format.
    if ((fb.output_merger.depth_write_enable != 0 || fb.output_merger.depth_test_enable != 0) &&
        fb.framebuffer.depth_format == Pica::FramebufferRegs::DepthFormat::D24S8) {
        blockers |= DepthWriteEnabled;
    }
    if (fb.output_merger.stencil_test.enable != 0) {
        blockers |= StencilEnabled;
    }
    if (fb.output_merger.alphablend_enable != 0) {
        const auto blend = fb.output_merger.alpha_blending;
        const bool blend_supported = MapBlendFactor(blend.factor_source_rgb).has_value() &&
                                     MapBlendFactor(blend.factor_dest_rgb).has_value() &&
                                     MapBlendFactor(blend.factor_source_a).has_value() &&
                                     MapBlendFactor(blend.factor_dest_a).has_value();
        if (!blend_supported) {
            blockers |= BlendingEnabled;
        }
    }
    // Alpha test is implemented in the fixed fragment shader through a uniform buffer.
    if (fb.output_merger.alphablend_enable == 0 && fb.output_merger.logic_op != LogicOp::Copy) {
        blockers |= LogicOpUnsupported;
    }
    // Color write masks are mapped in SubmitHardwareChunk; allow the hardware path.
    (void)ColorWriteMask(fb);

    const auto pica_textures = regs.texturing.GetTextures();
    for (const auto& texture : pica_textures) {
        if (texture.enabled != 0) {
            blockers |= TexturesEnabled;
        }
    }

    ASSERT_MSG(batch_valid || (blockers & InvalidBatch) != 0,
               "Transformed batch validity did not set InvalidBatch blocker");
    ASSERT_MSG(!batch_valid || (blockers & InvalidBatch) == 0,
               "Valid transformed batch incorrectly set InvalidBatch blocker");

    const auto signature_id = TransformedStateSignature(pica, regs);
    static std::unordered_set<std::size_t> observed_signatures;
    const bool new_signature = observed_signatures.insert(signature_id).second;
    RecordStateSignature(signature_id, new_signature);
    if (new_signature && observed_signatures.size() <= 8) {
        LOG_INFO(Render,
                 "Deko3D transformed state signature: id={} color=0x{:08x} depth=0x{:08x} "
                 "color_size={}x{} depth_size={}x{} color_format={} depth_format={} "
                 "blend={} depth_test={} depth_write={} alpha_test={} blockers=0x{:08x} "
                 "display_match={}",
                 static_cast<unsigned long long>(signature_id),
                 fb.framebuffer.GetColorBufferPhysicalAddress(),
                 fb.framebuffer.GetDepthBufferPhysicalAddress(), fb.framebuffer.GetWidth(),
                 fb.framebuffer.GetHeight(), fb.framebuffer.GetWidth(), fb.framebuffer.GetHeight(),
                 static_cast<u32>(fb.framebuffer.color_format.Value()),
                 static_cast<u32>(fb.framebuffer.depth_format.Value()),
                 fb.output_merger.alphablend_enable.Value(),
                 fb.output_merger.depth_test_enable.Value(),
                 fb.output_merger.depth_write_enable.Value(), fb.output_merger.alpha_test.enable.Value(),
                 blockers, "unknown_guest_vram");
    } else if ((blockers & FramebufferDimensions) != 0) {
        LOG_INFO(Render,
                 "Deko3D framebuffer dimension rejection: id={} color=0x{:08x} size={}x{} "
                 "format={} blockers=0x{:08x}",
                 static_cast<unsigned long long>(signature_id),
                 fb.framebuffer.GetColorBufferPhysicalAddress(), fb.framebuffer.GetWidth(),
                 fb.framebuffer.GetHeight(), static_cast<u32>(fb.framebuffer.color_format.Value()),
                 blockers);
    }

    FallbackReason primary = FallbackReason::UnsupportedState;
    if ((blockers & InvalidBatch) != 0) {
        primary = FallbackReason::UnsupportedState;
    } else if ((blockers & MissingGpuResources) != 0 || (blockers & ShaderUnavailable) != 0) {
        primary = FallbackReason::UnsupportedState;
    } else if ((blockers & TexturesEnabled) != 0) {
        primary = FallbackReason::TexturesEnabled;
    } else if ((blockers & (DepthTestEnabled | DepthWriteEnabled)) != 0) {
        primary = FallbackReason::DepthEnabled;
    } else if ((blockers & StencilEnabled) != 0) {
        primary = FallbackReason::StencilEnabled;
    } else if ((blockers & BlendingEnabled) != 0) {
        primary = FallbackReason::BlendEnabled;
    } else if ((blockers & LogicOpUnsupported) != 0) {
        primary = FallbackReason::LogicOp;
    } else if ((blockers & ShadowRendering) != 0) {
        primary = FallbackReason::Shadow;
    } else if ((blockers & FramebufferFormat) != 0) {
        primary = FallbackReason::FramebufferFormat;
    } else if ((blockers & (WrongRenderTarget | FramebufferDimensions)) != 0) {
        primary = FallbackReason::WrongRenderTarget;
    }
    return {blockers == 0, primary, blockers};
}

Rasterizer::HardwareEligibility Rasterizer::EvaluateDirectBatchEligibility(bool is_indexed) const {
    using TriangleTopology = Pica::PipelineRegs::TriangleTopology;
    using UseGS = Pica::PipelineRegs::UseGS;

    u32 blockers = DirectUnimplemented;
    if (regs.pipeline.triangle_topology != TriangleTopology::List) {
        blockers |= DirectTopology;
    }
    if (regs.pipeline.use_gs != UseGS::No) {
        blockers |= DirectGeometryShader;
    }
    (void)is_indexed;
    return {false, blockers & DirectTopology ? FallbackReason::Topology
                                             : FallbackReason::UnsupportedState,
            blockers};
}

bool Rasterizer::TryDrawHardwareBatch(std::size_t& submitted_vertices) {
    submitted_vertices = 0;
#if defined(AZAHAR_SWITCH_DEKO3D_FORCE_SW_RASTERIZER)
    // Baseline safety: keep the hardware rasterizer path disabled while it is
    // still being validated on Switch. Software fallback is slower but stable.
    RecordFallbackReason(FallbackReason::UnsupportedState);
    return false;
#endif
    if (!initialized || !device || !queue || !command_buffer || !vertex_cpu_buffer ||
        vertex_gpu_addr == 0) {
        return false;
    }

    const DkShader* const vertex_shader = shader_cache.GetColorVertexShader();
    const DkShader* const fragment_shader = shader_cache.GetColorFragmentShader();
    if (!vertex_shader || !fragment_shader) {
        return false;
    }

    const auto eligibility = EvaluateTransformedBatchEligibility();
    RecordTransformedBlocker(eligibility.blockers);
    if (!eligibility.supported) {
        if ((eligibility.blockers & InvalidBatch) != 0) {
            RecordFallbackInvalidTransformedBatch();
        }
        RecordFallbackReason(eligibility.reason);
        return false;
    }

    const auto& fb = regs.framebuffer.framebuffer;
    State::RenderTargetKey color_key{
        .color_address = fb.GetColorBufferPhysicalAddress(),
        .width = fb.GetWidth(),
        .height = fb.GetHeight(),
        .format = static_cast<u32>(fb.color_format.Value()),
    };
    State::CachedRenderTarget* color_target = state.GetOrCreateRenderTarget(color_key);
    if (!color_target) {
        RecordTransformedBlocker(MissingGpuResources);
        RecordFallbackReason(FallbackReason::WrongRenderTarget);
        return false;
    }
    const DkImageView* const depth_target = GetOrCreateDepthTarget();
    if ((regs.framebuffer.output_merger.depth_write_enable != 0 ||
         regs.framebuffer.output_merger.depth_test_enable != 0) &&
        !depth_target) {
        RecordDepthState(false);
        RecordFallbackReason(FallbackReason::DepthEnabled);
        return false;
    }
    RecordTransformedBatchEligible();

    const std::size_t vertices_per_slice =
        3 * (static_cast<std::size_t>(frame_slices[0].vertex_size) /
             (3 * sizeof(HardwareVertex)));
    if (vertices_per_slice < 3) {
        return false;
    }

    for (std::size_t base_vertex = 0; base_vertex < vertex_batch.size();
         base_vertex += vertices_per_slice) {
        const std::size_t remaining = vertex_batch.size() - base_vertex;
        const std::size_t vertex_count = std::min(vertices_per_slice, remaining);
        const std::size_t aligned_vertex_count = vertex_count - (vertex_count % 3);
        if (aligned_vertex_count == 0) {
            return false;
        }

        FrameSlice& slice = CurrentFrameSlice();
        if (!WaitForFrameSlice(slice)) {
            return false;
        }
        if (!SubmitHardwareChunk(slice, *color_target, depth_target, base_vertex,
                                 aligned_vertex_count)) {
            return false;
        }
        submitted_vertices += aligned_vertex_count;
    }

    return true;
}

const DkImageView* Rasterizer::GetOrCreateDepthTarget() {
    const auto& fb = regs.framebuffer;
    if (fb.output_merger.depth_write_enable == 0 && fb.output_merger.depth_test_enable == 0) {
        return nullptr;
    }
    const u32 width = fb.framebuffer.GetWidth();
    const u32 height = fb.framebuffer.GetHeight();
    const u32 format = static_cast<u32>(fb.framebuffer.depth_format.Value());
    if (depth_mem_block && depth_width == width && depth_height == height &&
        depth_format == format) {
        RecordDepthState(true);
        return &depth_view;
    }
    if (depth_mem_block) {
        dkMemBlockDestroy(depth_mem_block);
        depth_mem_block = nullptr;
        depth_image = {};
        depth_view = {};
    }

    DkImageFormat dk_format = DkImageFormat_None;
    switch (fb.framebuffer.depth_format) {
    case Pica::FramebufferRegs::DepthFormat::D16:
        dk_format = DkImageFormat_Z16;
        break;
    case Pica::FramebufferRegs::DepthFormat::D24:
        dk_format = DkImageFormat_Z24X8;
        break;
    case Pica::FramebufferRegs::DepthFormat::D24S8:
        RecordDepthState(false);
        return nullptr;
    }

    DkImageLayoutMaker layout_maker;
    dkImageLayoutMakerDefaults(&layout_maker, device);
    layout_maker.type = DkImageType_2D;
    layout_maker.flags = DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine;
    layout_maker.format = dk_format;
    layout_maker.dimensions[0] = width;
    layout_maker.dimensions[1] = height;
    DkImageLayout layout;
    dkImageLayoutInitialize(&layout, &layout_maker);
    const u32 size = AlignUp(static_cast<u32>(dkImageLayoutGetSize(&layout)),
                             dkImageLayoutGetAlignment(&layout));
    DkMemBlockMaker mem_block_maker;
    dkMemBlockMakerDefaults(&mem_block_maker, device, size);
    mem_block_maker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
    depth_mem_block = dkMemBlockCreate(&mem_block_maker);
    if (!depth_mem_block) {
        RecordDepthState(false);
        return nullptr;
    }
    dkImageInitialize(&depth_image, &layout, depth_mem_block, 0);
    dkImageViewDefaults(&depth_view, &depth_image);
    depth_width = width;
    depth_height = height;
    depth_format = format;
    RecordDepthState(true);
    LOG_INFO(Render,
             "Deko3D depth target create: addr=0x{:08x} size={}x{} format={} compare={} "
             "write_mask={}",
             fb.framebuffer.GetDepthBufferPhysicalAddress(), width, height, format,
             static_cast<u32>(fb.output_merger.depth_test_func.Value()),
             fb.framebuffer.allow_depth_stencil_write.Value());
    return &depth_view;
}

bool Rasterizer::SubmitHardwareChunk(FrameSlice& slice, State::CachedRenderTarget& color_target,
                                     const DkImageView* depth_target, std::size_t base_vertex,
                                     std::size_t vertex_count) {
    const std::size_t vertex_bytes = vertex_count * sizeof(HardwareVertex);
    if (vertex_bytes > slice.vertex_size) {
        return false;
    }

    std::memcpy(static_cast<u8*>(vertex_cpu_buffer) + slice.vertex_offset,
                vertex_batch.data() + base_vertex, vertex_bytes);

    const DkShader* const shaders[] = {shader_cache.GetColorVertexShader(),
                                       shader_cache.GetColorFragmentShader()};
    if (!shaders[0] || !shaders[1]) {
        return false;
    }

    const DkVtxAttribState attribs[] = {
        {0, 0, offsetof(HardwareVertex, position), DkVtxAttribSize_4x32, DkVtxAttribType_Float, 0},
        {0, 0, offsetof(HardwareVertex, color), DkVtxAttribSize_4x32, DkVtxAttribType_Float, 0},
        {0, 0, offsetof(HardwareVertex, tex_coord0), DkVtxAttribSize_2x32, DkVtxAttribType_Float,
         0},
        {0, 0, offsetof(HardwareVertex, tex_coord1), DkVtxAttribSize_2x32, DkVtxAttribType_Float,
         0},
        {0, 0, offsetof(HardwareVertex, tex_coord2), DkVtxAttribSize_2x32, DkVtxAttribType_Float,
         0},
        {0, 0, offsetof(HardwareVertex, tex_coord0_w), DkVtxAttribSize_1x32,
         DkVtxAttribType_Float, 0},
        {0, 0, offsetof(HardwareVertex, normquat), DkVtxAttribSize_4x32, DkVtxAttribType_Float, 0},
        {0, 0, offsetof(HardwareVertex, view), DkVtxAttribSize_3x32, DkVtxAttribType_Float, 0},
    };
    const DkVtxBufferState vtx_buffer_state[] = {{sizeof(HardwareVertex), 0}};

    DkRasterizerState rasterizer_state;
    DkMultisampleState multisample_state;
    DkColorState color_state;
    DkColorWriteState color_write_state;
    DkDepthStencilState depth_stencil_state;
    dkRasterizerStateDefaults(&rasterizer_state);
    dkMultisampleStateDefaults(&multisample_state);
    dkColorStateDefaults(&color_state);
    dkColorWriteStateDefaults(&color_write_state);
    dkDepthStencilStateDefaults(&depth_stencil_state);
    rasterizer_state.cullMode = DkFace_None;
    dkColorStateSetBlendEnable(&color_state, 0, regs.framebuffer.output_merger.alphablend_enable != 0);
    dkColorWriteStateSetMask(&color_write_state, 0, ColorWriteMask(regs.framebuffer));
    DkBlendState blend_state;
    dkBlendStateDefaults(&blend_state);
    if (regs.framebuffer.output_merger.alphablend_enable != 0) {
        const auto blend = regs.framebuffer.output_merger.alpha_blending;
        const auto src_rgb = MapBlendFactor(blend.factor_source_rgb);
        const auto dst_rgb = MapBlendFactor(blend.factor_dest_rgb);
        const auto src_a = MapBlendFactor(blend.factor_source_a);
        const auto dst_a = MapBlendFactor(blend.factor_dest_a);
        if (!src_rgb || !dst_rgb || !src_a || !dst_a) {
            return false;
        }
        dkBlendStateSetOps(&blend_state, MapBlendEquation(blend.blend_equation_rgb),
                           MapBlendEquation(blend.blend_equation_a));
        dkBlendStateSetFactors(&blend_state, *src_rgb, *dst_rgb, *src_a, *dst_a);
        std::size_t blend_signature = 0;
        HashCombine(blend_signature, static_cast<u32>(blend.blend_equation_rgb.Value()));
        HashCombine(blend_signature, static_cast<u32>(blend.blend_equation_a.Value()));
        HashCombine(blend_signature, static_cast<u32>(blend.factor_source_rgb.Value()));
        HashCombine(blend_signature, static_cast<u32>(blend.factor_dest_rgb.Value()));
        HashCombine(blend_signature, static_cast<u32>(blend.factor_source_a.Value()));
        HashCombine(blend_signature, static_cast<u32>(blend.factor_dest_a.Value()));
        HashCombine(blend_signature, regs.framebuffer.output_merger.blend_const.raw);
        static std::unordered_set<std::size_t> blend_signatures;
        const bool cache_hit = !blend_signatures.insert(blend_signature).second;
        RecordBlendState(true, cache_hit);
    }
    depth_stencil_state.depthTestEnable =
        regs.framebuffer.output_merger.depth_test_enable != 0 ||
        regs.framebuffer.output_merger.depth_write_enable != 0;
    depth_stencil_state.depthWriteEnable =
        regs.framebuffer.framebuffer.allow_depth_stencil_write != 0 &&
        regs.framebuffer.output_merger.depth_write_enable != 0;
    depth_stencil_state.depthCompareOp =
        regs.framebuffer.output_merger.depth_test_enable != 0
            ? MapCompare(regs.framebuffer.output_merger.depth_test_func)
            : DkCompareOp_Always;
    depth_stencil_state.stencilTestEnable = false;

    const u32 target_width = color_target.key.width;
    const u32 target_height = color_target.key.height;
    const DkViewport viewport = {0.0f, 0.0f, static_cast<float>(target_width),
                                 static_cast<float>(target_height), 0.0f, 1.0f};
    const DkScissor scissor = {0, 0, target_width, target_height};

    // Fill the per-slice uniform buffer with PICA fragment state.
    const auto& alpha_test = regs.framebuffer.output_merger.alpha_test;
    PicaFragmentState fragment_state{
        .alpha_test_enabled = static_cast<s32>(alpha_test.enable.Value()),
        .alpha_test_func = MapAlphaTestFunc(alpha_test.func.Value()),
        .alpha_test_ref = alpha_test.ref.Value() / 255.0f,
        .alpha_test_pad = 0.0f,
    };
    std::memcpy(static_cast<u8*>(uniform_cpu_buffer) + slice.uniform_offset, &fragment_state,
                sizeof(fragment_state));

    dkCmdBufClear(command_buffer);
    dkCmdBufAddMemory(command_buffer, command_mem_block, slice.command_offset,
                      slice.command_size);
    dkCmdBufBindUniformBuffer(command_buffer, DkStage_Fragment, 0,
                              uniform_gpu_addr + slice.uniform_offset, sizeof(PicaFragmentState));
    dkCmdBufBindRenderTarget(command_buffer, &color_target.view, depth_target);
    dkCmdBufSetViewports(command_buffer, 0, &viewport, 1);
    dkCmdBufSetScissors(command_buffer, 0, &scissor, 1);
    dkCmdBufBindShaders(command_buffer, DkStageFlag_GraphicsMask, shaders, 2);
    dkCmdBufBindRasterizerState(command_buffer, &rasterizer_state);
    dkCmdBufBindMultisampleState(command_buffer, &multisample_state);
    dkCmdBufBindColorState(command_buffer, &color_state);
    dkCmdBufBindColorWriteState(command_buffer, &color_write_state);
    dkCmdBufBindBlendState(command_buffer, 0, &blend_state);
    dkCmdBufSetBlendConst(command_buffer,
                          regs.framebuffer.output_merger.blend_const.r.Value() / 255.0f,
                          regs.framebuffer.output_merger.blend_const.g.Value() / 255.0f,
                          regs.framebuffer.output_merger.blend_const.b.Value() / 255.0f,
                          regs.framebuffer.output_merger.blend_const.a.Value() / 255.0f);
    dkCmdBufBindDepthStencilState(command_buffer, &depth_stencil_state);
    dkCmdBufBindVtxAttribState(command_buffer, attribs,
                               sizeof(attribs) / sizeof(attribs[0]));
    dkCmdBufBindVtxBufferState(command_buffer, vtx_buffer_state,
                               sizeof(vtx_buffer_state) / sizeof(vtx_buffer_state[0]));
    dkCmdBufBindVtxBuffer(command_buffer, 0, vertex_gpu_addr + slice.vertex_offset,
                          static_cast<u32>(vertex_bytes));
    dkCmdBufDraw(command_buffer, DkPrimitive_Triangles, static_cast<u32>(vertex_count), 1, 0, 0);
    dkCmdBufSignalFence(command_buffer, &slice.fence, true);

    const DkCmdList draw_cmd = dkCmdBufFinishList(command_buffer);
    if (!draw_cmd) {
        return false;
    }

    dkQueueSubmitCommands(queue, draw_cmd);
    RecordRasterQueueSubmit();
    if (QueueHasError("after draw submit")) {
        return false;
    }
    FlushQueue();
    if (QueueHasError("after draw flush")) {
        return false;
    }
    slice.fence_pending = true;
    slice.pending_vertices = vertex_count;
    state.MarkRenderTargetGpuDirty(color_target);
    RecordHardwareRasterFrame();
    RecordHardwareDrawSubmitted(vertex_count / 3);
    RecordTransformedBatchSubmitted(vertex_count);
    return true;
}

bool Rasterizer::QueueHasError(const char* context) {
    if (queue && dkQueueIsInErrorState(queue)) {
        RecordQueueError();
        RecordRasterQueueError();
        LOG_ERROR(Render, "Deko3D queue entered error state {}", context ? context : "");
        return true;
    }
    return false;
}

void Rasterizer::FlushQueue() {
    if (queue) {
        dkQueueFlush(queue);
        RecordQueueFlush();
        RecordRasterQueueFlush();
    }
}
#endif

void Rasterizer::AddTriangle(const Pica::OutputVertex& v0, const Pica::OutputVertex& v1,
                             const Pica::OutputVertex& v2) {
    RasterizerAccelerated::AddTriangle(v0, v1, v2);
    fallback_vertex_batch.push_back(v0);
    fallback_vertex_batch.push_back(v1);
    fallback_vertex_batch.push_back(v2);
}

void Rasterizer::DrawTriangles() {
    if (vertex_batch.empty()) {
        return;
    }

#ifdef __SWITCH__
    const bool valid_transformed_batch =
        vertex_batch.size() >= 3 && (vertex_batch.size() % 3) == 0 &&
        fallback_vertex_batch.size() == vertex_batch.size();
    RecordTransformedBatchCheck(valid_transformed_batch);
    if (!valid_transformed_batch) {
        RecordFallbackInvalidTransformedBatch();
        RecordFallbackReason(FallbackReason::UnsupportedState);
        DrawSoftwareFallback();
        return;
    }

    RecordHardwareDrawAttempt();
    std::size_t submitted_vertices = 0;
    if (TryDrawHardwareBatch(submitted_vertices)) {
        vertex_batch.clear();
        fallback_vertex_batch.clear();
        return;
    }
    if (submitted_vertices != 0) {
        RecordHardwareDrawFailure();
        const std::uint64_t hw_triangles = submitted_vertices / 3;
        const std::uint64_t sw_triangles = (vertex_batch.size() - submitted_vertices) / 3;
        RecordPartialBatch(hw_triangles, sw_triangles);
        DrawSoftwareFallback(submitted_vertices);
        return;
    }
#endif

    DrawSoftwareFallback();
}

void Rasterizer::DrawSoftwareFallback(std::size_t first_vertex) {
    LogSoftwareBridgeOnce();
    first_vertex -= first_vertex % 3;
    for (std::size_t index = first_vertex; index + 2 < fallback_vertex_batch.size(); index += 3) {
        software_fallback.AddTriangle(fallback_vertex_batch[index], fallback_vertex_batch[index + 1],
                                      fallback_vertex_batch[index + 2]);
    }
    RecordSoftwareFallback((fallback_vertex_batch.size() - first_vertex) / 3);
    RecordSoftwareRasterFrame();
#ifdef __SWITCH__
    const auto& fb = regs.framebuffer.framebuffer;
    state.MarkRenderTargetSoftwareDirty(
        fb.GetColorBufferPhysicalAddress(),
        fb.GetWidth() * fb.GetHeight() * Pica::FramebufferRegs::BytesPerColorPixel(fb.color_format));
#endif
    vertex_batch.clear();
    fallback_vertex_batch.clear();
    software_fallback.DrawTriangles();
}

void Rasterizer::FlushAll() {
    software_fallback.FlushAll();
}

void Rasterizer::FlushRegion(PAddr addr, u32 size) {
#ifdef __SWITCH__
    state.InvalidateRenderTargetsOverlapping(addr, size, State::SurfaceOwner::CpuMemory);
#endif
    software_fallback.FlushRegion(addr, size);
}

void Rasterizer::InvalidateRegion(PAddr addr, u32 size) {
#ifdef __SWITCH__
    state.InvalidateRenderTargetsOverlapping(addr, size, State::SurfaceOwner::CpuMemory);
#endif
    software_fallback.InvalidateRegion(addr, size);
}

void Rasterizer::FlushAndInvalidateRegion(PAddr addr, u32 size) {
#ifdef __SWITCH__
    state.InvalidateRenderTargetsOverlapping(addr, size, State::SurfaceOwner::CpuMemory);
#endif
    software_fallback.FlushAndInvalidateRegion(addr, size);
}

void Rasterizer::ClearAll(bool flush) {
    vertex_batch.clear();
    fallback_vertex_batch.clear();
    software_fallback.ClearAll(flush);
}

bool Rasterizer::AccelerateDrawBatch(bool is_indexed) {
    if (!initialized) {
        return false;
    }

    // Direct indexed/non-indexed acceleration is intentionally disabled for the first Deko3D
    // rasterizer milestone. The PICA frontend will emit triangles through AddTriangle(), keeping
    // the compatibility fallback correct while the native HardwareVertex path is added.
    const auto eligibility = EvaluateDirectBatchEligibility(is_indexed);
    RecordDirectBlocker(eligibility.blockers);
    RecordFallbackReason(eligibility.reason);
    RecordDirectBatchRejected();
    (void)is_indexed;
    return false;
}

} // namespace VideoCore::Deko3D
