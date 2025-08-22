module;

#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <cuchar>
#include <future>
#include <memory>
#include <string_view>
#include <string>
#include <unordered_map>
#include <vector>
#include <version>

#include <glm/ext/vector_int2.hpp>

#include <freetype2/ft2build.h>
#include <freetype/freetype.h>
#ifdef DREAMRENDER_USE_HARFBUZZ
#include <harfbuzz/hb.h>
#include <harfbuzz/hb-ft.h>
#endif

export module dreamrender:components.font_renderer;

import :resource_loader;
import :shaders;
import :texture;
import :utils;

import spdlog;
import glm;
import vulkan_hpp;
import vma;

namespace dreamrender {

struct ft_library_wrapper {
    FT_Library lib{};
    ft_library_wrapper() {
        if(FT_Init_FreeType(&lib)) {
            throw std::runtime_error("Failed to initialize FreeType library");
        }
    }
    ~ft_library_wrapper() {
        spdlog::debug("Destroying FreeType library");
        FT_Done_FreeType(lib);
    }
    FT_Library get() {
        return lib;
    }
    const FT_Library get() const {
        return lib;
    }
};
struct ft_face_wrapper {
    FT_Face face{};
    ft_face_wrapper(FT_Library lib, const std::string& fontName) {
        if(FT_New_Face(lib, fontName.c_str(), 0, &face)) {
            throw std::runtime_error("Failed to load font face");
        }
    }
    ~ft_face_wrapper() {
        spdlog::debug("Destroying font face");
        FT_Done_Face(face);
    }
    FT_Face get() {
        return face;
    }
    [[nodiscard]] const FT_Face get() const {
        return face;
    }
};

export class font_renderer {
    private:
        static constexpr bool check_features(const gpu_features& features) {
            if(!features.features.geometryShader)
                return false;
            return true;
        }
    public:
        static constexpr char default_start_char = 32;
        static constexpr char default_end_char = 127;
        static constexpr size_t default_max_characters = 1024;
        static constexpr size_t default_max_texts = 128;

        font_renderer(std::string  font_name, int font_size,
            vk::Device device, vma::Allocator allocator, vk::Extent2D frameSize, const gpu_features& features)
            : fontName(std::move(font_name)), fontSize(font_size), device(device), allocator(allocator),
              aspectRatio(static_cast<double>(frameSize.width) / frameSize.height),
              compat_mode(!check_features(features)) {}
        ~font_renderer() = default;

#ifdef DREAMRENDER_USE_HARFBUZZ
        // Upload required glyphs (with ligatures enabled) into the atlas before drawing
        void stage_glyphs(vk::CommandBuffer cmd, std::string_view text)
        {
            if(!hbFont || !hbBuffer) return;
            if(textureReady.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
            if(!glyphStagingMap) return;

            hb_buffer_clear_contents(hbBuffer.get());
            // Basic defaults; can be extended for Bidi/scripts later
            hb_buffer_set_direction(hbBuffer.get(), HB_DIRECTION_LTR);
            hb_buffer_set_script(hbBuffer.get(), HB_SCRIPT_COMMON);
            hb_buffer_set_language(hbBuffer.get(), hb_language_from_string("en", -1));
            hb_buffer_add_utf8(hbBuffer.get(), text.data(), text.size(), 0, text.size());
            hb_shape(hbFont.get(), hbBuffer.get(), nullptr, 0);

            unsigned int gn = 0;
            auto* infos = hb_buffer_get_glyph_infos(hbBuffer.get(), &gn);
            // Collect missing glyphs
            std::vector<uint32_t> missing;
            missing.reserve(gn);
            for(unsigned int i=0; i<gn; ++i) {
                uint32_t gid = infos[i].codepoint;
                if(hbGlyphs.find(gid) == hbGlyphs.end()) missing.push_back(gid);
            }
            if(missing.empty()) return;

            // Transition atlas to transfer dst
            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eFragmentShader, vk::PipelineStageFlagBits::eTransfer,
                {}, {}, {}, vk::ImageMemoryBarrier(
                    vk::AccessFlagBits::eShaderRead, vk::AccessFlagBits::eTransferWrite,
                    vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eTransferDstOptimal,
                    vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                    fontTexture->image,
                    vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)));

