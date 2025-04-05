module;

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>

export module dreamrender:model;

import glm;
import vulkan_hpp;
import vma;

import :utils;

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

export struct abstract_model {
    virtual ~abstract_model();
    abstract_model() = default;
    abstract_model(const abstract_model&) = delete;
    abstract_model(abstract_model&&) = default;

    abstract_model& operator=(const abstract_model&) = delete;
    abstract_model& operator=(abstract_model&&) = default;

    virtual void create_buffers(std::span<const vertex_data> vertices, std::span<const uint32_t> indices) {
        this->vertexCount = static_cast<int>(vertices.size());
        this->indexCount = static_cast<int>(indices.size());
    };
    [[nodiscard]] virtual std::tuple<vk::Buffer, vk::DeviceSize> get_vertex_buffer() const = 0;
    [[nodiscard]] virtual std::tuple<vk::Buffer, vk::DeviceSize> get_index_buffer() const = 0;

    int indexCount = -1;
    int vertexCount = -1;

    bool loaded = false;

    private:
        std::shared_ptr<std::atomic<loading_state>> state = std::make_shared<std::atomic<loading_state>>(loading_state::none);
        friend class resource_loader;
};

export struct model : public abstract_model
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

    void create_buffers(std::span<const vertex_data> vertices, std::span<const uint32_t> indices) override {
        abstract_model::create_buffers(vertices, indices);

        vk::BufferCreateInfo vertex_info({}, sizeof(vertex_data)*vertexCount,
            vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::SharingMode::eExclusive);
        vk::BufferCreateInfo index_info({}, sizeof(uint32_t)*indexCount,
            vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::SharingMode::eExclusive);
        vma::AllocationCreateInfo alloc_info({}, vma::MemoryUsage::eGpuOnly);

        auto [vb, va] = allocator.createBuffer(vertex_info, alloc_info); vertexBuffer = vb; vertexAllocation = va;
        auto [ib, ia] = allocator.createBuffer(index_info, alloc_info); indexBuffer = ib; indexAllocation = ia;

        for(auto& v : vertices) {
            min = glm::min(min, v.position);
            max = glm::max(max, v.position);
        }
    }
    [[nodiscard]] std::tuple<vk::Buffer, vk::DeviceSize> get_vertex_buffer() const override {
        return {vertexBuffer, 0};
    }
    [[nodiscard]] std::tuple<vk::Buffer, vk::DeviceSize> get_index_buffer() const override {
        return {indexBuffer, 0};
    }

    glm::vec3 min = glm::vec3(std::numeric_limits<float>::max());
    glm::vec3 max = glm::vec3(std::numeric_limits<float>::lowest());
};

}
