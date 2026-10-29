/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;

#if defined(__unix__) || defined(__APPLE__)
#include <termios.h>
#include <unistd.h> // For STDIN_FILENO
#endif
#if defined(_WIN32) && !defined(STDIN_FILENO)
#define STDIN_FILENO 0
#endif

#include "swapchain_policy.hpp"
#include "queue_family_policy.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iterator>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#ifdef SDL_PIXELFORMAT_RGBA32
#undef SDL_PIXELFORMAT_RGBA32
#endif
#ifdef SDL_WINDOW_FULLSCREEN
#undef SDL_WINDOW_FULLSCREEN
#endif
#ifdef SDL_WINDOW_FULLSCREEN_DESKTOP
#undef SDL_WINDOW_FULLSCREEN_DESKTOP
#endif
#ifdef SDL_WINDOW_SHOWN
#undef SDL_WINDOW_SHOWN
#endif
#ifdef SDL_WINDOW_VULKAN
#undef SDL_WINDOW_VULKAN
#endif
#include <SDL2/SDL_video.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_hpp_macros.hpp>

export module auroreengine:window;

import :resource_loader;
import :phase;
import :input;
import :utils;

import sdl2;
import vulkan_hpp;
import vma;

namespace auroreengine {

static vk::Bool32 debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    vk::DebugUtilsMessageSeverityFlagBitsEXT severity = (vk::DebugUtilsMessageSeverityFlagBitsEXT) messageSeverity;
    vk::DebugUtilsMessageTypeFlagsEXT type(messageType);

    if(type==vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral &&
        severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose &&
        pCallbackData->messageIdNumber == 0)
        return VK_FALSE;

    switch(severity)
    {
        case vk::DebugUtilsMessageSeverityFlagBitsEXT::eError:
            spdlog::error("[Vulkan Validation] {}", pCallbackData->pMessage);
            break;
        case vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo:
            spdlog::info("[Vulkan Validation] {}", pCallbackData->pMessage);
            break;
        case vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning:
            spdlog::warn("[Vulkan Validation] {}", pCallbackData->pMessage);
            break;
        case vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose:
            spdlog::debug("[Vulkan Validation] {}", pCallbackData->pMessage);
            break;
    }

