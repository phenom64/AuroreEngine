/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;

#include <vector>
#include <array>
#include <cstdint>

export module dreamrender:components.image_renderer;

import :shaders;
import :texture;
import :utils;

import glm;
import vulkan_hpp;

namespace dreamrender {

struct push_constants {
    glm::mat4 matrix;
    glm::vec4 color;
    unsigned int index;
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
        }

        void prepare(int frameCount) {
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

        void setGlassBackground(vk::ImageView view) {
            glassBackgroundView = view;
        }

        void setGlassMaterial(vk::ImageView ambientView, vk::ImageView environmentView) {
            glassAmbientView = ambientView;
            glassEnvironmentView = environmentView;
        }

        // Glass icon variant: imported xmb-web normal map + live background.
        void renderImageGlass(vk::CommandBuffer cmd, int frame, vk::RenderPass renderPass, vk::ImageView iconView,
                              float x, float y, float scaleX, float scaleY, glm::vec4 color = glm::vec4(1.0,1.0,1.0,1.0))
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
            push.index = 0;
            push.color = color;
            cmd.pushConstants<push_constants>(pipelineLayoutGlass.get(), vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, push);
            cmd.draw(4, 1, 0, 0);
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

            cmd.pushConstants<push_constants>(pipelineLayout.get(),
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, push);
            cmd.draw(4, 1, 0, 0);
        }
        void renderImage(vk::CommandBuffer cmd, int frame, vk::RenderPass renderPass, const texture& texture, float x, float y, float scaleX = 1.0, float scaleY = 1.0, glm::vec4 color = glm::vec4(1.0, 1.0, 1.0, 1.0)) {
            if(!texture.loaded) return;
            renderImage(cmd, frame, renderPass, texture.imageView.get(), x, y, scaleX, scaleY, color);
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

            cmd.pushConstants<push_constants>(pipelineLayout.get(),
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, push);
            cmd.draw(4, 1, 0, 0);
        }
        void renderImageSized(vk::CommandBuffer cmd, int frame, vk::RenderPass renderPass, const texture& texture, float x, float y, int width = -1, int height = -1, glm::vec4 color = glm::vec4(1.0, 1.0, 1.0, 1.0)) {
            if(!texture.loaded) return;
            renderImageSized(cmd, frame, renderPass, texture.imageView.get(), x, y, width == -1 ? texture.width : width, height == -1 ? texture.height : height, color);
        }
    private:
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
