/* SPDX-License-Identifier: MPL-2.0 */
#include <vulkan/vulkan.h>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

import auroreengine;
import glm;
import vulkan_hpp;
import vma;

void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Renderer>void target_extent(Renderer& renderer,vk::Extent2D extent){
    renderer.set_target_extent(extent);
    // Rejected updates must leave the last valid conversion metadata intact;
    // the subsequent readback checks that metadata as well as rejection.
    for(const vk::Extent2D invalid:std::array{vk::Extent2D{0,extent.height},vk::Extent2D{extent.width,0}}){
        bool rejected=false;
        try{renderer.set_target_extent(invalid);}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"renderer accepted an empty target extent");
    }
}
struct CommandCounter {
    static inline CommandCounter* active{};
    PFN_vkAllocateCommandBuffers allocate;PFN_vkFreeCommandBuffers free;
    VkCommandPool pool;unsigned live{},peak{};
    CommandCounter(vk::CommandPool watched,unsigned initial):allocate(vk::detail::defaultDispatchLoaderDynamic.vkAllocateCommandBuffers),free(vk::detail::defaultDispatchLoaderDynamic.vkFreeCommandBuffers),pool(watched),live(initial),peak(initial){active=this;vk::detail::defaultDispatchLoaderDynamic.vkAllocateCommandBuffers=&on_allocate;vk::detail::defaultDispatchLoaderDynamic.vkFreeCommandBuffers=&on_free;}
    ~CommandCounter(){vk::detail::defaultDispatchLoaderDynamic.vkAllocateCommandBuffers=allocate;vk::detail::defaultDispatchLoaderDynamic.vkFreeCommandBuffers=free;active=nullptr;}
    static VKAPI_ATTR VkResult VKAPI_CALL on_allocate(VkDevice device,const VkCommandBufferAllocateInfo* info,VkCommandBuffer* buffers){const auto result=active->allocate(device,info,buffers);if(result==VK_SUCCESS&&info->commandPool==active->pool){active->live+=info->commandBufferCount;active->peak=std::max(active->peak,active->live);}return result;}
    static VKAPI_ATTR void VKAPI_CALL on_free(VkDevice device,VkCommandPool pool,uint32_t count,const VkCommandBuffer* buffers){if(pool==active->pool)active->live-=count;active->free(device,pool,count,buffers);}
};

