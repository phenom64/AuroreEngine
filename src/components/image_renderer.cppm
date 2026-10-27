/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;

#include <vector>
#include <array>
#include <cstdint>
#include <span>
#include <atomic>
#include <bit>
#include <memory>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>

export module auroreengine:components.image_renderer;

import :shaders;
import :texture;
import :utils;

import glm;
import vulkan_hpp;
import vma;

namespace auroreengine {

struct push_constants {
    glm::mat4 matrix;
    glm::vec4 color;
    unsigned int index;
    unsigned int encoding;
};

export class image_renderer {
    private:
        static constexpr bool check_features(const gpu_features& features) {
            if(!features.vulkan12Features.descriptorBindingPartiallyBound)
                return false;
            if(!features.vulkan12Features.descriptorBindingSampledImageUpdateAfterBind)
                return false;
            return true;
        }
    public:
        // Positions use normalized screen x/y, Vulkan depth z, and clip-space w.
        // UVs and colours are perspective-correctly interpolated by the GPU.
        struct textured_vertex {
            glm::vec4 position;
            glm::vec2 uv;
            glm::vec4 color{1.0f};
        };
        struct geometry_statistics {
            std::uint64_t draw_calls{},triangles{},bytes_uploaded{};
            std::size_t resident_vertex_capacity{};
        };
        // Supply before drawing. Existing allocatorless clients retain the
        // small, push-constant triangle fallback; full scenes use retained VMA
        // vertex chunks and one draw per mesh in the usual single-chunk case.
        void setAllocator(vma::Allocator allocator) {
            if(allocator==meshAllocator) return;
            for(const auto& arena:meshArenas) if(!arena.chunks.empty())
                throw std::logic_error("Set the image renderer allocator before preparing mesh resources");
            meshAllocator=allocator;
        }
        [[nodiscard]] geometry_statistics geometryStatistics(int frame) const {
            if(frame<0 || static_cast<std::size_t>(frame)>=completedMeshStatistics.size()) return {};
            return completedMeshStatistics[frame];
        }
        constexpr static unsigned int default_max_images = 512;

        image_renderer(vk::Device device, vk::Extent2D frameSize, const gpu_features& features) : device(device), frameSize(frameSize),
            aspectRatio(static_cast<double>(frameSize.width)/frameSize.height), features(features),
            compat_mode(!check_features(features)) {}
        ~image_renderer() = default;

        void preload(const std::vector<vk::RenderPass>& renderPasses, vk::SampleCountFlagBits sampleCount,
            vk::PipelineCache pipelineCache = {}, unsigned int max_images = default_max_images)
        {
            this->max_images = max_images;
            if(compat_mode) {
                spdlog::warn("Image Renderer: No update-after-bind support, falling back to compatibility mode");
            } else if(max_images > features.limits.maxPerStageDescriptorSamplers) {
                spdlog::warn("Image Renderer: max images ({}) exceeds maxPerStageDescriptorSamplers ({}); using update-after-bind descriptor indexing",
                    max_images, features.limits.maxPerStageDescriptorSamplers);
            }

            {
                vk::SamplerCreateInfo sampler_info({}, vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
                    vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
                    0.0f, false, 0.0f, false, vk::CompareOp::eNever, 0.0f, 0.0f, vk::BorderColor::eFloatTransparentBlack, false);
                sampler = device.createSamplerUnique(sampler_info);
            }
            {
                std::array<vk::DescriptorSetLayoutBinding, 1> bindings = {
                    vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eCombinedImageSampler, compat_mode ? 1 : this->max_images, vk::ShaderStageFlagBits::eFragment)
                };
                vk::DescriptorBindingFlags flags = vk::DescriptorBindingFlagBits::eUpdateAfterBind | vk::DescriptorBindingFlagBits::ePartiallyBound;
                if(compat_mode)
                    flags = vk::DescriptorBindingFlags{};
                vk::DescriptorSetLayoutBindingFlagsCreateInfo bindingInfo(flags);
                vk::DescriptorSetLayoutCreateInfo layout_info(
                    compat_mode ? vk::DescriptorSetLayoutCreateFlags{} : vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool,
                    bindings, &bindingInfo);
                descriptorLayout = device.createDescriptorSetLayoutUnique(layout_info);
                debugName(device, descriptorLayout.get(), "Image Renderer Descriptor Layout");
            }
            {
                std::array<vk::PushConstantRange, 1> push_constant_ranges = {
                    vk::PushConstantRange(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, sizeof(push_constants)),
                };
                vk::PipelineLayoutCreateInfo layout_info({}, descriptorLayout.get(), push_constant_ranges);
                pipelineLayout = device.createPipelineLayoutUnique(layout_info);
                debugName(device, pipelineLayout.get(), "Image Renderer Pipeline Layout");
            }
            {
                vk::UniqueShaderModule vertexShader = shaders::image_renderer::vert(device);
                vk::UniqueShaderModule fragmentShader =
                    compat_mode ? shaders::image_renderer::frag_compat(device) :
                                  shaders::image_renderer::frag(device);
                std::array<vk::PipelineShaderStageCreateInfo, 2> shaders = {
                    vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, vertexShader.get(), "main"),
                    vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, fragmentShader.get(), "main")
                };