    return VK_FALSE;
}

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;
    std::optional<uint32_t> transferFamily;

    bool isComplete()
    {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

struct SwapChainSupportDetails {
    vk::SurfaceCapabilitiesKHR capabilities;
    std::vector<vk::SurfaceFormatKHR> formats;
    std::vector<vk::PresentModeKHR> presentModes;
};

export struct window_config {
    std::string title;
    unsigned int display_index = 0;
    unsigned int x = -1;
    unsigned int y = -1;
    unsigned int width = -1;
    unsigned int height = -1;
    bool fullscreen = true;
    std::map<std::string, std::string> sdl_hints{};

    bool headless = false;
    bool headless_terminal = false;
    std::filesystem::path headless_output_dir = "output";
    std::string headless_output_format = "{:05d}.jpg";
    int headless_output_quality = 90;
    int headless_frames = -1;

    std::string name;
    int version = 1;

    std::optional<unsigned int> device_index;
    std::optional<std::string> device_name;
    std::optional<std::string> device_uuid;

    vk::PresentModeKHR preferredPresentMode = vk::PresentModeKHR::eFifoRelaxed;
    int fpsLimit = -1;
    vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e1;
    bool profileFrames = false;
    uint64_t profileFrameInterval = 120;

    bool workaround_no_swapchain = false;
};

// Borrowed, tightly packed BGRA8 sRGB pixels. The callback is synchronous on
// the window owner thread after GPU completion; bytes must not be retained.
// RGB contains the renderer's linear-premultiplied color, encoded by sRGB.
export struct OffscreenFrame {
    std::span<const std::uint8_t> bytes;
    std::uint32_t width{},height{},row_stride{};
    vk::Format format{vk::Format::eB8G8R8A8Srgb};
};

static std::filesystem::path env_path(const char* name) {
    const char* value = std::getenv(name);
    if(value && *value) {
        return value;
    }
    return {};
}

static std::filesystem::path get_cache_dir() {
#if defined(_WIN32)
    if(auto path = env_path("LOCALAPPDATA"); !path.empty()) {
        return path / "AuroreEngine" / "Cache";
    }
    if(auto path = env_path("APPDATA"); !path.empty()) {
        return path / "AuroreEngine" / "Cache";
    }
#elif defined(__APPLE__)
    if(auto path = env_path("HOME"); !path.empty()) {
        return path / "Library" / "Caches" / "auroreengine";
    }
#elif defined(__linux__)
    if(auto path = env_path("XDG_CACHE_HOME"); !path.empty()) {
        return path / "auroreengine";
    }
    if(auto path = env_path("HOME"); !path.empty()) {
        return path / ".cache" / "auroreengine";
    }
#else
    if(auto path = env_path("HOME"); !path.empty()) {
        return path / ".cache" / "auroreengine";
    }
#endif
    return std::filesystem::temp_directory_path() / "auroreengine";
}

static bool env_truthy(const char* name) {
    const char* value = std::getenv(name);
    if(!value) {
        return false;
    }
    std::string_view sv{value};
    return sv == "1" || sv == "true" || sv == "TRUE" || sv == "on" || sv == "ON" || sv == "yes" || sv == "YES";
}

export class window
{
    public:
        window(window_config config) : config(std::move(config)) {}
        window(window_config&& config) : config(std::move(config)) {}
        ~window() {
            spdlog::debug("Destroying window");

            std::scoped_lock lock(renderLock);

            // Drain uploads while their renderer-owned destinations are alive.
            // Keep the loader itself alive until phase destructors finish:
            // components may wait for their outstanding upload during teardown.
            if(loader) loader->waitIdle();

            if(device) {
                std::scoped_lock queueGuard(queueLock);
                try {
                    device->waitIdle();
                } catch(const std::exception& e) {
                    spdlog::warn("Failed to wait for device idle during shutdown: {}", e.what());
                }

                if(pipelineCache) {
                    try {
                        auto data = device->getPipelineCacheData(pipelineCache.get());
                        spdlog::debug("Saving pipeline cache of {} bytes", data.size()*sizeof(decltype(data)::value_type));
                        auto path = get_cache_dir() / config.name / "pipeline_cache.bin";

                        if(!std::filesystem::exists(path.parent_path()))
                            std::filesystem::create_directories(path.parent_path());

                        std::ofstream out(path, std::ios::binary);
                        if(out) {
                            std::ranges::copy(data, std::ostream_iterator<uint8_t>(out));
                        } else {
                            spdlog::warn("Failed to open pipeline cache for writing: {}", path.string());
                        }
                    } catch(const std::exception& e) {
                        spdlog::warn("Failed to save pipeline cache: {}", e.what());
                    }
                }
            }
            current_renderer.reset();
            loader.reset();

            offscreenSink = {};
            normalPresentation.reset();

            headlessTextures.clear();
            headlessOutputBuffers.clear();
            headlessOutputAllocations.clear();
            if(allocatorInitialized) {
                try {
                    allocator.destroy();
                } catch(const std::exception& e) {
                    spdlog::warn("Failed to destroy Vulkan memory allocator: {}", e.what());
                }
                allocatorInitialized = false;
            }

            if(audioInitialized) {
                sdl::mix::CloseAudio();
                sdl::mix::Quit();
                audioInitialized = false;
            }
        }

        void init() {
            presentationOwner=std::this_thread::get_id();
            if(const char* c = std::getenv("AUROREENGINE_HEADLESS")) {
                std::string_view sv{c};
                if(sv == "1") {
                    config.headless = true;
                } else if(sv == "0") {
                    config.headless = false;
                } else {
                    spdlog::warn("Unknown value for AUROREENGINE_HEADLESS: \"{}\"", c);
                }
            }
            if(config.headless) {
                sdl::SetHint("SDL_NO_SIGNAL_HANDLERS", "1");
                if(const char* c = std::getenv("AUROREENGINE_HEADLESS_TERMINAL")) {
                    std::string_view sv{c};
                    if(sv == "1") {
                        config.headless_terminal = true;
                        config.headless_output_dir.clear();
                    } else if(sv == "0") {
                        config.headless_terminal = false;
                    } else {
                        spdlog::warn("Unknown value for AUROREENGINE_HEADLESS_TERMINAL: \"{}\"", c);
                    }
                }
                if(config.headless_terminal) {
                    spdlog::info("Switching log to stderr");
                    // https://github.com/gabime/spdlog/wiki/0.-FAQ#switch-the-default-logger-to-stderr
                    //spdlog::set_default_logger(spdlog::stderr_color_st("temp")); does not work :(
                    spdlog::set_default_logger(spdlog::stderr_color_st("stderr"));

#if __linux__
                    struct termios term{};
                    tcgetattr(STDIN_FILENO, &term);
                    term.c_lflag &= ~ICANON & ~ECHO;
                    tcsetattr(STDIN_FILENO, TCSANOW, &term);
#endif

                    static std::vector<char> buffer(32*1024*1024); // 32 MiB buffer, so it is smooth
                    std::setvbuf(stdout, buffer.data(), _IOFBF, buffer.size());
                }
                if(const char* c = std::getenv("AUROREENGINE_HEADLESS_OUTPUT_DIR")) {
                    config.headless_output_dir = c;
                }
                if(const char* c = std::getenv("AUROREENGINE_HEADLESS_OUTPUT_PATTERN")) {
                    config.headless_output_format = c;
                }
                if(const char* c = std::getenv("AUROREENGINE_HEADLESS_OUTPUT_QUALITY")) {
                    config.headless_output_quality = std::stoi(c);
                }
                if(const char* c = std::getenv("AUROREENGINE_HEADLESS_WIDTH")) {
                    config.width = std::stoi(c);
                }
                if(const char* c = std::getenv("AUROREENGINE_HEADLESS_HEIGHT")) {
                    config.height = std::stoi(c);
                }
                if(const char* c = std::getenv("AUROREENGINE_HEADLESS_FRAMES")) {
                    config.headless_frames = std::stoi(c);
                }
                if(!config.headless_output_dir.empty() &&
                   !std::filesystem::exists(config.headless_output_dir))
                {
                    std::filesystem::create_directories(config.headless_output_dir);
                }
            }
            if(const char* c = std::getenv("AUROREENGINE_NO_SWAPCHAIN")) {
                config.workaround_no_swapchain = (std::string_view{c} == "1");
            }
            if(const char* c = std::getenv("AUROREENGINE_PROFILE_FRAMES")) {
                std::string_view sv{c};
                config.profileFrames = sv == "1" || sv == "true" || sv == "TRUE";
            }
            if(const char* c = std::getenv("AUROREENGINE_PROFILE_FRAME_INTERVAL")) {
                config.profileFrameInterval = std::max<uint64_t>(1, std::stoull(c));
            }
            static sdl::initializer sdl_init;
            if(!config.headless) {
                spdlog::debug("Initializing SDL window");
                initWindow();
                spdlog::debug("Initializing SDL_mixer audio");
                initAudio();
                spdlog::debug("Initializing SDL input");
                initInput();
            }
            if(config.headless) {
                if(const auto* audio = std::getenv("AUROREENGINE_HEADLESS_AUDIO"); audio && std::string_view(audio) == "1")
                    initAudio();
            }
            spdlog::debug("Initializing Vulkan");
            initVulkan();
            spdlog::debug("Window initialization complete");
        }
        // Queue operations, rather than the entire render callback, own this
        // mutex. A phase may drain uploads while preparing a new scene without
        // preventing an upload worker from submitting its final transfer.
        void submitGraphics(const vk::SubmitInfo& info, vk::Fence fence = {}) {
            std::scoped_lock queueGuard(queueLock);
            graphicsQueue.submit(info, fence);
        }
        void waitIdle() {
            if(loader) loader->waitIdle();
            std::scoped_lock queueGuard(queueLock);
            if(device) device->waitIdle();
        }

        [[nodiscard]] bool offscreen_presentation_active() const noexcept {
            return offscreenPresentation;
        }

        // Invoke before loop() or from phase::after_events(), never render() or
        // the frame sink. Re-entry replaces only the sink extent/callback and
        // keeps the original surface swapchain retained for eventual restore.
        void begin_offscreen_presentation(vk::Extent2D extent,
            std::function<void(const OffscreenFrame&)> sink) {
            requirePresentationOwner();
            if(config.headless || config.workaround_no_swapchain)
                throw std::runtime_error("Offscreen presentation requires a normal surface window; headless and no-swapchain output are separate modes");
            if(!device || !allocatorInitialized || !surface ||
               (!offscreenPresentation && !swapchain))
                throw std::runtime_error("Offscreen presentation requires an initialized surface window");
            if(!sink)throw std::invalid_argument("Offscreen presentation requires a frame sink callback");
            validateOffscreenExtent(extent);

            std::scoped_lock lock(renderLock);
            PresentationCall transition(presentationTransition);
            // Allocate every image, view, copy buffer, command buffer and sync
            // object before changing fields visible to the current phase.
            auto next=makeOffscreenPresentation(extent);
            next.dirty=swapchainDirty;
            waitIdle();
            auto previous=takePresentation();
            const bool wasOffscreen=offscreenPresentation;
            auto previousSink=std::move(offscreenSink);
            installPresentation(std::move(next));
            offscreenPresentation=true;offscreenSink=std::move(sink);
            try {
                if(current_renderer)current_renderer->prepare(swapchainImages,swapchainImageViewsRaw);
            } catch(const std::exception& error) {
                // Device loss must not bypass restoration of the public
                // presentation state, even when draining itself fails.
                try{waitIdle();}catch(...){}
                auto rejected=takePresentation();
                installPresentation(std::move(previous));
                offscreenPresentation=wasOffscreen;offscreenSink=std::move(previousSink);
                try {if(current_renderer)current_renderer->prepare(swapchainImages,swapchainImageViewsRaw);}
                catch(...) {throw std::runtime_error("Offscreen preparation failed and the restored phase could not prepare; stop rendering and recreate the phase");}
                throw std::runtime_error(std::string("Offscreen preparation failed; previous presentation restored: ")+error.what());
            } catch(...) {
                try{waitIdle();}catch(...){}
                auto rejected=takePresentation();installPresentation(std::move(previous));
                offscreenPresentation=wasOffscreen;offscreenSink=std::move(previousSink);
                if(current_renderer)current_renderer->prepare(swapchainImages,swapchainImageViewsRaw);
                throw;
            }
            if(!wasOffscreen)normalPresentation.emplace(std::move(previous));
        }

        void end_offscreen_presentation() {
            requirePresentationOwner();
            if(!offscreenPresentation)return;
            std::scoped_lock lock(renderLock);
            PresentationCall transition(presentationTransition);
            waitIdle();
            auto previous=takePresentation();auto previousSink=std::move(offscreenSink);
            auto normal=std::move(*normalPresentation);
            // Real-window resize notifications during the sink are deferred
            // until the retained WSI presentation is visible again.
            normal.dirty=normal.dirty||previous.dirty;
            installPresentation(std::move(normal));offscreenPresentation=false;
            try {
                if(current_renderer)current_renderer->prepare(swapchainImages,swapchainImageViewsRaw);
            } catch(...) {
                try{waitIdle();}catch(...){}
                normalPresentation.emplace(takePresentation());
                installPresentation(std::move(previous));offscreenPresentation=true;offscreenSink=std::move(previousSink);
                try {if(current_renderer)current_renderer->prepare(swapchainImages,swapchainImageViewsRaw);}
                catch(...) {throw std::runtime_error("Normal presentation and rollback preparation failed; stop rendering and recreate the phase");}
                throw std::runtime_error("Normal presentation could not prepare; the previous frame sink remains active");
            }
            normalPresentation.reset();
        }

        void loop() {
            startTime = std::chrono::steady_clock::now();
            lastFrame = startTime;
            lastFPS = startTime;
            framesInSecond = 0;

            int currentFrame = 0;
            vk::Result r{};
            for(;;) {
                auto frameStart = std::chrono::steady_clock::now();
                auto afterEvents = frameStart;
                auto afterLimiter = frameStart;
                auto afterFence = frameStart;
                auto afterAcquire = frameStart;
                auto afterRender = frameStart;
                auto afterPresent = frameStart;
                if(config.headless) {
                    if(config.headless_frames > 0 && totalFrameNumber >= config.headless_frames) {
                        spdlog::info("Finished rendering {} frames", totalFrameNumber);
                        return;
                    }

                    bool quit = false;
                    if(config.headless_terminal) {
                        handle_headless_terminal(quit);
                    } else {
                        handle_headless_commands(quit);
                    }
                    handle_sdl_events(quit);
                    if(quit) {
                        return;
                    }
                    if(current_renderer) current_renderer->after_events();
                    afterEvents = std::chrono::steady_clock::now();
                    afterLimiter = afterEvents;
                } else {
                    bool quit = false;
                    handle_sdl_events(quit);
                    if(quit) {
                        return;
                    }
                    if(current_renderer) current_renderer->after_events();
                    if(!offscreenPresentation && !config.workaround_no_swapchain &&
                       (SDL_GetWindowFlags(win.get())&SDL_WINDOW_MINIMIZED)) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        continue;
                    }

                    auto now = std::chrono::steady_clock::now();
                    afterEvents = now;
                    auto dt = std::chrono::duration<double>(now - lastFrame).count();

                    // Honour FPS limit if set — sleep until target time based on lastFrame
                    if(config.fpsLimit > 0) {
                        auto frame_time = std::chrono::duration<double>(std::chrono::seconds(1)) / config.fpsLimit;
                        auto target = lastFrame + frame_time;
                        if(now < target) {
                            std::this_thread::sleep_until(target);
                            now = std::chrono::steady_clock::now();
                        }
                    }
                    lastFrame = now;
                    afterLimiter = now;
                }

                std::scoped_lock lock(renderLock);
                PresentationCall frame(presentationFrame);
                r = device->waitForFences(inFlightFences[currentFrame], true, UINT64_MAX);
                if(r != vk::Result::eSuccess) {
                    if(offscreenPresentation)throw std::runtime_error("Offscreen render fence wait failed: "+vk::to_string(r));
                    spdlog::error("Waiting for inFlightFences[{}] failed with result {}", currentFrame, vk::to_string(r));
                }
                afterFence = std::chrono::steady_clock::now();

                if(swapchainDirty && !offscreenPresentation && !config.headless && !config.workaround_no_swapchain) {
                    spdlog::debug("Swapchain marked dirty; recreating before acquire");
                    recreateSwapchain();
                    continue;
                }

                unsigned int imageIndex = 0;
                bool suboptimalAcquire = false;
                if(offscreenPresentation || config.headless || config.workaround_no_swapchain) {
                    // This is incredibly weird and hacky, but by some miracle it works.
                    imageIndex = (currentFrame)%swapchainImageCount;

                    if(imagesInFlight[imageIndex]) {
                        r = device->waitForFences({imagesInFlight[imageIndex], headlessFences[imageIndex].get()}, true, UINT64_MAX);
                        if(r != vk::Result::eSuccess) {
                            if(offscreenPresentation)throw std::runtime_error("Offscreen readback fence wait failed: "+vk::to_string(r));
                            spdlog::error("Waiting for imagesInFlight[{0}] and headlessFences[{0}] failed with result {1}", imageIndex, vk::to_string(r));
                        }

                        if(offscreenPresentation) {
                            const auto bytes=static_cast<std::size_t>(swapchainExtent.width)*swapchainExtent.height*4;
                            r=allocator.copyAllocationToMemory(headlessOutputAllocations[imageIndex].get(),0,offscreenPixels.data(),bytes);
                            if(r!=vk::Result::eSuccess)
                                throw std::runtime_error("Offscreen GPU readback failed: "+vk::to_string(r));
                            offscreenSink(OffscreenFrame{std::span<const std::uint8_t>{offscreenPixels.data(),bytes},
                                swapchainExtent.width,swapchainExtent.height,swapchainExtent.width*4,swapchainFormat.format});
                        } else if(!config.headless_output_dir.empty() || config.headless_terminal || config.workaround_no_swapchain) {
                            std::vector<char> data(swapchainExtent.width*swapchainExtent.height*4);
                            r = allocator.copyAllocationToMemory(headlessOutputAllocations[imageIndex].get(),
                                0, data.data(), swapchainExtent.width*swapchainExtent.height*4);
                            if(r != vk::Result::eSuccess)
                                spdlog::error("Copying headlessOutputAllocations[{}] to memory failed with result {}", imageIndex, vk::to_string(r));

                            sdl::unique_surface surface{sdl::CreateRGBSurfaceWithFormatFrom(data.data(),
                                swapchainExtent.width, swapchainExtent.height, 32, swapchainExtent.width*4,
                                sdl::PixelFormatEnumVales::ARGB8888)};

                            if(!config.headless_output_dir.empty()) {
                                std::filesystem::path path = config.headless_output_dir / std::vformat(config.headless_output_format, std::make_format_args(totalFrameNumber));
                                std::string ext = path.extension().string();
                                if(ext == ".png") {
                                    auto temporary = path;
                                    temporary += ".part";
                                    if(sdl::image::SavePNG(surface.get(), temporary.string().c_str()) == 0)
                                        std::filesystem::rename(temporary, path);
                                    else spdlog::error("Failed to save headless PNG {}", path.string());
                                } else if(ext == ".jpg" || ext == ".jpeg") {
                                    sdl::image::SaveJPG(surface.get(), path.string().c_str(), config.headless_output_quality);
                                } else if(ext == ".bmp") {
                                    save_bmp(data, static_cast<int>(swapchainExtent.width), static_cast<int>(swapchainExtent.height), path);
                                } else {
                                    spdlog::error("Unknown output format \"{}\". Supported formats are: PNG, JPG, BMP", ext);
                                    spdlog::warn("Disabling output of images!");
                                    config.headless_output_dir.clear();
                                }
                                spdlog::debug("Saved headless output to {}", path.string());
                            }
                            if(config.headless_terminal) {
                                terminal_output(data, swapchainExtent, std::cout);
                                std::cout << std::flush;
                            }
                            if(config.workaround_no_swapchain) {
                                auto window_surface = sdl::GetWindowSurface(win.get());
                                sdl::BlitSurface(surface.get(), nullptr, window_surface, nullptr);
                                sdl::UpdateWindowSurface(win.get());
                            }
                        }
                    }

                    // We need this just to signal the semaphore. THIS IS BAD. Oh well...
                    vk::CommandBuffer& commandBuffer = headlessCommandBuffersPre[imageIndex];
                    commandBuffer.begin(vk::CommandBufferBeginInfo());
                    commandBuffer.end();

                    vk::SubmitInfo submitInfo(0, {}, {}, 1, &commandBuffer, 1, &imageAvailableSemaphores[currentFrame].get());
                    submitGraphics(submitInfo, {});
                    afterAcquire = std::chrono::steady_clock::now();
                } else {
                    r = device->acquireNextImageKHR(swapchain.get(), UINT64_MAX, imageAvailableSemaphores[currentFrame].get(), {}, &imageIndex);
                    if(r == vk::Result::eErrorOutOfDateKHR) {
                        spdlog::debug("Swapchain out of date on acquire; recreating");
                        recreateSwapchain();
                        continue; // retry next frame
                    } else if(r == vk::Result::eSuboptimalKHR) {
                        // A successful suboptimal acquire has signalled its semaphore.
                        // Submit and present this image before replacing the swapchain.
                        suboptimalAcquire = true;
                    } else if(r != vk::Result::eSuccess) {
                        spdlog::error("AcquireNextImage failed with result {}", vk::to_string(r));
                        recreateSwapchain();
                        continue;
                    }
                    if(imagesInFlight[imageIndex]) {
                        r = device->waitForFences(imagesInFlight[imageIndex], true, UINT64_MAX);
                        if(r != vk::Result::eSuccess)
                            spdlog::error("Waiting for imagesInFlight[{}] failed with result {}", imageIndex, vk::to_string(r));
                    }
                    afterAcquire = std::chrono::steady_clock::now();
                }
                imagesInFlight[imageIndex] = inFlightFences[currentFrame];

                device->resetFences(fences[currentFrame].get());

                if(!current_renderer)
                    throw std::runtime_error("No renderer set!");
                current_renderer->render(imageIndex, imageAvailableSemaphores[currentFrame].get(), renderFinishedSemaphores[imageIndex].get(), inFlightFences[currentFrame]);
                afterRender = std::chrono::steady_clock::now();

                if(offscreenPresentation || config.headless || config.workaround_no_swapchain) {
                    device->resetFences(headlessFences[imageIndex].get());

                    vk::CommandBuffer& commandBuffer = headlessCommandBuffersPost[imageIndex];
                    commandBuffer.begin(vk::CommandBufferBeginInfo());
                    commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                        vk::PipelineStageFlagBits::eTransfer, {}, {}, {},
                        vk::ImageMemoryBarrier(vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eTransferRead,
                            swapchainFinalLayout, vk::ImageLayout::eTransferSrcOptimal,
                            VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, headlessTextures[imageIndex].image,
                            vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)));
                    commandBuffer.copyImageToBuffer(headlessTextures[imageIndex].image, vk::ImageLayout::eTransferSrcOptimal,
                        headlessOutputBuffers[imageIndex].get(),
                        vk::BufferImageCopy(0, 0, 0, vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1),
                            vk::Offset3D(0, 0, 0),
                            vk::Extent3D(swapchainExtent, 1)));
                    commandBuffer.end();

                    vk::PipelineStageFlags waitStages = vk::PipelineStageFlagBits::eTransfer;
                    vk::SubmitInfo submitInfo(1, &renderFinishedSemaphores[imageIndex].get(), &waitStages, 1, &commandBuffer, 0, {});
                    submitGraphics(submitInfo, headlessFences[imageIndex].get());
                    afterPresent = std::chrono::steady_clock::now();
                } else {
                    vk::PresentInfoKHR present_info(renderFinishedSemaphores[imageIndex].get(), swapchain.get(), imageIndex);
                    {
                        std::scoped_lock queueGuard(queueLock);
                        r = presentQueue.presentKHR(&present_info);
                    }
                    if(r == vk::Result::eErrorOutOfDateKHR || r == vk::Result::eSuboptimalKHR || (r == vk::Result::eSuccess && suboptimalAcquire)) {
                        spdlog::debug("Present {} ; recreating swapchain", (r==vk::Result::eErrorOutOfDateKHR?"out-of-date":"suboptimal"));
                        recreateSwapchain();
                    } else if(r != vk::Result::eSuccess) {
                        spdlog::error("Present failed with result {}", vk::to_string(r));
                    }
                    afterPresent = std::chrono::steady_clock::now();
                }

                uint64_t profileInterval = std::max<uint64_t>(1, config.profileFrameInterval);
                if(config.profileFrames && totalFrameNumber % profileInterval == 0) {
                    auto ms = [](auto duration) {
                        return std::chrono::duration<double, std::milli>(duration).count();
                    };
                    spdlog::debug("Frame {} CPU timings: events/limit/fence/acquire/render/present/total = {:.3f}/{:.3f}/{:.3f}/{:.3f}/{:.3f}/{:.3f}/{:.3f} ms",
                        totalFrameNumber,
                        ms(afterEvents - frameStart),
                        ms(afterLimiter - afterEvents),
                        ms(afterFence - afterLimiter),
                        ms(afterAcquire - afterFence),
                        ms(afterRender - afterAcquire),
                        ms(afterPresent - afterRender),
                        ms(afterPresent - frameStart));
                }

                currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;

                totalFrameNumber++;
                framesInSecond++;
                auto t = std::chrono::steady_clock::now();
                using namespace std::chrono_literals;
                if((t-lastFPS) > (1000ms/fpsSampleRate))
                {
                    currentFPS = framesInSecond / std::chrono::duration<double>(t-lastFPS).count();
                    lastFPS = t;
                    framesInSecond = 0;

                    if(fpsCount%fpsSampleRate == 0)
                    {
                        spdlog::debug("{} FPS", currentFPS);
                        fpsCount = 0;
                    }
                    fpsCount++;
                }
            }
        }

        void recreateSwapchain(bool recreate = true) {
            std::scoped_lock lock(renderLock);
            std::scoped_lock queueGuard(queueLock);
            spdlog::debug("(Re)Creating swapchain");

            if(offscreenPresentation || config.headless || config.workaround_no_swapchain) {
                return;
            }
            if(recreate && (SDL_GetWindowFlags(win.get())&SDL_WINDOW_MINIMIZED)) {
                // Presentation can become out-of-date after a frame has been
                // acquired. Keep its resources until the surface is restored.
                swapchainDirty=true;
                return;
            }

            if(recreate) {
                device->waitIdle();
            }

            swapchainSupport = querySwapChainSupport(physicalDevice);
            if(swapchainSupport.formats.empty() || swapchainSupport.presentModes.empty()) {
                throw std::runtime_error("Swapchain support is incomplete");
            }

            auto presentModeIt = std::ranges::find_if(swapchainSupport.presentModes, [this](auto p){
                return p == config.preferredPresentMode;
            });
            swapchainPresentMode = presentModeIt == swapchainSupport.presentModes.end() ? swapchainSupport.presentModes[0] : *presentModeIt;
            const auto nextExtent = chooseSwapchainExtent(swapchainSupport.capabilities);
            if(nextExtent.width==0 || nextExtent.height==0) {
                if(!recreate) throw std::runtime_error("Cannot create a swapchain for a zero-sized surface");
                swapchainDirty=true;
                return;
            }
            swapchainExtent = nextExtent;
            swapchainImageCount = swapchainSupport.capabilities.minImageCount + 1;
            if(swapchainSupport.capabilities.maxImageCount > 0) {
                swapchainImageCount = std::min(swapchainImageCount, swapchainSupport.capabilities.maxImageCount);
            }

            spdlog::debug("Swapchain of format {}, present mode {}, extent {}x{} and {} images (composite alpha: {})",
                vk::to_string(swapchainFormat.format), vk::to_string(swapchainPresentMode),
                swapchainExtent.width, swapchainExtent.height, swapchainImageCount,
                vk::to_string(swapchainSupport.capabilities.supportedCompositeAlpha));

            auto usage = vk::ImageUsageFlagBits::eColorAttachment
                | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc
                | vk::ImageUsageFlagBits::eSampled;
            vk::SwapchainCreateInfoKHR swapchain_info({}, surface.get(), swapchainImageCount,
                swapchainFormat.format, swapchainFormat.colorSpace,
                swapchainExtent, 1, usage);
            swapchain_info.setPresentMode(swapchainPresentMode);
            // Graphics renders, the present queue presents. If they are different
            // families the images must be shared concurrently: the engine records
            // no queue-family ownership transfers.
            const auto sharing = detail::choose_swapchain_sharing(
                queueFamilyIndices.graphicsFamily.value(), queueFamilyIndices.presentFamily.value());
            if(sharing.concurrent) {
                swapchain_info.setImageSharingMode(vk::SharingMode::eConcurrent);
                swapchain_info.setQueueFamilyIndexCount(sharing.count);
                swapchain_info.setPQueueFamilyIndices(sharing.indices.data());
            }
            swapchain_info.setOldSwapchain(recreate ? swapchain.get() : vk::SwapchainKHR{});
            swapchain_info.setCompositeAlpha(static_cast<vk::CompositeAlphaFlagBitsKHR>(detail::choose_composite_alpha(
                static_cast<VkCompositeAlphaFlagsKHR>(swapchainSupport.capabilities.supportedCompositeAlpha))));
            auto newSwapchain = device->createSwapchainKHRUnique(swapchain_info);
            swapchain = std::move(newSwapchain);
            swapchainImages = device->getSwapchainImagesKHR(swapchain.get());
            swapchainImageCount = static_cast<uint32_t>(swapchainImages.size());

            swapchainImageViews.clear();
            swapchainImageViewsRaw.clear();
            for(auto& image : swapchainImages)
            {
                vk::ImageViewCreateInfo view_info({}, image, vk::ImageViewType::e2D, swapchainFormat.format,
                    vk::ComponentMapping{}, vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
                swapchainImageViews.push_back(device->createImageViewUnique(view_info));
                swapchainImageViewsRaw.push_back(swapchainImageViews.back().get());
            }
            imagesInFlight.assign(swapchainImageCount, vk::Fence());
            // Presentation owns these waits until its image can be acquired again.
            // Frame fences only protect graphics submissions, not presentation.
            resetPresentationSemaphores();
            swapchainDirty = false;

            if(recreate && current_renderer) {
                current_renderer->prepare(swapchainImages, swapchainImageViewsRaw);
            }
        }

        void resetPresentationSemaphores() {
            std::vector<vk::UniqueSemaphore> next;
            next.reserve(swapchainImageCount);
            for(uint32_t i=0;i<swapchainImageCount;++i)
                next.push_back(device->createSemaphoreUnique(vk::SemaphoreCreateInfo()));
            renderFinishedSemaphores=std::move(next);
        }

        void set_phase(phase* renderer, input::keyboard_handler* keyboard_handler = nullptr, input::controller_handler* controller_handler = nullptr) {
            current_renderer.reset(renderer);

            this->keyboard_handler = keyboard_handler;
            this->controller_handler = controller_handler;
            if(controller_handler) {
                for(auto& [id, controller] : controllers) {
                    controller_handler->add_controller(controller.get());
                }
            }

            auto t0 = std::chrono::high_resolution_clock::now();
            spdlog::debug("Preparing phase: preload");
            current_renderer->preload();
            auto tPreload = std::chrono::high_resolution_clock::now();
            spdlog::debug("Preparing phase: prepare");
            current_renderer->prepare(swapchainImages, swapchainImageViewsRaw);
            auto tPrepare = std::chrono::high_resolution_clock::now();
            spdlog::debug("Preparing phase: waitLoad");
            current_renderer->waitLoad(); // replace with loading screen
            loader->waitIdle();
            auto tWaitLoad = std::chrono::high_resolution_clock::now();
            spdlog::debug("Preparing phase: init");
            current_renderer->init();
            auto tInit = std::chrono::high_resolution_clock::now();

            auto dPreload = std::chrono::duration_cast<std::chrono::milliseconds>(tPreload - t0).count();
            auto dPrepare = std::chrono::duration_cast<std::chrono::milliseconds>(tPrepare - tPreload).count();
            auto dWaitLoad = std::chrono::duration_cast<std::chrono::milliseconds>(tWaitLoad - tPrepare).count();
            auto dInit = std::chrono::duration_cast<std::chrono::milliseconds>(tInit - tWaitLoad).count();
            auto dTotal = std::chrono::duration_cast<std::chrono::milliseconds>(tInit - t0).count();

            auto name = type_name(*renderer);
            spdlog::debug("Timing for phase \"{}\": preload/prepare/load/init/total: {}/{}/{}/{}/{} ms", name, dPreload, dPrepare, dWaitLoad, dInit, dTotal);
        }

        window_config config;

        std::unique_ptr<resource_loader> loader;

        std::unique_ptr<phase> current_renderer;
        input::keyboard_handler* keyboard_handler = nullptr;
        input::controller_handler* controller_handler = nullptr;

        sdl::unique_window win;
        uint32_t window_width, window_height;

        vk::UniqueInstance instance;
        vk::UniqueSurfaceKHR surface;

        vk::PhysicalDevice physicalDevice;

        vk::PhysicalDeviceProperties deviceProperties;
        QueueFamilyIndices queueFamilyIndices;
        SwapChainSupportDetails swapchainSupport;
        gpu_features gpuFeatures;

        vk::UniqueDevice device;
        vma::Allocator allocator;

        vk::Queue graphicsQueue;
        vk::Queue presentQueue;
        std::vector<vk::Queue> transferQueues;

        vk::SurfaceFormatKHR swapchainFormat;
        vk::PresentModeKHR swapchainPresentMode;
        vk::Extent2D swapchainExtent;
        uint32_t swapchainImageCount;
        vk::UniqueSwapchainKHR swapchain;
        vk::ImageLayout swapchainFinalLayout = vk::ImageLayout::ePresentSrcKHR;

        std::vector<vk::Image> swapchainImages;
        std::vector<vk::UniqueImageView> swapchainImageViews;
        std::vector<vk::ImageView> swapchainImageViewsRaw;

        std::vector<texture> headlessTextures;
        vk::UniqueCommandPool headlessCommandPool;
        std::vector<vk::CommandBuffer> headlessCommandBuffersPre;
        std::vector<vk::CommandBuffer> headlessCommandBuffersPost;
        std::vector<vma::UniqueBuffer> headlessOutputBuffers;
        std::vector<vma::UniqueAllocation> headlessOutputAllocations;
        std::vector<vk::UniqueFence> headlessFences;

        const int MAX_FRAMES_IN_FLIGHT = 2;
        std::vector<vk::UniqueSemaphore> imageAvailableSemaphores;
        std::vector<vk::UniqueSemaphore> renderFinishedSemaphores;
        std::vector<vk::UniqueFence> fences;

        std::vector<vk::Fence> imagesInFlight;
        std::vector<vk::Fence> inFlightFences;

        vk::UniquePipelineCache pipelineCache;

        std::chrono::steady_clock::time_point startTime;
        std::chrono::steady_clock::time_point lastFrame;

        uint64_t totalFrameNumber{};
        static constexpr int fpsSampleRate = 5;
        uint64_t framesInSecond{};
        std::chrono::steady_clock::time_point lastFPS;
        int fpsCount{};
        double currentFPS{};
        int refreshRate{};
        bool swapchainDirty = false;
        bool audioInitialized = false;
        bool allocatorInitialized = false;
        std::recursive_mutex renderLock{};
        std::recursive_mutex queueLock{};

        struct sdl_controller_closer {
            void operator()(sdl::GameController* ptr) const {
                sdl::GameControllerClose(ptr);
            }
        };
        std::map<sdl::JoystickID, std::unique_ptr<sdl::GameController, sdl_controller_closer>> controllers;

        vk::UniqueDebugUtilsMessengerEXT debugMessenger;
    private:
        struct PresentationCall {
            bool& active;
            explicit PresentationCall(bool& value):active(value){active=true;}
            ~PresentationCall(){active=false;}
        };
        struct PresentationResources {
            vk::SurfaceFormatKHR format;
            vk::PresentModeKHR presentMode{};
            vk::Extent2D extent;
            std::uint32_t count{};
            vk::ImageLayout finalLayout{};
            bool dirty{};
            std::vector<texture> textures;
            vk::UniqueSwapchainKHR swapchain;
            std::vector<vk::Image> images;
            std::vector<vk::UniqueImageView> views;
            std::vector<vk::ImageView> rawViews;
            vk::UniqueCommandPool commandPool;
            std::vector<vk::CommandBuffer> before,after;
            std::vector<vma::UniqueAllocation> allocations;
            std::vector<vma::UniqueBuffer> buffers;
            std::vector<vk::UniqueFence> copyFences;
            std::vector<vk::UniqueSemaphore> available,finished;
            std::vector<vk::UniqueFence> fences;
            std::vector<vk::Fence> imagesInFlight,inFlight;
            std::vector<std::uint8_t> pixels;
        };
        bool offscreenPresentation{},presentationTransition{},presentationFrame{};
        std::thread::id presentationOwner{std::this_thread::get_id()};
        std::function<void(const OffscreenFrame&)> offscreenSink;
        std::vector<std::uint8_t> offscreenPixels;
        std::optional<PresentationResources> normalPresentation;

        void requirePresentationOwner() const {
            if(std::this_thread::get_id()!=presentationOwner)
                throw std::runtime_error("Presentation transitions must run on the window owner thread");
            if(presentationTransition||presentationFrame)
                throw std::runtime_error("Schedule presentation transitions before loop() or in phase::after_events(), outside render() and the frame sink");
        }
        void validateOffscreenExtent(vk::Extent2D extent) const {
            const auto limit=physicalDevice.getProperties().limits.maxImageDimension2D;
            if(!extent.width||!extent.height||extent.width>limit||extent.height>limit||
               extent.width>static_cast<std::uint32_t>(std::numeric_limits<int>::max())||
               extent.height>static_cast<std::uint32_t>(std::numeric_limits<int>::max())||
               extent.width>std::numeric_limits<std::uint32_t>::max()/4||
               static_cast<std::uint64_t>(extent.width)*extent.height>std::numeric_limits<std::size_t>::max()/4)
                throw std::invalid_argument("Offscreen extent must be nonzero and fit the device's native 2D image and row-stride limits");
            constexpr auto format=vk::Format::eB8G8R8A8Srgb;
            const auto required=vk::FormatFeatureFlagBits::eColorAttachment|vk::FormatFeatureFlagBits::eTransferSrc;
            if((physicalDevice.getFormatProperties(format).optimalTilingFeatures&required)!=required)
                throw std::runtime_error("This device cannot render and read back BGRA8 sRGB offscreen images");
            try {
                const auto properties=physicalDevice.getImageFormatProperties(format,vk::ImageType::e2D,vk::ImageTiling::eOptimal,
                    vk::ImageUsageFlagBits::eColorAttachment|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst|vk::ImageUsageFlagBits::eSampled,{});
                const auto bytes=static_cast<std::uint64_t>(extent.width)*extent.height*4;
                if(extent.width>properties.maxExtent.width||extent.height>properties.maxExtent.height||bytes>properties.maxResourceSize)
                    throw std::runtime_error("The requested native offscreen extent exceeds this format's image limits");
            }catch(const vk::SystemError& error) {
                throw std::runtime_error(std::string("BGRA8 sRGB offscreen images are unavailable on this device: ")+error.what());
            }
        }
        PresentationResources makeOffscreenPresentation(vk::Extent2D extent) {
            PresentationResources result;
            result.format={vk::Format::eB8G8R8A8Srgb,vk::ColorSpaceKHR::eSrgbNonlinear};
            result.presentMode=vk::PresentModeKHR::eImmediate;result.extent=extent;
            result.count=static_cast<std::uint32_t>(MAX_FRAMES_IN_FLIGHT);
            result.finalLayout=vk::ImageLayout::eTransferSrcOptimal;
            const auto bytes=static_cast<std::size_t>(extent.width)*extent.height*4;
            result.pixels.resize(bytes);result.textures.reserve(result.count);result.images.reserve(result.count);
            result.views.reserve(result.count);result.rawViews.reserve(result.count);
            for(std::uint32_t i=0;i<result.count;++i) {
                result.textures.emplace_back(device.get(),allocator,static_cast<int>(extent.width),static_cast<int>(extent.height),
                    vk::ImageUsageFlagBits::eColorAttachment|vk::ImageUsageFlagBits::eTransferDst|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eSampled,
                    result.format.format,vk::SampleCountFlagBits::e1);
                result.images.push_back(result.textures.back().image);
                result.views.push_back(device->createImageViewUnique(vk::ImageViewCreateInfo({},result.images.back(),vk::ImageViewType::e2D,
                    result.format.format,{},vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor,0,1,0,1))));
                result.rawViews.push_back(result.views.back().get());
                auto [buffer,allocation]=allocator.createBufferUnique(vk::BufferCreateInfo({},bytes,vk::BufferUsageFlagBits::eTransferDst),
                    vma::AllocationCreateInfo({},vma::MemoryUsage::eGpuToCpu));
                result.buffers.push_back(std::move(buffer));result.allocations.push_back(std::move(allocation));
                result.copyFences.push_back(device->createFenceUnique(vk::FenceCreateInfo()));
                result.available.push_back(device->createSemaphoreUnique(vk::SemaphoreCreateInfo()));
                result.finished.push_back(device->createSemaphoreUnique(vk::SemaphoreCreateInfo()));
                result.fences.push_back(device->createFenceUnique(vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled)));
                result.inFlight.push_back(result.fences.back().get());
            }
            result.imagesInFlight.assign(result.count,vk::Fence{});
            result.commandPool=device->createCommandPoolUnique(vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer,queueFamilyIndices.graphicsFamily.value()));
            result.before=device->allocateCommandBuffers(vk::CommandBufferAllocateInfo(result.commandPool.get(),vk::CommandBufferLevel::ePrimary,result.count));
            result.after=device->allocateCommandBuffers(vk::CommandBufferAllocateInfo(result.commandPool.get(),vk::CommandBufferLevel::ePrimary,result.count));
            return result;
        }
        PresentationResources takePresentation() {
            PresentationResources result;
            result.format=swapchainFormat;result.presentMode=swapchainPresentMode;result.extent=swapchainExtent;
            result.count=swapchainImageCount;result.finalLayout=swapchainFinalLayout;result.dirty=swapchainDirty;
            result.swapchain=std::move(swapchain);result.images=std::move(swapchainImages);
            result.views=std::move(swapchainImageViews);result.rawViews=std::move(swapchainImageViewsRaw);
            result.textures=std::move(headlessTextures);result.commandPool=std::move(headlessCommandPool);
            result.before=std::move(headlessCommandBuffersPre);result.after=std::move(headlessCommandBuffersPost);
            result.buffers=std::move(headlessOutputBuffers);result.allocations=std::move(headlessOutputAllocations);
            result.copyFences=std::move(headlessFences);result.available=std::move(imageAvailableSemaphores);
            result.finished=std::move(renderFinishedSemaphores);result.fences=std::move(fences);
            result.imagesInFlight=std::move(imagesInFlight);result.inFlight=std::move(inFlightFences);
            result.pixels=std::move(offscreenPixels);return result;
        }
        void installPresentation(PresentationResources resources) {
            swapchainFormat=resources.format;swapchainPresentMode=resources.presentMode;swapchainExtent=resources.extent;
            swapchainImageCount=resources.count;swapchainFinalLayout=resources.finalLayout;swapchainDirty=resources.dirty;
            swapchain=std::move(resources.swapchain);swapchainImages=std::move(resources.images);
            swapchainImageViews=std::move(resources.views);swapchainImageViewsRaw=std::move(resources.rawViews);
            headlessTextures=std::move(resources.textures);headlessCommandPool=std::move(resources.commandPool);
            headlessCommandBuffersPre=std::move(resources.before);headlessCommandBuffersPost=std::move(resources.after);
            headlessOutputBuffers=std::move(resources.buffers);headlessOutputAllocations=std::move(resources.allocations);
            headlessFences=std::move(resources.copyFences);imageAvailableSemaphores=std::move(resources.available);
            renderFinishedSemaphores=std::move(resources.finished);fences=std::move(resources.fences);
            imagesInFlight=std::move(resources.imagesInFlight);inFlightFences=std::move(resources.inFlight);
            offscreenPixels=std::move(resources.pixels);
        }
        vk::Extent2D chooseSwapchainExtent(const vk::SurfaceCapabilitiesKHR& capabilities) {
            if(capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
                window_width = capabilities.currentExtent.width;
                window_height = capabilities.currentExtent.height;
                return capabilities.currentExtent;
            }

            int drawableWidth = 0;
            int drawableHeight = 0;
            SDL_Vulkan_GetDrawableSize(win.get(), &drawableWidth, &drawableHeight);
            if(drawableWidth <= 0 || drawableHeight <= 0) {
                SDL_GetWindowSize(win.get(), &drawableWidth, &drawableHeight);
            }

            window_width = static_cast<uint32_t>(std::max(1, drawableWidth));
            window_height = static_cast<uint32_t>(std::max(1, drawableHeight));
            return vk::Extent2D{
                std::clamp(window_width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
                std::clamp(window_height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
            };
        }

        void initWindow() {
            sdl::Rect rect;
            if(sdl::GetDisplayBounds(config.display_index, &rect) != 0) {
                spdlog::warn("Failed to get SDL display bounds for display {}: {}", config.display_index, sdl::GetError());
                rect = sdl::Rect{
                    static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(config.display_index)),
                    static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(config.display_index)),
                    config.width == static_cast<unsigned int>(-1) ? 1280 : static_cast<int>(config.width),
                    config.height == static_cast<unsigned int>(-1) ? 800 : static_cast<int>(config.height),
                };
            }

            if(config.x == -1) config.x = rect.x; else rect.x = config.x;
            if(config.y == -1) config.y = rect.y; else rect.y = config.y;
            if(config.width == -1) config.width = rect.w; else rect.w = config.width;
            if(config.height == -1) config.height = rect.h; else rect.h = config.height;

            for(const auto& [key, value] : config.sdl_hints) {
                sdl::SetHint(key.c_str(), value.c_str());
            }
            win = sdl::unique_window(
                sdl::CreateWindow(config.title.c_str(), rect.x, rect.y, rect.w, rect.h,
                    SDL_WINDOW_SHOWN | (config.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) | (config.workaround_no_swapchain ? 0 : SDL_WINDOW_VULKAN))
            );
            if(!win) {
                throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + sdl::GetError());
            }
            const char* displayName = sdl::GetDisplayName(config.display_index);
            spdlog::info("Created window ({}x{} @ {}:{}) on monitor \"{}\"", rect.w, rect.h, rect.x, rect.y,
                displayName ? displayName : "unknown");

            sdl::DisplayMode mode;
            if(sdl::GetWindowDisplayMode(win.get(), &mode) != 0) {
                spdlog::warn("Failed to get SDL window display mode: {}", sdl::GetError());
                mode = sdl::DisplayMode{};
            }

            window_width = rect.w;
            window_height = rect.h;

            refreshRate = mode.refresh_rate;
        }
        void initInput() {
            sdl::GameControllerEventState(sdl::enable);

            int numJoysticks = sdl::NumJoysticks();
            for(int i=0; i<numJoysticks; i++) {
                if(sdl::IsGameController(i)) {
                    auto controller = sdl::GameControllerOpen(i);
                    if(controller) {
                        sdl::JoystickID id = sdl::JoystickInstanceID(sdl::GameControllerGetJoystick(controller));
                        controllers[id] = std::unique_ptr<sdl::GameController, sdl_controller_closer>(controller);
                        spdlog::debug("Opened controller \"{}\" with id {}", sdl::GameControllerName(controller), id);
                    } else {
                        spdlog::warn("Failed to open controller {}: {}", i, sdl::GetError());
                    }
                }
            }
        }
        void initAudio() {
            if(sdl::mix::OpenAudio(sdl::mix::default_frequency, sdl::mix::default_format, sdl::mix::default_channels, 2048) < 0) {
                spdlog::error("Failed to open audio: {}", sdl::mix::GetError());
                return;
            }
            audioInitialized = true;
        }
        void initVulkan() {
            std::scoped_lock lock(renderLock);

            auto vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(sdl::vk::GetVkGetInstanceProcAddr());
            if(!vkGetInstanceProcAddr) {
                const auto* library = std::getenv("AUROREENGINE_VULKAN_LIBRARY");
                static vk::detail::DynamicLoader dl(library ? library : "");
                vkGetInstanceProcAddr = dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
            }
            if(!vkGetInstanceProcAddr) {
                throw std::runtime_error("Failed to load Vulkan vkGetInstanceProcAddr");
            }
            VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);
            spdlog::debug("Initialized Vulkan dispatch loader");

            bool validationEnabled = env_truthy("AUROREENGINE_VULKAN_VALIDATION") || env_truthy("OPENXMB_VULKAN_VALIDATION");
