module;

#include <array>
#include <cstdint>

module dreamrender;

import :utils;
import vulkan_hpp;

namespace dreamrender::shaders {

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
constexpr char font_renderer_vert[] = {
#embed "shaders/font_renderer.vert.spv"
};
constexpr char font_renderer_geom[] = {
#embed "shaders/font_renderer.geom.spv"
};
constexpr char font_renderer_frag[] = {
#embed "shaders/font_renderer.frag.spv"
};
#pragma clang diagnostic pop

namespace font_renderer {
    constexpr std::array vert_shader = convert<std::to_array(font_renderer_vert), uint32_t>();
    constexpr std::array geom_shader = convert<std::to_array(font_renderer_geom), uint32_t>();
    constexpr std::array frag_shader = convert<std::to_array(font_renderer_frag), uint32_t>();

    vk::UniqueShaderModule vert(vk::Device device) {
        return createShader(device, vert_shader);
    }
    vk::UniqueShaderModule geom(vk::Device device) {
        return createShader(device, geom_shader);
    }
    vk::UniqueShaderModule frag(vk::Device device) {
        return createShader(device, frag_shader);
    }
}

}
