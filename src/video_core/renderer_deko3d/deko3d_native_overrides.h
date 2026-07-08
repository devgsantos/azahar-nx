// Native Nintendo Switch Deko3D runtime overrides.
#pragma once

#ifdef __SWITCH__

#include <deque>
#include <memory>
#include <utility>

#include <deko3d.h>

namespace std {

// State::CachedRenderTarget contains opaque DkImage/DkImageView objects whose addresses are used by
// command recording and presentation. std::vector relocation is therefore unsafe. This container
// retains the interface used by State while storing entries in std::deque, whose element addresses
// remain stable when entries are appended. A copied image view is rebound to the image at its final
// address immediately after insertion.
template <typename T, typename Allocator = std::allocator<T>>
class AzaharStableRenderTargetContainer {
public:
    using value_type = T;
    using size_type = typename std::deque<T, Allocator>::size_type;
    using iterator = typename std::deque<T, Allocator>::iterator;
    using const_iterator = typename std::deque<T, Allocator>::const_iterator;

    AzaharStableRenderTargetContainer() = default;

    void push_back(const T& value) {
        values.push_back(value);
        RebindImageView(values.back());
    }

    void push_back(T&& value) {
        values.push_back(std::move(value));
        RebindImageView(values.back());
    }

    T& back() {
        return values.back();
    }

    const T& back() const {
        return values.back();
    }

    iterator begin() noexcept {
        return values.begin();
    }

    const_iterator begin() const noexcept {
        return values.begin();
    }

    iterator end() noexcept {
        return values.end();
    }

    const_iterator end() const noexcept {
        return values.end();
    }

    bool empty() const noexcept {
        return values.empty();
    }

    size_type size() const noexcept {
        return values.size();
    }

    void clear() noexcept {
        values.clear();
    }

private:
    static void RebindImageView(T& value) {
        if constexpr (requires(T& item) { item.mem_block; item.image; item.view; }) {
            if (value.mem_block) {
                dkImageViewDefaults(&value.view, &value.image);
            }
        }
    }

    std::deque<T, Allocator> values;
};

} // namespace std

// Include State once with stable render-target storage. The original header uses std::vector only
// for the render-target cache, so the substitution is deliberately scoped to this include.
#define vector AzaharStableRenderTargetContainer
#include "video_core/renderer_deko3d/deko3d_state.h"
#undef vector

// State::CreateQueue currently overwrites deko3d's defaults with Graphics-only. Restore all host
// capabilities required by the native backend: graphics, compute and Z-cull at normal priority.
#ifdef AZAHAR_DEKO3D_STATE_IMPLEMENTATION
#define DkQueueFlags_Graphics                                                                    \
    (DkQueueFlags_Graphics | DkQueueFlags_Compute | DkQueueFlags_MediumPrio |                  \
     DkQueueFlags_EnableZcull)
#endif

#endif // __SWITCH__
