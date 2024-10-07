module;

#include <array>
#include <cstdint>
#include <cstddef>
#include <limits>

export module dreamrender:model;

import glm;
import vulkan_hpp;
import vma;

namespace dreamrender {

struct vertex_data
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;

    static std::array<vk::VertexInputAttributeDescription, 3> attributes(uint32_t binding) {
        return {
            vk::VertexInputAttributeDescription(0, binding, vk::Format::eR32G32B32Sfloat, offsetof(vertex_data, position)),
            vk::VertexInputAttributeDescription(1, binding, vk::Format::eR32G32B32Sfloat, offsetof(vertex_data, normal)),
            vk::VertexInputAttributeDescription(2, binding, vk::Format::eR32G32Sfloat, offsetof(vertex_data, texCoord)),
        };
    }
};

export struct model
{
    model(vk::Device device, vma::Allocator allocator) 
        : device(device), allocator(allocator) {}
    ~model() {
        allocator.destroyBuffer(vertexBuffer, vertexAllocation);
        allocator.destroyBuffer(indexBuffer, indexAllocation);
    }

    vk::Device device;
    vma::Allocator allocator;

    vk::Buffer vertexBuffer;
    vma::Allocation vertexAllocation;
    vk::Buffer indexBuffer;
    vma::Allocation indexAllocation;

    int vertexCount;
    int indexCount;

    bool loaded = false;

    void create_buffers(int vertexCount, int indexCount) {
        this->vertexCount = vertexCount;
        this->indexCount = indexCount;

        vk::BufferCreateInfo vertex_info({}, sizeof(vertex_data)*vertexCount, 
            vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::SharingMode::eExclusive);
        vk::BufferCreateInfo index_info({}, sizeof(uint32_t)*indexCount, 
            vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::SharingMode::eExclusive);
        vma::AllocationCreateInfo alloc_info({}, vma::MemoryUsage::eGpuOnly);

        auto [vb, va] = allocator.createBuffer(vertex_info, alloc_info); vertexBuffer = vb; vertexAllocation = va;
        auto [ib, ia] = allocator.createBuffer(index_info, alloc_info); indexBuffer = ib; indexAllocation = ia;
    }

    glm::vec3 min = glm::vec3(std::numeric_limits<float>::max());
    glm::vec3 max = glm::vec3(std::numeric_limits<float>::lowest());
};

}
