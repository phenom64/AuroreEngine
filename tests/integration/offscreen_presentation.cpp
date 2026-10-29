#include <array>
#include <cstdint>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_video.h>
#undef main

import auroreengine;
import vulkan_hpp;

// The default fixture never creates or focuses a desktop window. --native is
// explicitly opt-in because it creates/minimizes/restores a real SDL window.
void headless_contract() {
#if defined(_WIN32)
        char* headlessMode{};std::size_t headlessLength{};
        if(_dupenv_s(&headlessMode,&headlessLength,"AUROREENGINE_HEADLESS")!=0)
            throw std::runtime_error("Could not inspect headless environment override");
        const bool disabled=headlessMode&&std::string_view(headlessMode)=="0";std::free(headlessMode);
        if(disabled)throw std::runtime_error("Unset AUROREENGINE_HEADLESS=0 before the no-focus fixture");
#else
        if(const auto* mode=std::getenv("AUROREENGINE_HEADLESS");mode&&std::string_view(mode)=="0")
            throw std::runtime_error("Unset AUROREENGINE_HEADLESS=0 before the no-focus fixture");
#endif
        auroreengine::window_config config;
        config.name="offscreen-presentation-contract";config.headless=true;
        config.width=32;config.height=32;config.headless_output_dir.clear();
        auroreengine::window window{config};
        const auto expect_rejected=[&](auto&& function) {
            bool rejected=false;
            try {function();}catch(const std::exception& error) {
                rejected=*error.what()!=0;
            }
            if(!rejected||window.offscreen_presentation_active())
                throw std::runtime_error("Unavailable presentation changed live state");
        };
        const auto sink=[](const auroreengine::OffscreenFrame&){};
        expect_rejected([&]{window.begin_offscreen_presentation({32,32},sink);});
        window.init();
        const auto format=window.swapchainFormat;
        const auto extent=window.swapchainExtent;
        const auto images=window.swapchainImages;
        expect_rejected([&]{window.begin_offscreen_presentation({32,32},sink);});
        expect_rejected([&]{window.begin_offscreen_presentation({0,32},sink);});
        expect_rejected([&]{window.begin_offscreen_presentation({32,32},{});});
        expect_rejected([&]{window.begin_offscreen_presentation({std::numeric_limits<std::uint32_t>::max(),32},sink);});
        bool other_thread_rejected=false;
        std::thread other([&]{try{window.begin_offscreen_presentation({32,32},sink);}catch(const std::exception&){other_thread_rejected=true;}});
        other.join();
        if(!other_thread_rejected)throw std::runtime_error("Presentation accepted a non-owner thread");
        window.end_offscreen_presentation();
        if(window.swapchainFormat!=format||window.swapchainExtent!=extent||window.swapchainImages!=images)
            throw std::runtime_error("Rejected sink changed the headless presentation");
        std::cout<<"Offscreen presentation unavailable-mode contract passed\n";
}

