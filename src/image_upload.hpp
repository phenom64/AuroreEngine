#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>

namespace auroreengine::detail {
inline constexpr std::size_t maximum_texture_upload_bytes=128ULL*1024*1024;
struct RgbaUploadLayout {
    std::size_t width{},height{},row_bytes{},pitch{},bytes{},source_bytes{};
};
inline RgbaUploadLayout rgba_upload_layout(std::size_t width,std::size_t height,
    std::size_t pitch,std::size_t available) {
    if(!width||!height||width>std::numeric_limits<std::size_t>::max()/4)
        throw std::invalid_argument("RGBA image dimensions are zero or overflow the row size");
    const auto row=width*4;
    if(pitch<row)throw std::invalid_argument("RGBA image pitch is shorter than a row");
    if(row>maximum_texture_upload_bytes||height>maximum_texture_upload_bytes/row)
        throw std::length_error("Native texture upload exceeds the 128 MiB limit; reduce the source image dimensions");
    if(height-1>(std::numeric_limits<std::size_t>::max()-row)/pitch)
        throw std::invalid_argument("RGBA image pitch and height overflow the source size");
    const auto required=(height-1)*pitch+row;
    if(available<required)throw std::invalid_argument("RGBA image source does not contain every row");
    return {width,height,row,pitch,row*height,required};
}
inline void pack_rgba_rows(std::span<std::uint8_t> destination,
    std::span<const std::uint8_t> source,const RgbaUploadLayout& layout) {
    // Revalidate the supplied layout so even internal callers cannot forge
    // arithmetic or source bounds when forwarding a decoded surface.
    const auto checked=rgba_upload_layout(layout.width,layout.height,layout.pitch,source.size());
    if(destination.size()<checked.bytes)throw std::invalid_argument("RGBA upload destination is too small");
    for(std::size_t y=0;y<checked.height;++y)
        std::memcpy(destination.data()+y*checked.row_bytes,source.data()+y*checked.pitch,checked.row_bytes);
}
}
