// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "video_core/renderer_deko3d/deko3d_rasterizer.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>

#include "common/logging/log.h"
#include "video_core/pica/pica_core.h"
#include "video_core/renderer_deko3d/deko3d_shader.h"
#include "video_core/renderer_deko3d/deko3d_texture_cache.h"

namespace VideoCore::Deko3D {
namespace {

#ifdef __SWITCH__
u32 AlignUp(u32 value, u32 alignment) {
    return alignment == 0 ? value : ((value + alignment - 1) / alignment) * alignment;
}

DkBlendOp MapBlendEquation(Pica::FramebufferRegs::BlendEquation equation) {
    using Equation = Pica::FramebufferRegs::BlendEquation;
    switch (equation) {
    case Equation::Add:
        return DkBlendOp_Add;
    case Equation::Subtract:
        return DkBlendOp_Sub;
    case Equation::ReverseSubtract:
        return DkBlendOp_RevSub;
    case Equation::Min:
        return DkBlendOp_Min;
    case Equation::Max:
        return DkBlendOp_Max;
    }
    return DkBlendOp_Add;
}

DkBlendFactor MapBlendFactor(Pica::FramebufferRegs::BlendFactor factor) {
    using Factor = Pica::FramebufferRegs::BlendFactor;
    switch (factor) {
    case Factor::Zero:
        return DkBlendFactor_Zero;
    case Factor::One:
        return DkBlendFactor_One;
    case Factor::SourceColor:
        return DkBlendFactor_SrcColor;
    case Factor::OneMinusSourceColor:
        return DkBlendFactor_InvSrcColor;
    case Factor::DestColor:
        return DkBlendFactor_DstColor;
    case Factor::OneMinusDestColor:
        return DkBlendFactor_InvDstColor;
    case Factor::SourceAlpha:
        return DkBlendFactor_SrcAlpha;
    case Factor::OneMinusSourceAlpha:
        return DkBlendFactor_InvSrcAlpha;
    case Factor::DestAlpha:
        return DkBlendFactor_DstAlpha;
    case Factor::OneMinusDestAlpha:
        return DkBlendFactor_InvDstAlpha;
    case Factor::ConstantColor:
        return DkBlendFactor_ConstColor;
    case Factor::OneMinusConstantColor:
        return DkBlendFactor_InvConstColor;
    case Factor::ConstantAlpha:
        return DkBlendFactor_ConstAlpha;
    case Factor::OneMinusConstantAlpha:
        return DkBlendFactor_InvConstAlpha;
    case Factor::SourceAlphaSaturate:
        return DkBlendFactor_SrcAlphaSaturate;
    }
    return DkBlendFactor_One;
}

DkCompareOp MapCompare(Pica::FramebufferRegs::CompareFunc function) {
    using Compare = Pica::FramebufferRegs::CompareFunc;
    switch (function) {
    case Compare::Never:
        return DkCompareOp_Never;
    case Compare::Always:
        return DkCompareOp_Always;
    case Compare::Equal:
        return DkCompareOp_Equal;
    case Compare::NotEqual:
        return DkCompareOp_NotEqual;
    case Compare::LessThan:
        return DkCompareOp_Less;
    case Compare::LessThanOrEqual:
        return DkCompareOp_Lequal;
    case Compare::GreaterThan:
        return DkCompareOp_Greater;
    case Compare::GreaterThanOrEqual:
        return DkCompareOp_Gequal;
    }
    return DkCompareOp_Always;
}

DkLogicOp MapLogicOp(Pica::FramebufferRegs::LogicOp operation) {
    using Logic = Pica::FramebufferRegs::LogicOp;
    switch (operation) {
    case Logic::Clear:
        return DkLogicOp_Clear;
    case Logic::And:
        return DkLogicOp_And;
    case Logic::AndReverse:
        return DkLogicOp_AndReverse;
    case Logic::Copy:
        return DkLogicOp_Copy;
    case Logic::Set:
        return DkLogicOp_Set;
    case Logic::CopyInverted:
        return DkLogicOp_CopyInverted;
    case Logic::NoOp:
        return DkLogicOp_NoOp;
    case Logic::Invert:
        return DkLogicOp_Invert;
    case Logic::Nand:
        return DkLogicOp_Nand;
    case Logic::Or:
        return DkLogicOp_Or;
    case Logic::Nor:
        return DkLogicOp_Nor;
    case Logic::Xor:
        return DkLogicOp_Xor;
    case Logic::Equiv:
        return DkLogicOp_Equivalent;
    case Logic::AndInverted:
        return DkLogicOp_AndInverted;
    case Logic::OrReverse:
        return DkLogicOp_OrReverse;
    case Logic::OrInverted:
        return DkLogicOp_OrInverted;
    }
    return DkLogicOp_Copy;
}

DkStencilOp MapStencilAction(Pica::FramebufferRegs::StencilAction action) {
    using Stencil = Pica::FramebufferRegs::StencilAction;
    switch (action) {
    case Stencil::Keep:
        return DkStencilOp_Keep;
    case Stencil::Zero:
        return DkStencilOp_Zero;
    case Stencil::Replace:
        return DkStencilOp_Replace;
    case Stencil::Increment:
        return DkStencilOp_Incr;
    case Stencil::Decrement:
        return DkStencilOp_Decr;
    case Stencil::Invert:
        return DkStencilOp_Invert;
    case Stencil::IncrementWrap:
        return DkStencilOp_IncrWrap;
    case Stencil::DecrementWrap:
        return DkStencilOp_DecrWrap;
    }
    return DkStencilOp_Keep;
}

DkImageFormat MapDepthFormat(Pica::FramebufferRegs::DepthFormat format) {
    using Depth = Pica::FramebufferRegs::DepthFormat;
    switch (format) {
    case Depth::D16:
        return DkImageFormat_Z16;
    case Depth::D24:
        return DkImageFormat_Z24X8;
    case Depth::D24S8:
        return DkImageFormat_Z24S8;
    }
    return DkImageFormat_None;
}

u32 ColorWriteMask(const Pica::FramebufferRegs& framebuffer) {
    if (framebuffer.framebuffer.allow_color_write == 0) {
        return 0;
    }
    u32 mask = 0;
    if (framebuffer.output_merger.red_enable) {
        mask |= DkColorMask_R;
    }
    if (framebuffer.output_merger.green_enable) {
        mask |= DkColorMask_G;
    }
    if (framebuffer.output_merger.blue_enable) {
        mask |= DkColorMask_B;
    }
    if (framebuffer.output_merger.alpha_enable) {
        mask |= DkColorMask_A;
    }
    return mask;
}
#endif

} // namespace

Rasterizer::Rasterizer(Memory::MemorySystem& memory, Pica::PicaCore& pica, State& state_,
                       TextureCache& texture_cache_, ShaderCache& shader_cache_)
    : RasterizerAccelerated{memory, pica}, state{state_}, texture_cache{texture_cache_},
      shader_cache{shader_cache_} {}

Rasterizer::~Rasterizer() {
    Shutdown();
}

bool Rasterizer::Initialize() {
    if (!state.IsInitialized() || !texture_cache.IsInitialized() || !shader_cache.IsInitialized()) {
        return false;
    }
#ifdef __SWITCH__
    if (!texture_cache.Attach(state, memory) || !InitializeGpuResources()) {
        ShutdownGpuResources();
        return false;
    }
#endif
    initialized = true;
    LOG_INFO(Render, "Deko3D strict native rasterizer initialized");
    return true;
}

void Rasterizer::Shutdown() {
#ifdef __SWITCH__
    if (device || queue) {
        FlushQueueIfNeeded(true);
        state.WaitIdle();
        texture_cache.Shutdown();
        ShutdownGpuResources();
    }
#endif
    initialized = false;
}

#ifdef __SWITCH__
bool Rasterizer::InitializeGpuResources() {
    device = state.GetDevice();
    queue = state.GetQueue();
    if (!device || !queue) {
        return false;
    }

    DkMemBlockMaker command_maker;
    dkMemBlockMakerDefaults(&command_maker, device,
                            AlignUp(RasterCommandMemorySize, DK_MEMBLOCK_ALIGNMENT));
    command_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    command_mem_block = dkMemBlockCreate(&command_maker);

    DkCmdBufMaker command_buffer_maker;
    dkCmdBufMakerDefaults(&command_buffer_maker, device);
    command_buffer = dkCmdBufCreate(&command_buffer_maker);

    DkMemBlockMaker vertex_maker;
    dkMemBlockMakerDefaults(&vertex_maker, device, AlignUp(VertexBufferSize, DK_MEMBLOCK_ALIGNMENT));
    vertex_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    vertex_mem_block = dkMemBlockCreate(&vertex_maker);
    vertex_cpu_buffer = vertex_mem_block ? dkMemBlockGetCpuAddr(vertex_mem_block) : nullptr;
    vertex_gpu_addr = vertex_mem_block ? dkMemBlockGetGpuAddr(vertex_mem_block) : DK_GPU_ADDR_INVALID;

    DkMemBlockMaker uniform_maker;
    dkMemBlockMakerDefaults(&uniform_maker, device,
                            AlignUp(UniformBufferSize, DK_MEMBLOCK_ALIGNMENT));
    uniform_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    uniform_mem_block = dkMemBlockCreate(&uniform_maker);
    uniform_cpu_buffer = uniform_mem_block ? dkMemBlockGetCpuAddr(uniform_mem_block) : nullptr;
    uniform_gpu_addr = uniform_mem_block ? dkMemBlockGetGpuAddr(uniform_mem_block) : DK_GPU_ADDR_INVALID;

    if (!command_mem_block || !command_buffer || !vertex_mem_block || !uniform_mem_block ||
        !vertex_cpu_buffer || !uniform_cpu_buffer || vertex_gpu_addr == DK_GPU_ADDR_INVALID ||
        uniform_gpu_addr == DK_GPU_ADDR_INVALID) {
        return false;
    }

    const u32 command_slice = RasterCommandMemorySize / FrameContextCount;
    const u32 vertex_slice = VertexBufferSize / FrameContextCount;
    const u32 uniform_slice = UniformBufferSize / FrameContextCount;
    for (u32 index = 0; index < FrameContextCount; ++index) {
        auto& context = frame_contexts[index];
        context.command_offset = index * command_slice;
        context.command_size = command_slice;
        context.vertex_offset = index * vertex_slice;
        context.vertex_size = vertex_slice;
        context.uniform_offset = index * uniform_slice;
        context.uniform_size = uniform_slice;
    }
    return true;
}

void Rasterizer::ShutdownGpuResources() {
    if (queue) {
        state.WaitIdle();
    }
    for (auto& surface : depth_surfaces) {
        if (surface->mem) {
            dkMemBlockDestroy(surface->mem);
        }
    }
    depth_surfaces.clear();
    for (auto& source : transfer_sources) {
        if (source->mem) {
            dkMemBlockDestroy(source->mem);
        }
    }
    transfer_sources.clear();
    for (auto& upload : transfer_upload_contexts) {
        if (upload.mem) {
            dkMemBlockDestroy(upload.mem);
        }
        upload = {};
        upload.gpu = DK_GPU_ADDR_INVALID;
    }
    if (command_buffer) {
        dkCmdBufDestroy(command_buffer);
        command_buffer = nullptr;
    }
    if (uniform_mem_block) {
        dkMemBlockDestroy(uniform_mem_block);
        uniform_mem_block = nullptr;
    }
    if (vertex_mem_block) {
        dkMemBlockDestroy(vertex_mem_block);
        vertex_mem_block = nullptr;
    }
    if (command_mem_block) {
        dkMemBlockDestroy(command_mem_block);
        command_mem_block = nullptr;
    }
    uniform_cpu_buffer = nullptr;
    vertex_cpu_buffer = nullptr;
    uniform_gpu_addr = DK_GPU_ADDR_INVALID;
    vertex_gpu_addr = DK_GPU_ADDR_INVALID;
    device = nullptr;
    queue = nullptr;
}

Rasterizer::FrameContext& Rasterizer::CurrentFrameContext() {
    FrameContext& context = frame_contexts[current_frame_context];
    current_frame_context = (current_frame_context + 1) % FrameContextCount;
    return context;
}

bool Rasterizer::WaitForFrameContext(FrameContext& context) {
    if (!context.fence_pending) {
        return true;
    }
    FlushQueueIfNeeded(true);
    RecordRasterFencePoll();
    DkResult result = dkFenceWait(&context.fence, 0);
    if (result == DkResult_Timeout) {
        RecordRingWait();
        RecordRasterFenceWait();
        const auto start = std::chrono::steady_clock::now();
        result = dkFenceWait(&context.fence, 500'000'000LL);
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                                 std::chrono::steady_clock::now() - start)
                                 .count();
        RecordRasterFenceWaitDurationUs(static_cast<u64>(std::max<s64>(elapsed, 0)));
    }
    if (result != DkResult_Success) {
        RecordFenceTimeout();
        RecordRasterFenceTimeout();
        return false;
    }
    RecordFencePollSuccess();
    RecordRasterFencePollSuccess();
    if (context.pending_vertices != 0) {
        RecordHardwareDrawCompleted(context.pending_vertices / 3);
        RecordTransformedBatchCompleted(context.pending_vertices);
    }
    context.pending_vertices = 0;
    context.fence_pending = false;
    return !QueueHasError("after native frame-context wait");
}