                vk::PipelineVertexInputStateCreateInfo vertex_input{};
                vk::PipelineInputAssemblyStateCreateInfo input_assembly({}, vk::PrimitiveTopology::eTriangleStrip);
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

                vk::GraphicsPipelineCreateInfo info({},
                    shaders, &vertex_input, &input_assembly, &tesselation, &viewport,
                    &rasterization, &multisample, &depthStencil, &colorBlend, &dynamic,
                    pipelineLayout.get(), renderPasses[0], 0, {}, {});
                pipelines = createPipelines(device, pipelineCache, info, renderPasses, "Image Renderer Pipeline");

                // Glass pipeline: normal, ambient ramp, environment matcap,
                // and live background samplers, matching xmb-web's icon pass.
                std::array<vk::DescriptorSetLayoutBinding,4> glassBindings = {
                    vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
                    vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
                    vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
                    vk::DescriptorSetLayoutBinding(3, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment)
                };
                glassDescriptorLayout = device.createDescriptorSetLayoutUnique(
                    vk::DescriptorSetLayoutCreateInfo({}, glassBindings));

                auto glassPush = std::array{ vk::PushConstantRange(vk::ShaderStageFlagBits::eVertex|vk::ShaderStageFlagBits::eFragment, 0, sizeof(push_constants)) };
                pipelineLayoutGlass = device.createPipelineLayoutUnique(
                    vk::PipelineLayoutCreateInfo({}, glassDescriptorLayout.get(), glassPush));
                debugName(device, pipelineLayoutGlass.get(), "Image Renderer Glass Pipeline Layout");