#if !defined(NDEBUG) && __linux__
            validationEnabled = true;
#endif
            std::vector<const char*> layers;
            std::vector<const char*> extensions = {
                vk::KHRGetPhysicalDeviceProperties2ExtensionName,
            };
            if(validationEnabled) {
                auto availableLayers = vk::enumerateInstanceLayerProperties();
                auto layerIt = std::ranges::find_if(availableLayers, [](const auto& layer) {
                    return std::string_view{layer.layerName} == "VK_LAYER_KHRONOS_validation";
                });
                if(layerIt != availableLayers.end()) {
                    layers.push_back("VK_LAYER_KHRONOS_validation");
                    extensions.push_back(vk::EXTDebugUtilsExtensionName);
                } else {
                    spdlog::warn("Vulkan validation requested, but VK_LAYER_KHRONOS_validation is not available");
                    validationEnabled = false;
                }
            }

            if(!config.headless && !config.workaround_no_swapchain) {
                uint32_t sdlExtensionCount = 0;
                if(!sdl::vk::GetInstanceExtensions(win.get(), &sdlExtensionCount, nullptr)) {
                    throw std::runtime_error(std::string("SDL_Vulkan_GetInstanceExtensions failed: ") + sdl::GetError());
                }

                std::vector<const char*> sdlExtensions(sdlExtensionCount);
                if(!sdl::vk::GetInstanceExtensions(win.get(), &sdlExtensionCount, sdlExtensions.data())) {
                    throw std::runtime_error(std::string("SDL_Vulkan_GetInstanceExtensions failed: ") + sdl::GetError());
                }
                std::ranges::copy(sdlExtensions, std::back_inserter(extensions));
            }

            const auto instanceExtensions = vk::enumerateInstanceExtensionProperties();
            const bool portabilityEnumeration = std::ranges::any_of(instanceExtensions, [](const auto& extension) {
                return std::string_view{extension.extensionName} == vk::KHRPortabilityEnumerationExtensionName;
            });
            if(portabilityEnumeration) extensions.push_back(vk::KHRPortabilityEnumerationExtensionName);

            spdlog::debug("Using extensions: {}", fmt::join(extensions, ", "));

            auto const app = vk::ApplicationInfo()
                .setPApplicationName(config.name.c_str())
                .setApplicationVersion(config.version)
                .setPEngineName("auroreengine")
                .setEngineVersion(1)
                .setApiVersion(vk::makeApiVersion(0, 1, 2, 0));
            auto inst_info = vk::InstanceCreateInfo()
                .setPApplicationInfo(&app)
                .setPEnabledLayerNames(layers)
                .setPEnabledExtensionNames(extensions)
                ;
            if(portabilityEnumeration) inst_info.setFlags(vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR);
            instance = vk::createInstanceUnique(inst_info);
            VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance);

            if(validationEnabled) {
                debugMessenger = instance->createDebugUtilsMessengerEXTUnique(vk::DebugUtilsMessengerCreateInfoEXT({},
                    vk::DebugUtilsMessageSeverityFlagBitsEXT::eError | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo,
                    vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
                    debugCallback));
            }
            spdlog::info("Initialized Vulkan with {} layer(s) and {} extension(s)", layers.size(), extensions.size());

            if(!config.headless && !config.workaround_no_swapchain) {
                VkSurfaceKHR surface_ = VK_NULL_HANDLE;
                if(!sdl::vk::CreateSurface(win.get(), instance.get(), &surface_) || surface_ == VK_NULL_HANDLE) {
                    throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface failed: ") + sdl::GetError());
                }
                vk::detail::ObjectDestroy<vk::Instance, vk::DispatchLoaderDynamic> deleter(instance.get(), nullptr, instance.getDispatch());
                surface = vk::UniqueSurfaceKHR(surface_, deleter);
            }

            auto devices = instance->enumeratePhysicalDevices();
            if(devices.empty())
            {
                spdlog::error("Found no video device with Vulkan support!");
                throw std::runtime_error("no vulkan device");
            }
            spdlog::debug("Found {} video devices:", devices.size());

            const char* env;
            std::optional<unsigned int> device_index = config.device_index;
            if(!device_index.has_value() && (env = std::getenv("AUROREENGINE_DEVICE_INDEX"))) {
                device_index = std::stoi(env);
            }
            std::optional<std::string> device_name = config.device_name;
            if(!device_name.has_value() && (env = std::getenv("AUROREENGINE_DEVICE_NAME"))) {
                device_name = env;
            }
            std::optional<std::string> device_uuid = config.device_uuid;
            if(!device_uuid.has_value() && (env = std::getenv("AUROREENGINE_DEVICE_UUID"))) {
                device_uuid = env;
            }

            std::multimap<int, vk::PhysicalDevice> candidates;
            for(unsigned int i=0; i<devices.size(); i++)
            {
                const auto& dev = devices[i];
                auto props = dev.getProperties();
                auto uuid = props.pipelineCacheUUID;
                std::ostringstream uuidStrStream;
                uuidStrStream << std::hex << std::setfill('0');
                for(unsigned char i : uuid)
                {
                    uuidStrStream << std::setw(2) << static_cast<unsigned int>(i);
                }
                std::string uuidStr = uuidStrStream.str();

                int score = rateDeviceSuitability(dev);
                spdlog::debug("- Device {} \"{}\" ({}) -> {}", vk::to_string(props.deviceType), std::string{props.deviceName}, uuidStr, score);

                if(device_index.has_value() && device_index.value() == i) {
                    score = std::numeric_limits<int>::max();
                }
                if(device_name.has_value() && device_name.value() == std::string{props.deviceName}) {
                    score = std::numeric_limits<int>::max();
                }
                if(device_uuid.has_value() && device_uuid.value() == uuidStr) {
                    score = std::numeric_limits<int>::max();
                }

                if(score == std::numeric_limits<int>::max()) {
                    spdlog::info("Force selected device {} \"{}\" ({})", vk::to_string(props.deviceType), std::string{props.deviceName}, uuidStr);
                }
                candidates.insert({score, dev});
            }
            if(candidates.rbegin()->first > 0) {
                physicalDevice = candidates.rbegin()->second;
            } else {
                spdlog::error("Found suitable video device!");
                throw std::runtime_error("no suitable device");
            }

            deviceProperties = physicalDevice.getProperties();
            queueFamilyIndices = findQueueFamilies(physicalDevice);
            if(!config.headless && !config.workaround_no_swapchain) {
                swapchainSupport = querySwapChainSupport(physicalDevice);
                if(swapchainSupport.formats.empty() || swapchainSupport.presentModes.empty()) {
                    throw std::runtime_error("Selected Vulkan device has no compatible swapchain formats or present modes");
                }
            }
            spdlog::info("Using video device {} of type {}", std::string{deviceProperties.deviceName}, vk::to_string(deviceProperties.deviceType));

            vk::SampleCountFlags supportedSamples = deviceProperties.limits.framebufferColorSampleCounts;
            if(!(supportedSamples & config.sampleCount)) {
                spdlog::warn("Sample count {} not supported, searching for closest match", vk::to_string(config.sampleCount));
                constexpr std::array<vk::SampleCountFlagBits, 7> sampleCounts = {
                    vk::SampleCountFlagBits::e1,
                    vk::SampleCountFlagBits::e2,
                    vk::SampleCountFlagBits::e4,
                    vk::SampleCountFlagBits::e8,
                    vk::SampleCountFlagBits::e16,
                    vk::SampleCountFlagBits::e32,
                    vk::SampleCountFlagBits::e64,
                };
                int index = std::distance(sampleCounts.begin(), std::ranges::find(sampleCounts, config.sampleCount));
                for(int i=1; i<8; i++)
                {
                    if(index+i < sampleCounts.size() && (supportedSamples & sampleCounts[index+i]))
                    {
                        config.sampleCount = sampleCounts[index+i];
                        spdlog::warn("Using sample count {}", vk::to_string(config.sampleCount));
                        break;
                    }
                    if(index-i >= 0 && (supportedSamples & sampleCounts[index-i]))
                    {
                        config.sampleCount = sampleCounts[index-i];
                        spdlog::warn("Using sample count {}", vk::to_string(config.sampleCount));
                        break;
                    }
                }
            }

            std::vector<vk::DeviceQueueCreateInfo> queueInfos;
            std::set<uint32_t> uniqueQueueFamilies = {queueFamilyIndices.graphicsFamily.value(), queueFamilyIndices.presentFamily.value(),
                queueFamilyIndices.transferFamily.value_or(UINT32_MAX)};
            auto families = physicalDevice.getQueueFamilyProperties();

            // Bound the number of queues we request to avoid oversubscription on low-core CPUs
            unsigned int desiredTransferQueues = []{
                unsigned int hc = std::thread::hardware_concurrency();
                if(hc == 0) hc = 2;
                // Use up to half the cores, clamped between 1 and 4
                unsigned int n = std::max(1u, std::min(4u, hc/2));
                return n;
            }();

            // Prepare a priorities array large enough for the largest queueCount we will request
            auto maxRequested = 1u;
            if(queueFamilyIndices.transferFamily.has_value()) {
                maxRequested = std::max(maxRequested, std::min<unsigned int>(families[queueFamilyIndices.transferFamily.value()].queueCount, desiredTransferQueues));
            }
            std::vector<float> priorities(maxRequested, 1.0f);

            for(uint32_t queueFamily : uniqueQueueFamilies)
            {
                if(queueFamily == UINT32_MAX)
                    continue;

                uint32_t count = 1;
                if(queueFamilyIndices.transferFamily.has_value() && queueFamily == queueFamilyIndices.transferFamily.value()) {
                    count = std::min<uint32_t>(families[queueFamily].queueCount, desiredTransferQueues);
                }

                queueInfos.emplace_back()
                    .setQueueFamilyIndex(queueFamily)
                    .setQueueCount(count)
                    .setPQueuePriorities(priorities.data());
            }

            auto featureChain = physicalDevice.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan12Features>();
            auto supportedFeatures = featureChain.get<vk::PhysicalDeviceFeatures2>().features;
            auto supportedVulkan12Features = featureChain.get<vk::PhysicalDeviceVulkan12Features>();
            if(std::getenv("AUROREENGINE_NO_FEATURES")) {
                spdlog::warn("Disabling all device features");
                supportedFeatures = vk::PhysicalDeviceFeatures();
                supportedVulkan12Features = vk::PhysicalDeviceVulkan12Features();
            }
            vk::PhysicalDeviceFeatures features = vk::PhysicalDeviceFeatures()
                .setGeometryShader(supportedFeatures.geometryShader)
                .setSampleRateShading(supportedFeatures.sampleRateShading)
                .setFillModeNonSolid(supportedFeatures.fillModeNonSolid)
                .setShaderSampledImageArrayDynamicIndexing(supportedFeatures.shaderSampledImageArrayDynamicIndexing)
                .setWideLines(supportedFeatures.wideLines)
                .setMultiDrawIndirect(supportedFeatures.multiDrawIndirect)
                .setDrawIndirectFirstInstance(supportedFeatures.drawIndirectFirstInstance);
            vk::PhysicalDeviceVulkan12Features vulkan12Features = vk::PhysicalDeviceVulkan12Features()
                .setDescriptorIndexing(supportedVulkan12Features.descriptorBindingPartiallyBound) // TODO: fix
                .setDescriptorBindingPartiallyBound(supportedVulkan12Features.descriptorBindingPartiallyBound)
                .setDescriptorBindingSampledImageUpdateAfterBind(supportedVulkan12Features.descriptorBindingSampledImageUpdateAfterBind)
                .setDrawIndirectCount(supportedVulkan12Features.drawIndirectCount);
            vk::PhysicalDeviceFeatures2 features2 = vk::PhysicalDeviceFeatures2()
                .setFeatures(features)
                .setPNext(&vulkan12Features);

            std::vector<const char*> deviceExtensions = {
                VK_KHR_MAINTENANCE_3_EXTENSION_NAME,
            };
            if(vulkan12Features.descriptorBindingPartiallyBound) {
                deviceExtensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
            }
            if(!config.headless && !config.workaround_no_swapchain) {
                deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
            }
            const auto availableDeviceExtensions = physicalDevice.enumerateDeviceExtensionProperties();
            if(std::ranges::any_of(availableDeviceExtensions, [](const auto& extension) {
                return std::string_view{extension.extensionName} == "VK_KHR_portability_subset";
            })) deviceExtensions.push_back("VK_KHR_portability_subset");
            vk::DeviceCreateInfo device_info = vk::DeviceCreateInfo()
                .setQueueCreateInfos(queueInfos)
                .setPEnabledLayerNames(layers)
                .setPEnabledExtensionNames(deviceExtensions)
                .setPNext(&features2);
            gpuFeatures.features = features;
            gpuFeatures.vulkan12Features = vulkan12Features;
            gpuFeatures.limits = physicalDevice.getProperties().limits;

            device = physicalDevice.createDeviceUnique(device_info);
            VULKAN_HPP_DEFAULT_DISPATCHER.init(*device);
            graphicsQueue = device->getQueue(queueFamilyIndices.graphicsFamily.value(), 0);
            presentQueue = device->getQueue(queueFamilyIndices.presentFamily.value(), 0);
            if(queueFamilyIndices.transferFamily.has_value())
            {
                int index = queueFamilyIndices.transferFamily.value();
                unsigned int count = std::min<unsigned int>(families[index].queueCount, desiredTransferQueues);
                for(unsigned int i=0; i<count; i++)
                {
                    transferQueues.push_back(device->getQueue(index, i));
                }
            }
            else
            {
                transferQueues.push_back(graphicsQueue);
            }

            vma::AllocatorCreateInfo allocator_info({}, physicalDevice, device.get());
            allocator_info.setInstance(instance.get());
            vma::VulkanFunctions functs{instance.getDispatch().vkGetInstanceProcAddr, device.getDispatch().vkGetDeviceProcAddr};
            allocator_info.setPVulkanFunctions(&functs);
            allocator = vma::createAllocator(allocator_info);
            allocatorInitialized = true;

            // Upload command buffers publish resources for shader and vertex reads, so keep
            // them on the graphics family until cross-family ownership transfers are added.
            loader = std::make_unique<resource_loader>(device.get(), allocator,
                queueFamilyIndices.graphicsFamily.value(),
                queueFamilyIndices.graphicsFamily.value(),
                std::vector<vk::Queue>{graphicsQueue}, queueLock);

            if(config.headless || config.workaround_no_swapchain) {
                swapchainFormat = vk::SurfaceFormatKHR{
                    (config.headless_terminal && !config.workaround_no_swapchain) ? vk::Format::eR8G8B8A8Srgb : vk::Format::eB8G8R8A8Srgb,
                    vk::ColorSpaceKHR::eSrgbNonlinear
                };
                swapchainExtent = config.workaround_no_swapchain ? vk::Extent2D{window_width, window_height} : vk::Extent2D{config.width, config.height};
                swapchainImageCount = MAX_FRAMES_IN_FLIGHT;
                swapchainPresentMode = vk::PresentModeKHR::eImmediate;
                swapchainFinalLayout = vk::ImageLayout::eTransferSrcOptimal;
                spdlog::debug("Using fake swapchain with {} images of format {}, extent {}x{}",
                    swapchainImageCount, vk::to_string(swapchainFormat.format), swapchainExtent.width, swapchainExtent.height);

                swapchainImages.clear();
                swapchainImageViews.clear();
                swapchainImageViewsRaw.clear();
                for(unsigned int i=0; i<swapchainImageCount; i++) {
                    headlessTextures.emplace_back(device.get(), allocator,
                        static_cast<int>(swapchainExtent.width), static_cast<int>(swapchainExtent.height),
                        vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled,
                        swapchainFormat.format, vk::SampleCountFlagBits::e1);
                    swapchainImages.push_back(headlessTextures.back().image);
                    vk::ImageViewCreateInfo view_info({}, swapchainImages.back(), vk::ImageViewType::e2D, swapchainFormat.format,
                        vk::ComponentMapping{}, vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
                    swapchainImageViews.push_back(device->createImageViewUnique(view_info));
                    swapchainImageViewsRaw.push_back(swapchainImageViews.back().get());

                    vk::BufferCreateInfo buffer_info({}, swapchainExtent.width*swapchainExtent.height*4, vk::BufferUsageFlagBits::eTransferDst);
                    vma::AllocationCreateInfo alloc_info({}, vma::MemoryUsage::eGpuToCpu);
                    auto [buf, alloc] = allocator.createBufferUnique(buffer_info, alloc_info);
                    headlessOutputBuffers.push_back(std::move(buf));
                    headlessOutputAllocations.push_back(std::move(alloc));

                    headlessFences.push_back(device->createFenceUnique(vk::FenceCreateInfo()));
                }

                vk::CommandPoolCreateInfo pool_info({}, queueFamilyIndices.graphicsFamily.value());
                headlessCommandPool = device->createCommandPoolUnique(vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer, queueFamilyIndices.graphicsFamily.value()));
                headlessCommandBuffersPre = device->allocateCommandBuffers(vk::CommandBufferAllocateInfo(headlessCommandPool.get(), vk::CommandBufferLevel::ePrimary, swapchainImageCount));
                headlessCommandBuffersPost = device->allocateCommandBuffers(vk::CommandBufferAllocateInfo(headlessCommandPool.get(), vk::CommandBufferLevel::ePrimary, swapchainImageCount));
            } else {
                auto formatIt = std::ranges::find_if(swapchainSupport.formats, [](auto f){
                    return f.format == vk::Format::eB8G8R8A8Srgb && f.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
                });
                swapchainFormat = formatIt == swapchainSupport.formats.end() ? swapchainSupport.formats[0] : *formatIt;
                if(spdlog::should_log(spdlog::level::debug))
                {
                    auto props = physicalDevice.getFormatProperties(swapchainFormat.format);
                    spdlog::debug("Swapchain format: {}, color space: {}, linear tiling features: {}, optimal tiling features: {}, buffer features: {}",
                        vk::to_string(swapchainFormat.format), vk::to_string(swapchainFormat.colorSpace),
                        vk::to_string(props.linearTilingFeatures), vk::to_string(props.optimalTilingFeatures),
                        vk::to_string(props.bufferFeatures));
                }

                recreateSwapchain(false);
            }

            if(config.headless || config.workaround_no_swapchain)resetPresentationSemaphores();
            for(int frame=0;frame<MAX_FRAMES_IN_FLIGHT;++frame)
            {
                imageAvailableSemaphores.push_back(device->createSemaphoreUnique(vk::SemaphoreCreateInfo()));
                fences.push_back(device->createFenceUnique(vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled)));

                inFlightFences.push_back(fences.back().get());
            }
            imagesInFlight.assign(swapchainImageCount,vk::Fence());