bool Rasterizer::IsNativeBatchValid() const {
    return initialized && device && queue && command_buffer && vertex_cpu_buffer &&
           uniform_cpu_buffer && vertex_batch.size() >= 3 && (vertex_batch.size() % 3) == 0 &&
           shader_cache.GetColorVertexShader() && shader_cache.GetColorFragmentShader() &&
           regs.framebuffer.framebuffer.GetColorBufferPhysicalAddress() != 0 &&
           regs.framebuffer.framebuffer.GetWidth() != 0 &&
           regs.framebuffer.framebuffer.GetHeight() != 0 &&
           !regs.framebuffer.IsShadowRendering();
}

Rasterizer::DepthSurface* Rasterizer::GetOrCreateDepthSurface() {
    const auto& framebuffer = regs.framebuffer;
    const bool needs_depth = framebuffer.output_merger.depth_test_enable != 0 ||
                             framebuffer.output_merger.depth_write_enable != 0 ||
                             framebuffer.output_merger.stencil_test.enable != 0;
    if (!needs_depth) {
        return nullptr;
    }
    DepthKey key{
        .address = framebuffer.framebuffer.GetDepthBufferPhysicalAddress(),
        .width = framebuffer.framebuffer.GetWidth(),
        .height = framebuffer.framebuffer.GetHeight(),
        .format = framebuffer.framebuffer.depth_format.Value(),
    };
    if (key.address == 0 || key.width == 0 || key.height == 0) {
        return nullptr;
    }
    for (auto& surface : depth_surfaces) {
        if (surface->key == key) {
            return surface.get();
        }
    }

    const DkImageFormat format = MapDepthFormat(key.format);
    if (format == DkImageFormat_None) {
        return nullptr;
    }
    auto surface = std::make_unique<DepthSurface>();
    surface->key = key;
    DkImageLayoutMaker layout_maker;
    dkImageLayoutMakerDefaults(&layout_maker, device);
    layout_maker.type = DkImageType_2D;
    layout_maker.flags = DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine;
    layout_maker.format = format;
    layout_maker.dimensions[0] = key.width;
    layout_maker.dimensions[1] = key.height;
    DkImageLayout layout{};
    dkImageLayoutInitialize(&layout, &layout_maker);
    const u64 layout_size = dkImageLayoutGetSize(&layout);
    const u32 alignment = dkImageLayoutGetAlignment(&layout);
    if (layout_size == 0 || layout_size > std::numeric_limits<u32>::max()) {
        return nullptr;
    }
    DkMemBlockMaker memory_maker;
    dkMemBlockMakerDefaults(&memory_maker, device,
                            AlignUp(static_cast<u32>(layout_size), alignment));
    memory_maker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
    surface->mem = dkMemBlockCreate(&memory_maker);
    if (!surface->mem) {
        return nullptr;
    }
    dkImageInitialize(&surface->image, &layout, surface->mem, 0);
    dkImageViewDefaults(&surface->view, &surface->image);
    DepthSurface* const result = surface.get();
    depth_surfaces.emplace_back(std::move(surface));
    RecordDepthState(true);
    return result;
}

