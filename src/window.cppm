module;

#include <cstdint>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <vector>

#include <termios.h>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_hpp_macros.hpp>

export module dreamrender:window;

import :resource_loader;
import :phase;
import :input;
import :utils;

import sdl2;
import vulkan_hpp;
import vma;

namespace dreamrender {

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
};

export class window
{
    public:
        window(window_config config) : config(config) {
            if(std::getenv("DREAMRENDER_HEADLESS")) {
                this->config.headless = true;
            }
        }
        window(window_config&& config) : config(std::move(config)) {
            if(std::getenv("DREAMRENDER_HEADLESS")) {
                this->config.headless = true;
            }
        }
        ~window() {
            spdlog::debug("Destroying window");

            std::scoped_lock lock(renderLock);
            device->waitIdle();
            {
                auto data = device->getPipelineCacheData(pipelineCache.get());
                spdlog::debug("Saving pipeline cache of {} bytes", data.size()*sizeof(decltype(data)::value_type));
                std::filesystem::path cache_dir = std::filesystem::path{std::getenv("HOME")} / ".cache";
                auto path = cache_dir / "dreamrender" / config.name / "pipeline_cache.bin";

                if(!std::filesystem::exists(path.parent_path()))
                    std::filesystem::create_directories(path.parent_path());

                std::ofstream out(path, std::ios::binary);
                std::copy(data.begin(), data.end(), std::ostream_iterator<uint8_t>(out));
            }
            current_renderer.reset();
            loader.reset();

            allocator.destroy();

            if(!config.headless) {
                sdl::mix::Quit();
            }
        }

