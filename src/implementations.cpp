#include <vulkan/vulkan.h>
#if VK_HEADER_VERSION >= 290
#include <vulkan/vulkan.hpp>
namespace vk {
    using vk::detail::resultCheck;
    using vk::detail::createResultValueType;
}
#else
import vulkan_hpp;
#include <vulkan/vulkan_hpp_macros.hpp>
#endif

#if VULKAN_HPP_DISPATCH_LOADER_DYNAMIC == 1
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
#endif

#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include "vk_mem_alloc.hpp"
