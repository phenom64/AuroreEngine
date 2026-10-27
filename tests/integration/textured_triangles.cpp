/* SPDX-License-Identifier: MPL-2.0 */
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

import auroreengine;
import glm;
import vulkan_hpp;

class geometry_phase final : public auroreengine::phase {
public:
    explicit geometry_phase(auroreengine::window* window)
        : phase(window),image(device,allocator,2,2),drained(device,allocator,1,1),
          renderer(device,window->swapchainExtent,window->gpuFeatures) {
        renderer.setAllocator(allocator);
        using Vertex=auroreengine::image_renderer::textured_vertex;
        checker.reserve(24576);
        for(int y=0;y<64;++y) for(int x=0;x<64;++x) {
            const float left=x/64.0f,right=(x+1)/64.0f,top=y/64.0f,bottom=(y+1)/64.0f;
            const glm::vec4 color=(x+y)%2==0?glm::vec4(1,0,0,1):glm::vec4(0,0,1,1);
            const Vertex a{{left,top,0.5f,1},{0,0},color},b{{right,top,0.5f,1},{1,0},color},
                         c{{left,bottom,0.5f,1},{0,1},color},d{{right,bottom,0.5f,1},{1,1},color};
            const std::array vertices{a,b,c,c,b,d};
            checker.insert(checker.end(),vertices.begin(),vertices.end());
        }
        using V=auroreengine::image_renderer::textured_vertex;
        overlay={V{{0.4f,0.4f,0.2f,1},{0,0},{0,1,0,0.25f}},V{{0.6f,0.4f,0.2f,1},{1,0},{0,1,0,0.25f}},
                 V{{0.4f,0.6f,0.2f,1},{0,1},{0,1,0,0.25f}},V{{0.4f,0.6f,0.2f,1},{0,1},{0,1,0,0.25f}},
                 V{{0.6f,0.4f,0.2f,1},{1,0},{0,1,0,0.25f}},V{{0.6f,0.6f,0.2f,1},{1,1},{0,1,0,0.25f}}};
    }
    ~geometry_phase() override { try { win->waitIdle(); } catch(...) {} }
    void preload() override {
        phase::preload();
        const vk::AttachmentDescription color({},win->swapchainFormat.format,win->config.sampleCount,
            vk::AttachmentLoadOp::eClear,vk::AttachmentStoreOp::eStore,vk::AttachmentLoadOp::eDontCare,
            vk::AttachmentStoreOp::eDontCare,vk::ImageLayout::eUndefined,win->swapchainFinalLayout);
        const vk::AttachmentDescription depth({},vk::Format::eD32Sfloat,win->config.sampleCount,
            vk::AttachmentLoadOp::eClear,vk::AttachmentStoreOp::eDontCare,vk::AttachmentLoadOp::eDontCare,
            vk::AttachmentStoreOp::eDontCare,vk::ImageLayout::eUndefined,vk::ImageLayout::eDepthStencilAttachmentOptimal);
        const std::array attachments{color,depth};
        const vk::AttachmentReference ref(0,vk::ImageLayout::eColorAttachmentOptimal),depth_ref(1,vk::ImageLayout::eDepthStencilAttachmentOptimal);
        vk::SubpassDescription subpass({},vk::PipelineBindPoint::eGraphics,{},ref);subpass.setPDepthStencilAttachment(&depth_ref);
        const auto stages=vk::PipelineStageFlagBits::eColorAttachmentOutput|vk::PipelineStageFlagBits::eEarlyFragmentTests|vk::PipelineStageFlagBits::eLateFragmentTests;
        const vk::SubpassDependency dependency(vk::SubpassExternal,0,stages,stages,
            vk::AccessFlagBits::eDepthStencilAttachmentWrite,vk::AccessFlagBits::eColorAttachmentWrite|vk::AccessFlagBits::eDepthStencilAttachmentWrite);
        pass=device.createRenderPassUnique(vk::RenderPassCreateInfo({},attachments,subpass,dependency));
        renderer.preload({pass.get()},win->config.sampleCount);
        add_task(loader->loadTexture(&image,auroreengine::LoaderFunction{[](std::uint8_t* data,std::size_t capacity) {
            if(capacity<16) throw std::runtime_error("Upload staging capacity");std::memset(data,255,16);
        }}));
    }
    void prepare(std::vector<vk::Image> images,std::vector<vk::ImageView> views) override {
        phase::prepare(images,views);
        for(auto view:views) {
            auto depth=std::make_unique<auroreengine::texture>(device,allocator,win->swapchainExtent,
                vk::ImageUsageFlagBits::eDepthStencilAttachment,vk::Format::eD32Sfloat,
                win->config.sampleCount,false,vk::ImageAspectFlagBits::eDepth);
            const std::array attachments{view,depth->imageView.get()};
            framebuffers.push_back(device.createFramebufferUnique(vk::FramebufferCreateInfo({},pass.get(),attachments,
                win->swapchainExtent.width,win->swapchainExtent.height,1)));
            depths.push_back(std::move(depth));
        }
        renderer.prepare(images.size());
    }
    void render(int frame,vk::Semaphore available,vk::Semaphore finished,vk::Fence fence) override {
        if(!drain_checked) {
            auto pending=loader->loadTexture(&drained,auroreengine::LoaderFunction{[](std::uint8_t* bytes,std::size_t) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));std::memset(bytes,255,4);
            }});
            // This used to deadlock when the window held the shared queue mutex
            // across the entire phase callback instead of queue operations.
            win->waitIdle();pending.get();
            if(!drained.loaded) throw std::runtime_error("Upload drain did not complete");
            drain_checked=true;
        }
        auto& command=commandBuffers[frame];command.begin(vk::CommandBufferBeginInfo());
        const std::array clear{vk::ClearValue(vk::ClearColorValue(std::array<float,4>{0,0,0,1})),
            vk::ClearValue(vk::ClearDepthStencilValue(1,0))};
        command.beginRenderPass(vk::RenderPassBeginInfo(pass.get(),framebuffers[frame].get(),vk::Rect2D({0,0},win->swapchainExtent),clear),vk::SubpassContents::eInline);
        const vk::Viewport viewport(0,0,win->swapchainExtent.width,win->swapchainExtent.height,0,1);
        const vk::Rect2D scissor({0,0},win->swapchainExtent);command.setViewport(0,viewport);command.setScissor(0,scissor);
        auroreengine::gui_renderer gui(command,frame,pass.get(),win->swapchainExtent,nullptr,&renderer,nullptr);
        gui.draw_textured_triangles(image,checker,false,true);
        auto hidden=overlay;
        for(auto& vertex:hidden) { vertex.position.z=0.8f;vertex.color=glm::vec4(1); }
        // The white square is behind the checkerboard and must be occluded.
        gui.draw_textured_triangles(image,hidden,false,true);
        gui.draw_textured_triangles(image,overlay,true);
        command.endRenderPass();renderer.finish(frame);command.end();
        const auto stats=renderer.geometryStatistics(frame);
        if(stats.triangles!=8196 || stats.draw_calls!=3 || stats.bytes_uploaded!=24588*sizeof(auroreengine::image_renderer::textured_vertex))
            throw std::runtime_error("Mesh batching statistics are incorrect");
        std::cout<<"geometry draws="<<stats.draw_calls<<" triangles="<<stats.triangles<<" upload_bytes="<<stats.bytes_uploaded<<" capacity="<<stats.resident_vertex_capacity<<'\n';
        const vk::PipelineStageFlags wait=vk::PipelineStageFlagBits::eColorAttachmentOutput;
        const vk::SubmitInfo submit(1,&available,&wait,1,&command,1,&finished);submit_graphics(submit,fence);
    }
