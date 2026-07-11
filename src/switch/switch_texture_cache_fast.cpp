// SPDX-License-Identifier: GPL-2.0-or-later
// Switch-specific read-only texture cache flush behavior.

#include "video_core/renderer_deko3d/deko3d_texture_cache.h"

#if defined(__SWITCH__) && !defined(AZAHAR_SWITCH_PERF_DIAGNOSTICS)

namespace VideoCore::Deko3D {

void TextureCache::FlushRegion(PAddr address, u32 size) {
    // Deko3D cached textures are decoded copies of guest memory and are never rendered into.
    // A plain flush requests host-to-guest writeback, so there is nothing to do. Actual guest writes
    // arrive through InvalidateRegion/FlushAndInvalidateRegion and still evict overlapping entries.
    (void)address;
    (void)size;
}

} // namespace VideoCore::Deko3D

#endif
