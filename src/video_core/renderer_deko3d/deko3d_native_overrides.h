// Native Nintendo Switch Deko3D state overrides.
#pragma once

#ifdef __SWITCH__

#include <deque>
#include <memory>
#include <utility>

#include <deko3d.h>

namespace std {

template <typename T, typename Allocator = std::allocator<T>>
class AzaharStableRenderTargetContainer {
public:
    using value_type = T;
    using size_type = typename std::deque<T, Allocator>::size_type;
    using iterator = typename std::deque<T, Allocator>::iterator;
    using const_iterator = typename std::deque<T, Allocator>::const_iterator;

    void push_back(const T& value) {
        values.push_back(value);
        RebindImageView(values.back());
    }

    void push_back(T&& value) {
        values.push_back(std::move(value));
        RebindImageView(values.back());
    }

    T& back() { return values.back(); }
    const T& back() const { return values.back(); }
    iterator begin() noexcept { return values.begin(); }
    const_iterator begin() const noexcept { return values.begin(); }
    iterator end() noexcept { return values.end(); }
    const_iterator end() const noexcept { return values.end(); }
    bool empty() const noexcept { return values.empty(); }
    size_type size() const noexcept { return values.size(); }
    void clear() noexcept { values.clear(); }

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

// DkImageView is initialized from a DkImage address. Keep cache entries stable and rebuild the
// copied view after insertion instead of retaining a pointer to stack or relocated vector storage.
#define vector AzaharStableRenderTargetContainer
#include "video_core/renderer_deko3d/deko3d_state.h"
#undef vector

// Restore the complete queue capabilities which the current backend accidentally replaces with
// Graphics-only: graphics, compute, Z-cull and normal-priority scheduling.
#define DkQueueFlags_Graphics                                                                    \
    (DkQueueFlags_Graphics | DkQueueFlags_Compute | DkQueueFlags_MediumPrio |                  \
     DkQueueFlags_EnableZcull)

#endif // __SWITCH__
