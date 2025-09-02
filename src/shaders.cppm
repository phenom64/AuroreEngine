/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;

export module dreamrender:shaders;

import vulkan_hpp;

export namespace dreamrender::shaders {

namespace font_renderer {
    vk::UniqueShaderModule vert(vk::Device device);
    vk::UniqueShaderModule vert_compat(vk::Device device);
    vk::UniqueShaderModule geom(vk::Device device);
    vk::UniqueShaderModule frag(vk::Device device);
}

namespace image_renderer {
    vk::UniqueShaderModule vert(vk::Device device);
    vk::UniqueShaderModule frag(vk::Device device);
    vk::UniqueShaderModule frag_compat(vk::Device device);
    vk::UniqueShaderModule frag_glass(vk::Device device);
}

namespace simple_renderer {
    vk::UniqueShaderModule vert(vk::Device device);
    vk::UniqueShaderModule frag(vk::Device device);
}

}
