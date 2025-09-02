/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;

#include <cstdint>
#include <future>
#include <vector>

export module dreamrender:phase;

import :resource_loader;

import vulkan_hpp;
import vma;

namespace dreamrender {

export class window;

export class phase
{
    public:
        phase(window*);
        virtual ~phase() {}

        virtual void preload() {
            pool = device.createCommandPoolUnique(vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer, graphicsFamily));
        }
        virtual void prepare(std::vector<vk::Image> swapchainImages, std::vector<vk::ImageView> swapchainViews) {
            const int imageCount = swapchainImages.size();
            commandBuffers = device.allocateCommandBuffers(vk::CommandBufferAllocateInfo(pool.get(), vk::CommandBufferLevel::ePrimary, imageCount));
        }
        virtual void init() {}
        virtual void render(int frame, vk::Semaphore imageAvailable, vk::Semaphore renderFinished, vk::Fence fence) {}

        void waitLoad() {
            for(auto& f : loadingFutures)
            {
                f.wait();
            }
        }
        bool ready() const {
            for(auto& f : loadingFutures)
            {
                if(f.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                {
                    return false;
                }
            }
            return true;
        }
        std::pair<unsigned int, unsigned int> get_progress() const {
            unsigned int total = loadingFutures.size();
            unsigned int done = 0;
            for(auto& f : loadingFutures)
            {
                if(f.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                {
                    done++;
                }
            }
            return {done, total};
        }
        void add_task(std::shared_future<void> f) {
            loadingFutures.push_back(f);
        }
    protected:
        window* win;

        vk::Instance instance;
        vk::Device device;
        vma::Allocator allocator;
        resource_loader* loader;

        uint32_t graphicsFamily;
        vk::Queue graphicsQueue;

        std::vector<std::shared_future<void>> loadingFutures;

        // Managed resources
        vk::UniqueCommandPool pool;
        std::vector<vk::CommandBuffer> commandBuffers;

        // Utility functions
        std::vector<vk::UniqueFramebuffer> createFramebuffers(vk::RenderPass renderPass, const std::vector<vk::ImageView>& swapchainViews, vk::Extent2D extent) const {
            std::vector<vk::UniqueFramebuffer> framebuffers;
            for(auto& view : swapchainViews) {
                vk::FramebufferCreateInfo framebufferInfo({}, renderPass, 1, &view, extent.width, extent.height, 1);
                framebuffers.push_back(device.createFramebufferUnique(framebufferInfo));
            }
            return framebuffers;
        }
        std::vector<vk::UniqueFramebuffer> createFramebuffers(vk::RenderPass renderPass, const std::vector<vk::ImageView>& swapchainViews) const;
        std::vector<vk::UniqueFramebuffer> createFramebuffers(vk::RenderPass renderPass) const;
};

}