            std::vector<vk::BufferImageCopy> copies;
            copies.reserve(missing.size());
            uint8_t* base = static_cast<uint8_t*>(glyphStagingMap->get());
            vk::DeviceSize offset = 0;
            for(uint32_t gid : missing) {
                // Ensure we have space in staging buffer (simple guard)
                if(offset + 1024*1024 > glyphStagingSize) break;
                HBGlyph* entry = rasterize_and_pack_glyph(gid, base, offset);
                if(entry && entry->bitmapSize.x > 0 && entry->bitmapSize.y > 0) {
                    copies.push_back(vk::BufferImageCopy(offset, 0, 0,
                        vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1),
                        vk::Offset3D(entry->atlasPos.x, entry->atlasPos.y, 0),
                        vk::Extent3D(entry->bitmapSize.x, entry->bitmapSize.y, 1)));
                    offset += static_cast<vk::DeviceSize>(entry->bitmapSize.x * entry->bitmapSize.y * 4);
                }
            }
            if(!copies.empty()) {
                // Ensure CPU writes to the staging buffer are visible to the GPU
                // before issuing the copy. On non-coherent memory, this flush is required.
                // We flush only the written range [0, offset).
                allocator.flushAllocation(glyphStagingAlloc.get(), 0, offset);
                cmd.copyBufferToImage(glyphStagingBuf.get(), fontTexture->image, vk::ImageLayout::eTransferDstOptimal, copies);
            }
            // Back to shader read
            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
                {}, {}, {}, vk::ImageMemoryBarrier(
                    vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead,
                    vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                    vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                    fontTexture->image,
                    vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)));
        }