class transition_phase final : public auroreengine::phase {
public:
    explicit transition_phase(auroreengine::window* value):phase(value),originalDevice(value->device.get()),
        originalImages(value->swapchainImages),originalFormat(value->swapchainFormat),originalLayout(value->swapchainFinalLayout),originalExtent(value->swapchainExtent){}
    void prepare(std::vector<vk::Image> next,std::vector<vk::ImageView> views) override {
        if(failPreparation){failPreparation=false;throw std::runtime_error("Injected preparation failure");}
        if(next.size()!=views.size()||next.empty())throw std::runtime_error("Phase received inconsistent targets");
        phase::prepare(next,views);images=std::move(next);
        if(win->offscreen_presentation_active()&&
           (win->swapchainFormat.format!=vk::Format::eB8G8R8A8Srgb||win->swapchainFinalLayout!=vk::ImageLayout::eTransferSrcOptimal))
            throw std::runtime_error("Phase prepared with incorrect offscreen format/layout");
    }
    void after_events() override {
        if(std::chrono::steady_clock::now()-started>std::chrono::seconds(15))throw std::runtime_error("Presentation fixture timed out");
        ++events;
        if(stage==0&&events>=3) {
            // Initial SDL size events may already have recreated the surface.
            // The retained baseline is the live WSI bundle at actual entry.
            originalImages=win->swapchainImages;originalFormat=win->swapchainFormat;
            originalLayout=win->swapchainFinalLayout;originalExtent=win->swapchainExtent;
            failPreparation=true;bool rolledBack=false;
            try{win->begin_offscreen_presentation({48,24},sink({48,24}));}
            catch(const std::exception&){rolledBack=!win->offscreen_presentation_active()&&win->swapchainImages==originalImages;}
            if(!rolledBack)throw std::runtime_error("Failed initial entry did not restore normal WSI targets");
            win->begin_offscreen_presentation({64,32},sink({64,32}));stage=1;
            SDL_MinimizeWindow(win->win.get());
        }else if(stage==1&&frames>=3) {
            const auto previous=win->swapchainImages;failPreparation=true;bool rolledBack=false;
            try{win->begin_offscreen_presentation({80,40},sink({80,40}));}
            catch(const std::exception&){rolledBack=win->offscreen_presentation_active()&&win->swapchainExtent==vk::Extent2D{64,32}&&win->swapchainImages==previous;}
            if(!rolledBack)throw std::runtime_error("Failed sink re-entry did not restore previous targets");
            frames=0;win->begin_offscreen_presentation({128,64},sink({128,64}));stage=2;
        }else if(stage==2&&frames>=3) {
            failPreparation=true;bool rolledBack=false;
            try{win->end_offscreen_presentation();}
            catch(const std::exception&){rolledBack=win->offscreen_presentation_active()&&win->swapchainExtent==vk::Extent2D{128,64};}
            if(!rolledBack)throw std::runtime_error("Failed normal restoration did not preserve the working sink");
            SDL_RestoreWindow(win->win.get());win->end_offscreen_presentation();
            if(win->offscreen_presentation_active()||win->swapchainImages!=originalImages||win->swapchainFormat!=originalFormat||
               win->swapchainFinalLayout!=originalLayout||win->swapchainExtent!=originalExtent)
                throw std::runtime_error("Normal WSI resources were not restored");
            stage=3;restoreEvent=events;
        }else if(stage==3&&events>=restoreEvent+3) {
            SDL_Event quit{};quit.type=SDL_QUIT;SDL_PushEvent(&quit);
        }
        if(win->device.get()!=originalDevice||win->current_renderer.get()!=this)
            throw std::runtime_error("Transition replaced the device or phase");
    }
    void render(int frame,vk::Semaphore available,vk::Semaphore finished,vk::Fence fence) override {
        auto command=commandBuffers.at(frame);command.begin(vk::CommandBufferBeginInfo());
        const vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor,0,1,0,1);
        command.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,vk::PipelineStageFlagBits::eTransfer,{}, {}, {},
            vk::ImageMemoryBarrier({},vk::AccessFlagBits::eTransferWrite,vk::ImageLayout::eUndefined,vk::ImageLayout::eTransferDstOptimal,
                std::numeric_limits<std::uint32_t>::max(),std::numeric_limits<std::uint32_t>::max(),images.at(frame),range));
        const float alpha=win->swapchainExtent.width==128?0.25F:0.5F;
        command.clearColorImage(images.at(frame),vk::ImageLayout::eTransferDstOptimal,vk::ClearColorValue(std::array<float,4>{alpha*0.5F,alpha*0.25F,0,alpha}),range);
        command.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,vk::PipelineStageFlagBits::eBottomOfPipe,{}, {}, {},
            vk::ImageMemoryBarrier(vk::AccessFlagBits::eTransferWrite,{},vk::ImageLayout::eTransferDstOptimal,win->swapchainFinalLayout,
                std::numeric_limits<std::uint32_t>::max(),std::numeric_limits<std::uint32_t>::max(),images.at(frame),range));
        command.end();const vk::PipelineStageFlags wait=vk::PipelineStageFlagBits::eTransfer;
        submit_graphics(vk::SubmitInfo(1,&available,&wait,1,&command,1,&finished),fence);
    }
    int stage{};
private:
    std::function<void(const auroreengine::OffscreenFrame&)> sink(vk::Extent2D extent) {
        return [this,extent,owner=std::this_thread::get_id()](const auroreengine::OffscreenFrame& frame) {
            if(std::this_thread::get_id()!=owner||frame.width!=extent.width||frame.height!=extent.height||
               frame.row_stride!=extent.width*4||frame.bytes.size()!=static_cast<std::size_t>(extent.width)*extent.height*4||
               frame.format!=vk::Format::eB8G8R8A8Srgb)
                throw std::runtime_error("Sink received invalid owner/extent/row-stride/format");
            const int expectedAlpha=extent.width==128?64:128;
            for(const auto offset:std::array<std::size_t,3>{0,(frame.bytes.size()/2)&~std::size_t{3},frame.bytes.size()-4})
                if(frame.bytes[offset]>2||frame.bytes[offset+2]<frame.bytes[offset+1]||
                   std::abs(static_cast<int>(frame.bytes[offset+3])-expectedAlpha)>1)
                    throw std::runtime_error("Sink received stale or incorrectly ordered BGRA/alpha pixels");
            bool rejected=false;try{win->end_offscreen_presentation();}catch(const std::exception&){rejected=true;}
            if(!rejected)throw std::runtime_error("Sink accepted a reentrant presentation transition");
            ++frames;
        };
    }
    vk::Device originalDevice;std::vector<vk::Image> images,originalImages;
    vk::SurfaceFormatKHR originalFormat;vk::ImageLayout originalLayout;vk::Extent2D originalExtent;
    bool failPreparation{};int events{},frames{},restoreEvent{};
    std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();
};

void native_contract() {
    auroreengine::window_config config;config.name="offscreen-native-contract";config.title="Aurore offscreen transition fixture";
    config.width=160;config.height=90;config.fullscreen=false;config.fpsLimit=60;config.headless_output_dir.clear();
    auroreengine::window window{config};window.init();
    if(window.config.headless||window.config.workaround_no_swapchain)throw std::runtime_error("Unset headless/no-swapchain environment overrides for --native");
    auto* phase=new transition_phase(&window);window.set_phase(phase);window.loop();window.waitIdle();
    if(phase->stage!=3)throw std::runtime_error("Native fixture exited before all transitions completed");
    std::cout<<"Native offscreen enter/resize/rollback/minimized/readback/restore contract passed\n";
}

int main(int argc,char** argv) {
    try{if(argc==2&&std::string_view(argv[1])=="--native")native_contract();else headless_contract();}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
