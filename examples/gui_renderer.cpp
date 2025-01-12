#include <array>
#include <cstdint>
#include <vector>

import dreamrender;
import glm;
import spdlog;
import vulkan_hpp;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
constexpr char example_image_data[] = {
#embed "example.png"
};
#pragma clang diagnostic pop
constexpr std::array<uint8_t, sizeof(example_image_data)> example_image =
    std::bit_cast<std::array<uint8_t, sizeof(example_image_data)>>(example_image_data);

class simple_phase : public dreamrender::phase {
    public:
        simple_phase(dreamrender::window* win) : dreamrender::phase(win),
            fontRenderer{"/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", 64, device, allocator, win->swapchainExtent, win->gpuFeatures},
            imageRenderer(device, win->swapchainExtent, win->gpuFeatures),
            simpleRenderer(device, allocator, win->swapchainExtent, win->gpuFeatures) {}

        vk::UniqueRenderPass renderPass;
        std::vector<vk::UniqueFramebuffer> framebuffers;

        dreamrender::texture texture{device, allocator};
        dreamrender::font_renderer fontRenderer;
        dreamrender::image_renderer imageRenderer;
        dreamrender::simple_renderer simpleRenderer;

        void preload() override {
            phase::preload();

            vk::AttachmentDescription attachment{{}, win->swapchainFormat.format, win->config.sampleCount,
                vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
                vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
                vk::ImageLayout::eUndefined, win->swapchainFinalLayout};
            vk::AttachmentReference ref(0, vk::ImageLayout::eColorAttachmentOptimal);
            vk::SubpassDescription subpass({}, vk::PipelineBindPoint::eGraphics, {}, ref);
            vk::SubpassDependency dependency(vk::SubpassExternal, 0, vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eColorAttachmentOutput, {}, vk::AccessFlagBits::eColorAttachmentWrite, {});

            renderPass = device.createRenderPassUnique(vk::RenderPassCreateInfo({}, attachment, subpass, dependency));

            add_task(fontRenderer.preload(loader, {renderPass.get()}, win->config.sampleCount));
            imageRenderer.preload({renderPass.get()}, win->config.sampleCount);
            simpleRenderer.preload({renderPass.get()}, win->config.sampleCount);
            add_task(loader->loadTexture(&texture, dreamrender::LoadDataView{example_image, "PNG"}));
        }
        void prepare(std::vector<vk::Image> swapchainImages, std::vector<vk::ImageView> swapchainViews) override {
            phase::prepare(swapchainImages, swapchainViews);

            framebuffers = createFramebuffers(renderPass.get());

            fontRenderer.prepare(swapchainImages.size());
            imageRenderer.prepare(swapchainImages.size());
            simpleRenderer.prepare(swapchainImages.size());
        }
        void init() override {
            phase::init();
        }
        void render(int frame, vk::Semaphore imageAvailable, vk::Semaphore renderFinished, vk::Fence fence) override {
            phase::render(frame, imageAvailable, renderFinished, fence);

            vk::CommandBuffer& commandBuffer = commandBuffers[frame];
            commandBuffer.begin(vk::CommandBufferBeginInfo());
            vk::ClearValue clearValue(vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}));
            vk::RenderPassBeginInfo renderPassInfo(renderPass.get(), framebuffers[frame].get(), vk::Rect2D({0, 0}, win->swapchainExtent), clearValue);
            commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

            vk::Viewport viewport(0.0f, 0.0f, win->swapchainExtent.width, win->swapchainExtent.height, 0.0f, 1.0f);
            vk::Rect2D scissor({0,0}, win->swapchainExtent);
            commandBuffer.setViewport(0, viewport);
            commandBuffer.setScissor(0, scissor);

            dreamrender::gui_renderer gui(commandBuffer, frame, renderPass.get(), win->swapchainExtent, &fontRenderer, &imageRenderer, &simpleRenderer);
            gui.draw_text("Hello World!", 0.0f, 0.0f, 0.1f);
            gui.draw_image_sized(texture, 0.25f, 0.25f, gui.frame_size.width/2, gui.frame_size.height/2);
            gui.draw_quad(std::array{
                dreamrender::simple_renderer::vertex_data{{0.75f, 0.0f}, {0.2f, 0.2f, 0.2f, 0.2f}, {0.0f, 0.0f}},
                dreamrender::simple_renderer::vertex_data{{0.75f, 1.0f}, {0.2f, 0.2f, 0.2f, 0.2f}, {0.0f, 1.0f}},
                dreamrender::simple_renderer::vertex_data{{0.9f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
                dreamrender::simple_renderer::vertex_data{{0.9f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
            }, dreamrender::simple_renderer::params{
                std::array{
                    glm::vec2{0.0f, 0.1f},
                    glm::vec2{0.0f, 0.0f},
                    glm::vec2{-0.05f, 0.05f},
                    glm::vec2{-0.05f, 0.05f},
                }
            });
            gui.draw_text("Sidebar!", 0.75f, 0.0f, 0.1f);

            commandBuffer.endRenderPass();

            imageRenderer.finish(frame);
            fontRenderer.finish(frame);
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

    dreamrender::window window{config};
    window.init();
    window.set_phase(new simple_phase(&window));
    window.loop();
}