Rasterizer::FragmentUniforms Rasterizer::BuildFragmentUniforms(u32 texture_enable_mask) const {
    FragmentUniforms uniforms{};
    const auto stages = regs.texturing.GetTevStages();
    for (u32 index = 0; index < stages.size(); ++index) {
        uniforms.stages[index].sources = stages[index].sources_raw;
        uniforms.stages[index].modifiers = stages[index].modifiers_raw;
        uniforms.stages[index].operations = stages[index].ops_raw;
        uniforms.stages[index].scales = stages[index].scales_raw;
        uniforms.constants[index] = {
            stages[index].const_r.Value() / 255.0f,
            stages[index].const_g.Value() / 255.0f,
            stages[index].const_b.Value() / 255.0f,
            stages[index].const_a.Value() / 255.0f,
        };
    }
    uniforms.combiner_buffer = {
        regs.texturing.tev_combiner_buffer_color.r.Value() / 255.0f,
        regs.texturing.tev_combiner_buffer_color.g.Value() / 255.0f,
        regs.texturing.tev_combiner_buffer_color.b.Value() / 255.0f,
        regs.texturing.tev_combiner_buffer_color.a.Value() / 255.0f,
    };
    uniforms.update_mask_rgb = regs.texturing.tev_combiner_buffer_input.update_mask_rgb.Value();
    uniforms.update_mask_alpha = regs.texturing.tev_combiner_buffer_input.update_mask_a.Value();
    uniforms.texture_enable_mask = texture_enable_mask;
    uniforms.alpha_test = static_cast<u32>(regs.framebuffer.output_merger.alpha_test.func.Value());
    uniforms.alpha_reference = regs.framebuffer.output_merger.alpha_test.ref.Value() / 255.0f;
    return uniforms;
}