private:
    auroreengine::texture image,drained;
    auroreengine::image_renderer renderer;
    vk::UniqueRenderPass pass;
    std::vector<std::unique_ptr<auroreengine::texture>> depths;
    std::vector<vk::UniqueFramebuffer> framebuffers;
    std::vector<auroreengine::image_renderer::textured_vertex> checker;
    std::array<auroreengine::image_renderer::textured_vertex,6> overlay;
    bool drain_checked{};
};
int main(int argc,char** argv) {
    try {
        auroreengine::window_config config;config.title="Textured triangle smoke";config.name="triangle-smoke";
        config.width=128;config.height=128;config.headless=true;config.headless_frames=8;
        config.headless_output_format="{:05d}.bmp";
        config.headless_output_dir=argc>1?std::filesystem::path(argv[1]):std::filesystem::temp_directory_path()/"aurore-triangle-smoke";
        config.sampleCount=vk::SampleCountFlagBits::e1;
        auroreengine::window window{config};window.init();
        // set_phase transfers ownership to the window.
        window.set_phase(new geometry_phase(&window));window.loop();window.waitIdle();
        std::ifstream file(config.headless_output_dir/"00007.bmp",std::ios::binary);
        const std::vector<unsigned char> bitmap{std::istreambuf_iterator<char>(file),{}};
        if(bitmap.size()<54+128*128*4 || bitmap[0]!='B' || bitmap[1]!='M') throw std::runtime_error("Headless BMP was not captured");
        auto u32=[&](std::size_t at) { return static_cast<std::uint32_t>(bitmap[at]) | static_cast<std::uint32_t>(bitmap[at+1])<<8 | static_cast<std::uint32_t>(bitmap[at+2])<<16 | static_cast<std::uint32_t>(bitmap[at+3])<<24; };
        const auto offset=u32(10);
        if(u32(18)!=128 || static_cast<std::int32_t>(u32(22))!=-128 || bitmap[28]!=32) throw std::runtime_error("Unexpected BMP layout");
        auto pixel=[&](int x,int y) { const auto at=offset+(y*128+x)*4;return std::array{bitmap.at(at+2),bitmap.at(at+1),bitmap.at(at)}; };
        const auto red=pixel(1,1),blue=pixel(3,1),center=pixel(64,64);
        if(red[0]<240 || red[2]>10 || blue[2]<240 || blue[0]>10 || center[1]<50 || center[2]>10) throw std::runtime_error("Textured geometry/additive/depth pixel check failed");
        std::cout<<"Headless textured triangle smoke passed: "<<config.headless_output_dir<<'\n';
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
