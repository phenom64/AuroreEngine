#pragma once
// Backend-neutral queue-family selection and swapchain sharing policy.
// Deliberately free of Vulkan types so it can be unit-tested with synthetic
// device layouts.
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>

namespace auroreengine::detail {

struct queue_family_caps {
    bool graphics = false;
    bool transfer = false;
    bool present = false; // Surface support; ignored (and expected false) without a surface.
};

struct queue_family_selection {
    std::optional<uint32_t> graphics;
    std::optional<uint32_t> present;
    std::optional<uint32_t> transfer; // Dedicated (non-graphics) transfer family, if any.
};

// With a surface: one family supporting both graphics and present is preferred
// (the last such family, matching the historical "last match wins" order, so
// single-family devices are unaffected). Only when no family supports both are
// separate graphics and present families kept; the caller must then share the
// swapchain concurrently (see choose_swapchain_sharing).
// Without a surface: present aliases graphics and surface support is ignored.
// Throws std::runtime_error if no graphics family exists, or if a surface is
// used and no family can present.
inline queue_family_selection select_queue_families(std::span<const queue_family_caps> families, bool uses_surface) {
    queue_family_selection out;
    std::optional<uint32_t> last_graphics, last_present, last_common;
    for(uint32_t i = 0; i < families.size(); ++i) {
        const auto& f = families[i];
        if(f.graphics)
            last_graphics = i;
        else if(f.transfer)
            out.transfer = i;
        if(uses_surface && f.present) {
            last_present = i;
            if(f.graphics)
                last_common = i;
        }
    }
    if(!last_graphics)
        throw std::runtime_error("Selected Vulkan device has no graphics queue family");
    if(!uses_surface) {
        out.graphics = out.present = last_graphics;
        return out;
    }
    if(!last_present)
        throw std::runtime_error("Selected Vulkan device has no queue family that can present to the surface");
    if(last_common) {
        out.graphics = out.present = last_common;
    } else {
        out.graphics = last_graphics;
        out.present = last_present;
    }
    return out;
}

struct swapchain_sharing {
    bool concurrent = false;
    uint32_t count = 0; // 0 for exclusive, otherwise number of unique indices
    std::array<uint32_t, 2> indices{};
};

// Exclusive when one family does both; concurrent over both unique indices otherwise.
inline swapchain_sharing choose_swapchain_sharing(uint32_t graphics, uint32_t present) {
    swapchain_sharing out;
    if(graphics == present)
        return out;
    out.concurrent = true;
    out.count = 2;
    out.indices = {graphics, present};
    return out;
}

}