bool Rasterizer::SubmitHardwareBatch() {
    if (!IsNativeBatchValid()) {
        return false;
    }
    const auto& framebuffer = regs.framebuffer.framebuffer;
    State::RenderTargetKey color_key{
        .color_address = framebuffer.GetColorBufferPhysicalAddress(),
        .width = framebuffer.GetWidth(),
        .height = framebuffer.GetHeight(),
        .format = static_cast<u32>(framebuffer.color_format.Value()),
    };
    State::CachedRenderTarget* const color_target = state.GetOrCreateRenderTarget(color_key);
    if (!color_target) {
        return false;
    }

    DepthSurface* const depth_surface = GetOrCreateDepthSurface();
    const bool depth_required = regs.framebuffer.output_merger.depth_test_enable != 0 ||
                                regs.framebuffer.output_merger.depth_write_enable != 0 ||
                                regs.framebuffer.output_merger.stencil_test.enable != 0;
    if (depth_required && !depth_surface) {
        return false;
    }

    const auto textures = texture_cache.PrepareTextures(regs.texturing);
    const FragmentUniforms fragment_uniforms = BuildFragmentUniforms(textures.enabled_mask);
    RecordTransformedBatchEligible();

    const std::size_t vertices_per_context =
        3 * (frame_contexts[0].vertex_size / (3 * sizeof(HardwareVertex)));
    for (std::size_t base = 0; base < vertex_batch.size(); base += vertices_per_context) {
        const std::size_t count =
            std::min(vertices_per_context, vertex_batch.size() - base) / 3 * 3;
        if (count == 0) {
            return false;
        }
        FrameContext& context = CurrentFrameContext();
        if (!WaitForFrameContext(context) ||
            !SubmitHardwareChunk(context, *color_target,
                                 depth_surface ? &depth_surface->view : nullptr, base, count,
                                 textures.handles, fragment_uniforms)) {
            return false;
        }
    }
    return true;
}

