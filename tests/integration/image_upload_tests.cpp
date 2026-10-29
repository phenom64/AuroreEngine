#include "../../src/image_upload.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

int main() {
    try {
        using namespace auroreengine::detail;
        constexpr std::size_t width=2305,height=2049,pitch=width*4+12;
        std::vector<std::uint8_t> source(pitch*height,0xee);
        for(std::size_t y=0;y<height;++y)for(std::size_t x=0;x<width*4;++x)
            source[y*pitch+x]=static_cast<std::uint8_t>((x*13+y*7)&255);
        const auto layout=rgba_upload_layout(width,height,pitch,source.size());
        if(layout.width!=width||layout.height!=height||layout.bytes!=width*height*4||layout.bytes<=16*1024*1024)
            throw std::runtime_error("Large non-square dimensions changed");
        std::vector<std::uint8_t> target(layout.bytes+32,0xab);
        pack_rgba_rows(std::span(target).subspan(16,layout.bytes),source,layout);
        for(std::size_t y=0;y<height;++y)for(std::size_t x=0;x<width*4;++x)
            if(target[16+y*width*4+x]!=source[y*pitch+x])throw std::runtime_error("RGBA row/pixel fidelity changed");
        for(std::size_t i=0;i<16;++i)if(target[i]!=0xab||target[16+layout.bytes+i]!=0xab)
            throw std::runtime_error("Packing overwrote its destination bounds");
        const auto rejects=[](auto&& function) {
            bool rejected=false;try{function();}catch(const std::exception&){rejected=true;}
            if(!rejected)throw std::runtime_error("Invalid RGBA layout accepted");
        };
        rejects([&]{rgba_upload_layout(0,1,4,4);});
        rejects([&]{rgba_upload_layout(1,0,4,4);});
        rejects([&]{rgba_upload_layout(2,2,7,16);});
        rejects([&]{rgba_upload_layout(2,2,8,15);});
        rejects([&]{rgba_upload_layout(std::numeric_limits<std::size_t>::max(),2,8,16);});
        rejects([&]{rgba_upload_layout(1,std::numeric_limits<std::size_t>::max(),4,16);});
        rejects([&]{rgba_upload_layout(1,3,std::numeric_limits<std::size_t>::max(),16);});
        rejects([&]{rgba_upload_layout(8192,8192,32768,268435456);});
        rejects([&]{pack_rgba_rows(std::span(target).first(layout.bytes-1),source,layout);});
        rejects([&]{pack_rgba_rows(target,std::span(source).first(source.size()-pitch),layout);});
        const auto small=rgba_upload_layout(2,2,8,16);
        const std::array<std::uint8_t,16> pixels{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
        std::array<std::uint8_t,16> copied{};pack_rgba_rows(copied,pixels,small);
        if(copied!=pixels)throw std::runtime_error("Small image regression");
        std::cout<<"RGBA large-image row fidelity, bounds and overflow checks passed\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
