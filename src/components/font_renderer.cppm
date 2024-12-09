module;

#include <array>
#include <cassert>
#include <cmath>
#include <future>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <version>

#include <freetype2/ft2build.h>
#include <freetype/freetype.h>

export module dreamrender:components.font_renderer;

import :resource_loader;
import :shaders;
import :texture;
import :utils;

import vulkan_hpp;
import vma;

namespace dreamrender {

struct ft_library_wrapper {
    FT_Library lib;
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
    FT_Face face;
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
    const FT_Face get() const {
        return face;
    }
};

export class font_renderer {
    public:
        static constexpr char default_start_char = 32;
        static constexpr char default_end_char = 127;
        static constexpr size_t default_max_characters = 1024;
        static constexpr size_t default_max_texts = 128;

        font_renderer(const std::string& font_name, int font_size,
            vk::Device device, vma::Allocator allocator, vk::Extent2D frameSize)
            : fontName(font_name), fontSize(font_size), device(device), allocator(allocator),
              aspectRatio(static_cast<double>(frameSize.width) / frameSize.height) {}
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

            std::unique_ptr<ft_library_wrapper> ftManager;
            FT_Library ftLib;
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
                    FT_GlyphSlot slot = ftFace->get()->glyph;
                    glyphRects[ch] = vk::Rect2D{
                        vk::Offset2D{static_cast<int32_t>(c * maxWidth), static_cast<int32_t>(r * maxHeight)},
                        vk::Extent2D{static_cast<uint32_t>(slot->advance.x >> 6), maxHeight}
                    };
                    ch++;
                }
            }
            lineHeight = maxHeight;

            struct guarantee_order {
#if __cpp_lib_move_only_function >= 202110L
                std::unique_ptr<ft_library_wrapper> ftManager;
                std::unique_ptr<ft_face_wrapper> ftFace;
#else
                std::shared_ptr<ft_library_wrapper> ftManager;
                std::shared_ptr<ft_face_wrapper> ftFace;
#endif
            } go {
                std::move(ftManager), std::move(ftFace)
            };

            fontTexture = std::make_unique<texture>(device, allocator, width, height);
            textureReady = loader->loadTexture(fontTexture.get(),
                [
                    this, columns, rows, maxWidth, maxHeight, startChar, endChar, baseline, width,
                    go = std::move(go)
                ](uint8_t* p, size_t size)
                {
                    assert(sizeof(uint32_t)*rows*maxHeight*columns*maxWidth <= size);
                    uint32_t* pixels = reinterpret_cast<uint32_t*>(p);

                    char32_t ch = startChar;
                    FT_Face ftFace = go.ftFace->get();
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
                                    int dy = y + baseline - slot->bitmap_top;

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
                vk::UniqueShaderModule vertexShader = shaders::font_renderer::vert(device);
                vk::UniqueShaderModule geometryShader = shaders::font_renderer::geom(device);
                vk::UniqueShaderModule fragmentShader = shaders::font_renderer::frag(device);
                std::array<vk::PipelineShaderStageCreateInfo, 3> shaders = {
                    vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, vertexShader.get(), "main"),
                    vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eGeometry, geometryShader.get(), "main"),
                    vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, fragmentShader.get(), "main")
                };

                vk::VertexInputBindingDescription binding(0, sizeof(VertexCharacter), vk::VertexInputRate::eVertex);
                std::array<vk::VertexInputAttributeDescription, 4> attributes = {
                    vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32Sfloat, offsetof(VertexCharacter, position)),
                    vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32Sfloat, offsetof(VertexCharacter, texCoord)),
                    vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32Sfloat, offsetof(VertexCharacter, size)),
                    vk::VertexInputAttributeDescription(3, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(VertexCharacter, color)),
                };

                vk::PipelineVertexInputStateCreateInfo vertex_input({}, binding, attributes);
                vk::PipelineInputAssemblyStateCreateInfo input_assembly({}, vk::PrimitiveTopology::ePointList);
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
                    vk::BufferCreateInfo vertex_info({}, maxTexts*maxCharacters*sizeof(VertexCharacter), vk::BufferUsageFlagBits::eVertexBuffer);
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
                    uniformPointers.push_back(aligned_wrapper<TextUniform>(mapping.get(), alignment));

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

            {
                VertexCharacter* vc = vertexPointers[frame] + vertexOffsets[frame];
                float x = 0;
                for(int i=0; i<text.size(); i++)
                {
                    char32_t c = text[i];
                    if(c == '\n') {
                        y += lineHeight*scale;
                        x = 0;
                    } else if(!glyphRects.contains(c)) {
                        c = '?';
                    }

                    vk::Rect2D g = glyphRects[c];
                    vc[i] = {
                        .position = {x, 0},
                        .texCoord = {g.offset.x/lineHeight, g.offset.y/lineHeight},
                        .size = {((float)g.extent.width)/lineHeight, ((float)g.extent.height)/lineHeight},
                        .color = color};
                    x+= ((float)g.extent.width)/lineHeight;
                }
            }
            {
                glm::vec2 pos = glm::vec2(x, y)*2.0f - glm::vec2(1.0f);

                TextUniform& uni = uniformPointers[frame][uniformOffsets[frame]];
                uni.matrix = glm::mat4(1.0f);
                uni.matrix = glm::translate(uni.matrix, glm::vec3(pos, 0.0f));
                uni.matrix = glm::scale(uni.matrix, glm::vec3(scale / aspectRatio, scale, 1.0f));
                uni.textureSize = {fontTexture->width/lineHeight, fontTexture->height/lineHeight};
            }
            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines[renderPass].get());
            cmd.bindVertexBuffers(0, vertexBuffers[frame].get(), vertexOffsets[frame]*sizeof(VertexCharacter));
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout.get(), 0, descriptorSets[frame], uniformPointers[frame].offset(uniformOffsets[frame]));
            cmd.draw(text.size(), 1, 0, 0);

            vertexOffsets[frame] += text.size();
            uniformOffsets[frame]++;
        }
        glm::vec2 measureText(std::string_view text, float scale = 1.0f) {
            float width = 0;
            for(int i=0; i<text.size(); i++)
            {
                vk::Rect2D g = glyphRects[text[i]];
                width += static_cast<float>(g.extent.width)/lineHeight;
            }
            return glm::vec2{width*scale/aspectRatio, scale}/2.0f;
        }

    private:
        vk::Device device;
        vma::Allocator allocator;
        std::string fontName;
        int fontSize;

        std::map<char32_t, vk::Rect2D> glyphRects;
        size_t maxCharacters;
        size_t maxTexts;
        float lineHeight;

        struct VertexCharacter {
            glm::vec2 position;
            glm::vec2 texCoord;
            glm::vec2 size;
            glm::vec4 color;
        };
        struct TextUniform {
            glm::mat4 matrix;
            glm::vec2 textureSize;
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
