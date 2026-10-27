/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;

#include <array>
#include <cstdint>

module auroreengine;

import :utils;
import vulkan_hpp;

namespace auroreengine::shaders {

namespace font_renderer {
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wc23-extensions"
    constexpr char vert_array[] = {
    #embed "shaders/font_renderer.vert.spv"
    };
    constexpr char vert_compat_array[] = {
    #embed "shaders/font_renderer.compat.vert.spv"
    };
    constexpr char geom_array[] = {
    #embed "shaders/font_renderer.geom.spv"
    };
    constexpr char frag_array[] = {
    #embed "shaders/font_renderer.frag.spv"
    };
    #pragma clang diagnostic pop

    constexpr std::array vert_shader = convert<std::to_array(vert_array), uint32_t>();
    constexpr std::array vert_compat_shader = convert<std::to_array(vert_compat_array), uint32_t>();
    constexpr std::array geom_shader = convert<std::to_array(geom_array), uint32_t>();
    constexpr std::array frag_shader = convert<std::to_array(frag_array), uint32_t>();

    vk::UniqueShaderModule vert(vk::Device device) {
        return createShader(device, vert_shader);
    }
    vk::UniqueShaderModule vert_compat(vk::Device device) {
        return createShader(device, vert_compat_shader);
    }
    vk::UniqueShaderModule geom(vk::Device device) {
        return createShader(device, geom_shader);
    }
    vk::UniqueShaderModule frag(vk::Device device) {
        return createShader(device, frag_shader);
    }
}

namespace image_renderer {
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wc23-extensions"
    constexpr char vert_array[] = {
    #embed "shaders/image_renderer.vert.spv"
    };
    constexpr char frag_array[] = {
    #embed "shaders/image_renderer.frag.spv"
    };
    constexpr char frag_compat_array[] = {
    #embed "shaders/image_renderer.compat.frag.spv"
    };
    constexpr char frag_glass_array[] = {
    #embed "shaders/image_renderer.glass.frag.spv"
    };
    constexpr char triangles_vert_array[] = {
    #embed "shaders/image_renderer.triangles.vert.spv"
    };
    constexpr char triangles_batch_vert_array[] = {
    #embed "shaders/image_renderer.triangles.batch.vert.spv"
    };
    constexpr char triangles_frag_array[] = {
    #embed "shaders/image_renderer.triangles.frag.spv"
    };
    constexpr char triangles_frag_compat_array[] = {
    #embed "shaders/image_renderer.triangles.compat.frag.spv"
    };
    #pragma clang diagnostic pop

    constexpr std::array vert_shader = convert<std::to_array(vert_array), uint32_t>();
    constexpr std::array frag_shader = convert<std::to_array(frag_array), uint32_t>();
    constexpr std::array frag_compat_shader = convert<std::to_array(frag_compat_array), uint32_t>();
    constexpr std::array frag_glass_shader = convert<std::to_array(frag_glass_array), uint32_t>();

    vk::UniqueShaderModule vert(vk::Device device) {
        return createShader(device, vert_shader);
    }
    vk::UniqueShaderModule frag(vk::Device device) {
        return createShader(device, frag_shader);
    }
    vk::UniqueShaderModule frag_compat(vk::Device device) {
        return createShader(device, frag_compat_shader);
    }
    vk::UniqueShaderModule frag_glass(vk::Device device) {
        return createShader(device, frag_glass_shader);
    }
    vk::UniqueShaderModule triangles_vert(vk::Device device) {
        constexpr auto code=convert<std::to_array(triangles_vert_array), uint32_t>();
        return createShader(device,code);
    }
    vk::UniqueShaderModule triangles_batch_vert(vk::Device device) {
        constexpr auto code=convert<std::to_array(triangles_batch_vert_array), uint32_t>();
        return createShader(device,code);
    }
    vk::UniqueShaderModule triangles_frag(vk::Device device) {
        constexpr auto code=convert<std::to_array(triangles_frag_array), uint32_t>();
        return createShader(device,code);
    }
    vk::UniqueShaderModule triangles_frag_compat(vk::Device device) {
        constexpr auto code=convert<std::to_array(triangles_frag_compat_array), uint32_t>();
        return createShader(device,code);
    }
}

namespace simple_renderer {
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wc23-extensions"
    constexpr char vert_array[] = {
    #embed "shaders/simple_renderer.vert.spv"
    };
    constexpr char frag_array[] = {
    #embed "shaders/simple_renderer.frag.spv"
    };
    #pragma clang diagnostic pop

    constexpr std::array vert_shader = convert<std::to_array(vert_array), uint32_t>();
    constexpr std::array frag_shader = convert<std::to_array(frag_array), uint32_t>();

    vk::UniqueShaderModule vert(vk::Device device) {
        return createShader(device, vert_shader);
    }
    vk::UniqueShaderModule frag(vk::Device device) {
        return createShader(device, frag_shader);
    }
}

}
