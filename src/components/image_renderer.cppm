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
                spdlog::warn("Image Renderer: Max images exceeds maxPerStageDescriptorSamplers, falling back to compatibility mode");
                compat_mode = true;
            }

            {
                vk::SamplerCreateInfo sampler_info({}, vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
                    vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
                    0.0f, false, 0.0f, false, vk::CompareOp::eNever, 0.0f, 0.0f, vk::BorderColor::eFloatTransparentBlack, false);
                sampler = device.createSamplerUnique(sampler_info);
            }
            {
                std::array<vk::DescriptorSetLayoutBinding, 1> bindings = {
                    vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eCombinedImageSampler, compat_mode ? 1 : max_images, vk::ShaderStageFlagBits::eFragment)
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

                // Glass pipeline: simple descriptor set with one sampler and alternate fragment shader
                std::array<vk::DescriptorSetLayoutBinding,1> glassBindings = {
                    vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment)
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
                vk::GraphicsPipelineCreateInfo ginfo({},
                    glassStages, &vertex_input, &input_assembly, &tesselation, &viewport,
                    &rasterization, &multisample, &depthStencil, &colorBlend, &dynamic,
                    pipelineLayoutGlass.get(), renderPasses[0], 0, {}, {});
                pipelinesGlass = createPipelines(device, pipelineCache, ginfo, renderPasses, "Image Renderer Glass Pipeline");
            }
        }

        void prepare(int frameCount) {
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
                std::array<vk::DescriptorPoolSize,1> sizes = {
                    vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, static_cast<uint32_t>(frameCount))
                };
                glassDescriptorPool = device.createDescriptorPoolUnique(vk::DescriptorPoolCreateInfo({}, frameCount, sizes));
                std::vector<vk::DescriptorSetLayout> glLayouts(frameCount, glassDescriptorLayout.get());
                glassDescriptorSets = device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo(glassDescriptorPool.get(), glLayouts));
            }
        }

        void finish(int frame) {
            if(compat_mode) {
                descriptorSetIndicesCompat[frame] = 0;
                return;
            }

            if(imageInfos[frame].empty())
                return;
            vk::WriteDescriptorSet write(
                descriptorSets[frame], 0, 0,
                imageInfos[frame].size(), vk::DescriptorType::eCombinedImageSampler, imageInfos[frame].data());
            device.updateDescriptorSets(write, {});
            imageInfos[frame].clear();
        }

        // Glass icon variant: single icon sampler + special fragment
        void renderImageGlass(vk::CommandBuffer cmd, int frame, vk::RenderPass renderPass, vk::ImageView iconView,
                              float x, float y, float scaleX, float scaleY, glm::vec4 color = glm::vec4(1.0,1.0,1.0,1.0))
        {
            if(!iconView) return;
            vk::DescriptorImageInfo img(sampler.get(), iconView, vk::ImageLayout::eShaderReadOnlyOptimal);
            vk::WriteDescriptorSet write(glassDescriptorSets[frame], 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &img);
            device.updateDescriptorSets(write, {});

            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelinesGlass[renderPass].get());
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayoutGlass.get(), 0, glassDescriptorSets[frame], {});

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
                index = 0;
                descriptorSet = descriptorSetsCompat[frame][descriptorSetIndicesCompat[frame]++];
                vk::WriteDescriptorSet write(
                    descriptorSet, 0, 0,
                    1, vk::DescriptorType::eCombinedImageSampler, &image_info);
                device.updateDescriptorSets(write, {});
            } else {
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
                index = 0;
                descriptorSet = descriptorSetsCompat[frame][descriptorSetIndicesCompat[frame]++];
                vk::WriteDescriptorSet write(
                    descriptorSet, 0, 0,
                    1, vk::DescriptorType::eCombinedImageSampler, &image_info);
                device.updateDescriptorSets(write, {});
            } else {
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
        std::vector<vk::DescriptorSet> glassDescriptorSets;

        std::vector<
            std::vector<vk::DescriptorImageInfo>
        > imageInfos;

        std::vector<std::vector<vk::DescriptorSet>> descriptorSetsCompat;
        std::vector<unsigned int> descriptorSetIndicesCompat;
};

}