class retained_phase final:public auroreengine::phase {
public:
    retained_phase(auroreengine::window* window,const std::string& font):phase(window),
        corner(device,allocator,32,32),yellow(device,allocator,1,1),
        image(device,{320,180},window->gpuFeatures),
        text(font,32,device,allocator,{320,180},window->gpuFeatures),
        shape(device,allocator,{320,180},window->gpuFeatures){image.setAllocator(allocator);}
    ~retained_phase()override{try{win->waitIdle();}catch(...) {}}
    void preload()override{
        phase::preload();
        const vk::AttachmentDescription color({},vk::Format::eB8G8R8A8Srgb,vk::SampleCountFlagBits::e1,vk::AttachmentLoadOp::eClear,vk::AttachmentStoreOp::eStore,vk::AttachmentLoadOp::eDontCare,vk::AttachmentStoreOp::eDontCare,vk::ImageLayout::eUndefined,vk::ImageLayout::eTransferSrcOptimal);
        const vk::AttachmentReference ref(0,vk::ImageLayout::eColorAttachmentOptimal);
        const vk::SubpassDescription subpass({},vk::PipelineBindPoint::eGraphics,{},ref);
        const std::array dependencies{
            vk::SubpassDependency(vk::SubpassExternal,0,vk::PipelineStageFlagBits::eTopOfPipe,vk::PipelineStageFlagBits::eColorAttachmentOutput,{},vk::AccessFlagBits::eColorAttachmentWrite),
            vk::SubpassDependency(0,vk::SubpassExternal,vk::PipelineStageFlagBits::eColorAttachmentOutput,vk::PipelineStageFlagBits::eTransfer,vk::AccessFlagBits::eColorAttachmentWrite,vk::AccessFlagBits::eTransferRead)};
        pass=device.createRenderPassUnique(vk::RenderPassCreateInfo({},color,subpass,dependencies));
        image.preload({pass.get()},vk::SampleCountFlagBits::e1);shape.preload({pass.get()},vk::SampleCountFlagBits::e1);
        text.set_preload_texts({"MMMM"});add_task(text.preload(loader,{pass.get()},vk::SampleCountFlagBits::e1));
        add_task(loader->loadTexture(&corner,auroreengine::LoaderFunction{[](std::uint8_t* output,std::size_t size){require(size>=32*32*4,"corner upload capacity");for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x){const auto at=(y*32+x)*4;output[at]=(x<16)==(y<16)?255:0;output[at+1]=x>=16?255:0;output[at+2]=y>=16?255:0;output[at+3]=255;}}}));
        add_task(loader->loadTexture(&yellow,auroreengine::LoaderFunction{[](std::uint8_t* output,std::size_t size){require(size>=4,"pixel upload capacity");output[0]=output[1]=output[3]=255;output[2]=0;}}));
    }
    void prepare(std::vector<vk::Image> images,std::vector<vk::ImageView> views)override{
        phase::prepare(images,views);window_images=std::move(images);slots=window_images.size();
        install({320,180});
    }
    void init()override{
        win->waitIdle();
        CommandCounter counter(pool.get(),commandBuffers.size());
        for(unsigned cycle=0;cycle<100;++cycle)for(unsigned count:{2u,2u,3u,0u,2u}){
            const auto before=commandBuffers;
            phase::prepare(std::vector<vk::Image>(count),{});
            require(counter.live==count,"phase preparation leaked a retired command-buffer batch");
            if(before.size()==count)require(before==commandBuffers,"same-count phase preparation did not reuse its batch");
        }
        phase::prepare(window_images,{});require(counter.live==slots&&counter.peak<=std::max<unsigned>(3,slots),"command-buffer batches exceeded active target capacity");
        std::cout<<"Command buffers: live="<<counter.live<<" peak="<<counter.peak<<" after 500 preparations\n";
        atlas=text.get_atlas();atlas_image=atlas->image;
    }
    void after_events()override{
        if(completed>=sequence.size())return;
        if(completed!=configured){win->waitIdle();install(sequence[completed]);configured=completed;}
    }
    void render(int frame,vk::Semaphore available,vk::Semaphore finished,vk::Fence fence)override{
        auto command=commandBuffers.at(frame);command.begin(vk::CommandBufferBeginInfo());
        const auto measured=text.measureText("MMMM",24.0F/extent.height*text.emScale()*0.5F).x*extent.width;
        if(!measured_width)measured_width=measured;
        require(std::abs(measured-measured_width)<0.01F,"retained font used the previous target aspect ratio");
        require(text.get_atlas()==atlas&&text.get_atlas()->image==atlas_image,"target resize discarded the resident font atlas");
#ifdef AUROREENGINE_USE_HARFBUZZ
        text.stage_glyphs(command,"MMMM");
#endif
        const vk::ClearValue clear(vk::ClearColorValue(std::array<float,4>{0,0,0,1}));
        command.beginRenderPass(vk::RenderPassBeginInfo(pass.get(),framebuffer.get(),vk::Rect2D({0,0},extent),clear),vk::SubpassContents::eInline);
        command.setViewport(0,vk::Viewport(0,0,extent.width,extent.height,0,1));command.setScissor(0,vk::Rect2D({0,0},extent));
        image.renderImageSized(command,frame,pass.get(),corner,0,0,extent.width,extent.height);
        image.renderImageSized(command,frame,pass.get(),yellow,0.05F,0.4F,40,20);
        auroreengine::gui_renderer gui(command,frame,pass.get(),extent,&text,&image,&shape);
        gui.draw_text_pixels("MMMM",extent.width*0.15F,extent.height*0.25F,24,{1,1,1,1});
        auroreengine::simple_params rounded;rounded.radius_px=12;
        shape.renderRect(command,frame,pass.get(),{0.6F,0.55F},{80.0F/extent.width,60.0F/extent.height},{0,0,0,1},rounded);
        command.endRenderPass();image.finish(frame);text.finish(frame);shape.finish(frame);
        const vk::BufferImageCopy copy(0,0,0,vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor,0,0,1),{},vk::Extent3D(extent.width,extent.height,1));
        command.copyImageToBuffer(target->image,vk::ImageLayout::eTransferSrcOptimal,readback.get(),copy);
        command.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,vk::PipelineStageFlagBits::eHost,{}, {},vk::BufferMemoryBarrier(vk::AccessFlagBits::eTransferWrite,vk::AccessFlagBits::eHostRead,VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED,readback.get(),0,VK_WHOLE_SIZE),{});
        const vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor,0,1,0,1);
        command.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,vk::PipelineStageFlagBits::eTransfer,{}, {},{},vk::ImageMemoryBarrier({},vk::AccessFlagBits::eTransferWrite,vk::ImageLayout::eUndefined,vk::ImageLayout::eTransferDstOptimal,VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED,window_images.at(frame),range));
        command.clearColorImage(window_images.at(frame),vk::ImageLayout::eTransferDstOptimal,vk::ClearColorValue(std::array<float,4>{0,0,0,1}),range);
        command.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,vk::PipelineStageFlagBits::eBottomOfPipe,{}, {},{},vk::ImageMemoryBarrier(vk::AccessFlagBits::eTransferWrite,{},vk::ImageLayout::eTransferDstOptimal,win->swapchainFinalLayout,VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED,window_images.at(frame),range));
        command.end();const vk::PipelineStageFlags wait=vk::PipelineStageFlagBits::eTransfer;submit_graphics(vk::SubmitInfo(1,&available,&wait,1,&command,1,&finished),fence);
        require(device.waitForFences(fence,true,10000000000ull)==vk::Result::eSuccess,"render/readback timed out");allocator.invalidateAllocation(readback_memory.get(),0,VK_WHOLE_SIZE);check_pixels();++completed;
    }
    unsigned completed{};
    static constexpr std::array<vk::Extent2D,12> sequence{{{320,180},{640,360},{300,450},{320,180},{640,360},{300,450},{320,180},{640,360},{300,450},{320,180},{640,360},{320,180}}};