        void init() {
            if(config.headless) {
                sdl::SetHint("SDL_NO_SIGNAL_HANDLERS", "1");
                if(const char* c = std::getenv("DREAMRENDER_HEADLESS_TERMINAL")) {
                    config.headless_terminal = true;
                    config.headless_output_dir.clear();

                    spdlog::info("Switching log to stderr");
                    // https://github.com/gabime/spdlog/wiki/0.-FAQ#switch-the-default-logger-to-stderr
                    spdlog::set_default_logger(spdlog::stderr_color_st("temp"));
                    spdlog::set_default_logger(spdlog::stderr_color_st(""));

                    struct termios term{};
                    tcgetattr(STDIN_FILENO, &term);
                    term.c_lflag &= ~ICANON & ~ECHO;
                    tcsetattr(STDIN_FILENO, TCSANOW, &term);

                    static std::vector<char> buffer(32*1024*1024); // 32 MiB buffer, so it is smooth
                    std::setvbuf(stdout, buffer.data(), _IOFBF, buffer.size());
                }
                if(const char* c = std::getenv("DREAMRENDER_HEADLESS_OUTPUT_DIR")) {
                    config.headless_output_dir = c;
                }
                if(const char* c = std::getenv("DREAMRENDER_HEADLESS_OUTPUT_PATTERN")) {
                    config.headless_output_format = c;
                }
                if(const char* c = std::getenv("DREAMRENDER_HEADLESS_OUTPUT_QUALITY")) {
                    config.headless_output_quality = std::stoi(c);
                }
                if(const char* c = std::getenv("DREAMRENDER_HEADLESS_WIDTH")) {
                    config.width = std::stoi(c);
                }
                if(const char* c = std::getenv("DREAMRENDER_HEADLESS_HEIGHT")) {
                    config.height = std::stoi(c);
                }
                if(const char* c = std::getenv("DREAMRENDER_HEADLESS_FRAMES")) {
                    config.headless_frames = std::stoi(c);
                }
                if(!config.headless_output_dir.empty() &&
                   !std::filesystem::exists(config.headless_output_dir))
                {
                    std::filesystem::create_directories(config.headless_output_dir);
                }
            }
            static sdl::initializer sdl_init;
            if(!config.headless) {
                initWindow();
                initAudio();
                initInput();
            }
            initVulkan();
        }
        void loop() {
            startTime = std::chrono::high_resolution_clock::now();
            lastFrame = std::chrono::high_resolution_clock::now();
            lastFPS = std::chrono::high_resolution_clock::now();
            framesInSecond = 0;

            int currentFrame = 0;
            vk::Result r{};
            for(;;) {
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
                    if(quit) {
                        return;
                    }
                } else {
                    bool quit = false;
                    handle_sdl_events(quit);
                    if(quit) {
                        return;
                    }

                    auto now = std::chrono::high_resolution_clock::now();
                    auto dt = std::chrono::duration<double>(now - lastFrame).count();
                    lastFrame = now;

                    using vk::PresentModeKHR::eFifo;
                    using vk::PresentModeKHR::eFifoRelaxed;
                    if(config.fpsLimit > 0 && (config.fpsLimit < refreshRate ||
                        ~(swapchainPresentMode == eFifo || swapchainPresentMode == eFifoRelaxed)))
                    {
                        auto frame_time = std::chrono::duration<double>(std::chrono::seconds(1)) / config.fpsLimit;
                        auto sleep = frame_time - std::chrono::duration<double>(now - lastFrame);
                        if(sleep > frame_time / 10) {
                            std::this_thread::sleep_for(sleep);
                        }
                    }
                }

                std::scoped_lock lock(renderLock);
                r = device->waitForFences(inFlightFences[currentFrame], true, UINT64_MAX);
                if(r != vk::Result::eSuccess)
                    spdlog::error("Waiting for inFlightFences[{}] failed with result {}", currentFrame, vk::to_string(r));

                unsigned int imageIndex = 0;
                if(config.headless) {
                    // This is incredibly weird and hacky, but by some miracle it works.
                    imageIndex = (currentFrame)%swapchainImageCount;

                    if(imagesInFlight[imageIndex]) {
                        r = device->waitForFences({imagesInFlight[imageIndex], headlessFences[imageIndex].get()}, true, UINT64_MAX);
                        if(r != vk::Result::eSuccess)
                            spdlog::error("Waiting for imagesInFlight[{0}] and headlessFences[{0}] failed with result {1}", imageIndex, vk::to_string(r));

                        if(!config.headless_output_dir.empty() || config.headless_terminal) {
                            std::vector<char> data(swapchainExtent.width*swapchainExtent.height*4);
                            r = allocator.copyAllocationToMemory(headlessOutputAllocations[imageIndex].get(),
                                0, data.data(), swapchainExtent.width*swapchainExtent.height*4);
                            if(r != vk::Result::eSuccess)
                                spdlog::error("Copying headlessOutputAllocations[{}] to memory failed with result {}", imageIndex, vk::to_string(r));

                            if(!config.headless_output_dir.empty()) {
                                sdl::unique_surface surface{sdl::CreateRGBSurfaceWithFormatFrom(data.data(),
                                    swapchainExtent.width, swapchainExtent.height, 32, swapchainExtent.width*4,
                                    sdl::PixelFormatEnumVales::ABGR8888)};
                                std::filesystem::path path = config.headless_output_dir / std::vformat(config.headless_output_format, std::make_format_args(totalFrameNumber));
                                std::string ext = path.extension();
                                if(ext == ".png") {
                                    sdl::image::SavePNG(surface.get(), path.c_str());
                                } else if(ext == ".jpg" || ext == ".jpeg") {
                                    sdl::image::SaveJPG(surface.get(), path.c_str(), config.headless_output_quality);
                                } else {
                                    spdlog::error("Unknown output format \"{}\". Supported formats are: PNG, JPG", ext);
                                    spdlog::warn("Disabling output of images!");
                                    config.headless_output_dir.clear();
                                }
                            }
                            if(config.headless_terminal) {
                                terminal_output(data, swapchainExtent, std::cout);
                                std::cout << std::flush;
                            }
                        }
                    }

                    // We need this just to signal the semaphore. THIS IS BAD. Oh well...
                    vk::CommandBuffer& commandBuffer = headlessCommandBuffersPre[imageIndex];
                    commandBuffer.begin(vk::CommandBufferBeginInfo());
                    commandBuffer.end();

                    vk::PipelineStageFlags waitStages = vk::PipelineStageFlagBits::eColorAttachmentOutput;
                    vk::SubmitInfo submitInfo(0, {}, {}, 1, &commandBuffer, 1, &imageAvailableSemaphores[currentFrame].get());
                    graphicsQueue.submit(submitInfo, {}); // no fence here, we just don't give a shit anymore
                } else {
                    std::tie(r, imageIndex) = device->acquireNextImageKHR(swapchain.get(), UINT64_MAX, imageAvailableSemaphores[currentFrame].get());
                    if(imagesInFlight[imageIndex]) {
                        r = device->waitForFences(imagesInFlight[imageIndex], true, UINT64_MAX);
                        if(r != vk::Result::eSuccess)
                            spdlog::error("Waiting for imagesInFlight[{}] failed with result {}", imageIndex, vk::to_string(r));
                    }
                }
                imagesInFlight[imageIndex] = inFlightFences[currentFrame];

                device->resetFences(fences[currentFrame].get());

                if(!current_renderer)
                    throw std::runtime_error("No renderer set!");
                current_renderer->render(imageIndex, imageAvailableSemaphores[currentFrame].get(), renderFinishedSemaphores[currentFrame].get(), inFlightFences[currentFrame]);

                if(config.headless) {
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

                    vk::PipelineStageFlags waitStages = vk::PipelineStageFlagBits::eColorAttachmentOutput;
                    vk::SubmitInfo submitInfo(1, &renderFinishedSemaphores[currentFrame].get(), &waitStages, 1, &commandBuffer, 0, {});
                    graphicsQueue.submit(submitInfo, headlessFences[imageIndex].get()); // we need this fence, or everything explodes
                } else {
                    vk::PresentInfoKHR present_info(renderFinishedSemaphores[currentFrame].get(), swapchain.get(), imageIndex);
                    r = presentQueue.presentKHR(present_info);
                    if(r == vk::Result::eSuboptimalKHR) {
                        spdlog::debug("Suboptiomal present result; recreating swapchain");
                        recreateSwapchain();
                    } else if(r != vk::Result::eSuccess) {
                        spdlog::error("Present failed with result {}", vk::to_string(r));
                    }
                }

                currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;

                totalFrameNumber++;
                framesInSecond++;
                auto t = std::chrono::high_resolution_clock::now();
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
            spdlog::debug("(Re)Creating swapchain");

            if(recreate) {
                graphicsQueue.waitIdle();
            }

            auto presentModeIt = std::find_if(swapchainSupport.presentModes.begin(), swapchainSupport.presentModes.end(), [this](auto p){
                return p == config.preferredPresentMode;
            });
            swapchainPresentMode = presentModeIt == swapchainSupport.presentModes.end() ? swapchainSupport.presentModes[0] : *presentModeIt;

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
            swapchain_info.setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eInherit);
            if(swapchainSupport.capabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::ePreMultiplied)
                swapchain_info.setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::ePreMultiplied);
            swapchain = device->createSwapchainKHRUnique(swapchain_info);
            swapchainImages = device->getSwapchainImagesKHR(swapchain.get());

            swapchainImageViews.clear();
            swapchainImageViewsRaw.clear();
            for(auto& image : swapchainImages)
            {
                vk::ImageViewCreateInfo view_info({}, image, vk::ImageViewType::e2D, swapchainFormat.format,
                    vk::ComponentMapping{}, vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
                swapchainImageViews.push_back(device->createImageViewUnique(view_info));
                swapchainImageViewsRaw.push_back(swapchainImageViews.back().get());
            }

            if(recreate && current_renderer) {
                current_renderer->prepare(swapchainImages, swapchainImageViewsRaw);
            }
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
            current_renderer->preload();
            auto tPreload = std::chrono::high_resolution_clock::now();
            current_renderer->prepare(swapchainImages, swapchainImageViewsRaw);
            auto tPrepare = std::chrono::high_resolution_clock::now();
            current_renderer->waitLoad(); // replace with loading screen
            auto tWaitLoad = std::chrono::high_resolution_clock::now();
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
        input::keyboard_handler* keyboard_handler;
        input::controller_handler* controller_handler;

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

        decltype(std::chrono::high_resolution_clock::now()) startTime;
        decltype(std::chrono::high_resolution_clock::now()) lastFrame;

        uint64_t totalFrameNumber{};
        static constexpr int fpsSampleRate = 5;
        uint64_t framesInSecond{};
        decltype(std::chrono::high_resolution_clock::now()) lastFPS;
        int fpsCount{};
        double currentFPS{};
        int refreshRate{};
        std::recursive_mutex renderLock{};

        struct sdl_controller_closer {
            void operator()(sdl::GameController* ptr) const {
                sdl::GameControllerClose(ptr);
            }
        };
        std::map<sdl::JoystickID, std::unique_ptr<sdl::GameController, sdl_controller_closer>> controllers;

#ifndef NDEBUG
        vk::UniqueDebugUtilsMessengerEXT debugMessenger;
#endif
    private:
        void initWindow() {
            sdl::Rect rect;
            sdl::GetDisplayBounds(config.display_index, &rect);

            if(config.x == -1) config.x = rect.x; else rect.x = config.x;
            if(config.y == -1) config.y = rect.y; else rect.y = config.y;
            if(config.width == -1) config.width = rect.w; else rect.w = config.width;
            if(config.height == -1) config.height = rect.h; else rect.h = config.height;

            for(const auto& [key, value] : config.sdl_hints) {
                sdl::SetHint(key.c_str(), value.c_str());
            }
            win = sdl::unique_window(
                sdl::CreateWindow(config.title.c_str(), rect.x, rect.y, rect.w, rect.h,
                    SDL_WINDOW_SHOWN | (config.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) | SDL_WINDOW_VULKAN)
            );
            spdlog::info("Created window ({}x{} @ {}:{}) on monitor \"{}\"", rect.w, rect.h, rect.x, rect.y,
                sdl::GetDisplayName(config.display_index));

            sdl::DisplayMode mode;
            sdl::GetWindowDisplayMode(win.get(), &mode);

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
        }
        void initVulkan() {
            std::scoped_lock lock(renderLock);

            static vk::DynamicLoader dl;
            PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
            VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

            std::vector<const char*> layers = {
                #ifndef NDEBUG
                    "VK_LAYER_KHRONOS_validation",
                #endif
            };
            std::vector<const char*> extensions = {
                #ifndef NDEBUG
                    vk::EXTDebugUtilsExtensionName,
                #endif
                vk::KHRGetPhysicalDeviceProperties2ExtensionName,
            };

            if(!config.headless) {
                uint32_t sdlExtensionCount = 0;
                sdl::vk::GetInstanceExtensions(win.get(), &sdlExtensionCount, nullptr);

                std::vector<const char*> sdlExtensions(sdlExtensionCount);
                sdl::vk::GetInstanceExtensions(win.get(), &sdlExtensionCount, sdlExtensions.data());
                std::copy(sdlExtensions.begin(), sdlExtensions.end(), std::back_inserter(extensions));
            }

            spdlog::debug("Using extensions: {}", fmt::join(extensions, ", "));

            auto const app = vk::ApplicationInfo()
                .setPApplicationName(config.name.c_str())
                .setApplicationVersion(config.version)
                .setPEngineName("dreamrender")
                .setEngineVersion(1)
                .setApiVersion(vk::makeApiVersion(1, 2, 0, 0));
            auto const inst_info = vk::InstanceCreateInfo()
                .setPApplicationInfo(&app)
                .setPEnabledLayerNames(layers)
                .setPEnabledExtensionNames(extensions);
            instance = vk::createInstanceUnique(inst_info);
            VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance);

    #ifndef NDEBUG
            debugMessenger = instance->createDebugUtilsMessengerEXTUnique(vk::DebugUtilsMessengerCreateInfoEXT({},
                    vk::DebugUtilsMessageSeverityFlagBitsEXT::eError | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo,
                    vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
                    debugCallback));
    #endif
            spdlog::info("Initialized Vulkan with {} layer(s) and {} extension(s)", layers.size(), extensions.size());

            if(!config.headless) {
                VkSurfaceKHR surface_;
                sdl::vk::CreateSurface(win.get(), instance.get(), &surface_);
                vk::ObjectDestroy<vk::Instance, vk::DispatchLoaderDynamic> deleter(instance.get(), nullptr, instance.getDispatch());
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
            if(!device_index.has_value() && (env = std::getenv("DREAMRENDER_DEVICE_INDEX"))) {
                device_index = std::stoi(env);
            }
            std::optional<std::string> device_name = config.device_name;
            if(!device_name.has_value() && (env = std::getenv("DREAMRENDER_DEVICE_NAME"))) {
                device_name = env;
            }
            std::optional<std::string> device_uuid = config.device_uuid;
            if(!device_uuid.has_value() && (env = std::getenv("DREAMRENDER_DEVICE_UUID"))) {
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
                for(int i=0; i<uuid.size(); i++)
                {
                    uuidStrStream << std::setw(2) << static_cast<unsigned int>(uuid[i]);
                }
                std::string uuidStr = uuidStrStream.str();

                int score = rateDeviceSuitability(dev);
                spdlog::debug("- Device {} \"{}\" ({}) -> {}", vk::to_string(props.deviceType), props.deviceName, uuidStr, score);

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
                    spdlog::info("Force selected device {} \"{}\" ({})", vk::to_string(props.deviceType), props.deviceName, uuidStr);
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
            if(!config.headless) swapchainSupport = querySwapChainSupport(physicalDevice);
            spdlog::info("Using video device {} of type {}", deviceProperties.deviceName, vk::to_string(deviceProperties.deviceType));

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
                int index = std::distance(sampleCounts.begin(), std::find(sampleCounts.begin(), sampleCounts.end(), config.sampleCount));
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

            std::vector<float> priorities(std::max_element(families.begin(), families.end(), [](auto a, auto b){
                return a.queueCount < b.queueCount;
            })->queueCount);
            std::fill(priorities.begin(), priorities.end(), 1.0f);
            for(uint32_t queueFamily : uniqueQueueFamilies)
            {
                if(queueFamily == UINT32_MAX)
                    continue;

                queueInfos.emplace_back()
                    .setQueueFamilyIndex(queueFamily)
                    .setQueueCount(families[queueFamily].queueCount)
                    .setPQueuePriorities(priorities.data());
            }

            auto featureChain = physicalDevice.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceDescriptorIndexingFeatures>();
            auto supportedFeatures = featureChain.get<vk::PhysicalDeviceFeatures2>().features;
            auto supportedIndexingFeatures = featureChain.get<vk::PhysicalDeviceDescriptorIndexingFeatures>();
            if(std::getenv("DREAMRENDER_NO_FEATURES")) {
                spdlog::warn("Disabling all device features");
                supportedFeatures = vk::PhysicalDeviceFeatures();
                supportedIndexingFeatures = vk::PhysicalDeviceDescriptorIndexingFeatures();
            }
            vk::PhysicalDeviceFeatures features = vk::PhysicalDeviceFeatures()
                .setGeometryShader(supportedFeatures.geometryShader)
                .setSampleRateShading(supportedFeatures.sampleRateShading)
                .setFillModeNonSolid(supportedFeatures.fillModeNonSolid)
                .setShaderSampledImageArrayDynamicIndexing(supportedFeatures.shaderSampledImageArrayDynamicIndexing)
                .setWideLines(supportedFeatures.wideLines);
            vk::PhysicalDeviceDescriptorIndexingFeatures indexingFeatures = vk::PhysicalDeviceDescriptorIndexingFeatures()
                .setDescriptorBindingPartiallyBound(supportedIndexingFeatures.descriptorBindingPartiallyBound)
                .setDescriptorBindingSampledImageUpdateAfterBind(supportedIndexingFeatures.descriptorBindingSampledImageUpdateAfterBind);
            vk::PhysicalDeviceFeatures2 features2 = vk::PhysicalDeviceFeatures2()
                .setFeatures(features)
                .setPNext(&indexingFeatures);

            std::vector<const char*> deviceExtensions = {
                VK_KHR_MAINTENANCE3_EXTENSION_NAME,
            };
            if(indexingFeatures.descriptorBindingPartiallyBound) {
                deviceExtensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
            }
            if(!config.headless) {
                deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
            }
            vk::DeviceCreateInfo device_info = vk::DeviceCreateInfo()
                .setQueueCreateInfos(queueInfos)
                .setPEnabledLayerNames(layers)
                .setPEnabledExtensionNames(deviceExtensions)
                .setPNext(&features2);
            gpuFeatures.features = features;
            gpuFeatures.indexingFeatures = indexingFeatures;
            gpuFeatures.limits = physicalDevice.getProperties().limits;

            device = physicalDevice.createDeviceUnique(device_info);
            VULKAN_HPP_DEFAULT_DISPATCHER.init(*device);
            graphicsQueue = device->getQueue(queueFamilyIndices.graphicsFamily.value(), 0);
            presentQueue = device->getQueue(queueFamilyIndices.presentFamily.value(), 0);
            if(queueFamilyIndices.transferFamily.has_value())
            {
                int index = queueFamilyIndices.transferFamily.value();
                for(int i=0; i<families[index].queueCount; i++)
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

            loader = std::make_unique<resource_loader>(device.get(), allocator,
                queueFamilyIndices.transferFamily.value_or(queueFamilyIndices.graphicsFamily.value()),
                queueFamilyIndices.graphicsFamily.value(),
                transferQueues);

            if(config.headless) {
                swapchainFormat = vk::SurfaceFormatKHR{vk::Format::eR8G8B8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear};
                swapchainExtent = vk::Extent2D{config.width, config.height};
                swapchainImageCount = MAX_FRAMES_IN_FLIGHT;
                swapchainPresentMode = vk::PresentModeKHR::eImmediate;
                swapchainFinalLayout = vk::ImageLayout::eTransferSrcOptimal;

                swapchainImages.clear();
                swapchainImageViews.clear();
                swapchainImageViewsRaw.clear();
                for(unsigned int i=0; i<swapchainImageCount; i++) {
                    headlessTextures.push_back(texture{device.get(), allocator,
                        static_cast<int>(swapchainExtent.width), static_cast<int>(swapchainExtent.height),
                        vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled,
                        swapchainFormat.format, vk::SampleCountFlagBits::e1});
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
                auto formatIt = std::find_if(swapchainSupport.formats.begin(), swapchainSupport.formats.end(), [](auto f){
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

                swapchainExtent = vk::Extent2D{
                    std::clamp(window_width, swapchainSupport.capabilities.minImageExtent.width, swapchainSupport.capabilities.maxImageExtent.width),
                    std::clamp(window_height, swapchainSupport.capabilities.minImageExtent.height, swapchainSupport.capabilities.maxImageExtent.height)
                };

                swapchainImageCount = swapchainSupport.capabilities.minImageCount + 1;
                if(swapchainSupport.capabilities.maxImageCount > 0) {
                    swapchainImageCount = std::min(swapchainImageCount, swapchainSupport.capabilities.maxImageCount);
                }

                recreateSwapchain(false);
            }

            for(auto& image : swapchainImages)
            {
                imageAvailableSemaphores.push_back(device->createSemaphoreUnique(vk::SemaphoreCreateInfo()));
                renderFinishedSemaphores.push_back(device->createSemaphoreUnique(vk::SemaphoreCreateInfo()));
                fences.push_back(device->createFenceUnique(vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled)));

                inFlightFences.push_back(fences.back().get());
                imagesInFlight.push_back(vk::Fence());
            }

    #ifndef NDEBUG
            debugName(device.get(), graphicsQueue, "Graphics Queue");
            debugName(device.get(), presentQueue, "Present Queue");
            for(int i=0; i<swapchainImageCount; i++)
            {
                debugName(device.get(), imageAvailableSemaphores[i].get(), "Image available Semaphore #"+std::to_string(i));
                debugName(device.get(), renderFinishedSemaphores[i].get(), "Render finished Semaphore #"+std::to_string(i));
                debugName(device.get(), fences[i].get(), "Render Fence #"+std::to_string(i));
            }
    #endif

            {
                std::filesystem::path cache_dir = std::filesystem::path{std::getenv("HOME")} / ".cache";
                auto path = cache_dir / "dreamrender" / config.name / "pipeline_cache.bin";

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
                score += 10000;
            if(props.deviceType == vk::PhysicalDeviceType::eIntegratedGpu)
                score += 5000;
            score += props.limits.maxImageDimension2D;

            if(!findQueueFamilies(phyDev).isComplete())
                score = 0;

            return score;
        }
        QueueFamilyIndices findQueueFamilies(vk::PhysicalDevice phyDev) {
            QueueFamilyIndices indices{};

            auto families = phyDev.getQueueFamilyProperties();

            unsigned index = 0;
            for(auto& family : families)
            {
                if(family.queueFlags & vk::QueueFlagBits::eGraphics)
                    indices.graphicsFamily = index;
                else if(family.queueFlags & vk::QueueFlagBits::eTransfer)
                    indices.transferFamily = index;
                if(!config.headless && phyDev.getSurfaceSupportKHR(index, surface.get()))
                    indices.presentFamily = index;
                index++;
            }
            if(config.headless) {
                indices.presentFamily = indices.graphicsFamily;
            }

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
            while(input_available(STDIN_FILENO)) {
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
            while(input_available(STDIN_FILENO)) {
                int c = std::cin.get();
                if(c == '\e') {
                    c = std::cin.get();
                    if(c != '[') {
                        spdlog::warn("Invalid escape sequence: \\e\\x{:02x}", c);
                        continue;
                    }
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
                }
            }
        }
};

}
