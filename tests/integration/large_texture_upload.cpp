#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#undef main
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

import auroreengine;
import vulkan_hpp;
import vma;

namespace {
constexpr std::uint32_t width=2305,height=2049;
constexpr std::size_t bytes=static_cast<std::size_t>(width)*height*4;
static_assert(bytes>16*1024*1024);
std::array<std::uint8_t,4> pattern(std::uint32_t x,std::uint32_t y) {
    return {static_cast<std::uint8_t>((x*13+y*3)&255),static_cast<std::uint8_t>((x*7+y*11)&255),
            static_cast<std::uint8_t>((x^y)&255),static_cast<std::uint8_t>(128+(x+y)%128)};
}
void fill(std::uint8_t* pixels,std::size_t pitch) {
    for(std::uint32_t y=0;y<height;++y)for(std::uint32_t x=0;x<width;++x) {
        const auto value=pattern(x,y);std::copy(value.begin(),value.end(),pixels+static_cast<std::size_t>(y)*pitch+x*4);
    }
}
void verify(auroreengine::window& window,auroreengine::texture& texture) {
    if(!texture.loaded||texture.width!=static_cast<int>(width)||texture.height!=static_cast<int>(height))
        throw std::runtime_error("Large decoded image lost its native non-square dimensions");
    auto [buffer,allocation]=window.allocator.createBufferUnique(vk::BufferCreateInfo({},bytes,vk::BufferUsageFlagBits::eTransferDst),
        vma::AllocationCreateInfo({},vma::MemoryUsage::eGpuToCpu));
    auto pool=window.device->createCommandPoolUnique(vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eTransient,window.queueFamilyIndices.graphicsFamily.value()));
    auto commands=window.device->allocateCommandBuffersUnique(vk::CommandBufferAllocateInfo(pool.get(),vk::CommandBufferLevel::ePrimary,1));
    auto command=commands.front().get();auto fence=window.device->createFenceUnique(vk::FenceCreateInfo());
    const auto ignored=std::numeric_limits<std::uint32_t>::max();
    command.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
    command.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,vk::PipelineStageFlagBits::eTransfer,{}, {}, {},
        vk::ImageMemoryBarrier(vk::AccessFlagBits::eMemoryWrite,vk::AccessFlagBits::eTransferRead,
            vk::ImageLayout::eShaderReadOnlyOptimal,vk::ImageLayout::eTransferSrcOptimal,ignored,ignored,texture.image,
            vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor,0,1,0,1)));
    command.copyImageToBuffer(texture.image,vk::ImageLayout::eTransferSrcOptimal,buffer.get(),
        vk::BufferImageCopy(0,0,0,vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor,0,0,1),{},vk::Extent3D(width,height,1)));
    command.end();window.submitGraphics(vk::SubmitInfo({}, {},command,{}),fence.get());
    if(window.device->waitForFences(fence.get(),true,std::numeric_limits<std::uint64_t>::max())!=vk::Result::eSuccess)
        throw std::runtime_error("Large texture readback fence did not complete");
    std::vector<std::uint8_t> pixels(bytes);
    if(window.allocator.copyAllocationToMemory(allocation.get(),0,pixels.data(),bytes)!=vk::Result::eSuccess)
        throw std::runtime_error("Large texture GPU readback failed");
    for(std::uint32_t y=0;y<height;++y)for(std::uint32_t x=0;x<width;++x) {
        const auto expected=pattern(x,y);const auto offset=(static_cast<std::size_t>(y)*width+x)*4;
        if(!std::equal(expected.begin(),expected.end(),pixels.begin()+offset))
            throw std::runtime_error("Large texture GPU pixel mismatch at "+std::to_string(x)+","+std::to_string(y));
    }
}
}

int main() {
    try {
#if defined(_WIN32)
        char* mode{};std::size_t length{};
        if(_dupenv_s(&mode,&length,"AUROREENGINE_HEADLESS")!=0)throw std::runtime_error("Cannot inspect headless override");
        const bool disabled=mode&&std::string_view(mode)=="0";std::free(mode);
        if(disabled)throw std::runtime_error("Unset AUROREENGINE_HEADLESS=0 before this no-focus fixture");
#else
        if(const auto* mode=std::getenv("AUROREENGINE_HEADLESS");mode&&std::string_view(mode)=="0")
            throw std::runtime_error("Unset AUROREENGINE_HEADLESS=0 before this no-focus fixture");
#endif
        auroreengine::window_config config;config.name="large-texture-upload-contract";config.headless=true;
        config.width=8;config.height=8;config.headless_output_dir.clear();auroreengine::window window{config};window.init();
        const auto directory=std::filesystem::temp_directory_path()/("aurore-large-texture-"+
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        if(!std::filesystem::create_directory(directory))throw std::runtime_error("Could not create isolated PNG fixture directory");
        const auto path=directory/std::filesystem::path(std::u8string(u8"native-\u03bb-image.png"));
        const auto filename=path.u8string();
        using Surface=std::unique_ptr<SDL_Surface,decltype(&SDL_FreeSurface)>;
        Surface surface{SDL_CreateRGBSurfaceWithFormat(0,width,height,32,SDL_PIXELFORMAT_RGBA32),SDL_FreeSurface};
        if(!surface||SDL_LockSurface(surface.get())!=0)throw std::runtime_error("Could not create the PNG source surface");
        fill(static_cast<std::uint8_t*>(surface->pixels),static_cast<std::size_t>(surface->pitch));SDL_UnlockSurface(surface.get());
        if(IMG_SavePNG(surface.get(),reinterpret_cast<const char*>(filename.c_str()))!=0)
            throw std::runtime_error("PNG fixture encoding failed; install the SDL2_image PNG codec");
        surface.reset();
        const auto usage=vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc;
        {
            auroreengine::texture texture(window.device.get(),window.allocator,usage);
            window.loader->loadTexture(&texture,path).get();verify(window,texture);
        }
        std::ifstream input(path,std::ios::binary);
        std::vector<std::uint8_t> encoded{std::istreambuf_iterator<char>(input),{}};
        if(encoded.empty())throw std::runtime_error("PNG fixture could not be read");
        {
            auroreengine::texture texture(window.device.get(),window.allocator,usage);
            window.loader->loadTexture(&texture,auroreengine::LoadDataView{encoded,"PNG"}).get();verify(window,texture);
        }
        {
            auroreengine::texture texture(window.device.get(),window.allocator,width,height,usage);
            window.loader->loadTexture(&texture,auroreengine::LoaderFunction{[](std::uint8_t* target,std::size_t capacity) {
                if(capacity<bytes)throw std::runtime_error("Generated image capacity was truncated to the small staging buffer");
                fill(target,static_cast<std::size_t>(width)*4);
            }}).get();verify(window,texture);
        }
        {
            std::vector<std::uint8_t> raw(bytes);fill(raw.data(),static_cast<std::size_t>(width)*4);
            auroreengine::texture texture(window.device.get(),window.allocator,width,height,usage);
            window.loader->loadTexture(&texture,auroreengine::LoadDataView{raw,"RAW"}).get();verify(window,texture);
        }
        window.waitIdle();
        std::cout<<"Native "<<width<<'x'<<height<<" PNG path/memory/generated/RAW GPU fidelity passed; fixture: "
                 <<std::string(filename.begin(),filename.end())<<'\n';
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
