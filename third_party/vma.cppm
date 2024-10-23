module;

#include <utility>
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