bool Rasterizer::SubmitHardwareChunk(FrameContext& context,
                                     State::CachedRenderTarget& color_target,
                                     const DkImageView* depth_target, std::size_t base_vertex,
                                     std::size_t vertex_count,
                                     const std::array<DkResHandle, 3>& texture_handles,
                                     const FragmentUniforms& fragment_uniforms) {
    const std::size_t vertex_bytes = vertex_count * sizeof(HardwareVertex);
    if (vertex_bytes > context.vertex_size || sizeof(FragmentUniforms) > context.uniform_size) {
        return false;
    }
    std::memcpy(static_cast<u8*>(vertex_cpu_buffer) + context.vertex_offset,
                vertex_batch.data() + base_vertex, vertex_bytes);
    std::memcpy(static_cast<u8*>(uniform_cpu_buffer) + context.uniform_offset, &fragment_uniforms,
                sizeof(fragment_uniforms));

    const DkShader* const shaders[] = {shader_cache.GetColorVertexShader(),
                                       shader_cache.GetColorFragmentShader()};
    const DkVtxAttribState attributes[] = {
        {0, 0, offsetof(HardwareVertex, position), DkVtxAttribSize_4x32, DkVtxAttribType_Float, 0},
        {0, 0, offsetof(HardwareVertex, color), DkVtxAttribSize_4x32, DkVtxAttribType_Float, 0},
        {0, 0, offsetof(HardwareVertex, tex_coord0), DkVtxAttribSize_2x32, DkVtxAttribType_Float, 0},
        {0, 0, offsetof(HardwareVertex, tex_coord1), DkVtxAttribSize_2x32, DkVtxAttribType_Float, 0},
        {0, 0, offsetof(HardwareVertex, tex_coord2), DkVtxAttribSize_2x32, DkVtxAttribType_Float, 0},
    };
    const DkVtxBufferState vertex_buffer_state[] = {{sizeof(HardwareVertex), 0}};

    DkRasterizerState rasterizer_state;
    dkRasterizerStateDefaults(&rasterizer_state);
    using Cull = Pica::RasterizerRegs::CullMode;
    switch (regs.rasterizer.cull_mode.Value()) {
    case Cull::KeepAll:
        rasterizer_state.cullMode = DkFace_None;
        break;
    case Cull::KeepClockWise:
        rasterizer_state.cullMode = DkFace_Back;
        rasterizer_state.frontFace = DkFrontFace_CW;
        break;
    case Cull::KeepCounterClockWise:
        rasterizer_state.cullMode = DkFace_Back;
        rasterizer_state.frontFace = DkFrontFace_CCW;
        break;
    }

    DkMultisampleState multisample_state;
    dkMultisampleStateDefaults(&multisample_state);

    DkColorState color_state;
    dkColorStateDefaults(&color_state);
    dkColorStateSetBlendEnable(&color_state, 0,
                               regs.framebuffer.output_merger.alphablend_enable != 0);
    color_state.logicOp = MapLogicOp(regs.framebuffer.output_merger.logic_op.Value());
    color_state.alphaCompareOp = regs.framebuffer.output_merger.alpha_test.enable != 0
                                     ? MapCompare(regs.framebuffer.output_merger.alpha_test.func.Value())
                                     : DkCompareOp_Always;

    DkColorWriteState color_write_state;
    dkColorWriteStateDefaults(&color_write_state);
    dkColorWriteStateSetMask(&color_write_state, 0, ColorWriteMask(regs.framebuffer));

    DkBlendState blend_state;
    dkBlendStateDefaults(&blend_state);
    const auto blend = regs.framebuffer.output_merger.alpha_blending;
    dkBlendStateSetOps(&blend_state, MapBlendEquation(blend.blend_equation_rgb.Value()),
                       MapBlendEquation(blend.blend_equation_a.Value()));
    dkBlendStateSetFactors(&blend_state, MapBlendFactor(blend.factor_source_rgb.Value()),
                           MapBlendFactor(blend.factor_dest_rgb.Value()),
                           MapBlendFactor(blend.factor_source_a.Value()),
                           MapBlendFactor(blend.factor_dest_a.Value()));

    DkDepthStencilState depth_state;
    dkDepthStencilStateDefaults(&depth_state);
    depth_state.depthTestEnable = regs.framebuffer.output_merger.depth_test_enable != 0;
    depth_state.depthWriteEnable =
        regs.framebuffer.framebuffer.allow_depth_stencil_write != 0 &&
        regs.framebuffer.output_merger.depth_write_enable != 0;
    depth_state.depthCompareOp = MapCompare(regs.framebuffer.output_merger.depth_test_func.Value());
    depth_state.stencilTestEnable = regs.framebuffer.output_merger.stencil_test.enable != 0;
    depth_state.stencilFrontCompareOp =
        MapCompare(regs.framebuffer.output_merger.stencil_test.func.Value());
    depth_state.stencilBackCompareOp = depth_state.stencilFrontCompareOp;
    depth_state.stencilFrontFailOp =
        MapStencilAction(regs.framebuffer.output_merger.stencil_test.action_stencil_fail.Value());
    depth_state.stencilFrontDepthFailOp =
        MapStencilAction(regs.framebuffer.output_merger.stencil_test.action_depth_fail.Value());
    depth_state.stencilFrontPassOp =
        MapStencilAction(regs.framebuffer.output_merger.stencil_test.action_depth_pass.Value());
    depth_state.stencilBackFailOp = depth_state.stencilFrontFailOp;
    depth_state.stencilBackDepthFailOp = depth_state.stencilFrontDepthFailOp;
    depth_state.stencilBackPassOp = depth_state.stencilFrontPassOp;

    const u32 width = color_target.key.width;
    const u32 height = color_target.key.height;
    const DkViewport viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height),
                              0.0f, 1.0f};
    DkScissor scissor{0, 0, width, height};
    if (regs.rasterizer.scissor_test.mode == Pica::RasterizerRegs::ScissorMode::Include) {
        scissor.x = std::min<u32>(regs.rasterizer.scissor_test.x1.Value(), width);
        scissor.y = std::min<u32>(regs.rasterizer.scissor_test.y1.Value(), height);
        const u32 x2 = std::min<u32>(regs.rasterizer.scissor_test.x2.Value() + 1, width);
        const u32 y2 = std::min<u32>(regs.rasterizer.scissor_test.y2.Value() + 1, height);
        scissor.width = x2 > scissor.x ? x2 - scissor.x : 0;
        scissor.height = y2 > scissor.y ? y2 - scissor.y : 0;
    }

    dkCmdBufClear(command_buffer);
    dkCmdBufAddMemory(command_buffer, command_mem_block, context.command_offset,
                      context.command_size);
    dkCmdBufBindRenderTarget(command_buffer, &color_target.view, depth_target);
    dkCmdBufSetViewports(command_buffer, 0, &viewport, 1);
    dkCmdBufSetScissors(command_buffer, 0, &scissor, 1);
    dkCmdBufBindShaders(command_buffer, DkStageFlag_GraphicsMask, shaders, 2);
    dkCmdBufBindRasterizerState(command_buffer, &rasterizer_state);
    dkCmdBufBindMultisampleState(command_buffer, &multisample_state);
    dkCmdBufBindColorState(command_buffer, &color_state);
    dkCmdBufBindColorWriteState(command_buffer, &color_write_state);
    dkCmdBufBindBlendState(command_buffer, 0, &blend_state);
    dkCmdBufBindDepthStencilState(command_buffer, &depth_state);
    dkCmdBufSetAlphaRef(command_buffer, fragment_uniforms.alpha_reference);
    dkCmdBufSetBlendConst(command_buffer,
                          regs.framebuffer.output_merger.blend_const.r.Value() / 255.0f,
                          regs.framebuffer.output_merger.blend_const.g.Value() / 255.0f,
                          regs.framebuffer.output_merger.blend_const.b.Value() / 255.0f,
                          regs.framebuffer.output_merger.blend_const.a.Value() / 255.0f);
    if (depth_state.stencilTestEnable) {
        dkCmdBufSetStencil(command_buffer, DkFace_FrontAndBack,
                           regs.framebuffer.output_merger.stencil_test.write_mask.Value(),
                           regs.framebuffer.output_merger.stencil_test.reference_value.Value(),
                           regs.framebuffer.output_merger.stencil_test.input_mask.Value());
    }
    dkCmdBufBindVtxAttribState(command_buffer, attributes,
                               sizeof(attributes) / sizeof(attributes[0]));
    dkCmdBufBindVtxBufferState(command_buffer, vertex_buffer_state, 1);
    dkCmdBufBindVtxBuffer(command_buffer, 0, vertex_gpu_addr + context.vertex_offset,
                          static_cast<u32>(vertex_bytes));
    dkCmdBufBindUniformBuffer(command_buffer, DkStage_Fragment, 0,
                              uniform_gpu_addr + context.uniform_offset,
                              static_cast<u32>(sizeof(FragmentUniforms)));
    dkCmdBufBindImageDescriptorSet(command_buffer, texture_cache.ImageDescriptorGpuAddress(),
                                    texture_cache.DescriptorCapacity());
    dkCmdBufBindSamplerDescriptorSet(command_buffer, texture_cache.SamplerDescriptorGpuAddress(),
                                      texture_cache.DescriptorCapacity());
    dkCmdBufBindTextures(command_buffer, DkStage_Fragment, 0, texture_handles.data(),
                         static_cast<u32>(texture_handles.size()));
    dkCmdBufDraw(command_buffer, DkPrimitive_Triangles, static_cast<u32>(vertex_count), 1, 0, 0);
    dkCmdBufSignalFence(command_buffer, &context.fence, true);

    const DkCmdList list = dkCmdBufFinishList(command_buffer);
    if (!list) {
        return false;
    }
    dkQueueSubmitCommands(queue, list);
    ++submissions_since_flush;
    RecordRasterQueueSubmit();
    context.fence_pending = true;
    context.pending_vertices = vertex_count;
    state.MarkRenderTargetGpuDirty(color_target);
    state.SelectPresentRenderTarget(color_target.key.color_address);
    RecordHardwareRasterFrame();
    RecordHardwareDrawSubmitted(vertex_count / 3);
    RecordTransformedBatchSubmitted(vertex_count);
    FlushQueueIfNeeded(false);
    return !QueueHasError("after native draw submit");
}