#if !defined(NDEBUG) && __linux__
            debugName(device.get(), graphicsQueue, "Graphics Queue");
            debugName(device.get(), presentQueue, "Present Queue");
            for(int i=0; i<MAX_FRAMES_IN_FLIGHT; i++)
            {
                debugName(device.get(), imageAvailableSemaphores[i].get(), "Image available Semaphore #"+std::to_string(i));
                debugName(device.get(), fences[i].get(), "Render Fence #"+std::to_string(i));
            }
#endif

            {
                auto path = get_cache_dir() / config.name / "pipeline_cache.bin";

                std::ifstream in(path, std::ios::binary);
                if(in) {
                    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                    spdlog::debug("Loaded pipeline cache of {} bytes", data.size()*sizeof(decltype(data)::value_type));
                    vk::PipelineCacheCreateInfo cache_info({}, data.size(), data.data());
                    pipelineCache = device->createPipelineCacheUnique(cache_info);
                }
                else {
                    spdlog::debug("No existing pipeline cache found");
                    vk::PipelineCacheCreateInfo cache_info({}, 0, nullptr);
                    pipelineCache = device->createPipelineCacheUnique(cache_info);
                }
            }
        }

        int rateDeviceSuitability(vk::PhysicalDevice phyDev) {
            int score = 0;

            auto props = phyDev.getProperties();
            if(props.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
                score += 100000;
            if(props.deviceType == vk::PhysicalDeviceType::eIntegratedGpu)
                score += 50000;
            score += props.limits.maxImageDimension2D;

            if(!findQueueFamilies(phyDev).isComplete())
                score = 0;
            if(score > 0 && !config.headless && !config.workaround_no_swapchain) {
                auto support = querySwapChainSupport(phyDev);
                if(support.formats.empty() || support.presentModes.empty()) {
                    score = 0;
                }
            }

            return score;
        }
        QueueFamilyIndices findQueueFamilies(vk::PhysicalDevice phyDev) {
            const bool usesSurface = !config.headless && !config.workaround_no_swapchain;
            auto families = phyDev.getQueueFamilyProperties();

            std::vector<detail::queue_family_caps> caps;
            caps.reserve(families.size());
            uint32_t index = 0;
            for(auto& family : families)
            {
                caps.push_back({
                    .graphics = static_cast<bool>(family.queueFlags & vk::QueueFlagBits::eGraphics),
                    .transfer = static_cast<bool>(family.queueFlags & vk::QueueFlagBits::eTransfer),
                    .present = usesSurface && phyDev.getSurfaceSupportKHR(index, surface.get()) == VK_TRUE,
                });
                index++;
            }
            const auto selected = detail::select_queue_families(caps, usesSurface);

            QueueFamilyIndices indices{};
            indices.graphicsFamily = selected.graphics;
            indices.presentFamily = selected.present;
            indices.transferFamily = selected.transfer;
            return indices;
        }
        SwapChainSupportDetails querySwapChainSupport(vk::PhysicalDevice phyDev) {
            SwapChainSupportDetails details;
            details.capabilities = phyDev.getSurfaceCapabilitiesKHR(surface.get());
            details.formats = phyDev.getSurfaceFormatsKHR(surface.get());
            details.presentModes = phyDev.getSurfacePresentModesKHR(surface.get());

            return details;
        }

        void handle_sdl_events(bool& should_exit) {
            sdl::Event event;
            while(sdl::PollEvent(&event)) {
                switch(event.type) {
                    case sdl::EventType::SDL_QUIT:
                        should_exit = true;
                        return;
                    case sdl::EventType::SDL_KEYDOWN:
                        if(keyboard_handler)
                            keyboard_handler->key_down(event.key.keysym);
                        break;
                    case sdl::EventType::SDL_KEYUP:
                        if(keyboard_handler)
                            keyboard_handler->key_up(event.key.keysym);
                        break;
                    case sdl::EventType::SDL_WINDOWEVENT:
                        if(event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || event.window.event == SDL_WINDOWEVENT_RESIZED) {
                            window_width = static_cast<uint32_t>(std::max(1, event.window.data1));
                            window_height = static_cast<uint32_t>(std::max(1, event.window.data2));
                            swapchainDirty = true;
                        }
                        break;
                    case sdl::EventType::SDL_CONTROLLERBUTTONDOWN:
                        if(controller_handler)
                            controller_handler->button_down(controllers[event.cbutton.which].get(),
                                static_cast<sdl::GameControllerButton>(event.cbutton.button)
                            );
                        break;
                    case sdl::EventType::SDL_CONTROLLERBUTTONUP:
                        if(controller_handler)
                            controller_handler->button_up(controllers[event.cbutton.which].get(),
                                static_cast<sdl::GameControllerButton>(event.cbutton.button)
                            );
                        break;
                    case sdl::EventType::SDL_CONTROLLERAXISMOTION:
                        if(controller_handler)
                            controller_handler->axis_motion(controllers[event.caxis.which].get(),
                                static_cast<sdl::GameControllerAxis>(event.caxis.axis),
                                event.caxis.value
                            );
                        break;
                    case sdl::EventType::SDL_CONTROLLERDEVICEADDED:
                        if(sdl::IsGameController(event.cdevice.which)) {
                            auto controller = sdl::GameControllerOpen(event.cdevice.which);
                            if(controller) {
                                sdl::JoystickID id = sdl::JoystickInstanceID(sdl::GameControllerGetJoystick(controller));
                                controllers[id] = std::unique_ptr<sdl::GameController, sdl_controller_closer>(controller);
                                spdlog::debug("Connected controller \"{}\" with id {}", sdl::GameControllerName(controller), id);
                                if(controller_handler)
                                    controller_handler->add_controller(controller);
                            } else {
                                spdlog::warn("Failed to open controller {}: {}", event.cdevice.which, sdl::GetError());
                            }
                        }
                        break;
                    case sdl::EventType::SDL_CONTROLLERDEVICEREMOVED:
                        if(controller_handler)
                            controller_handler->remove_controller(controllers[event.cdevice.which].get());
                        if(controllers.contains(event.cdevice.which)) {
                            spdlog::debug("Disconnected controller \"{}\" with id {}", sdl::GameControllerName(controllers[event.cdevice.which].get()), event.cdevice.which);
                            controllers.erase(event.cdevice.which);
                        }
                }
            }
        }

        void handle_headless_commands(bool& should_exit) {
            while(input_available(STDIN_FILENO) && !std::cin.eof()) {
                std::string line;
                std::getline(std::cin, line);

                std::istringstream iss(line);
                std::string cmd;
                iss >> cmd;
                if(cmd == "quit" || cmd == "exit") {
                    should_exit = true;
                    return;
                } else if(cmd == "keyboard") {
                    std::string subcmd;
                    iss >> subcmd;

                    sdl::Keysym keysym;
                    std::underlying_type_t<decltype(keysym.scancode)> scancode{};
                    iss >> scancode >> keysym.sym >> keysym.mod;
                    keysym.scancode = static_cast<decltype(keysym.scancode)>(scancode);

                    if(subcmd == "down") {
                        if(keyboard_handler) {
                            keyboard_handler->key_down(keysym);
                        }
                    } else if(subcmd == "up") {
                        if(keyboard_handler) {
                            keyboard_handler->key_up(keysym);
                        }
                    }
                }
            }
        }

        void handle_headless_terminal(bool& should_exit) {
            const auto emulate_key = [this](sdl::Scancode scancode, sdl::KeyCode sym, sdl::Keymod mod) {
                if(keyboard_handler) {
                    sdl::Keysym keysym{scancode, sym, static_cast<uint16_t>(mod)};
                    keyboard_handler->key_down(keysym);
                    keyboard_handler->key_up(keysym);
                }
            };

            using sdl::Scancode;
            using sdl::KeyCode;
            using sdl::Keymod;
            while(input_available(STDIN_FILENO) && !std::cin.eof()) {
                int c = std::cin.get();
                if(c == '\e') {
                    c = std::cin.get();
                    if(c == '[') {
                        c = std::cin.get();
                        switch(c) {
                            case 'A': // up
                                emulate_key(Scancode::SDL_SCANCODE_UP, KeyCode::SDLK_UP, Keymod::KMOD_NONE);
                                break;
                            case 'B': // down
                                emulate_key(Scancode::SDL_SCANCODE_DOWN, KeyCode::SDLK_DOWN, Keymod::KMOD_NONE);
                                break;
                            case 'C': // right
                                emulate_key(Scancode::SDL_SCANCODE_RIGHT, KeyCode::SDLK_RIGHT, Keymod::KMOD_NONE);
                                break;
                            case 'D': // left
                                emulate_key(Scancode::SDL_SCANCODE_LEFT, KeyCode::SDLK_LEFT, Keymod::KMOD_NONE);
                                break;
                            default:
                                spdlog::warn("Unknown escape sequence: \\e[\\x5b{}", c);
                                break;
                        }
                    } else if(c == '\x1b') {
                        emulate_key(Scancode::SDL_SCANCODE_ESCAPE, KeyCode::SDLK_ESCAPE, Keymod::KMOD_NONE);
                    } else {
                        spdlog::warn("Unknown escape sequence: \\e{}", c);
                    }
                } else {
                    switch(c) {
                        case '\n': emulate_key(Scancode::SDL_SCANCODE_RETURN, KeyCode::SDLK_RETURN, Keymod::KMOD_NONE); break;
                        case '\r': emulate_key(Scancode::SDL_SCANCODE_RETURN, KeyCode::SDLK_RETURN, Keymod::KMOD_NONE); break;
                        case '\t': emulate_key(Scancode::SDL_SCANCODE_TAB, KeyCode::SDLK_TAB, Keymod::KMOD_NONE); break;
                        case '\b': emulate_key(Scancode::SDL_SCANCODE_BACKSPACE, KeyCode::SDLK_BACKSPACE, Keymod::KMOD_NONE); break;
                        default: {
                            if(c >= 'a' && c <= 'z') {
                                emulate_key(static_cast<Scancode>(Scancode::SDL_SCANCODE_A + c - 'a'), static_cast<KeyCode>(KeyCode::SDLK_a + c - 'a'), Keymod::KMOD_NONE);
                            } else if(c >= 'A' && c <= 'Z') {
                                emulate_key(static_cast<Scancode>(Scancode::SDL_SCANCODE_A + c - 'A'), static_cast<KeyCode>(KeyCode::SDLK_a + c - 'A'), Keymod::KMOD_SHIFT);
                            } else if(c >= '0' && c <= '9') {
                                emulate_key(static_cast<Scancode>(Scancode::SDL_SCANCODE_0 + c - '0'), static_cast<KeyCode>(KeyCode::SDLK_0 + c - '0'), Keymod::KMOD_NONE);
                            } else if(c == ' ') {
                                emulate_key(Scancode::SDL_SCANCODE_SPACE, KeyCode::SDLK_SPACE, Keymod::KMOD_NONE);
                            } else {
                                spdlog::warn("Unknown character: {}", c);
                            }
                        }
                    }
                }
            }
        }
};

}
