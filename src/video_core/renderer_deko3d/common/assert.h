// Rasterizer-translation-unit policy overlay. Include project declarations before defining the
// relaxed native blocker aliases so no generic macro leaks into unrelated video-core sources.
#pragma once

#include "../../../common/assert.h"
#include "video_core/renderer_deko3d/deko3d_stats.h"

#if defined(__SWITCH__) && defined(AZAHAR_DEKO3D_NATIVE_RUNTIME)

namespace VideoCore::Deko3D {

struct NativeFallbackReasonProxy {
    using Real = FallbackReason;

    constexpr NativeFallbackReasonProxy(Real reason = Real::UnsupportedState) : value{reason} {}
    constexpr operator Real() const { return value; }
    constexpr NativeFallbackReasonProxy& operator=(Real reason) {
        value = reason;
        return *this;
    }

    inline static constexpr Real None = Real::UnsupportedState;
    inline static constexpr Real TexturesEnabled = Real::TexturesEnabled;
    inline static constexpr Real DepthEnabled = Real::DepthEnabled;
    inline static constexpr Real StencilEnabled = Real::StencilEnabled;
    inline static constexpr Real BlendEnabled = Real::BlendEnabled;
    inline static constexpr Real AlphaTest = Real::AlphaTest;
    inline static constexpr Real LogicOp = Real::LogicOp;
    inline static constexpr Real GeometryShader = Real::GeometryShader;
    inline static constexpr Real WrongRenderTarget = Real::WrongRenderTarget;
    inline static constexpr Real FramebufferFormat = Real::FramebufferFormat;
    inline static constexpr Real Topology = Real::Topology;
    inline static constexpr Real Shadow = Real::Shadow;
    inline static constexpr Real UnsupportedState = Real::UnsupportedState;

    Real value;
};

} // namespace VideoCore::Deko3D

// Final transformed PICA vertices are sent to Deko3D even when textures, alpha test, stencil,
// logic-op or depth-write state is present. Those states may be visually incomplete in this first
// native test, but they no longer route the frame through CPU triangle rasterization. Depth-test
// only draws remain rejected until a depth target can always be bound safely.
#define FallbackReason NativeFallbackReasonProxy
#define TexturesEnabled None
#define DepthWriteEnabled None
#define StencilEnabled None
#define AlphaTestUnsupported None
#define LogicOpUnsupported None

#define RecordSoftwareFallback(...) ((void)0)
#define RecordSoftwareRasterFrame(...) ((void)0)
#define MarkRenderTargetSoftwareDirty(...) MarkTopScreenGpuDirty()

#endif
