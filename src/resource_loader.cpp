module;

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <format>
#include <mutex>
#include <variant>
#include <vector>

module dreamrender;

import :debug;
import :resource_loader;
import :texture;
import :utils;

import glm;
import sdl2;
import spdlog;
import vulkan_hpp;
import vma;

namespace dreamrender {

    constexpr unsigned int safe_size = 256;

    std::string LoadTask::source_name() const {
        if(std::holds_alternative<std::filesystem::path>(src))
            return std::get<std::filesystem::path>(src).string();
        else if(std::holds_alternative<LoaderFunction>(src))
            return "dynamic data";
        else if(std::holds_alternative<LoadDataView>(src))
            return std::format("data at {}", static_cast<const void*>(std::get<LoadDataView>(src).data.data()));
        else
            return "unknown";
    }

    [[gnu::always_inline]] static bool check_state(unsigned int index, LoadTask& task) {
        loading_state state = loading_state::queued;
        if(!task.state->compare_exchange_strong(state, loading_state::loading)) {
            spdlog::debug("[Resource Loader {}] Task {} is already destroyed", index, task.source_name());
            return false;
        }
        return true;
    }

    bool load_texture(
        int index, LoadTask& task, std::mutex& lock,
        vk::Device device, vma::Allocator allocator, vma::Allocation allocation,
        vk::CommandBuffer commandBuffer,
        uint8_t* decodeBuffer, size_t stagingSize, vk::Buffer stagingBuffer)
    {
        std::string name = task.source_name();
        texture* tex = std::get<texture*>(task.dst);
        if(std::holds_alternative<std::filesystem::path>(task.src) ||
            (std::holds_alternative<LoadDataView>(task.src) && std::get<LoadDataView>(task.src).type != "RAW"))
        {
            sdl::unique_surface surface;

            if(std::holds_alternative<std::filesystem::path>(task.src))
            {
                const auto& path = std::get<std::filesystem::path>(task.src);
                surface = sdl::unique_surface{sdl::image::Load(path.c_str())};
            }
            else
            {
                const auto& data = std::get<LoadDataView>(task.src);
                sdl::unique_rwops rwops = sdl::unique_rwops{sdl::RWFromConstMem(data.data.data(), data.data.size())};
                surface = sdl::unique_surface{sdl::image::LoadTyped_RW(rwops.get(), 0, data.type.empty() ? nullptr : data.type.c_str())};
            }
            if(!surface)
            {
                spdlog::error("[Resource Loader {}] Failed to load image {}", index, name);
                std::scoped_lock<std::mutex> l(lock);
                if(!check_state(index, task))
                    return false;
                tex->create_image(1, 1); // create fake image to avoid crash
                return false;
            }

            if(surface->format->format != sdl::PixelFormatEnum::SDL_PIXELFORMAT_RGBA32)
            {
                sdl::unique_surface newSurface = sdl::unique_surface{sdl::ConvertSurfaceFormat(surface.get(), SDL_PIXELFORMAT_RGBA32, 0)};
                surface = std::move(newSurface);
            }

            std::size_t size = surface->w * surface->h * surface->format->BytesPerPixel;
            if(size > stagingSize)
            {
                spdlog::warn("[Resource Loader {}] Image {} is too large ({} bytes), scaling it to {}x{}", index, name, size,
                    safe_size, safe_size);
                sdl::unique_surface newSurface = sdl::unique_surface{sdl::CreateRGBSurface(0, safe_size, safe_size, 32, 0, 0, 0, 0)};
                sdl::BlitScaled(surface.get(), nullptr, newSurface.get(), nullptr);
                surface = std::move(newSurface);
                size = surface->w * surface->h * surface->format->BytesPerPixel;
                assert(size <= stagingSize);
            }

            if(!check_state(index, task)) {
                return false;
            } else {
                std::scoped_lock<std::mutex> l(lock);
                tex->create_image(surface->w, surface->h);
            }

            sdl::surface_lock lock{surface.get()};
            allocator.copyMemoryToAllocation(lock.pixels(), allocation, 0, surface->w * surface->h * 4);
        }
        else
        {
            name = "dynamic data";
            std::fill(decodeBuffer, decodeBuffer+stagingSize, 0x00);
            std::get<LoaderFunction>(task.src)(decodeBuffer, stagingSize);
            allocator.copyMemoryToAllocation(decodeBuffer, allocation, 0, stagingSize);
            allocator.flushAllocation(allocation, 0, stagingSize);

            if(!check_state(index, task)) {
                return false;
            } // no need to create the image, because it already needs to be created
        }

        commandBuffer.begin(vk::CommandBufferBeginInfo());

        commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, {}, {}, {},
            vk::ImageMemoryBarrier(
                {}, vk::AccessFlagBits::eTransferWrite,
                vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                tex->image, vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)));
        std::array<vk::BufferImageCopy, 1> copies = {
            vk::BufferImageCopy(0, 0, 0, vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1),
                {}, {static_cast<uint32_t>(tex->width), static_cast<uint32_t>(tex->height), 1})
        };
        commandBuffer.copyBufferToImage(stagingBuffer, tex->image, vk::ImageLayout::eTransferDstOptimal, copies);
        commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, {}, {}, {},
            vk::ImageMemoryBarrier(
                vk::AccessFlagBits::eTransferWrite, {},
                vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                tex->image, vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)));
        commandBuffer.end();

        debugName(device, tex->image, "Texture \""+name+"\"");
        debugName(device, tex->imageView.get(), "Texture \""+name+"\" View");
        return true;
    }

    void load_obj(std::istream &in, std::vector<vertex_data> &vertices, std::vector<uint32_t> &indices)
    {
        std::vector<glm::vec3> positions;
        std::vector<glm::vec3> normals;
        std::vector<glm::vec2> texCoords;
        std::vector<std::tuple<int, int, int>> cindices;

        std::string line;
        while(std::getline(in, line))
        {
            std::istringstream is(line);
            std::string type;
            is >> type;
            if(type=="v") {
                float x, y, z;
                is >> x >> y >> z;
                positions.push_back({x, y, z});
            } else if(type=="vt") {
                float u, v;
                is >> u >> v;
                texCoords.push_back({u, -v});
            } else if(type=="vn") {
                float x, y, z;
                is >> x >> y >> z;
                normals.push_back({x, y, z});
            } else if(type=="f") {
                std::array<std::string, 3> args;
                is >> args[0] >> args[1] >> args[2];
                for(int i=0; i<3; i++) {
                    std::stringstream s(args[i]);
                    int vertex, uv, normal;

                    s >> vertex;
                    s.ignore(1);
                    s >> uv;
                    s.ignore(1);
                    s >> normal;

                    cindices.push_back({vertex-1, uv-1, normal-1});
                }
            }
        }

        std::vector<std::tuple<int, int, int>> indexCombos;
        for(int i=0; i<cindices.size(); i++) {
            auto index = cindices[i];
            auto p = std::find(indexCombos.begin(), indexCombos.end(), index);
            if(p == indexCombos.end()) {
                indices.push_back(vertices.size());
                auto [pos, tex, nor] = index;
                vertices.push_back({positions[pos], normals[nor], texCoords[tex]});
                indexCombos.push_back(index);
            } else {
                indices.push_back(std::distance(indexCombos.begin(), p));
            }
        }
    }

    bool load_model(
        int index, LoadTask& task,
        vk::Device device, vma::Allocator allocator, vma::Allocation allocation,
        vk::CommandBuffer commandBuffer,
        size_t stagingSize, vk::Buffer stagingBuffer)
    {
        std::ifstream obj(std::get<std::filesystem::path>(task.src));
        std::vector<vertex_data> vertices;
        std::vector<uint32_t> indices;
        load_obj(obj, vertices, indices);

        if(!check_state(index, task)) {
            return false;
        }

        abstract_model* mesh = std::get<abstract_model*>(task.dst);
        mesh->create_buffers(vertices, indices);

        vk::DeviceSize vertexOffset = 0;
        vk::DeviceSize vertexSize = vertices.size() * sizeof(vertex_data);
        vk::DeviceSize indexOffset = vertexSize;
        vk::DeviceSize indexSize = indices.size() * sizeof(uint32_t);

        void* buf = allocator.mapMemory(allocation);
        std::ranges::copy(vertices, (vertex_data*)((uint8_t*)buf+vertexOffset));
        std::ranges::copy(indices, (uint32_t*)((uint8_t*)buf+indexOffset));
        allocator.unmapMemory(allocation);

        commandBuffer.begin(vk::CommandBufferBeginInfo());
        auto [dst_vertex_buffer, dst_vertex_offset] = mesh->get_vertex_buffer();
        auto [dst_index_buffer, dst_index_offset] = mesh->get_index_buffer();

        commandBuffer.copyBuffer(stagingBuffer, dst_vertex_buffer, vk::BufferCopy{vertexOffset, dst_vertex_offset, vertexSize});
        commandBuffer.copyBuffer(stagingBuffer, dst_index_buffer, vk::BufferCopy{indexOffset, dst_index_offset, indexSize});
        commandBuffer.end();

        std::string name = task.source_name();
        debugName(device, dst_vertex_buffer, "Model \""+name+"\" Vertex Buffer"); // TODO: only do this for exclusive buffers
        debugName(device, dst_index_buffer, "Model \""+name+"\" Index Buffer");
        return true;
    }
}
