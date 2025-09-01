/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <span>
#include <vector>

#ifdef __GNUG__
#include <cxxabi.h>
#endif

#if __linux__
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

export module dreamrender:utils;

import :debug;
import vulkan_hpp;

namespace dreamrender {

export template<std::array src, typename ToType, typename FromType = typename decltype(src)::value_type>
constexpr auto convert() requires (std::integral<FromType> && std::integral<ToType>) {
    static_assert(std::is_same_v<
        std::remove_cv_t<FromType>,
        std::remove_cv_t<typename decltype(src)::value_type>
        >, "Source type must match the type of the input array");

    return std::bit_cast<std::array<ToType, src.size()*sizeof(FromType)/sizeof(ToType)>>(src);
}

// Some constexpr tests
namespace {
    constexpr std::array<uint8_t, 4> test_array1 = {0x12, 0x34, 0x56, 0x78};
    static_assert(convert<test_array1, uint16_t>() == std::array<uint16_t, 2>{0x3412, 0x7856});
    static_assert(convert<test_array1, uint32_t>() == std::array<uint32_t, 1>{0x78563412});
    static_assert(convert<test_array1, uint8_t>() == test_array1);

    constexpr std::array<uint16_t, 2> test_array2 = {0x3412, 0x7856};
    static_assert(convert<test_array2, uint8_t>() == std::array<uint8_t, 4>{0x12, 0x34, 0x56, 0x78});
    static_assert(convert<test_array2, uint32_t>() == std::array<uint32_t, 1>{0x78563412});
    static_assert(convert<test_array2, uint16_t>() == test_array2);
}

export struct gpu_features {
    vk::PhysicalDeviceFeatures features;
    vk::PhysicalDeviceVulkan12Features vulkan12Features;
    vk::PhysicalDeviceLimits limits;
};

export vk::UniqueShaderModule createShader(vk::Device device, std::span<const uint32_t> code) {
    vk::ShaderModuleCreateInfo shader_info({}, code.size()*sizeof(decltype(code)::element_type), code.data());
    vk::UniqueShaderModule shader = device.createShaderModuleUnique(shader_info);
    return std::move(shader);
}
export vk::UniqueShaderModule createShader(vk::Device device, const std::filesystem::path& path) {
    std::ifstream in(path, std::ios_base::binary | std::ios_base::ate);

    size_t size = in.tellg();
    std::vector<uint32_t> buf(size/sizeof(uint32_t));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(buf.data()), size);

    vk::UniqueShaderModule shader = createShader(device, buf);
    debugName(device, shader.get(), "Shader Module \""+path.filename().string()+"\"");
    return shader;
}

export using UniquePipelineMap = std::map<vk::RenderPass, vk::UniquePipeline>;

export UniquePipelineMap createPipelines(
    vk::Device device,
    vk::PipelineCache pipelineCache,
    const vk::GraphicsPipelineCreateInfo& createInfo,
    const std::vector<vk::RenderPass>& renderPasses,
    const std::string& debugName)
{
    std::vector<vk::GraphicsPipelineCreateInfo> createInfos(renderPasses.size(), createInfo);
    createInfos[0].flags |= vk::PipelineCreateFlagBits::eAllowDerivatives;
    createInfos[0].renderPass = renderPasses[0];
    createInfos[0].basePipelineIndex = -1;

    for (size_t i = 1; i < renderPasses.size(); ++i)
    {
        createInfos[i].flags |= vk::PipelineCreateFlagBits::eDerivative;
        createInfos[i].renderPass = renderPasses[i];
        createInfos[i].basePipelineHandle = nullptr;
        createInfos[i].basePipelineIndex = 0;
    }
    auto result = device.createGraphicsPipelinesUnique(
        pipelineCache, createInfos, nullptr);
    if(result.result != vk::Result::eSuccess)
        throw std::runtime_error("Failed to create graphics pipeline(s) (error code: "+to_string(result.result)+")");

    UniquePipelineMap pipelines;
    for (size_t i = 0; i < renderPasses.size(); ++i)
    {
        dreamrender::debugName(device, result.value[i].get(), debugName);
        pipelines[renderPasses[i]] = std::move(result.value[i]);
    }
    return pipelines;
}

export enum class loading_state {
    none,
    queued,
    loading,
    loaded,
    destroyed
};

export template<typename T>
class aligned_wrapper {
    public:
        aligned_wrapper(void* data, std::size_t alignment) :
            data(static_cast<std::byte*>(data)), alignment(alignment) {}

        T& operator[](std::size_t i) {
            // use std::start_lifetime_as, once it is supported
            return *reinterpret_cast<T*>(data + i * aligned(sizeof(T), alignment));
        }

        const T& operator[](std::size_t i) const {
            // use std::start_lifetime_as, once it is supported
            return *reinterpret_cast<const T*>(data + i * aligned(sizeof(T), alignment));
        }

        T* operator+(std::size_t i) {
            // use std::start_lifetime_as, once it is supported
            return reinterpret_cast<T*>(data + i * aligned(sizeof(T), alignment));
        }

        std::size_t offset(std::size_t i) const {
            return i * aligned(sizeof(T), alignment);
        }
    private:
        constexpr static std::size_t aligned(std::size_t size, std::size_t alignment) {
            return (size + alignment - 1) & ~(alignment - 1);
        }

