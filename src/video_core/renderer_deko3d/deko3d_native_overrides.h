// Native Switch renderer build overrides.
// This header is force-included only by the deko3d-native-runtime build script.
#pragma once

#ifdef __SWITCH__

#include <deque>
#include <memory>
#include <type_traits>
#include <utility>

#include <deko3d.h>

// The original State stored opaque DkImage objects in std::vector and retained pointers to them.
// Appending a target could relocate all elements. In addition, GetOrCreateRenderTarget initialized
// DkImageView against a stack-local DkImage before copying it into the container. This adapter keeps
// element addresses stable and rebuilds an image view after the object reaches stable storage.
namespace std {
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
        RebindView(values.back());
    }

    void push_back(T&& value) {
        values.push_back(std::move(value));
        RebindView(values.back());
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
    static void RebindView(T& value) {
        if constexpr (requires(T& item) { item.mem_block; item.image; item.view; }) {
            if (value.mem_block) {
                dkImageViewDefaults(&value.view, &value.image);
            }
        }
    }

    std::deque<T, Allocator> values;
};
} // namespace std

// Force the existing State declaration to use the stable adapter without modifying main.
#define vector AzaharStableRenderTargetContainer
#include "video_core/renderer_deko3d/deko3d_state.h"
#undef vector

// Restore the complete default deko3d queue capabilities in the State implementation. The old
// code overwrote the defaults with Graphics-only and disabled compute/Zcull unintentionally.
#ifdef AZAHAR_DEKO3D_STATE_IMPLEMENTATION
#define DkQueueFlags_Graphics                                                                    \
    (DkQueueFlags_Graphics | DkQueueFlags_Compute | DkQueueFlags_MediumPrio |                  \
     DkQueueFlags_EnableZcull)
#endif

#endif // __SWITCH__