bool Rasterizer::QueueHasError(const char* context) {
    if (queue && dkQueueIsInErrorState(queue)) {
        RecordQueueError();
        RecordRasterQueueError();
        LOG_ERROR(Render, "Deko3D queue error {}", context ? context : "");
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

void Rasterizer::FlushQueueIfNeeded(bool force) {
    if (force || submissions_since_flush >= FlushBatchSize) {
        FlushQueue();
        submissions_since_flush = 0;
    }
}
#endif

void Rasterizer::AddTriangle(const Pica::OutputVertex& v0, const Pica::OutputVertex& v1,
                             const Pica::OutputVertex& v2) {
    RasterizerAccelerated::AddTriangle(v0, v1, v2);
}

void Rasterizer::DrawTriangles() {
    if (vertex_batch.empty()) {
        return;
    }
#ifdef __SWITCH__
    const bool valid = IsNativeBatchValid();
    RecordTransformedBatchCheck(valid);
    RecordHardwareDrawAttempt();
    if (valid && SubmitHardwareBatch()) {
        vertex_batch.clear();
        return;
    }
    RecordHardwareDrawFailure();
#endif
    // Strict native mode: an unsupported state is skipped, never CPU-rasterized.
    vertex_batch.clear();
}

void Rasterizer::FlushAll() {
#ifdef __SWITCH__
    FlushQueueIfNeeded(true);
    texture_cache.FlushAll();
#endif
}

void Rasterizer::FlushRegion(PAddr address, u32 size) {
#ifdef __SWITCH__
    FlushQueueIfNeeded(true);
    texture_cache.FlushRegion(address, size);
#else
    (void)address;
    (void)size;
#endif
}

void Rasterizer::InvalidateRegion(PAddr address, u32 size) {
#ifdef __SWITCH__
    texture_cache.InvalidateRegion(address, size);
    state.InvalidateRenderTargetsOverlapping(address, size, State::SurfaceOwner::CpuMemory);
#else
    (void)address;
    (void)size;
#endif
}

void Rasterizer::FlushAndInvalidateRegion(PAddr address, u32 size) {
    FlushRegion(address, size);
    InvalidateRegion(address, size);
}

void Rasterizer::ClearAll(bool flush) {
    if (flush) {
        FlushAll();
    }
    vertex_batch.clear();
}

bool Rasterizer::AccelerateDrawBatch(bool is_indexed) {
    // The PICA frontend executes the guest vertex program and emits final OutputVertex triangles.
    // DrawTriangles sends those transformed vertices to Deko3D without CPU rasterization.
    (void)is_indexed;
    return false;
}

} // namespace VideoCore::Deko3D
