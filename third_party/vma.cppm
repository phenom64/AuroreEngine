module;

#include <utility>

#include <vulkan/vulkan.h>
#if VK_HEADER_VERSION >= 290
#include <vulkan/vulkan.hpp>
namespace vk {
    using vk::detail::resultCheck;
    using vk::detail::createResultValueType;
}
#endif

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

    using vma::UniqueBuffer;
    using vma::UniqueImage;
    using vma::UniqueAllocation;
    using vma::UniqueAllocator;

    using vma::VirtualBlock;
    using vma::VirtualBlockCreateInfo;
    using vma::VirtualBlockCreateFlags;
    using vma::VirtualBlockCreateFlagBits;
    using vma::UniqueVirtualBlock;
    using vma::VirtualAllocation;
    using vma::VirtualAllocationCreateInfo;
    using vma::VirtualAllocationCreateFlags;
    using vma::VirtualAllocationCreateFlagBits;
    using vma::UniqueVirtualAllocation;

    class MemoryMapping {
        public:
            MemoryMapping(Allocator allocator, Allocation allocation) : allocator(allocator), allocation(allocation) {
                ptr = allocator.mapMemory(allocation);
            }
            ~MemoryMapping() {
                if(allocator && allocation && ptr) {
                    allocator.unmapMemory(allocation);
                }
            }

            MemoryMapping(const MemoryMapping&) = delete;
            MemoryMapping(MemoryMapping&& other) :
                allocator(std::exchange(other.allocator, Allocator{})),
                allocation(std::exchange(other.allocation, Allocation{})),
                ptr(std::exchange(other.ptr, nullptr)) {}

            MemoryMapping& operator=(const MemoryMapping&) = delete;
            MemoryMapping& operator=(MemoryMapping&& other) {
                allocator = std::exchange(other.allocator, Allocator{});
                allocation = std::exchange(other.allocation, Allocation{});
                ptr = std::exchange(other.ptr, nullptr);
                return *this;
            }

            operator void*() {
                return ptr;
            }
            operator const void*() const {
                return ptr;
            }

            void* get() {
                return ptr;
            }
            const void* get() const {
                return ptr;
            }
        private:
            Allocator allocator;
            Allocation allocation;
            void* ptr;
    };
};
