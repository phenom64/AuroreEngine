module;

#include <vk_mem_alloc.hpp>

export module vma;

export import vulkan_hpp;
export namespace vma {
    using vma::Allocator;
    using vma::AllocatorCreateInfo;
    using vma::VulkanFunctions;
    using vma::createAllocator;

    using vma::Allocation;
    using vma::AllocationCreateInfo;
    using vma::MemoryUsage;
};
