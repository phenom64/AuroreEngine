module;

export module dreamrender:shaders;

import vulkan_hpp;

export namespace dreamrender::shaders {

namespace font_renderer {
    vk::UniqueShaderModule vert(vk::Device device);
    vk::UniqueShaderModule geom(vk::Device device);
    vk::UniqueShaderModule frag(vk::Device device);
}

namespace image_renderer {
    vk::UniqueShaderModule vert(vk::Device device);
    vk::UniqueShaderModule frag(vk::Device device);
    vk::UniqueShaderModule frag_compat(vk::Device device);
}

namespace simple_renderer {
    vk::UniqueShaderModule vert(vk::Device device);
    vk::UniqueShaderModule frag(vk::Device device);
}

}
