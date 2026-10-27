/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;

#include <vector>

module auroreengine;

import :window;
import :phase;

import vulkan_hpp;

namespace auroreengine {

phase::phase(window* window) :
    win(window),
    instance(window->instance.get()), device(window->device.get()),
    allocator(window->allocator), loader(window->loader.get()),
    graphicsQueue(window->graphicsQueue), graphicsFamily(window->queueFamilyIndices.graphicsFamily.value())
{

}

void phase::submit_graphics(const vk::SubmitInfo& info, vk::Fence fence) {
    win->submitGraphics(info, fence);
}

std::vector<vk::UniqueFramebuffer> phase::createFramebuffers(vk::RenderPass renderPass, const std::vector<vk::ImageView>& swapchainViews) const {
    return createFramebuffers(renderPass, swapchainViews, win->swapchainExtent);
}
std::vector<vk::UniqueFramebuffer> phase::createFramebuffers(vk::RenderPass renderPass) const {
    return createFramebuffers(renderPass, win->swapchainImageViewsRaw, win->swapchainExtent);
}

}