private:
    void install(vk::Extent2D size){
        mapping.reset();framebuffer.reset();target.reset();readback.reset();readback_memory.reset();extent=size;
        target=std::make_unique<auroreengine::texture>(device,allocator,extent,vk::ImageUsageFlagBits::eColorAttachment|vk::ImageUsageFlagBits::eTransferSrc,vk::Format::eB8G8R8A8Srgb,vk::SampleCountFlagBits::e1,false);
        framebuffer=device.createFramebufferUnique(vk::FramebufferCreateInfo({},pass.get(),target->imageView.get(),extent.width,extent.height,1));
        std::tie(readback,readback_memory)=allocator.createBufferUnique(vk::BufferCreateInfo({},extent.width*extent.height*4,vk::BufferUsageFlagBits::eTransferDst),vma::AllocationCreateInfo({},vma::MemoryUsage::eGpuToCpu));mapping=std::make_unique<vma::MemoryMapping>(allocator,readback_memory.get());
        const auto cache=text.textCacheStatistics();
        target_extent(image,extent);target_extent(text,extent);target_extent(shape,extent);image.prepare(slots);text.prepare(slots);shape.prepare(slots);
        const auto retained_cache=text.textCacheStatistics();
        require(cache.entries==retained_cache.entries&&cache.glyphs==retained_cache.glyphs,"target resize discarded the shaped-text cache");
    }
    void check_pixels(){
        const auto* data=static_cast<const unsigned char*>(mapping->get());
        const auto pixel=[&](unsigned x,unsigned y){const auto at=(static_cast<std::size_t>(y)*extent.width+x)*4;return std::array{data[at+2],data[at+1],data[at]};};
        const auto red=pixel(extent.width/8,extent.height/8),green=pixel(extent.width*7/8,extent.height/8),white=pixel(extent.width*7/8,extent.height*7/8);
        require(red[0]>240&&red[1]<10&&green[1]>240&&green[0]<10&&white[0]>240&&white[1]>240&&white[2]>240,"retained image renderer cropped the full-viewport corner texture");
        const auto yellow_in=pixel(static_cast<unsigned>(extent.width*0.05F)+5,static_cast<unsigned>(extent.height*0.4F)+5),yellow_out=pixel(static_cast<unsigned>(extent.width*0.05F)+45,static_cast<unsigned>(extent.height*0.4F)+5);
        require(yellow_in[0]>240&&yellow_in[1]>240&&yellow_in[2]<10&&yellow_out[1]<10,"pixel-sized image changed size with the render target");
        const auto x=static_cast<unsigned>(extent.width*0.6F),y=static_cast<unsigned>(extent.height*0.55F);const auto outside=pixel(x+2,y+2),inside=pixel(x+12,y+2);
        require(outside[0]>220&&inside[0]<30,"rounded shape radius retained the previous target pixel dimensions");
        unsigned ink{};for(unsigned row=extent.height/4-24;row<extent.height/4;++row)for(unsigned col=extent.width*0.15F;col<extent.width*0.15F+measured_width&&col<extent.width/2;++col){const auto p=pixel(col,row);if(p[0]>200&&p[1]>200&&p[2]>200)++ink;}
        require(ink>30,"resident font atlas stopped producing glyph pixels after resize");
        std::cout<<"Target "<<completed+1<<": "<<extent.width<<'x'<<extent.height<<" pixels, text/atlas/cache/rounded shape passed\n";
    }
    auroreengine::texture corner,yellow;auroreengine::image_renderer image;auroreengine::font_renderer text;auroreengine::simple_renderer shape;
    vk::UniqueRenderPass pass;std::unique_ptr<auroreengine::texture> target;vk::UniqueFramebuffer framebuffer;
    vma::UniqueBuffer readback;vma::UniqueAllocation readback_memory;std::unique_ptr<vma::MemoryMapping> mapping;
    std::vector<vk::Image> window_images;vk::Extent2D extent{};unsigned slots{},configured{};float measured_width{};
    const auroreengine::texture* atlas{};vk::Image atlas_image{};
};
int main(int argc,char** argv)try{
    require(argc==2,"Supply a real packaged TrueType font path");
    auroreengine::window_config config;config.name="retained-target-extent";config.headless=true;config.width=32;config.height=32;config.headless_frames=retained_phase::sequence.size();config.headless_output_dir.clear();config.sampleCount=vk::SampleCountFlagBits::e1;
    auroreengine::window window{config};window.init();auto* phase=new retained_phase(&window,argv[1]);window.set_phase(phase);window.loop();window.waitIdle();require(phase->completed==retained_phase::sequence.size(),"fixture did not complete all size/aspect/restore cycles");std::cout<<"Retained image/text/shape target extent and command-buffer GPU regression passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
