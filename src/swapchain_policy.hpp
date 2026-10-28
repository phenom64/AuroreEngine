/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include <vulkan/vulkan_core.h>
#include <stdexcept>
#include <initializer_list>
namespace auroreengine::detail {
inline VkCompositeAlphaFlagBitsKHR choose_composite_alpha(VkCompositeAlphaFlagsKHR supported) {
    for(const auto mode:{VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                         VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
        if(supported&mode)return mode;
    throw std::runtime_error("Surface exposes no supported composite alpha mode");
}
}