#endif

        std::shared_future<void> preload(resource_loader* loader,
            const std::vector<vk::RenderPass>& renderPasses, vk::SampleCountFlagBits sampleCount,
            vk::PipelineCache pipelineCache = {},
            FT_Library ft = nullptr,
            char32_t startChar = default_start_char, char32_t endChar = default_end_char,
            size_t maxCharacters = default_max_characters, size_t maxTexts = default_max_texts)
        {
            this->maxCharacters = maxCharacters;
            this->maxTexts = maxTexts;

            if(compat_mode) {
                spdlog::warn("Font Renderer: No geometry shader support, falling back to compatibility mode");
            }

            std::unique_ptr<ft_library_wrapper> ftManager;
            FT_Library ftLib = nullptr;
            if(ft == nullptr) {
                ftManager = std::make_unique<ft_library_wrapper>();
                ftLib = ftManager->get();
            } else {
                ftLib = ft;
            }

            std::unique_ptr<ft_face_wrapper> ftFace = std::make_unique<ft_face_wrapper>(ftLib, fontName);
            if(FT_Set_Pixel_Sizes(ftFace->get(), 0, fontSize) != 0) {
                throw std::runtime_error("Failed to set font size");
            }
#ifdef DREAMRENDER_USE_HARFBUZZ
            int width = 2048, height = 2048;
            spdlog::debug("Font atlas (HB) {}x{}", width, height);
            // Init line height/baseline from face metrics
            lineHeight = static_cast<float>(ftFace->get()->size->metrics.height >> 6);
            baselinePx = (ftFace->get()->size->metrics.ascender >> 6);
            atlasWidth = width; atlasHeight = height; packX = 0; packY = 0; packShelfH = 0;
            // Create empty texture; upload zeros
            fontTexture = std::make_unique<texture>(device, allocator, width, height,
                vk::ImageUsageFlagBits::eSampled, vk::Format::eR8G8B8A8Unorm);
            textureReady = loader->loadTexture(fontTexture.get(), [width, height](uint8_t* p, size_t size){
                size_t need = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
                if(need > size) throw std::runtime_error("Font atlas staging too small");
                std::fill(p, p+need, 0x00);
            });
            // Create staging buffer for glyph uploads
            glyphStagingSize = 8ull*1024ull*1024ull;
            vk::BufferCreateInfo bi({}, glyphStagingSize, vk::BufferUsageFlagBits::eTransferSrc);
            vma::AllocationCreateInfo ai({}, vma::MemoryUsage::eCpuToGpu);
            auto [sbuf, salloc] = allocator.createBufferUnique(bi, ai);
            glyphStagingBuf = std::move(sbuf);
            glyphStagingAlloc = std::move(salloc);
            glyphStagingMap = std::make_unique<vma::MemoryMapping>(allocator, glyphStagingAlloc.get());
#else
            unsigned int columns = std::ceil(std::sqrt(static_cast<double>(endChar - startChar + 1)));
            unsigned int rows = std::ceil(static_cast<double>(endChar - startChar + 1) / columns);

            unsigned int maxWidth = 0;
            unsigned int maxHeight = 0;
            FT_Int baseline = 0;

            for(char32_t c=startChar; c<=endChar; c++) {
                FT_UInt glyph_index = FT_Get_Char_Index(ftFace->get(), c);
                FT_Int32 load_flags = FT_LOAD_DEFAULT;
                if(FT_Load_Glyph(ftFace->get(), glyph_index, load_flags) != 0) {
                    throw std::runtime_error("Failed to load glyph");
                }
                FT_GlyphSlot slot = ftFace->get()->glyph;
                baseline = std::max(baseline, slot->bitmap_top);
                maxWidth = std::max(maxWidth, slot->bitmap.width);
                maxHeight = std::max(maxHeight, slot->bitmap.rows);
            }
            maxWidth += 4;
            maxHeight += 4;

            long width = columns * maxWidth;
            long height = rows * maxHeight;
            spdlog::debug("Font texture will be {}x{} ({}x{})", width, height, columns, rows);

            char32_t ch = startChar;
            for(int r=0; r<rows; r++) {
                for(int c=0; c<columns; c++) {
                    if(ch > endChar) {
                        break;
                    }

                    FT_UInt glyph_index = FT_Get_Char_Index(ftFace->get(), ch);
                    FT_Int32 load_flags = FT_LOAD_DEFAULT;
                    if(FT_Load_Glyph(ftFace->get(), glyph_index, load_flags) != 0) {
                        throw std::runtime_error("Failed to load glyph");
                    }
                    if(FT_Render_Glyph(ftFace->get()->glyph, FT_RENDER_MODE_NORMAL) != 0) {
                        throw std::runtime_error("Failed to render glyph");
                    }
                    FT_GlyphSlot slot = ftFace->get()->glyph;
                    int tileX = c * maxWidth;
                    int tileY = r * maxHeight;
                    GlyphMetrics gm;
                    gm.bitmapSize = {slot->bitmap.width, slot->bitmap.rows};
                    gm.bearing = {slot->bitmap_left, slot->bitmap_top};
                    gm.advance = (slot->advance.x >> 6);
                    // top-left of the bitmap inside the tile
                    gm.atlasPos = {tileX + 0, tileY + (baseline - slot->bitmap_top)};
                    glyphs.emplace(ch, gm);
                    ch++;
                }
            }
            lineHeight = static_cast<float>(maxHeight);
            baselinePx = baseline;
#endif

            struct guarantee_order {
#if __cpp_lib_move_only_function >= 202110L && __linux__
                std::unique_ptr<ft_library_wrapper> ftManager;
                std::unique_ptr<ft_face_wrapper> ftFace;
#else
                std::shared_ptr<ft_library_wrapper> ftManager;
                std::shared_ptr<ft_face_wrapper> ftFace;
#endif
            } go {
                .ftManager=std::move(ftManager), .ftFace=std::move(ftFace)
            };

#ifndef DREAMRENDER_USE_HARFBUZZ
            // Use UNORM to avoid sRGB gamma interaction on grayscale glyphs
            fontTexture = std::make_unique<texture>(device, allocator, width, height,
                vk::ImageUsageFlagBits::eSampled, vk::Format::eR8G8B8A8Unorm);
            textureReady = loader->loadTexture(fontTexture.get(),
                [
                    this, columns, rows, maxWidth, maxHeight, startChar, endChar, baseline, width,
                    go = std::move(go)
                ](uint8_t* p, size_t size)
                {
                    assert(sizeof(uint32_t)*rows*maxHeight*columns*maxWidth <= size);
                    auto* pixels = reinterpret_cast<uint32_t*>(p);

                    char32_t ch = startChar;
                    FT_Face ftFace = go.ftFace->get();
                    FT_Select_Charmap(ftFace, ft_encoding_unicode);
                    for(int r=0; r<rows; r++) {
                        for(int c=0; c<columns; c++) {
                            if(ch > endChar) {
                                break;
                            }

                            FT_UInt glyph_index = FT_Get_Char_Index(ftFace, ch);
                            FT_Int32 load_flags = FT_LOAD_DEFAULT;
                            if(FT_Load_Glyph(ftFace, glyph_index, load_flags) != 0) {
                                throw std::runtime_error("Failed to load glyph");
                            }
                            if(FT_Render_Glyph(ftFace->glyph, FT_RENDER_MODE_NORMAL) != 0) {
                                throw std::runtime_error("Failed to render glyph");
                            }
                            FT_GlyphSlot slot = ftFace->glyph;
                            FT_Bitmap bitmap = slot->bitmap;
                            for(int y=0; y<bitmap.rows; y++) {
                                for(int x=0; x<bitmap.width; x++) {
                                    if(ch > endChar) {
                                        break;
                                    }

                                    unsigned char pixel_brightness = bitmap.buffer[y * bitmap.pitch + x];
                                    unsigned int color = pixel_brightness | pixel_brightness << 8 | pixel_brightness << 16 | pixel_brightness << 24;

                                    int dx = x;
                                    int dy = y + baseline - slot->bitmap_top; // position glyph relative to baseline

                                    pixels[(r*maxHeight + dy)*width + c*maxWidth + dx] = color;
                                }
                            }
                            ch++;
                        }
                    }
                }
            );
#endif

            // Clamp to edge to avoid bleeding between glyph tiles
            vk::SamplerCreateInfo sampler_info({}, vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
                vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge,
                0.0f, false, 0.0f, false, vk::CompareOp::eNever, 0.0f, 0.0f, vk::BorderColor::eFloatTransparentBlack, false);
            sampler = device.createSamplerUnique(sampler_info);

#ifdef DREAMRENDER_USE_HARFBUZZ
            // Create persistent FT + HB objects for shaping
            hbFtLib = std::make_unique<ft_library_wrapper>();
            shapingFace = std::make_unique<ft_face_wrapper>(hbFtLib->get(), fontName);
            if(FT_Set_Pixel_Sizes(shapingFace->get(), 0, fontSize) != 0) {
                throw std::runtime_error("Failed to set font size (HB)");
            }
            hbFont.reset(hb_ft_font_create(shapingFace->get(), nullptr));
            hbBuffer.reset(hb_buffer_create());
            hb_buffer_set_cluster_level(hbBuffer.get(), HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS);
#endif

            {
                std::array<vk::DescriptorSetLayoutBinding, 2> bindings = {
                    vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBufferDynamic, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eGeometry | vk::ShaderStageFlagBits::eFragment),
                    vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment)
                };
                vk::DescriptorSetLayoutCreateInfo layout_info({}, bindings);
                descriptorLayout = device.createDescriptorSetLayoutUnique(layout_info);
                debugName(device, descriptorLayout.get(), "Font Renderer Descriptor Layout");
            }
            {
                vk::PipelineLayoutCreateInfo layout_info({}, descriptorLayout.get());
                pipelineLayout = device.createPipelineLayoutUnique(layout_info);
                debugName(device, pipelineLayout.get(), "Font Renderer Pipeline Layout");
            }
            {
                // Remember sample count for later on-demand pipeline builds
                last_sample_count = sampleCount;
                build_pipelines(renderPasses, pipelineCache);
            }
            return textureReady;
        }
        // Expose the font atlas for optional debugging (e.g., draw via image_renderer)
        const texture* get_atlas() const { return fontTexture.get(); }
        void prepare(int imageCount) {
            std::array<vk::DescriptorPoolSize, 2> sizes = {
                vk::DescriptorPoolSize(vk::DescriptorType::eUniformBufferDynamic, 1*imageCount),
                vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 1*imageCount)
            };
            vk::DescriptorPoolCreateInfo pool_info({}, imageCount, sizes);
            descriptorPool = device.createDescriptorPoolUnique(pool_info);

            std::vector<vk::DescriptorSetLayout> layouts(imageCount);
            std::fill(layouts.begin(), layouts.end(), descriptorLayout.get());
            vk::DescriptorSetAllocateInfo set_info(descriptorPool.get(), layouts);
            descriptorSets = device.allocateDescriptorSets(set_info);

            const auto alignment = allocator.getPhysicalDeviceProperties()->limits.minUniformBufferOffsetAlignment;

            vertexPointers.clear();
            vertexMappings.clear();
            vertexBuffers.clear();
            vertexMemories.clear();

            uniformPointers.clear();
            uniformMappings.clear();
            uniformBuffers.clear();
            uniformMemories.clear();

            std::vector<vk::WriteDescriptorSet> writes(imageCount*2);
            std::vector<vk::DescriptorBufferInfo> bufferInfos(imageCount);
            vk::DescriptorImageInfo imageInfo(sampler.get(), fontTexture->imageView.get(), vk::ImageLayout::eShaderReadOnlyOptimal);
            for(int i=0; i<imageCount; i++)
            {
                {
                    vk::DeviceSize size{};
                    if(compat_mode) {
                        size = maxTexts*maxCharacters*sizeof(VertexCharacter)*6;
                    } else {
                        size = maxTexts*maxCharacters*sizeof(VertexCharacter);
                    }
                    vk::BufferCreateInfo vertex_info({}, size, vk::BufferUsageFlagBits::eVertexBuffer);
                    vma::AllocationCreateInfo va_info({}, vma::MemoryUsage::eCpuToGpu);
                    auto [vb, va] = allocator.createBufferUnique(vertex_info, va_info);
                    auto& mapping = vertexMappings.emplace_back(allocator, va.get());
                    vertexPointers.push_back(reinterpret_cast<VertexCharacter*>(mapping.get()));
                    vertexBuffers.push_back(std::move(vb));
                    vertexMemories.push_back(std::move(va));
                }

                {
                    vk::BufferCreateInfo uniform_info({}, maxTexts*sizeof(TextUniform), vk::BufferUsageFlagBits::eUniformBuffer);
                    vma::AllocationCreateInfo ua_info({}, vma::MemoryUsage::eCpuToGpu);
                    auto [ub, ua] = allocator.createBufferUnique(uniform_info, ua_info);
                    auto& mapping = uniformMappings.emplace_back(allocator, ua.get());
                    uniformPointers.emplace_back(mapping.get(), alignment);

                    bufferInfos[i].setBuffer(ub.get()).setOffset(0).setRange(sizeof(TextUniform));
                    writes[2*i+0].setDstSet(descriptorSets[i]).setDstBinding(0).setDstArrayElement(0)
                        .setDescriptorType(vk::DescriptorType::eUniformBufferDynamic).setDescriptorCount(1).setBufferInfo(bufferInfos[i]);
                    writes[2*i+1].setDstSet(descriptorSets[i]).setDstBinding(1).setDstArrayElement(0)
                        .setDescriptorType(vk::DescriptorType::eCombinedImageSampler).setDescriptorCount(1).setImageInfo(imageInfo);

                    uniformBuffers.push_back(std::move(ub));
                    uniformMemories.push_back(std::move(ua));
                }
            }
            device.updateDescriptorSets(writes, {});

            uniformOffsets.resize(imageCount);
            vertexOffsets.resize(imageCount);
        }
        void finish(int frame) {
            vertexOffsets[frame] = 0;
            uniformOffsets[frame] = 0;
        }

        void renderText(vk::CommandBuffer cmd, int frame, vk::RenderPass renderPass, std::string_view text,
            float x, float y, float scale = 1.0f, glm::vec4 color = glm::vec4(1.0, 1.0, 1.0, 1.0))
        {
            if(vertexOffsets[frame] + text.size() > maxTexts*maxCharacters) {
                throw std::runtime_error("Too many characters");
            }
            if(uniformOffsets[frame] >= maxTexts) {
                throw std::runtime_error("Too many texts");
            }
            if(textureReady.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                return;
            }

            int compat_factor = compat_mode ? 6 : 1;
            int total_chars = 0;
            {
                VertexCharacter* vc = vertexPointers[frame] + compat_factor*vertexOffsets[frame];

#ifdef DREAMRENDER_USE_HARFBUZZ
                if(hbFont && hbBuffer) {
                    hb_buffer_clear_contents(hbBuffer.get());
                    hb_buffer_set_direction(hbBuffer.get(), HB_DIRECTION_LTR);
                    hb_buffer_set_script(hbBuffer.get(), HB_SCRIPT_COMMON);
                    hb_buffer_set_language(hbBuffer.get(), hb_language_from_string("en", -1));
                    hb_buffer_add_utf8(hbBuffer.get(), text.data(), text.size(), 0, text.size());
                    hb_shape(hbFont.get(), hbBuffer.get(), nullptr, 0);
                    unsigned int gn = 0;
                    auto* infos = hb_buffer_get_glyph_infos(hbBuffer.get(), &gn);
                    auto* pos = hb_buffer_get_glyph_positions(hbBuffer.get(), &gn);
                    float cx = 0.0f, cy = 0.0f;
                    for(unsigned int i=0; i<gn; ++i) {
                        uint32_t gid = infos[i].codepoint;
                        auto it = hbGlyphs.find(gid);
                        if(it == hbGlyphs.end()) { continue; }
                        const HBGlyph& g = it->second;
                        float x_off = pos[i].x_offset/64.0f/lineHeight;
                        float y_off = -pos[i].y_offset/64.0f/lineHeight;
                        if(compat_mode) {
                            float x0 = cx + x_off;
                            float y0 = cy + y_off;
                            glm::vec2 size = {static_cast<float>(g.bitmapSize.x) / lineHeight, static_cast<float>(g.bitmapSize.y) / lineHeight};
                            float invW = 1.0f / static_cast<float>(fontTexture->width);
                            float invH = 1.0f / static_cast<float>(fontTexture->height);
                            glm::vec2 uv0{static_cast<float>(g.atlasPos.x) * invW, static_cast<float>(g.atlasPos.y) * invH};
                            glm::vec2 uv1{static_cast<float>(g.atlasPos.x+g.bitmapSize.x) * invW, static_cast<float>(g.atlasPos.y+g.bitmapSize.y) * invH};
                            VertexCharacter tl{ {x0, y0}, {uv0.x, uv0.y}, {}, color };
                            VertexCharacter bl{ {x0, y0+size.y}, {uv0.x, uv1.y}, {}, color };
                            VertexCharacter tr{ {x0+size.x, y0}, {uv1.x, uv0.y}, {}, color };
                            VertexCharacter br{ {x0+size.x, y0+size.y}, {uv1.x, uv1.y}, {}, color };
                            vc[6*total_chars+0] = tl;
                            vc[6*total_chars+1] = bl;
                            vc[6*total_chars+2] = tr;
                            vc[6*total_chars+3] = tr;
                            vc[6*total_chars+4] = bl;
                            vc[6*total_chars+5] = br;
                        } else {
                            float x0 = cx + x_off + static_cast<float>(g.bearing.x) / lineHeight;
                            float y0 = cy + y_off + static_cast<float>(baselinePx - g.bearing.y) / lineHeight;
                            vc[total_chars] = { {x0, y0}, {static_cast<float>(g.atlasPos.x) / lineHeight, static_cast<float>(g.atlasPos.y) / lineHeight}, {static_cast<float>(g.bitmapSize.x) / lineHeight, static_cast<float>(g.bitmapSize.y) / lineHeight}, color };
                        }
                        cx += pos[i].x_advance/64.0f/lineHeight;
                        total_chars++;
                    }
                } else
#endif
                {
                    float cx = 0; float cy = 0; std::mbstate_t mb{};
                    for(int i=0; i<static_cast<int>(text.size());) {
                        char32_t c{};
#if __linux__
                        int j = std::mbrtoc32(&c, text.data()+i, text.size()-i, &mb);
                        if(j < 0) { spdlog::error("Failed to convert character at {}", i); break; } else { i += j; }
#else
                        c = static_cast<char32_t>(text[i]); i++;
#endif
                        if(c == '\n') { cy += 1; cx = 0; continue; }
                        if(!glyphs.contains(c)) c = '?';
                        const GlyphMetrics& g = glyphs.at(c);
                        if(compat_mode) {
                            float x0 = cx; float y0 = cy;
                            glm::vec2 size = {static_cast<float>(g.bitmapSize.x) / lineHeight, static_cast<float>(g.bitmapSize.y) / lineHeight};
                            float invW = 1.0f / static_cast<float>(fontTexture->width);
                            float invH = 1.0f / static_cast<float>(fontTexture->height);
                            VertexCharacter tl{ {x0, y0}, {static_cast<float>(g.atlasPos.x) * invW, static_cast<float>(g.atlasPos.y) * invH}, {}, color };
                            VertexCharacter bl{ {x0, y0+size.y}, {static_cast<float>(g.atlasPos.x) * invW, static_cast<float>(g.atlasPos.y+g.bitmapSize.y) * invH}, {}, color };
                            VertexCharacter tr{ {x0+size.x, y0}, {static_cast<float>(g.atlasPos.x+g.bitmapSize.x) * invW, static_cast<float>(g.atlasPos.y) * invH}, {}, color };
                            VertexCharacter br{ {x0+size.x, y0+size.y}, {static_cast<float>(g.atlasPos.x+g.bitmapSize.x) * invW, static_cast<float>(g.atlasPos.y+g.bitmapSize.y) * invH}, {}, color };
                            vc[6*total_chars+0] = tl; vc[6*total_chars+1] = bl; vc[6*total_chars+2] = tr; vc[6*total_chars+3] = tr; vc[6*total_chars+4] = bl; vc[6*total_chars+5] = br;
                        } else {
                            float x0 = cx + static_cast<float>(g.bearing.x) / lineHeight;
                            float y0 = cy + static_cast<float>(baselinePx - g.bearing.y) / lineHeight;
                            vc[total_chars] = { {x0, y0}, {static_cast<float>(g.atlasPos.x) / lineHeight, static_cast<float>(g.atlasPos.y) / lineHeight}, {static_cast<float>(g.bitmapSize.x) / lineHeight, static_cast<float>(g.bitmapSize.y) / lineHeight}, color };
                        }
                        cx += static_cast<float>(g.advance) / lineHeight;
                        total_chars++;
                    }
                }
            }
            {
                // Position the text run using the provided (x,y) in normalized space
                glm::vec2 pos = glm::vec2(x, y)*2.0f - glm::vec2(1.0f);

                TextUniform& uni = uniformPointers[frame][uniformOffsets[frame]];
                glm::mat4 matrix = glm::mat4(1.0f);
                matrix = glm::translate(matrix, glm::vec3(pos, 0.0f));
                matrix = glm::scale(matrix, glm::vec3(scale/aspectRatio, scale, 1.0f));
                uni.matrix = matrix;
                // Provide atlas dimensions in pixels; shader divides by this to normalize
                if(compat_mode) {
                    // compat path uses normalized texcoords
                    uni.textureSize = {1.0f, 1.0f};
                } else {
                    // geometry path retains lineHeight-normalized texcoords
                    uni.textureSize = {
                        static_cast<float>(fontTexture->width) / lineHeight,
                        static_cast<float>(fontTexture->height) / lineHeight
                    };
                }
            }
            auto itp = pipelines.find(renderPass);
            if(itp == pipelines.end() || !itp->second) {
                spdlog::warn("[FontRenderer] Pipeline for renderPass not found; creating on-demand");
                build_pipelines({renderPass}, {});
                itp = pipelines.find(renderPass);
                if(itp == pipelines.end() || !itp->second) {
                    spdlog::error("[FontRenderer] Failed to build pipeline for current renderPass");
                    return;
                }
            }
            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, itp->second.get());
            cmd.bindVertexBuffers(0, vertexBuffers[frame].get(), compat_factor*vertexOffsets[frame]*sizeof(VertexCharacter));
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout.get(), 0, descriptorSets[frame], uniformPointers[frame].offset(uniformOffsets[frame]));
            cmd.draw(compat_factor*total_chars, 1, 0, 0);

            vertexOffsets[frame] += total_chars;
            uniformOffsets[frame]++;
        }
        glm::vec2 measureText(std::string_view text, float scale = 1.0f) {
            float width = 0;
#ifdef DREAMRENDER_USE_HARFBUZZ
            if(hbFont && hbBuffer) {
                hb_buffer_clear_contents(hbBuffer.get());
                hb_buffer_set_direction(hbBuffer.get(), HB_DIRECTION_LTR);
                hb_buffer_set_script(hbBuffer.get(), HB_SCRIPT_COMMON);
                hb_buffer_set_language(hbBuffer.get(), hb_language_from_string("en", -1));
                hb_buffer_add_utf8(hbBuffer.get(), text.data(), text.size(), 0, text.size());
                hb_shape(hbFont.get(), hbBuffer.get(), nullptr, 0);
                unsigned int gn = 0;
                auto* pos = hb_buffer_get_glyph_positions(hbBuffer.get(), &gn);
                for(unsigned int i=0; i<gn; ++i) {
                    width += pos[i].x_advance/64.0f/lineHeight;
                }
                return glm::vec2{width*scale/aspectRatio, scale}/2.0f;
            }
#endif
            for(char i : text)
            {
                if(i == '\n') continue;
                auto it = glyphs.find(static_cast<char32_t>(i));
                if(it == glyphs.end()) continue;
                const GlyphMetrics& m = (*it).second;
                width += static_cast<float>(m.advance)/lineHeight;
            }
            return glm::vec2{width*scale/aspectRatio, scale}/2.0f;
        }


    private:
        void build_pipelines(const std::vector<vk::RenderPass>& renderPasses, vk::PipelineCache pipelineCache) {
            vk::UniqueShaderModule vertexShader = compat_mode ?
                shaders::font_renderer::vert_compat(device) :
                shaders::font_renderer::vert(device);
            vk::UniqueShaderModule geometryShader = {};
            vk::UniqueShaderModule fragmentShader = shaders::font_renderer::frag(device);
            std::vector<vk::PipelineShaderStageCreateInfo> shaderStages = {
                vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, vertexShader.get(), "main"),
                vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, fragmentShader.get(), "main")
            };
            if(!compat_mode) {
                geometryShader = shaders::font_renderer::geom(device);
                shaderStages.insert(shaderStages.begin()+1,
                    vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eGeometry, geometryShader.get(), "main"));
            }

            vk::VertexInputBindingDescription binding(0, sizeof(VertexCharacter), vk::VertexInputRate::eVertex);
            std::array<vk::VertexInputAttributeDescription, 4> attributes = {
                vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32Sfloat, offsetof(VertexCharacter, position)),
                vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32Sfloat, offsetof(VertexCharacter, texCoord)),
                vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32Sfloat, offsetof(VertexCharacter, size)),
                vk::VertexInputAttributeDescription(3, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(VertexCharacter, color)),
            };

            vk::PipelineVertexInputStateCreateInfo vertex_input({}, binding, attributes);
            vk::PipelineInputAssemblyStateCreateInfo input_assembly({},
                compat_mode ? vk::PrimitiveTopology::eTriangleList : vk::PrimitiveTopology::ePointList);
            vk::PipelineTessellationStateCreateInfo tesselation({}, {});

            vk::Viewport v{};
            vk::Rect2D s{};
            vk::PipelineViewportStateCreateInfo viewport({}, v, s);

            vk::PipelineRasterizationStateCreateInfo rasterization({}, false, false, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise, false, 0.0f, 0.0f, 0.0f, 1.0f);
            vk::PipelineMultisampleStateCreateInfo multisample({}, last_sample_count);
            vk::PipelineDepthStencilStateCreateInfo depthStencil({}, false, false);

            vk::PipelineColorBlendAttachmentState attachment(true, vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd,
                vk::BlendFactor::eOne, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd,
                vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
            vk::PipelineColorBlendStateCreateInfo colorBlend({}, false, vk::LogicOp::eClear, attachment);

            std::array<vk::DynamicState, 2> dynamicStates{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
            vk::PipelineDynamicStateCreateInfo dynamic({}, dynamicStates);

            vk::GraphicsPipelineCreateInfo pipeline_info({}, shaderStages, &vertex_input,
                &input_assembly, &tesselation, &viewport, &rasterization, &multisample, &depthStencil, &colorBlend, &dynamic, pipelineLayout.get(), {});

            auto newPipelines = createPipelines(device, pipelineCache, pipeline_info, renderPasses, "Font Renderer Pipeline");
            for(auto& [rp, p] : newPipelines) {
                pipelines[rp] = std::move(p);
            }
        }
        
#ifdef DREAMRENDER_USE_HARFBUZZ
        struct HBGlyph {
            glm::ivec2 atlasPos;
            glm::ivec2 bitmapSize;
            glm::ivec2 bearing;
            int advance;
        };
        // Rasterize glyph and place it in the atlas using a simple shelf packer.
        // Writes RGBA pixels into the staging buffer at current offset.
        // Returns pointer to the inserted entry or nullptr on failure.
        HBGlyph* rasterize_and_pack_glyph(uint32_t gid, uint8_t* stagingBase, vk::DeviceSize curOffset) {
            if(!shapingFace) return nullptr;
            FT_Face face = shapingFace->get();
            if(FT_Load_Glyph(face, gid, FT_LOAD_DEFAULT) != 0) return nullptr;
            if(FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0) return nullptr;
            FT_GlyphSlot slot = face->glyph;
            FT_Bitmap bmp = slot->bitmap;
            int w = bmp.width, h = bmp.rows;
            if(w == 0 || h == 0) {
                // Space-like glyph; synthesize empty entry with zero bitmap
                auto [it, ok] = hbGlyphs.emplace(gid, HBGlyph{ {packX, packY}, {0,0}, {slot->bitmap_left, slot->bitmap_top}, int(slot->advance.x >> 6) });
                return &it->second;
            }
            // Advance packer to next shelf if needed
            if(packX + w > atlasWidth) { packX = 0; packY += packShelfH; packShelfH = 0; }
            if(packY + h > atlasHeight) {
                spdlog::error("Font atlas full ({}x{}); cannot place glyph {}", atlasWidth, atlasHeight, gid);
                return nullptr;
            }
            // Copy grayscale to RGBA in staging buffer
            uint8_t* dst = stagingBase + curOffset;
            for(int yy=0; yy<h; ++yy) {
                for(int xx=0; xx<w; ++xx) {
                    uint8_t v = bmp.buffer[yy*bmp.pitch + xx];
                    size_t o = (size_t(yy)*size_t(w) + size_t(xx)) * 4;
                    dst[o+0] = v; dst[o+1] = v; dst[o+2] = v; dst[o+3] = v;
                }
            }
            HBGlyph entry{ {packX, packY}, {w, h}, {slot->bitmap_left, slot->bitmap_top}, int(slot->advance.x >> 6) };
            auto res = hbGlyphs.emplace(gid, entry);
            packX += w + 1; packShelfH = std::max(packShelfH, h + 1);
            return &res.first->second;
        }
#endif
        vk::Device device;
        vma::Allocator allocator;
        std::string fontName;
        int fontSize;

        bool compat_mode{};
        vk::SampleCountFlagBits last_sample_count{vk::SampleCountFlagBits::e1};

#ifdef DREAMRENDER_USE_HARFBUZZ
        struct HbBufferDeleter { void operator()(hb_buffer_t* b) const noexcept { if(b) hb_buffer_destroy(b); } };
        struct HbFontDeleter { void operator()(hb_font_t* f) const noexcept { if(f) hb_font_destroy(f); } };
        std::unique_ptr<ft_library_wrapper> hbFtLib;
        std::unique_ptr<ft_face_wrapper> shapingFace;
        std::unique_ptr<hb_font_t, HbFontDeleter> hbFont;
        std::unique_ptr<hb_buffer_t, HbBufferDeleter> hbBuffer;
        // Dynamic atlas state
        int atlasWidth{};
        int atlasHeight{};
        int packX{};
        int packY{};
        int packShelfH{};
        std::unordered_map<uint32_t, HBGlyph> hbGlyphs;
        vma::UniqueBuffer glyphStagingBuf;
        vma::UniqueAllocation glyphStagingAlloc;
        std::unique_ptr<vma::MemoryMapping> glyphStagingMap;
        vk::DeviceSize glyphStagingSize{};
#endif

        struct GlyphMetrics {
            glm::ivec2 atlasPos;     // top-left in atlas (pixels)
            glm::ivec2 bitmapSize;   // width/height of bitmap (pixels)
            glm::ivec2 bearing;      // bitmap_left, bitmap_top (pixels)
            int advance;             // advance.x (pixels)
        };
        std::unordered_map<char32_t, GlyphMetrics> glyphs;
        size_t maxCharacters{};
        size_t maxTexts{};
        float lineHeight{};
        int baselinePx{};

        struct VertexCharacter {
            glm::vec2 position;
            glm::vec2 texCoord;
            glm::vec2 size;
            glm::vec4 color;
        };
        struct TextUniform {
            glm::mat4 matrix;
            glm::vec2 textureSize;
            glm::vec2 _pad; // std140 padding to 16-byte multiple
        };
        std::vector<VertexCharacter*> vertexPointers;
        std::vector<aligned_wrapper<TextUniform>> uniformPointers;

        double aspectRatio;

        std::unique_ptr<texture> fontTexture;
        std::shared_future<void> textureReady;
        vk::UniqueSampler sampler;

        vk::UniquePipelineLayout pipelineLayout;
        UniquePipelineMap pipelines;

        vk::UniqueDescriptorSetLayout descriptorLayout;
        vk::UniqueDescriptorPool descriptorPool;
        std::vector<vk::DescriptorSet> descriptorSets;

        std::vector<vma::UniqueBuffer> uniformBuffers;
        std::vector<vma::UniqueAllocation> uniformMemories;
        std::vector<vk::DeviceSize> uniformOffsets;
        std::vector<vma::MemoryMapping> uniformMappings;

        std::vector<vma::UniqueBuffer> vertexBuffers;
        std::vector<vma::UniqueAllocation> vertexMemories;
        std::vector<uint32_t> vertexOffsets;
        std::vector<vma::MemoryMapping> vertexMappings;
};

}
