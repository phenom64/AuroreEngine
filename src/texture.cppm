module;

#include <cstdint>
#include <string>

export module dreamrender:texture;

import :debug;

import vulkan_hpp;
import vma;

namespace dreamrender {

export struct texture
{
    texture(vk::Device device, vma::Allocator allocator, int width, int height,
        vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eSampled,
        vk::Format format = vk::Format::eR8G8B8A8Srgb,
        vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e1,
        bool transfer = true, vk::ImageAspectFlags aspects = vk::ImageAspectFlagBits::eColor)
        : device(device), allocator(allocator), width(width), height(height)
    {
        vk::ImageCreateInfo image_info({}, vk::ImageType::e2D, format,
            {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1}, 1, 1,
            sampleCount, vk::ImageTiling::eOptimal,
            usage | (transfer?vk::ImageUsageFlagBits::eTransferDst:vk::ImageUsageFlagBits{}),
            vk::SharingMode::eExclusive);
        vma::AllocationCreateInfo alloc_info({}, vma::MemoryUsage::eGpuOnly);
        std::tie(image, allocation) = allocator.createImage(image_info, alloc_info);

        view_info = vk::ImageViewCreateInfo({}, image, vk::ImageViewType::e2D, format,
        vk::ComponentMapping(), vk::ImageSubresourceRange(aspects, 0, 1, 0, 1));
        imageView = device.createImageViewUnique(view_info);
    }

    texture(vk::Device device, vma::Allocator allocator, vk::Extent2D extent,
        vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eSampled,
        vk::Format format = vk::Format::eR8G8B8A8Srgb,
        vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e1,
        bool transfer = true, vk::ImageAspectFlags aspects = vk::ImageAspectFlagBits::eColor)
        : texture(device, allocator, extent.width, extent.height, usage, format, sampleCount, transfer, aspects) {}

    texture(vk::Device device, vma::Allocator allocator,
        vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eSampled,
        vk::Format format = vk::Format::eR8G8B8A8Srgb,
        vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e1,
        bool transfer = true, vk::ImageAspectFlags aspects = vk::ImageAspectFlagBits::eColor)
        : device(device), allocator(allocator)
    {
        image_info = vk::ImageCreateInfo({}, vk::ImageType::e2D, format,
            {0, 0, 1}, 1, 1,
            sampleCount, vk::ImageTiling::eOptimal,
            usage | (transfer?vk::ImageUsageFlagBits::eTransferDst:vk::ImageUsageFlagBits{}),
            vk::SharingMode::eExclusive);
        view_info = vk::ImageViewCreateInfo({}, image, vk::ImageViewType::e2D, format,
            vk::ComponentMapping(), vk::ImageSubresourceRange(aspects, 0, 1, 0, 1));
    }

    texture(texture&) = delete;
    texture(texture&& other)
        : device(other.device), allocator(other.allocator), image(other.image), allocation(other.allocation),
        width(other.width), height(other.height), imageView(std::move(other.imageView)),
        image_info(other.image_info), view_info(other.view_info)
    {
        other.image = nullptr;
        other.allocation = nullptr;
        other.width = 0;
        other.height = 0;
    }

    ~texture() {
        imageView.reset();
        if(image && allocation) {
            allocator.destroyImage(image, allocation);
        }
    }

    void create_image(int width, int height) {
        if(imageView)
            return;

        this->width = width;
        this->height = height;
        image_info.extent = vk::Extent3D{static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};

        vma::AllocationCreateInfo alloc_info({}, vma::MemoryUsage::eGpuOnly);
        std::tie(image, allocation) = allocator.createImage(image_info, alloc_info);

        view_info.image = image;
        imageView = device.createImageViewUnique(view_info);
    }
    void name(std::string name) {
        debugName(device, image, name+" Image");
        debugName(device, imageView.get(), name+" Image View");
    }
    double aspectRatio() {
        return static_cast<double>(width)/height;
    }

    vk::Device device;
    vma::Allocator allocator;

    vk::Image image;
    vma::Allocation allocation;

    int width;
    int height;

    vk::UniqueImageView imageView;
    bool loaded = false;

    private:
    vk::ImageCreateInfo image_info;
    vk::ImageViewCreateInfo view_info;
};

}
