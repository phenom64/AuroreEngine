# AuroreEngine

A simple, modern C++23 rendering engine using Vulkan and SDL2, used in OpenXMB and MediaSonic.

## About The Project

AuroreEngine is a lightweight, modern C++23 rendering engine built on Vulkan and SDL2. It is a fork of the `dreamrender` project, refactored for maximal portability in mind and updated to integrate more smoothly with modern build systems and dependencies.

This engine is designed for simplicity, portability (really important) and ease of integration. It provides the essential building blocks for creating 2D/3D applications, including window and input management, a multi-threaded resource loading system, and a set of straightforward renderers for common use cases. The entire engine is structured around C++23 modules, offering a clean, componentized architecture. It is primarily designed to be used for OpenXMB and the audio visualisation effects of MediaSonic on SynOS and OpenXMB, but you may use it for whatever you wish.

## Features

*   **Modern C++23:** Fully utilizes C++ modules for a clean and organized codebase.
*   **Vulkan Backend:** Leverages the Vulkan API for high-performance, cross-platform graphics.
*   **SDL2 Integration:** Uses SDL2 for window creation, input handling (keyboard, mouse, controller), and audio mixing.
*   **Component-Based Renderers:**
    *   `font_renderer`: Renders text using FreeType.
    *   `image_renderer`: Renders 2D textures.
    *   `simple_renderer`: Renders basic, colored 2D primitives.
    *   `gui_renderer`: A convenience wrapper to easily combine the other renderers for UI construction.
*   **Asynchronous Resource Loading:** A multi-threaded `resource_loader` for non-blocking texture and model loading.
*   **CMake-Friendly:** Designed to be easily included in larger projects using CMake's `FetchContent`.
*   **Headless Rendering:** Supports rendering without a visible window, useful for testing or server-side tasks.

## Core Concepts

The engine's architecture is built around a few key classes:
*   `dreamrender::window`: The main entry point. It initializes SDL2 and Vulkan, manages the window, and runs the main event loop.
*   `dreamrender::phase`: A base class representing a distinct state or screen in your application (e.g., main menu, game level, settings screen). You implement your application logic by subclassing `phase`.
*   **Renderers**: The engine provides specialized renderer classes that you can use within your `phase` to draw content to the screen.

## Using AuroreEngine in Your Project

### 1. CMake Integration

The recommended way to use AuroreEngine is via `FetchContent` in your project's `CMakeLists.txt`. This allows CMake to handle fetching and building the engine as part of your project's configuration step.

```cmake
include(FetchContent)

# Set options to let AuroreEngine know you are providing the dependencies
set(DREAMS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(DREAMS_FIND_PACKAGES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  AuroreEngine
  GIT_REPOSITORY https://github.com/phenom64/AuroreEngine.git
  GIT_TAG        master # Or a specific commit/tag
)

FetchContent_MakeAvailable(AuroreEngine)

# Your project is responsible for finding dependencies like SDL2, Vulkan, etc.
# find_package(SDL2 REQUIRED)
# find_package(Vulkan REQUIRED)
# ... and so on

# Link your executable against the engine
target_link_libraries(YourApp PRIVATE dreams::dreamrender)
```

### 2. Basic Application Structure

Here is a minimal example of an AuroreEngine application:

```cpp
import dreamrender;
import vulkan_hpp;

// 1. Define your application's main screen or state
class MyPhase : public dreamrender::phase {
public:
    MyPhase(dreamrender::window* win) : dreamrender::phase(win) {}

    // Member variables for render passes, framebuffers, etc.
    vk::UniqueRenderPass renderPass;
    std::vector<vk::UniqueFramebuffer> framebuffers;

    // 2. Preload resources (optional)
    void preload() override {
        phase::preload();
        // Create render pass, load textures, etc.
    }

    // 3. Prepare resources that depend on the swapchain
    void prepare(std::vector<vk::Image> images, std::vector<vk::ImageView> views) override {
        phase::prepare(images, views);
        // Create framebuffers
    }

    // 4. Implement the main render loop
    void render(int frame, vk::Semaphore imageAvailable, vk::Semaphore renderFinished, vk::Fence fence) override {
        phase::render(frame, imageAvailable, renderFinished, fence);

        vk::CommandBuffer& cmd = commandBuffers[frame];
        cmd.begin(vk::CommandBufferBeginInfo());

        // ... Record rendering commands ...

        cmd.end();

        // Submit command buffer
        vk::PipelineStageFlags waitStages = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        vk::SubmitInfo submitInfo(imageAvailable, waitStages, cmd, renderFinished);
        graphicsQueue.submit(submitInfo, fence);
    }
};

// 5. Main entry point
int main() {
    // Configure the window
    dreamrender::window_config config;
    config.title = "My App";
    config.name = "my-app";

    // Create and run the window
    dreamrender::window window{config};
    window.init();
    window.set_phase(new MyPhase(&window));
    window.loop();

    return 0;
}
```

## Building the Examples

To build the included examples, you will need the engine's dependencies installed on your system.

### Dependencies

*   **Linux (APT):** `libvulkan-dev`, `libsdl2-dev`, `libsdl2-image-dev`, `libsdl2-mixer-dev`, `libglm-dev`, `libfreetype-dev`, `glslang-tools`.
*   **macOS (Homebrew):** `vulkan-sdk`, `sdl2`, `sdl2_image`, `sdl2_mixer`, `glm`, `freetype`, `glslang`.

### Build Steps

```bash
# Clone the repository
git clone https://github.com/phenom64/AuroreEngine.git
cd AuroreEngine

# Configure with examples enabled
cmake -B build -G Ninja -DDREAMS_BUILD_EXAMPLES=ON

# Build
cmake --build build

# Run an example
./build/examples/simple
```

## Acknowledgements

*   This project is a fork of and gives thanks to the original [Dreamrender](https://github.com/JnCrMx/dreamrender) engine.
*   It relies on several excellent third-party libraries:
    *   [Vulkan](https://www.khronos.org/vulkan/)
    *   [SDL2](https://libsdl.org/)
    *   [Vulkan-Hpp](https://github.com/KhronosGroup/Vulkan-Hpp)
    *   [VulkanMemoryAllocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)
    *   [spdlog](https://github.com/gabime/spdlog)
    *   [glm](https://github.com/g-truc/glm)
    *   [Freetype](https://freetype.org/)
