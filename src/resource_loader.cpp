/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;

#include "image_upload.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <istream>
#include <mutex>
#include <memory>
#include <sstream>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

module auroreengine;

import :debug;
import :resource_loader;
import :texture;
import :utils;

import glm;
import sdl2;
import spdlog;
import vulkan_hpp;
import vma;

namespace auroreengine {

    template<class Writer>static void write_to_allocation(vma::Allocator allocator, vma::Allocation allocation, vk::DeviceSize size, Writer writer) {
        if(size == 0) {
            return;
        }

        void* dst = allocator.mapMemory(allocation);
        try {
            writer(std::span<std::uint8_t>{static_cast<std::uint8_t*>(dst),static_cast<std::size_t>(size)});
            allocator.flushAllocation(allocation, 0, size);
        }catch(...) {allocator.unmapMemory(allocation);throw;}
        allocator.unmapMemory(allocation);
    }
    static void upload_to_allocation(vma::Allocator allocator, vma::Allocation allocation, const void* src, vk::DeviceSize size) {
        write_to_allocation(allocator,allocation,size,[src](auto bytes){std::memcpy(bytes.data(),src,bytes.size());});
    }

    struct TextureStaging {
        // The buffer is destroyed before the memory allocation.
        vma::UniqueAllocation allocation;
        vma::UniqueBuffer buffer;
    };

    std::string LoadTask::source_name() const {
        if(std::holds_alternative<std::filesystem::path>(src)) {
            const auto text=std::get<std::filesystem::path>(src).u8string();
            return {text.begin(),text.end()};
        }
        else if(std::holds_alternative<LoaderFunction>(src))
            return "dynamic data";
        else if(std::holds_alternative<LoadDataView>(src))
            return std::format("data at {}", static_cast<const void*>(std::get<LoadDataView>(src).data.data()));
        else
            return "unknown";
    }

    [[gnu::always_inline]] static bool check_state(unsigned int index, LoadTask& task) {
        loading_state state = loading_state::queued;
        if(!task.state->compare_exchange_strong(state, loading_state::loading, std::memory_order_acq_rel, std::memory_order_acquire)) {
            spdlog::debug("[Resource Loader {}] Task {} is already destroyed", index, task.source_name());
            return false;
        }
        return true;
    }

