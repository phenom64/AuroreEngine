module;

#include <vector>

module dreamrender;

import :window;
import :phase;

import vulkan_hpp;

namespace dreamrender {

phase::phase(window* window) :
    win(window),
    instance(window->instance.get()), device(window->device.get()),
    allocator(window->allocator), loader(window->loader.get()),
    graphicsQueue(window->graphicsQueue), graphicsFamily(window->queueFamilyIndices.graphicsFamily.value())
{

}

std::vector<vk::UniqueFramebuffer> phase::createFramebuffers(vk::RenderPass renderPass, const std::vector<vk::ImageView>& swapchainViews) const {
    return createFramebuffers(renderPass, swapchainViews, win->swapchainExtent);
}
std::vector<vk::UniqueFramebuffer> phase::createFramebuffers(vk::RenderPass renderPass) const {
    return createFramebuffers(renderPass, win->swapchainImageViewsRaw, win->swapchainExtent);
}

}