        std::byte* data;
        std::size_t alignment;
};

#ifdef __GNUG__
std::string demangle(const char *name) {
    int status = -4;
    std::unique_ptr<char, void(*)(void*)> res{
        abi::__cxa_demangle(name, nullptr, nullptr, &status),
        std::free
    };
    return (status==0) ? res.get() : name;
}
#else
std::string demangle(const char *name) {
    return std::string(name);
}
#endif

export template <class T>
std::string type_name(const T& t) {
    return demangle(typeid(t).name());
}

bool input_available(int fd) {
#if __linux__
    struct pollfd fds = {.fd = fd, .events = POLLIN, .revents = 0};
    return poll(&fds, 1, 0) == 1 && fds.revents & POLLIN;
#else
    return false;
#endif
}

export void save_bmp(std::span<const char> data, int width, int height, const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary);
    if(!out) {
        throw std::runtime_error("Failed to open file for writing: " + path.string());
    }

    struct [[gnu::packed]] bitmap_info {
        uint32_t biSize{sizeof(bitmap_info)};
        int32_t biWidth{0}; // Width of the bitmap
        int32_t biHeight{0}; // Height of the bitmap
        uint16_t biPlanes{1}; // Number of color planes
        uint16_t biBitCount{32}; // Bits per pixel
        uint32_t biCompression{0}; // No compression
        uint32_t biSizeImage{0}; // Size of the pixel data
        int32_t biXPelsPerMeter{0}; // Horizontal resolution
        int32_t biYPelsPerMeter{0}; // Vertical resolution
        uint32_t biClrUsed{0}; // Number of colors in the palette
        uint32_t biClrImportant{0}; // Important colors
    };

    struct [[gnu::packed]] bitmap_header {
        uint16_t bfType{0x4D42}; // 'BM'
        uint32_t bfSize{0}; // Size of the file
        uint32_t bfReserved{0};
        uint32_t bfOffBits{sizeof(bitmap_header) + sizeof(bitmap_info)}; // Offset to pixel data
    };

    bitmap_header header = {
        .bfSize = static_cast<uint32_t>(data.size() + sizeof(bitmap_header))
    };
    bitmap_info info = {
        .biWidth = width,
        .biHeight = -height, // Note: BMP stores height as negative for top-down
        .biSizeImage = static_cast<uint32_t>(data.size())
    };
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(reinterpret_cast<const char*>(&info), sizeof(info));
    out.write(data.data(), data.size());
}

void terminal_output(std::span<const char> data, vk::Extent2D extent, std::ostream& out) {
    std::cout << "\033[2J\033[0;0H";
#if __linux__
    struct winsize w{};
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
#else
    struct winsize {
        unsigned short ws_row;
        unsigned short ws_col;
        unsigned short ws_xpixel;
        unsigned short ws_ypixel;
    } w = {24, 80, 640, 480};
#endif

    float x_per_c = static_cast<float>(w.ws_xpixel) / static_cast<float>(w.ws_col);
    float y_per_c = static_cast<float>(w.ws_ypixel) / static_cast<float>(w.ws_row);
    float character_ratio = w.ws_xpixel == 0 ? 2.0f : x_per_c / y_per_c;
    character_ratio /= 2.0;

    float fsx = static_cast<float>(extent.width) / w.ws_col;
    float fsy = static_cast<float>(extent.height) / (w.ws_row*2) / character_ratio;
    fsx = std::max(fsx, fsy);
    fsy = fsx * character_ratio;
    int sx = std::ceil(fsx);
    int sy = std::ceil(fsy);

    using color = std::tuple<int, int, int>;

    auto get_color = [&](int x, int y) {
        int tr = 0, tg = 0, tb = 0, t = 0;
        for(int dy=0; dy<sy && y+dy < extent.height; dy++)
        {
            for(int dx=0; dx<sx && x+dx < extent.width; dx++)
            {
                unsigned int i = (y+dy)*extent.width*4 + (x+dx)*4;
                tr += static_cast<unsigned char>(data[i+0]);
                tg += static_cast<unsigned char>(data[i+1]);
                tb += static_cast<unsigned char>(data[i+2]);
                t++;
            }
        }
        if(t == 0) {
            return color{0, 0, 0}; // No pixels, return black
        }
        int r = tr/t, g = tg/t, b = tb/t;
        return color{r, g, b};
    };

    for(int y=0; y<extent.height; y+=2*sy)
    {
        color last_upper = {-1, -1, -1};
        color last_lower = {-1, -1, -1};
        for(int x=0; x<extent.width; x+=sx)
        {
            color upper = get_color(x, y);
            color lower = get_color(x, y+sy);

            if(upper != last_upper) {
                std::cout << std::format("\x1B[48;2;{};{};{}m",
                    std::get<0>(upper), std::get<1>(upper), std::get<2>(upper));
                last_upper = upper;
            }
            if(lower != last_lower) {
                std::cout << std::format("\x1B[38;2;{};{};{}m",
                    std::get<0>(lower), std::get<1>(lower), std::get<2>(lower));
                last_lower = lower;
            }
            std::cout << "\u2584";
        }
        std::cout << "\x1B[0m";
        if(y+sy < extent.height)
            std::cout << '\n';
    }
}

}