                vk::UniqueShaderModule glassFrag = shaders::image_renderer::frag_glass(device);
                std::array<vk::PipelineShaderStageCreateInfo, 2> glassStages = {
                    vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, vertexShader.get(), "main"),
                    vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, glassFrag.get(), "main")
                };
                // The glass fragment shader outputs premultiplied colour,
                // matching xmb-web's icon canvas blendFunc(ONE,
                // ONE_MINUS_SRC_ALPHA).  Keep the normal image pipeline
                // straight-alpha; only the glass variant uses premultiplied
                // blending so translucent icons reveal the live background.
                vk::PipelineColorBlendAttachmentState glassAttachment(
                    true, vk::BlendFactor::eOne,
                    vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd,
                    vk::BlendFactor::eOne,
                    vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd,
                    vk::ColorComponentFlagBits::eR |
                        vk::ColorComponentFlagBits::eG |
                        vk::ColorComponentFlagBits::eB |
                        vk::ColorComponentFlagBits::eA);
                vk::PipelineColorBlendStateCreateInfo glassColorBlend(
                    {}, false, vk::LogicOp::eClear, glassAttachment);

                vk::GraphicsPipelineCreateInfo ginfo({},
                    glassStages, &vertex_input, &input_assembly, &tesselation, &viewport,
                    &rasterization, &multisample, &depthStencil, &glassColorBlend, &dynamic,
                    pipelineLayoutGlass.get(), renderPasses[0], 0, {}, {});
                pipelinesGlass = createPipelines(device, pipelineCache, ginfo, renderPasses, "Image Renderer Glass Pipeline");
            }
            buildTrianglePipelines(renderPasses,sampleCount,pipelineCache);
        }

        void prepare(int frameCount) {
            meshArenas.clear();meshArenas.resize(frameCount);
            completedMeshStatistics.assign(frameCount,{});
            descriptorSets.clear();
            descriptorSetsCompat.clear();
            descriptorSetIndicesCompat.clear();
            imageInfos.clear();
            glassDescriptorSets.clear();
            glassDescriptorSetIndices.clear();

            std::array<vk::DescriptorPoolSize, 1> sizes = {
                vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 1*frameCount*max_images)
            };
            vk::DescriptorPoolCreateInfo pool_info(compat_mode ? vk::DescriptorPoolCreateFlags{} : vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind,
                compat_mode ? frameCount*max_images : frameCount, sizes);
            descriptorPool = device.createDescriptorPoolUnique(pool_info);

            if(compat_mode) {
                std::vector<vk::DescriptorSetLayout> layouts(max_images, descriptorLayout.get());
                for(int i = 0; i < frameCount; i++) {
                    vk::DescriptorSetAllocateInfo set_info(descriptorPool.get(), layouts);
                    descriptorSetsCompat.push_back(device.allocateDescriptorSets(set_info));
                    descriptorSetIndicesCompat.push_back(0);
                }
            } else {
                std::vector<vk::DescriptorSetLayout> layouts(frameCount, descriptorLayout.get());
                vk::DescriptorSetAllocateInfo set_info(descriptorPool.get(), layouts);
                descriptorSets = device.allocateDescriptorSets(set_info);
                imageInfos.resize(frameCount);
                for(auto& frameImages:imageInfos) frameImages.reserve(max_images);
            }

            // Allocate per-frame glass descriptor sets if glass pipeline is present
            if(glassDescriptorLayout && pipelineLayoutGlass) {
                const auto descriptorCount = static_cast<uint32_t>(frameCount) * max_images;
                std::array<vk::DescriptorPoolSize,1> sizes = {
                    vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, descriptorCount * 4)
                };
                glassDescriptorPool = device.createDescriptorPoolUnique(
                    vk::DescriptorPoolCreateInfo({}, descriptorCount, sizes));
                std::vector<vk::DescriptorSetLayout> glLayouts(max_images, glassDescriptorLayout.get());
                for(int i = 0; i < frameCount; ++i) {
                    glassDescriptorSets.push_back(device.allocateDescriptorSets(
                        vk::DescriptorSetAllocateInfo(glassDescriptorPool.get(), glLayouts)));
                    glassDescriptorSetIndices.push_back(0);
                }
            }
        }

        void finish(int frame) {
            if(frame < 0) {
                return;
            }
            const auto frameIndex = static_cast<std::size_t>(frame);
            if(frameIndex<meshArenas.size()) {
                auto& arena=meshArenas[frameIndex];
                arena.statistics.resident_vertex_capacity=arena.allocated;
                completedMeshStatistics[frameIndex]=arena.statistics;
                arena.statistics={};arena.budget_reported=false;
                for(auto& chunk:arena.chunks) chunk->used=0;
            }
            if(frameIndex < glassDescriptorSetIndices.size()) {
                glassDescriptorSetIndices[frameIndex] = 0;
            }
            if(compat_mode) {
                if(frameIndex >= descriptorSetIndicesCompat.size()) {
                    return;
                }
                descriptorSetIndicesCompat[frameIndex] = 0;
                return;
            }

            if(frameIndex >= imageInfos.size()) {
                return;
            }
            if(imageInfos[frameIndex].empty())
                return;
            vk::WriteDescriptorSet write(
                descriptorSets[frameIndex], 0, 0,
                imageInfos[frameIndex].size(), vk::DescriptorType::eCombinedImageSampler, imageInfos[frameIndex].data());
            device.updateDescriptorSets(write, {});
            imageInfos[frameIndex].clear();
        }

        void setOutputEncoded(bool encoded) noexcept { outputEncoding=encoded?1u:0u; }
        void setGlassBackground(vk::ImageView view) {
            glassBackgroundView = view;
        }

        void setGlassMaterial(vk::ImageView ambientView, vk::ImageView environmentView) {
            glassAmbientView = ambientView;
            glassEnvironmentView = environmentView;
        }

        // Glass icon variant: imported xmb-web normal map + live background.
        void renderImageGlass(vk::CommandBuffer cmd, int frame, vk::RenderPass renderPass, vk::ImageView iconView,
                              float x, float y, float scaleX, float scaleY, glm::vec4 color = glm::vec4(1.0,1.0,1.0,1.0),unsigned mode=0)
        {
            if(!iconView) return;
            if(frame < 0) return;
            const auto frameIndex = static_cast<std::size_t>(frame);
            if(frameIndex >= glassDescriptorSets.size() || frameIndex >= glassDescriptorSetIndices.size()) return;
            auto& frameSets = glassDescriptorSets[frameIndex];
            auto& nextSet = glassDescriptorSetIndices[frameIndex];
            if(nextSet >= frameSets.size()) return;
            const auto descriptorSet = frameSets[nextSet++];
            const auto ambientView = glassAmbientView ? glassAmbientView : iconView;
            const auto environmentView = glassEnvironmentView ? glassEnvironmentView : iconView;
            const auto backgroundView = glassBackgroundView ? glassBackgroundView : iconView;
            std::array<vk::DescriptorImageInfo, 4> images = {
                vk::DescriptorImageInfo(sampler.get(), iconView, vk::ImageLayout::eShaderReadOnlyOptimal),
                vk::DescriptorImageInfo(sampler.get(), ambientView, vk::ImageLayout::eShaderReadOnlyOptimal),
                vk::DescriptorImageInfo(sampler.get(), environmentView, vk::ImageLayout::eShaderReadOnlyOptimal),
                vk::DescriptorImageInfo(sampler.get(), backgroundView, vk::ImageLayout::eShaderReadOnlyOptimal)
            };
            std::array<vk::WriteDescriptorSet, 4> writes = {
                vk::WriteDescriptorSet(descriptorSet, 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &images[0]),
                vk::WriteDescriptorSet(descriptorSet, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &images[1]),
                vk::WriteDescriptorSet(descriptorSet, 2, 0, 1, vk::DescriptorType::eCombinedImageSampler, &images[2]),
                vk::WriteDescriptorSet(descriptorSet, 3, 0, 1, vk::DescriptorType::eCombinedImageSampler, &images[3])
            };
            device.updateDescriptorSets(writes, {});

            const auto pipeline = pipelinesGlass.find(renderPass);
            if(pipeline == pipelinesGlass.end() || !pipeline->second) return;
            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline->second.get());
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayoutGlass.get(), 0, descriptorSet, {});

            glm::vec2 pos = glm::vec2(x, y)*2.0f - glm::vec2(1.0f);
            push_constants push{};
            push.matrix = glm::mat4(1.0f);
            push.matrix = glm::translate(push.matrix, glm::vec3(pos, 0.0f));
            push.matrix = glm::scale(push.matrix, glm::vec3(scaleX / aspectRatio, scaleY, 1.0f));
            push.index = mode;
            push.color = color;
            push.encoding=outputEncoding;
            cmd.pushConstants<push_constants>(pipelineLayoutGlass.get(), vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, push);
            cmd.draw(4, 1, 0, 0);
        }

        void renderGlassFrame(vk::CommandBuffer cmd,int frame,vk::RenderPass pass,float x,float y,float width,float height,float opacity) {
            if(!glassBackgroundView) return;
            renderImageGlass(cmd,frame,pass,glassBackgroundView,x,y,width,height,{1,1,1,opacity},1);
        }
        void renderImage(vk::CommandBuffer cmd, int frame, vk::RenderPass renderPass, vk::ImageView view, float x, float y, float scaleX, float scaleY, glm::vec4 color = glm::vec4(1.0, 1.0, 1.0, 1.0)) {
            if(!view)
                return;
            vk::DescriptorImageInfo image_info(sampler.get(), view, vk::ImageLayout::eShaderReadOnlyOptimal);

            unsigned int index{};
            vk::DescriptorSet descriptorSet;
            if(compat_mode) {
                if(frame < 0 || static_cast<std::size_t>(frame) >= descriptorSetsCompat.size()) {
                    return;
                }
                auto& sets = descriptorSetsCompat[frame];
                auto& set_index = descriptorSetIndicesCompat[frame];
                if(static_cast<std::size_t>(set_index) >= sets.size()) {
                    return;
                }
                index = 0;
                descriptorSet = sets[set_index++];
                vk::WriteDescriptorSet write(
                    descriptorSet, 0, 0,
                    1, vk::DescriptorType::eCombinedImageSampler, &image_info);
                device.updateDescriptorSets(write, {});
            } else {
                if(frame < 0 || static_cast<std::size_t>(frame) >= imageInfos.size() || imageInfos[frame].size() >= max_images) {
                    return;
                }
                index = imageInfos[frame].size();
                descriptorSet = descriptorSets[frame];
                imageInfos[frame].push_back(image_info);
            }

            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines[renderPass].get());
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout.get(), 0, descriptorSet, {});

            glm::vec2 pos = glm::vec2(x, y)*2.0f - glm::vec2(1.0f);

            push_constants push{};
            push.matrix = glm::mat4(1.0f);
            push.matrix = glm::translate(push.matrix, glm::vec3(pos, 0.0f));
            push.matrix = glm::scale(push.matrix, glm::vec3(scaleX / aspectRatio, scaleY, 1.0f));
            push.index = index;
            push.color = color;
            push.encoding=outputEncoding;

            cmd.pushConstants<push_constants>(pipelineLayout.get(),
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, push);
            cmd.draw(4, 1, 0, 0);
        }
        void renderImage(vk::CommandBuffer cmd, int frame, vk::RenderPass renderPass, const texture& texture, float x, float y, float scaleX = 1.0, float scaleY = 1.0, glm::vec4 color = glm::vec4(1.0, 1.0, 1.0, 1.0)) {
            if(!texture.loaded) return;
            renderImage(cmd, frame, renderPass, texture.imageView.get(), x, y, scaleX, scaleY, color);
        }

        // One descriptor is reserved for the entire mesh. Small triangle push
        // constants avoid transient vertex allocations and preserve perspective
        // interpolation without requiring a new allocator in this renderer API.
        void renderTexturedTriangles(vk::CommandBuffer cmd,int frame,vk::RenderPass renderPass,
            vk::ImageView view,std::span<const textured_vertex> vertices,
            glm::vec4 tint=glm::vec4(1),bool additive=false,bool depth=false) {
            if(!view || vertices.empty() || frame<0) return;
            if(vertices.size()%3!=0) throw std::invalid_argument("Textured triangle vertex count must be divisible by three");
            const auto pipelineIndex=(depth?2u:0u)+(additive?1u:0u);
            const auto frameIndex=static_cast<std::size_t>(frame);
            const bool batched=static_cast<bool>(meshAllocator) && frameIndex<meshArenas.size();
            const auto& selectedPipelines=batched?batchedTrianglePipelines[pipelineIndex]:trianglePipelines[pipelineIndex];
            const auto pipeline=selectedPipelines.find(renderPass);
            if(pipeline==selectedPipelines.end() || !pipeline->second) return;
            vk::DescriptorSet descriptorSet;
            unsigned int imageIndex{};
            vk::DescriptorImageInfo info(sampler.get(),view,vk::ImageLayout::eShaderReadOnlyOptimal);
            if(compat_mode) {
                if(frameIndex>=descriptorSetsCompat.size()) return;
                auto& next=descriptorSetIndicesCompat[frameIndex];
                if(next>=descriptorSetsCompat[frameIndex].size()) return;
                descriptorSet=descriptorSetsCompat[frameIndex][next++];
                device.updateDescriptorSets(vk::WriteDescriptorSet(descriptorSet,0,0,1,vk::DescriptorType::eCombinedImageSampler,&info),{});
            } else {
                if(frameIndex>=imageInfos.size() || imageInfos[frameIndex].size()>=max_images) return;
                imageIndex=static_cast<unsigned int>(imageInfos[frameIndex].size());
                imageInfos[frameIndex].push_back(info);descriptorSet=descriptorSets[frameIndex];
            }
            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics,pipeline->second.get());
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,trianglePipelineLayout.get(),0,descriptorSet,{});
            TrianglePush push{};push.index=imageIndex;push.encoding=outputEncoding;
            if(batched) {
                // The batch vertex shader reads the arena, while the shared
                // fragment shader needs only the texture index at offset120.
                cmd.pushConstants(trianglePipelineLayout.get(),vk::ShaderStageFlagBits::eVertex|vk::ShaderStageFlagBits::eFragment,0,sizeof(push),&push);
                renderMeshBatches(cmd,frame,vertices,tint);
                return;
            }
            for(std::size_t first=0;first<vertices.size();first+=3) {
                bool valid=true;
                for(std::size_t i=0;i<3;++i) {
                    const auto& vertex=vertices[first+i];
                    for(int component=0;component<4;++component) valid=valid && std::isfinite(vertex.position[component]) && std::isfinite(vertex.color[component]);
                    valid=valid && vertex.position.w>0 && std::isfinite(vertex.uv.x) && std::isfinite(vertex.uv.y);
                    push.positions[i]=vertex.position;push.colors[i]=vertex.color*tint;push.uvs[i]=vertex.uv;
                }
                if(!valid) continue;
                cmd.pushConstants(trianglePipelineLayout.get(),vk::ShaderStageFlagBits::eVertex|vk::ShaderStageFlagBits::eFragment,0,sizeof(push),&push);
                cmd.draw(3,1,0,0);
                if(frameIndex<meshArenas.size()) { ++meshArenas[frameIndex].statistics.draw_calls; ++meshArenas[frameIndex].statistics.triangles; }
            }
        }
        void renderTexturedTriangles(vk::CommandBuffer cmd,int frame,vk::RenderPass pass,
            const texture& image,std::span<const textured_vertex> vertices,
            glm::vec4 tint=glm::vec4(1),bool additive=false,bool depth=false) {
            if(!image.loaded.load(std::memory_order_acquire)) return;
            renderTexturedTriangles(cmd,frame,pass,image.imageView.get(),vertices,tint,additive,depth);
        }

        void renderImageSized(vk::CommandBuffer cmd, int frame, vk::RenderPass renderPass, vk::ImageView view, float x, float y, int width, int height, glm::vec4 color = glm::vec4(1.0, 1.0, 1.0, 1.0)) {
            if(!view)
                return;
            vk::DescriptorImageInfo image_info(sampler.get(), view, vk::ImageLayout::eShaderReadOnlyOptimal);

            unsigned int index{};
            vk::DescriptorSet descriptorSet;
            if(compat_mode) {
                if(frame < 0 || static_cast<std::size_t>(frame) >= descriptorSetsCompat.size()) {
                    return;
                }
                auto& sets = descriptorSetsCompat[frame];
                auto& set_index = descriptorSetIndicesCompat[frame];
                if(static_cast<std::size_t>(set_index) >= sets.size()) {
                    return;
                }
                index = 0;
                descriptorSet = sets[set_index++];
                vk::WriteDescriptorSet write(
                    descriptorSet, 0, 0,
                    1, vk::DescriptorType::eCombinedImageSampler, &image_info);
                device.updateDescriptorSets(write, {});
            } else {
                if(frame < 0 || static_cast<std::size_t>(frame) >= imageInfos.size() || imageInfos[frame].size() >= max_images) {
                    return;
                }
                index = imageInfos[frame].size();
                descriptorSet = descriptorSets[frame];
                imageInfos[frame].push_back(image_info);
            }

            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines[renderPass].get());
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout.get(), 0, descriptorSet, {});

            double scaleX = static_cast<double>(width) / frameSize.width;
            double scaleY = static_cast<double>(height) / frameSize.height;

            glm::vec2 pos = glm::vec2(x, y)*2.0f - glm::vec2(1.0f);

            push_constants push{};
            push.matrix = glm::mat4(1.0f);
            push.matrix = glm::translate(push.matrix, glm::vec3(pos, 0.0f));
            push.matrix = glm::scale(push.matrix, glm::vec3(scaleX, scaleY, 1.0f));
            push.index = index;
            push.color = color;
            push.encoding=outputEncoding;

            cmd.pushConstants<push_constants>(pipelineLayout.get(),
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, push);
            cmd.draw(4, 1, 0, 0);
        }
        void renderImageSized(vk::CommandBuffer cmd, int frame, vk::RenderPass renderPass, const texture& texture, float x, float y, int width = -1, int height = -1, glm::vec4 color = glm::vec4(1.0, 1.0, 1.0, 1.0)) {
            if(!texture.loaded) return;
            renderImageSized(cmd, frame, renderPass, texture.imageView.get(), x, y, width == -1 ? texture.width : width, height == -1 ? texture.height : height, color);
        }
    private:
        unsigned outputEncoding{};
        struct TrianglePush {
            std::array<glm::vec4,3> positions;
            std::array<glm::vec4,3> colors;
            std::array<glm::vec2,3> uvs;
            std::uint32_t index;
            std::uint32_t encoding;
        };
        static_assert(offsetof(TrianglePush,colors)==48);
        static_assert(offsetof(TrianglePush,uvs)==96);
        static_assert(offsetof(TrianglePush,index)==120);
        static_assert(sizeof(TrianglePush)<=128);

        void buildTrianglePipelines(const std::vector<vk::RenderPass>& passes,
            vk::SampleCountFlagBits samples,vk::PipelineCache cache) {
            if(features.limits.maxPushConstantsSize<sizeof(TrianglePush))
                throw std::runtime_error("Textured triangles require 124 bytes of push constants");
            const auto range=vk::PushConstantRange(vk::ShaderStageFlagBits::eVertex|vk::ShaderStageFlagBits::eFragment,0,sizeof(TrianglePush));
            trianglePipelineLayout=device.createPipelineLayoutUnique(vk::PipelineLayoutCreateInfo({},descriptorLayout.get(),range));
            auto vertex=shaders::image_renderer::triangles_vert(device);
            auto batchVertex=shaders::image_renderer::triangles_batch_vert(device);
            auto fragment=compat_mode?shaders::image_renderer::triangles_frag_compat(device):shaders::image_renderer::triangles_frag(device);
            const std::array stages{
                vk::PipelineShaderStageCreateInfo({},vk::ShaderStageFlagBits::eVertex,vertex.get(),"main"),
                vk::PipelineShaderStageCreateInfo({},vk::ShaderStageFlagBits::eFragment,fragment.get(),"main")};
            const vk::PipelineVertexInputStateCreateInfo input{};
            const vk::VertexInputBindingDescription binding(0,sizeof(textured_vertex),vk::VertexInputRate::eVertex);
            const std::array attributes{
                vk::VertexInputAttributeDescription(0,0,vk::Format::eR32G32B32A32Sfloat,offsetof(textured_vertex,position)),
                vk::VertexInputAttributeDescription(1,0,vk::Format::eR32G32Sfloat,offsetof(textured_vertex,uv)),
                vk::VertexInputAttributeDescription(2,0,vk::Format::eR32G32B32A32Sfloat,offsetof(textured_vertex,color))};
            const vk::PipelineVertexInputStateCreateInfo batchInput({},binding,attributes);
            const std::array batchStages{
                vk::PipelineShaderStageCreateInfo({},vk::ShaderStageFlagBits::eVertex,batchVertex.get(),"main"),
                vk::PipelineShaderStageCreateInfo({},vk::ShaderStageFlagBits::eFragment,fragment.get(),"main")};
            const vk::PipelineInputAssemblyStateCreateInfo assembly({},vk::PrimitiveTopology::eTriangleList);
            const vk::Viewport viewportState{};
            const vk::Rect2D scissorState{};
            const vk::PipelineViewportStateCreateInfo viewport({},viewportState,scissorState);
            const vk::PipelineRasterizationStateCreateInfo raster({},false,false,vk::PolygonMode::eFill,vk::CullModeFlagBits::eNone,vk::FrontFace::eCounterClockwise,false,0,0,0,1);
            const vk::PipelineMultisampleStateCreateInfo multisample({},samples);
            const std::array dynamicStates{vk::DynamicState::eViewport,vk::DynamicState::eScissor};
            const vk::PipelineDynamicStateCreateInfo dynamic({},dynamicStates);
            for(unsigned int variant=0;variant<4;++variant) {
                const bool additive=(variant&1)!=0,depth=(variant&2)!=0;
                const vk::PipelineDepthStencilStateCreateInfo depthState({},depth,depth,vk::CompareOp::eLessOrEqual);
                const vk::PipelineColorBlendAttachmentState blend(true,vk::BlendFactor::eSrcAlpha,
                    additive?vk::BlendFactor::eOne:vk::BlendFactor::eOneMinusSrcAlpha,vk::BlendOp::eAdd,
                    vk::BlendFactor::eOne,vk::BlendFactor::eOneMinusSrcAlpha,vk::BlendOp::eAdd,
                    vk::ColorComponentFlagBits::eR|vk::ColorComponentFlagBits::eG|vk::ColorComponentFlagBits::eB|vk::ColorComponentFlagBits::eA);
                const vk::PipelineColorBlendStateCreateInfo color({},false,vk::LogicOp::eClear,blend);
                const vk::GraphicsPipelineCreateInfo pipeline({},stages,&input,&assembly,nullptr,&viewport,&raster,&multisample,&depthState,&color,&dynamic,trianglePipelineLayout.get(),passes.at(0));
                trianglePipelines[variant]=createPipelines(device,cache,pipeline,passes,"Textured triangle pipeline");
                auto batchPipeline=pipeline;batchPipeline.setStages(batchStages).setPVertexInputState(&batchInput);
                batchedTrianglePipelines[variant]=createPipelines(device,cache,batchPipeline,passes,"Batched textured triangle pipeline");
            }
        }
        struct MeshChunk {
            // Destruction order: unmap, destroy buffer, then free allocation.
            vma::UniqueAllocation allocation;
            vma::UniqueBuffer buffer;
            std::unique_ptr<vma::MemoryMapping> mapping;
            textured_vertex* vertices{};
            std::size_t capacity{},used{};
        };
        struct MeshArena {
            std::vector<std::unique_ptr<MeshChunk>> chunks;
            std::size_t allocated{};
            geometry_statistics statistics;
            bool budget_reported{};
        };
        static constexpr std::size_t maximum_mesh_vertices=262144,minimum_mesh_chunk=4096;
        vma::Allocator meshAllocator{};
        std::vector<MeshArena> meshArenas;
        std::vector<geometry_statistics> completedMeshStatistics;
        std::array<UniquePipelineMap,4> batchedTrianglePipelines;
        bool validTriangle(std::span<const textured_vertex> vertices) const {
            for(const auto& vertex:vertices) {
                if(vertex.position.w<=0 || !std::isfinite(vertex.uv.x) || !std::isfinite(vertex.uv.y)) return false;
                for(int component=0;component<4;++component)
                    if(!std::isfinite(vertex.position[component]) || !std::isfinite(vertex.color[component])) return false;
            }
            return true;
        }
        void renderMeshBatches(vk::CommandBuffer cmd,int frame,std::span<const textured_vertex> vertices,glm::vec4 tint) {
            auto& arena=meshArenas[frame];
            for(std::size_t first=0;first<vertices.size();) {
                MeshChunk* chunk=nullptr;
                for(auto& existing:arena.chunks) if(existing->capacity-existing->used>=3) { chunk=existing.get();break; }
                if(!chunk && arena.allocated+3<=maximum_mesh_vertices) {
                    const auto available=maximum_mesh_vertices-arena.allocated;
                    const auto requested=std::max(minimum_mesh_chunk,std::min(vertices.size()-first,maximum_mesh_vertices));
                    const auto capacity=std::min(available,std::bit_ceil(requested));
                    auto allocated=std::make_unique<MeshChunk>();allocated->capacity=capacity;
                    const vk::BufferCreateInfo info({},capacity*sizeof(textured_vertex),vk::BufferUsageFlagBits::eVertexBuffer,vk::SharingMode::eExclusive);
                    const vma::AllocationCreateInfo memory({},vma::MemoryUsage::eCpuToGpu);
                    auto [buffer,allocation]=meshAllocator.createBufferUnique(info,memory);
                    allocated->allocation=std::move(allocation);allocated->buffer=std::move(buffer);
                    allocated->mapping=std::make_unique<vma::MemoryMapping>(meshAllocator,allocated->allocation.get());
                    allocated->vertices=static_cast<textured_vertex*>(allocated->mapping->get());
                    chunk=allocated.get();arena.allocated+=capacity;arena.chunks.push_back(std::move(allocated));
                }
                if(!chunk) {
                    if(!arena.budget_reported) { spdlog::warn("Textured mesh vertex budget ({}) exhausted",maximum_mesh_vertices);arena.budget_reported=true; }
                    return;
                }
                const auto maximum=(chunk->capacity-chunk->used)/3*3;
                std::size_t count=0;
                while(first<vertices.size() && count+3<=maximum) {
                    const auto triangle=vertices.subspan(first,3);first+=3;
                    if(!validTriangle(triangle)) continue;
                    for(const auto& vertex:triangle) { auto& output=chunk->vertices[chunk->used+count++];output=vertex;output.color*=tint; }
                }
                if(count==0) continue;
                meshAllocator.flushAllocation(chunk->allocation.get(),chunk->used*sizeof(textured_vertex),count*sizeof(textured_vertex));
                cmd.bindVertexBuffers(0,chunk->buffer.get(),vk::DeviceSize{0});
                cmd.draw(static_cast<std::uint32_t>(count),1,static_cast<std::uint32_t>(chunk->used),0);
                chunk->used+=count;
                ++arena.statistics.draw_calls;arena.statistics.triangles+=count/3;arena.statistics.bytes_uploaded+=count*sizeof(textured_vertex);
            }
        }
        vk::UniquePipelineLayout trianglePipelineLayout;
        std::array<UniquePipelineMap,4> trianglePipelines;

        vk::Device device;
        vk::Extent2D frameSize;
        double aspectRatio;

        const gpu_features& features;
        bool compat_mode{};

        unsigned int max_images;

        vk::UniqueSampler sampler;
        vk::UniqueDescriptorSetLayout descriptorLayout;
        vk::UniqueDescriptorPool descriptorPool;
        std::vector<vk::DescriptorSet> descriptorSets;
        vk::UniquePipelineLayout pipelineLayout;
        UniquePipelineMap pipelines;

        // Glass variant
        vk::UniqueDescriptorSetLayout glassDescriptorLayout;
        vk::UniquePipelineLayout pipelineLayoutGlass;
        UniquePipelineMap pipelinesGlass;
        vk::UniqueDescriptorPool glassDescriptorPool;
        std::vector<std::vector<vk::DescriptorSet>> glassDescriptorSets;
        std::vector<unsigned int> glassDescriptorSetIndices;
        vk::ImageView glassAmbientView{};
        vk::ImageView glassEnvironmentView{};
        vk::ImageView glassBackgroundView{};

        std::vector<
            std::vector<vk::DescriptorImageInfo>
        > imageInfos;

        std::vector<std::vector<vk::DescriptorSet>> descriptorSetsCompat;
        std::vector<unsigned int> descriptorSetIndicesCompat;
};

}
