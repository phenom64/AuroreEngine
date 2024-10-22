#include <memory>
#include <vector>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

import dreamrender;
import sdl2;
import spdlog;
import vulkan_hpp;

class simple_phase : public dreamrender::phase {
    public:
        simple_phase(dreamrender::window* win) : dreamrender::phase(win) {}

        vk::UniqueRenderPass renderPass;
        std::vector<vk::UniqueFramebuffer> framebuffers;

        void preload() override {
            phase::preload();

            vk::AttachmentDescription attachment{{}, win->swapchainFormat.format, win->config.sampleCount,
                vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
                vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
                vk::ImageLayout::eUndefined, vk::ImageLayout::ePresentSrcKHR};
            vk::AttachmentReference ref(0, vk::ImageLayout::eColorAttachmentOptimal);
            vk::SubpassDescription subpass({}, vk::PipelineBindPoint::eGraphics, {}, ref);
            vk::SubpassDependency dependency(vk::SubpassExternal, 0, vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eColorAttachmentOutput, {}, vk::AccessFlagBits::eColorAttachmentWrite, {});

            renderPass = device.createRenderPassUnique(vk::RenderPassCreateInfo({}, attachment, subpass, dependency));
        }
        void prepare(std::vector<vk::Image> swapchainImages, std::vector<vk::ImageView> swapchainViews) override {
            phase::prepare(swapchainImages, swapchainViews);

            framebuffers = createFramebuffers(renderPass.get());
        }
        void init() override {
            phase::init();
        }
        void render(int frame, vk::Semaphore imageAvailable, vk::Semaphore renderFinished, vk::Fence fence) override {
            phase::render(frame, imageAvailable, renderFinished, fence);

            vk::CommandBuffer& commandBuffer = commandBuffers[frame];
            commandBuffer.begin(vk::CommandBufferBeginInfo());
            vk::ClearValue clearValue(vk::ClearColorValue(std::array<float, 4>{0.5f, 0.25f, 0.0f, 0.5f}));
            vk::RenderPassBeginInfo renderPassInfo(renderPass.get(), framebuffers[frame].get(), vk::Rect2D({0, 0}, win->swapchainExtent), clearValue);
            commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
            commandBuffer.endRenderPass();
            commandBuffer.end();

            vk::PipelineStageFlags waitStages = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            vk::SubmitInfo submitInfo(1, &imageAvailable, &waitStages, 1, &commandBuffer, 1, &renderFinished);
            graphicsQueue.submit(submitInfo, fence);
        }
};

int main() {
    spdlog::set_level(spdlog::level::debug);

    dreamrender::window_config config;
    config.title = "Simple Example";
    config.name = "simple-example";
    {
        std::unique_ptr<Display, decltype(&XCloseDisplay)> display(XOpenDisplay(nullptr), XCloseDisplay);
        if (!display) {
            spdlog::error("Failed to open X display");
            return 1;
        }
        XVisualInfo visualInfo;
        if (!XMatchVisualInfo(display.get(), DefaultScreen(display.get()), 32, TrueColor, &visualInfo)) {
            spdlog::error("Failed to find visual info");
            return 1;
        }
        config.sdl_hints[sdl::hints::video_x11_window_visualid] = std::to_string(visualInfo.visualid);
    }

    dreamrender::window window{config};
    window.init();
    window.set_phase(new simple_phase(&window));
    window.loop();
}
