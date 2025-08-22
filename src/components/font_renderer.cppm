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

#include <freetype2/ft2build.h>
#include <freetype/freetype.h>

export module dreamrender:components.font_renderer;

import :resource_loader;
import :shaders;
import :texture;
import :utils;

import spdlog;
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
                    glyphs[ch] = gm;
                    ch++;
                }
            }
            lineHeight = static_cast<float>(maxHeight);
            baselinePx = baseline;

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

            fontTexture = std::make_unique<texture>(device, allocator, width, height);
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

            vk::SamplerCreateInfo sampler_info({}, vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
                vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
                0.0f, false, 0.0f, false, vk::CompareOp::eNever, 0.0f, 0.0f, vk::BorderColor::eFloatTransparentBlack, false);
            sampler = device.createSamplerUnique(sampler_info);

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
                vk::UniqueShaderModule vertexShader = compat_mode ?
                    shaders::font_renderer::vert_compat(device) :
                    shaders::font_renderer::vert(device);
                vk::UniqueShaderModule geometryShader = {};
                vk::UniqueShaderModule fragmentShader = shaders::font_renderer::frag(device);
                std::vector<vk::PipelineShaderStageCreateInfo> shaders = {
                    vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, vertexShader.get(), "main"),
                    vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, fragmentShader.get(), "main")
                };
                if(!compat_mode) {
                    geometryShader = shaders::font_renderer::geom(device);
                    shaders.insert(shaders.begin()+1, // not sure if the position is important
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
                vk::PipelineMultisampleStateCreateInfo multisample({}, sampleCount);
                vk::PipelineDepthStencilStateCreateInfo depthStencil({}, false, false);

                vk::PipelineColorBlendAttachmentState attachment(true, vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd,
                    vk::BlendFactor::eOne, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd,
                    vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
                vk::PipelineColorBlendStateCreateInfo colorBlend({}, false, vk::LogicOp::eClear, attachment);

                std::array<vk::DynamicState, 2> dynamicStates{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
                vk::PipelineDynamicStateCreateInfo dynamic({}, dynamicStates);

                vk::GraphicsPipelineCreateInfo pipeline_info({}, shaders, &vertex_input,
                    &input_assembly, &tesselation, &viewport, &rasterization, &multisample, &depthStencil, &colorBlend, &dynamic, pipelineLayout.get(), {});
                pipelines = createPipelines(device, pipelineCache, pipeline_info, renderPasses, "Font Renderer Pipeline");
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
                float cx = 0; // cursor x within the text run
                float cy = 0; // cursor y within the text run (line index)
                std::mbstate_t mb{};
                for(int i=0; i<text.size();)
                {
                    char32_t c{};
#if __linux__ // for some reason this causes a crash in WINE, so for now this is Linux only
                    int j = std::mbrtoc32(&c, text.data()+i, text.size()-i, &mb);
                    if(j < 0) {
                        spdlog::error("Failed to convert character at index {}: {}", i, std::strerror(errno));
                        break;
                    } else {
                        i += j;
                    }
#else
                    c = static_cast<char32_t>(text[i]);
                    i++;
#endif

                    if(c == '\n') {
                        cy += 1;
                        cx = 0;
                        continue;
                    } else if(!glyphs.contains(c)) {
                        c = '?';
                    }

                    const GlyphMetrics& g = glyphs[c];
                    if(compat_mode) {
                        // Compute quad position using glyph bearings; texcoords in pixel space
                        float x0 = cx + static_cast<float>(g.bearing.x) / lineHeight;
                        float y0 = cy + static_cast<float>(baselinePx - g.bearing.y) / lineHeight;
                        glm::vec2 size = {static_cast<float>(g.bitmapSize.x) / lineHeight, static_cast<float>(g.bitmapSize.y) / lineHeight};

                        VertexCharacter topLeft = {
                            .position = {x0, y0},
                            .texCoord = {static_cast<float>(g.atlasPos.x), static_cast<float>(g.atlasPos.y)},
                            .size = {}, // unused in compat path
                            .color = color};
                        VertexCharacter bottomLeft = {
                            .position = {x0, y0+size.y},
                            .texCoord = {static_cast<float>(g.atlasPos.x), static_cast<float>(g.atlasPos.y+g.bitmapSize.y)},
                            .size = {}, // unused in compat path
                            .color = color};
                        VertexCharacter topRight = {
                            .position = {x0+size.x, y0},
                            .texCoord = {static_cast<float>(g.atlasPos.x+g.bitmapSize.x), static_cast<float>(g.atlasPos.y)},
                            .size = {}, // unused in compat path
                            .color = color};
                        VertexCharacter bottomRight = {
                            .position = {x0+size.x, y0+size.y},
                            .texCoord = {static_cast<float>(g.atlasPos.x+g.bitmapSize.x), static_cast<float>(g.atlasPos.y+g.bitmapSize.y)},
                            .size = {}, // unused in compat path
                            .color = color};

                        vc[6*total_chars+0] = topLeft;
                        vc[6*total_chars+1] = bottomLeft;
                        vc[6*total_chars+2] = topRight;
                        vc[6*total_chars+3] = topRight;
                        vc[6*total_chars+4] = bottomLeft;
                        vc[6*total_chars+5] = bottomRight;
                    } else {
                        // Geometry path: position in line-height units using bearings; texcoords AND inSize in line-height units
                        // so the geometry shader math remains consistent.
                        float x0 = cx + static_cast<float>(g.bearing.x) / lineHeight;
                        float y0 = cy + static_cast<float>(baselinePx - g.bearing.y) / lineHeight;
                        vc[total_chars] = {
                            .position = {x0, y0},
                            .texCoord = {static_cast<float>(g.atlasPos.x) / lineHeight, static_cast<float>(g.atlasPos.y) / lineHeight},
                            .size = {static_cast<float>(g.bitmapSize.x) / lineHeight, static_cast<float>(g.bitmapSize.y) / lineHeight},
                            .color = color};
                    }
                    cx += static_cast<float>(g.advance) / lineHeight;
                    total_chars++;
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
                    // pixel-space texcoords in compat path
                    uni.textureSize = {
                        static_cast<float>(fontTexture->width),
                        static_cast<float>(fontTexture->height)
                    };
                } else {
                    // geometry path retains lineHeight-normalized texcoords
                    uni.textureSize = {
                        static_cast<float>(fontTexture->width) / lineHeight,
                        static_cast<float>(fontTexture->height) / lineHeight
                    };
                }
            }
            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines[renderPass].get());
            cmd.bindVertexBuffers(0, vertexBuffers[frame].get(), compat_factor*vertexOffsets[frame]*sizeof(VertexCharacter));
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout.get(), 0, descriptorSets[frame], uniformPointers[frame].offset(uniformOffsets[frame]));
            cmd.draw(compat_factor*total_chars, 1, 0, 0);

            vertexOffsets[frame] += total_chars;
            uniformOffsets[frame]++;
        }
        glm::vec2 measureText(std::string_view text, float scale = 1.0f) {
            float width = 0;
            for(char i : text)
            {
                if(i == '\n') {
                    // very simple line handling: take longest line width
                    continue;
                }
                auto it = glyphs.find(i);
                if(it == glyphs.end()) continue;
                width += static_cast<float>(it->second.advance)/lineHeight;
            }
            return glm::vec2{width*scale/aspectRatio, scale}/2.0f;
        }

    private:
        vk::Device device;
        vma::Allocator allocator;
        std::string fontName;
        int fontSize;

        bool compat_mode{};

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
