// Deko3D-native overlay injected only for renderer_deko3d translation units that request
// common/assert.h. Include the real project header first.
#pragma once
#include "../../../common/assert.h"

#if defined(__SWITCH__) && defined(AZAHAR_DEKO3D_NATIVE_RUNTIME)

// Undo the first-stage policy aliases installed by src/unordered_set so we can provide a proxy for
// scoped FallbackReason values as well as the Rasterizer eligibility bit names.
#undef TexturesEnabled
#undef DepthTestEnabled
#undef DepthWriteEnabled
#undef StencilEnabled
#undef AlphaTestUnsupported
#undef LogicOpUnsupported

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

#define FallbackReason NativeFallbackReasonProxy
#define TexturesEnabled None
#define DepthTestEnabled None
#define DepthWriteEnabled None
#define StencilEnabled None
#define AlphaTestUnsupported None
#define LogicOpUnsupported None

#endif
