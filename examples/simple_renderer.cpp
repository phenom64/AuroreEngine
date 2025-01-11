#include <vector>

import dreamrender;
import glm;
import spdlog;
import vulkan_hpp;

class simple_phase : public dreamrender::phase {
    public:
        simple_phase(dreamrender::window* win) : dreamrender::phase(win),
            simpleRenderer(device, allocator, win->swapchainExtent) {}

        vk::UniqueRenderPass renderPass;
        std::vector<vk::UniqueFramebuffer> framebuffers;

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
            simpleRenderer.preload({renderPass.get()}, win->config.sampleCount);
        }
        void prepare(std::vector<vk::Image> swapchainImages, std::vector<vk::ImageView> swapchainViews) override {
            phase::prepare(swapchainImages, swapchainViews);

            framebuffers = createFramebuffers(renderPass.get());
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

            simpleRenderer.renderQuad(commandBuffer, frame, renderPass.get(), std::array{
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
            simpleRenderer.renderRect(commandBuffer, frame, renderPass.get(), glm::vec2{0.1f, 0.1f}, glm::vec2{0.25f, 0.2f}, glm::vec4{1.0f, 0.0f, 0.0f, 1.0f},
                dreamrender::simple_renderer::params{
                    {}, {0.1f, 0.3f, 0.5f, 0.7f}
                });

            commandBuffer.endRenderPass();
            commandBuffer.end();

            simpleRenderer.finish(frame);

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