    bool load_texture(
        int index, LoadTask& task, std::mutex& lock,
        vk::Device device, vma::Allocator allocator, vma::Allocation allocation,
        vk::CommandBuffer commandBuffer,
        uint8_t* decodeBuffer, size_t stagingSize, vk::Buffer stagingBuffer,
        std::shared_ptr<void>& stagingOwner)
    {
        std::string name = task.source_name();
        texture* tex = std::get<texture*>(task.dst);
        const auto validate_dimensions=[&](int width,int height) {
            const auto limit=allocator.getPhysicalDeviceProperties()->limits.maxImageDimension2D;
            if(width<=0||height<=0||static_cast<std::uint32_t>(width)>limit||static_cast<std::uint32_t>(height)>limit)
                throw std::invalid_argument("Texture dimensions are zero or exceed this device's native image limit");
            return detail::rgba_upload_layout(static_cast<std::size_t>(width),static_cast<std::size_t>(height),
                static_cast<std::size_t>(width)*4,std::numeric_limits<std::size_t>::max());
        };
        const auto ensure_staging=[&](std::size_t bytes) {
            if(bytes>detail::maximum_texture_upload_bytes)
                throw std::length_error("Native texture upload exceeds the 128 MiB limit");
            if(bytes<=stagingSize)return;
            auto dedicated=std::make_shared<TextureStaging>();
            auto [buffer,memory]=allocator.createBufferUnique(
                vk::BufferCreateInfo({},bytes,vk::BufferUsageFlagBits::eTransferSrc,vk::SharingMode::eExclusive),
                vma::AllocationCreateInfo({},vma::MemoryUsage::eCpuToGpu));
            dedicated->buffer=std::move(buffer);dedicated->allocation=std::move(memory);
            stagingBuffer=dedicated->buffer.get();allocation=dedicated->allocation.get();stagingOwner=std::move(dedicated);
            stagingSize=bytes;
        };
        if(std::holds_alternative<std::filesystem::path>(task.src) ||
            (std::holds_alternative<LoadDataView>(task.src) && std::get<LoadDataView>(task.src).type != "RAW"))
        {
            sdl::unique_surface surface;
            auto upload_transparent_fallback = [&]() {
                if(!check_state(index, task)) {
                    return false;
                } else {
                    std::scoped_lock<std::mutex> l(lock);
                    tex->create_image(1, 1);
                }

                const auto layout=validate_dimensions(tex->width,tex->height);ensure_staging(layout.bytes);
                write_to_allocation(allocator,allocation,layout.bytes,[](auto pixels) {
                    for(std::size_t i=0;i<pixels.size();i+=4) {
                        pixels[i]=pixels[i+1]=pixels[i+2]=0xff;pixels[i+3]=0;
                    }
                });
                return true;
            };

            if(std::holds_alternative<std::filesystem::path>(task.src))
            {
                const auto& path = std::get<std::filesystem::path>(task.src);
                const auto text=path.u8string();
                const std::string utf8(text.begin(),text.end());
                surface = sdl::unique_surface{sdl::image::Load(utf8.c_str())};
            }
            else
            {
                const auto& data = std::get<LoadDataView>(task.src);
                sdl::unique_rwops rwops = sdl::unique_rwops{sdl::RWFromConstMem(data.data.data(), data.data.size())};
                surface = sdl::unique_surface{sdl::image::LoadTyped_RW(rwops.get(), 0, data.type.empty() ? nullptr : data.type.c_str())};
            }
            if(!surface)
            {
                spdlog::error("[Resource Loader {}] Failed to load image {}; using transparent fallback", index, name);
                if(!upload_transparent_fallback())
                    return false;
            }
            else
            {
                bool fallbackUploaded = false;
                validate_dimensions(surface->w,surface->h);
                if(surface->format->format != sdl::PixelFormatEnumVales::RGBA32)
                {
                    sdl::unique_surface newSurface = sdl::unique_surface{sdl::ConvertSurfaceFormat(surface.get(), sdl::PixelFormatEnumVales::RGBA32, 0)};
                    if(!newSurface) {
                        spdlog::error("[Resource Loader {}] Failed to convert image {}; using transparent fallback", index, name);
                        if(!upload_transparent_fallback())
                            return false;
                        fallbackUploaded = true;
                    } else {
                        surface = std::move(newSurface);
                    }
                }

                if(!fallbackUploaded)
                {
                    if(!check_state(index, task))return false;
                    {
                        std::scoped_lock<std::mutex> guard(lock);
                        if(tex->imageView&&(tex->width!=surface->w||tex->height!=surface->h))
                            throw std::invalid_argument("Decoded image dimensions do not match the preallocated texture");
                        tex->create_image(surface->w,surface->h);
                    }
                    if(surface->pitch<=0)throw std::invalid_argument("Decoded image has an invalid row pitch");
                    const auto source_size=static_cast<std::size_t>(surface->pitch)*surface->h;
                    const auto layout=detail::rgba_upload_layout(surface->w,surface->h,surface->pitch,source_size);
                    ensure_staging(layout.bytes);
                    sdl::surface_lock pixels{surface.get()};
                    write_to_allocation(allocator,allocation,layout.bytes,[&](auto destination) {
                        detail::pack_rgba_rows(destination,std::span<const std::uint8_t>{
                            static_cast<const std::uint8_t*>(pixels.pixels()),source_size},layout);
                    });
                }
            }
        }
        else
        {
            name = "dynamic data";
            // Claim the destination before reading its fields or invoking user
            // code; destroying a queued texture must never leave a dangling dst.
            if(!check_state(index,task))return false;
            if(!tex->imageView)throw std::invalid_argument("RAW texture uploads require a preallocated native-size image");
            const auto layout=validate_dimensions(tex->width,tex->height);
            if(auto* raw=std::get_if<LoadDataView>(&task.src)) {
                if(raw->data.size()<layout.bytes)throw std::invalid_argument("RAW image data is shorter than its native texture");
                ensure_staging(layout.bytes);upload_to_allocation(allocator,allocation,raw->data.data(),layout.bytes);
            }else {
                const bool dedicated=layout.bytes>stagingSize;ensure_staging(layout.bytes);
                if(dedicated) {
                    write_to_allocation(allocator,allocation,layout.bytes,[&](auto destination) {
                        std::fill(destination.begin(),destination.end(),0);
                        std::get<LoaderFunction>(task.src)(destination.data(),destination.size());
                    });
                }else {
                    std::fill(decodeBuffer,decodeBuffer+layout.bytes,0);
                    std::get<LoaderFunction>(task.src)(decodeBuffer,stagingSize);
                    upload_to_allocation(allocator,allocation,decodeBuffer,layout.bytes);
                }
            }
        }

        commandBuffer.begin(vk::CommandBufferBeginInfo());

        commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, {}, {}, {},
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
        commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {},
            vk::ImageMemoryBarrier(
                vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead,
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

        auto read_line = [](std::istream& stream, std::string& line) {
            line.clear();

            char c{};
            while(stream.get(c)) {
                if(c == '\n') {
                    return true;
                }
                if(c != '\r') {
                    line.push_back(c);
                }
            }

            return !line.empty();
        };

        std::string line;
        while(read_line(in, line))
        {
            std::istringstream is(line);
            std::string type;
            is >> type;
            if(type=="v") {
                float x{}, y{}, z{};
                is >> x >> y >> z;
                positions.emplace_back(x, y, z);
            } else if(type=="vt") {
                float u{}, v{};
                is >> u >> v;
                texCoords.emplace_back(u, -v);
            } else if(type=="vn") {
                float x{}, y{}, z{};
                is >> x >> y >> z;
                normals.emplace_back(x, y, z);
            } else if(type=="f") {
                std::array<std::string, 3> args;
                is >> args[0] >> args[1] >> args[2];
                for(const auto& a : args) {
                    std::stringstream s(a);
                    int vertex{}, uv{}, normal{};

                    s >> vertex;
                    s.ignore(1);
                    s >> uv;
                    s.ignore(1);
                    s >> normal;

                    cindices.emplace_back(vertex-1, uv-1, normal-1);
                }
            }
        }

        std::vector<std::tuple<int, int, int>> indexCombos;
        for(auto index : cindices) {
            auto p = std::ranges::find(indexCombos, index);
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
        vk::DeviceSize uploadSize = vertexSize + indexSize;
        if(uploadSize > stagingSize) {
            spdlog::error("[Resource Loader {}] Model {} is too large for staging buffer ({} > {} bytes)",
                index, task.source_name(), uploadSize, stagingSize);
            return false;
        }

        void* buf = allocator.mapMemory(allocation);
        if(vertexSize > 0) {
            std::memcpy(static_cast<uint8_t*>(buf) + vertexOffset, vertices.data(), static_cast<std::size_t>(vertexSize));
        }
        if(indexSize > 0) {
            std::memcpy(static_cast<uint8_t*>(buf) + indexOffset, indices.data(), static_cast<std::size_t>(indexSize));
        }
        allocator.flushAllocation(allocation, 0, uploadSize);
        allocator.unmapMemory(allocation);

        commandBuffer.begin(vk::CommandBufferBeginInfo());
        auto [dst_vertex_buffer, dst_vertex_offset] = mesh->get_vertex_buffer();
        auto [dst_index_buffer, dst_index_offset] = mesh->get_index_buffer();

        std::vector<vk::BufferMemoryBarrier> uploadBarriers;
        uploadBarriers.reserve(2);
        if(vertexSize > 0) {
            commandBuffer.copyBuffer(stagingBuffer, dst_vertex_buffer, vk::BufferCopy{vertexOffset, dst_vertex_offset, vertexSize});
            uploadBarriers.emplace_back(
                vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eVertexAttributeRead,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                dst_vertex_buffer, dst_vertex_offset, vertexSize);
        }
        if(indexSize > 0) {
            commandBuffer.copyBuffer(stagingBuffer, dst_index_buffer, vk::BufferCopy{indexOffset, dst_index_offset, indexSize});
            uploadBarriers.emplace_back(
                vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eIndexRead,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                dst_index_buffer, dst_index_offset, indexSize);
        }
        if(!uploadBarriers.empty()) {
            commandBuffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eVertexInput,
                {}, {}, uploadBarriers, {});
        }
        commandBuffer.end();

        // std::string name = task.source_name();
        // debugName(device, dst_vertex_buffer, "Model \""+name+"\" Vertex Buffer"); // TODO: only do this for exclusive buffers
        // debugName(device, dst_index_buffer, "Model \""+name+"\" Index Buffer");
        return true;
    }
}
