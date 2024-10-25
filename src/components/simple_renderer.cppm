module;

#include <vector>

export module dreamrender:components.simple_renderer;

import :shaders;
import :texture;
import :utils;

import glm;
import vulkan_hpp;
import vma;

namespace dreamrender {

export class simple_renderer {
    public:
        struct vertex_data {
            glm::vec2 position;
            glm::vec4 color;
            glm::vec2 tex_coords;
        };
        struct params {
            std::array<glm::vec2, 4> blur;
        };

        simple_renderer(vk::Device device, vma::Allocator allocator, vk::Extent2D frameSize) :
            device(device), allocator(allocator), frameSize(frameSize),
            aspectRatio(static_cast<double>(frameSize.width)/frameSize.height) {}
        ~simple_renderer() = default;

        void preload(const std::vector<vk::RenderPass>& renderPasses, vk::SampleCountFlagBits sampleCount,
            vk::PipelineCache pipelineCache = {})
        {
            {
                std::array<vk::PushConstantRange, 1> push_constant_ranges = {
                    vk::PushConstantRange(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, sizeof(params)),
                };
                vk::PipelineLayoutCreateInfo layout_info({}, {}, push_constant_ranges);
                pipelineLayout = device.createPipelineLayoutUnique(layout_info);
                debugName(device, pipelineLayout.get(), "Simple Renderer Pipeline Layout");
            }
            {
                vk::UniqueShaderModule vertexShader = shaders::simple_renderer::vert(device);
                vk::UniqueShaderModule fragmentShader = shaders::simple_renderer::frag(device);
                std::array<vk::PipelineShaderStageCreateInfo, 2> shaders = {
                    vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, vertexShader.get(), "main"),
                    vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, fragmentShader.get(), "main")
                };

                vk::VertexInputBindingDescription binding(0, sizeof(vertex_data), vk::VertexInputRate::eVertex);
                std::array attributes = {
                    vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32Sfloat, offsetof(vertex_data, position)),
                    vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(vertex_data, color)),
                    vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32Sfloat, offsetof(vertex_data, tex_coords)),
                };
                vk::PipelineVertexInputStateCreateInfo vertex_input({}, binding, attributes);
                vk::PipelineInputAssemblyStateCreateInfo input_assembly({}, vk::PrimitiveTopology::eTriangleList);
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
                pipelines = createPipelines(device, pipelineCache, info, renderPasses, "Simple Renderer Pipeline");
            }
        }

        void prepare(int frameCount) {
            vertexBufferPointers.clear();
            vertexBufferMappings.clear();
            vertexBuffers.clear();
            vertexBufferAllocations.clear();
            vertexCounts.clear();
            for(int i = 0; i < frameCount; i++) {
                vk::BufferCreateInfo bufferInfo({}, sizeof(vertex_data)*vertexCount,
                    vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::SharingMode::eExclusive);
                vma::AllocationCreateInfo allocationInfo({}, vma::MemoryUsage::eCpuToGpu);
                auto [b, a] = allocator.createBufferUnique(bufferInfo, allocationInfo);
                auto& mapping = vertexBufferMappings.emplace_back(allocator, a.get());
                vertexBufferPointers.push_back(reinterpret_cast<vertex_data*>(mapping.get()));
                vertexBuffers.push_back(std::move(b));
                vertexBufferAllocations.push_back(std::move(a));
                vertexCounts.push_back(0);
            }
        }

        void renderGeneric(vk::CommandBuffer cmd, int frame, vk::RenderPass renderPass, std::ranges::range auto vertices, params p = {})
            requires(std::same_as<std::ranges::range_value_t<decltype(vertices)>, vertex_data>)
        {
            std::copy(vertices.begin(), vertices.end(), vertexBufferPointers[frame]+vertexCounts[frame]);

            cmd.bindVertexBuffers(0, vertexBuffers[frame].get(), {0});
            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines[renderPass].get());
            cmd.pushConstants(pipelineLayout.get(), vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, sizeof(params), &p);
            cmd.draw(vertices.size(), 1, vertexCounts[frame], 0);

            vertexCounts[frame] += vertices.size();
        }

        void renderQuad(vk::CommandBuffer cmd, int frame, vk::RenderPass renderPass, std::ranges::range auto vertices, params p = {})
            requires(std::same_as<std::ranges::range_value_t<decltype(vertices)>, vertex_data>)
        {
            std::array<vertex_data, 6> quad = {
                vertices[0], vertices[1], vertices[2],
                vertices[1], vertices[3], vertices[2],
            };
            renderGeneric(cmd, frame, renderPass, quad, p);
        }

        void renderRect(vk::CommandBuffer cmd, int frame, vk::RenderPass renderPass, glm::vec2 position, glm::vec2 size, glm::vec4 color, params p = {}) {
            std::array vertices = {
                vertex_data{position + glm::vec2(0.0f, 0.0f), color, glm::vec2(0.0f, 0.0f)},
                vertex_data{position + glm::vec2(size.x, 0.0f), color, glm::vec2(1.0f, 0.0f)},
                vertex_data{position + glm::vec2(size.x, size.y), color, glm::vec2(1.0f, 1.0f)},
                vertex_data{position + glm::vec2(0.0f, size.y), color, glm::vec2(0.0f, 1.0f)},
            };
            renderQuad(cmd, frame, renderPass, vertices, p);
        }

        void finish(int frame) {
            vertexCounts[frame] = 0;
        }
    private:
        constexpr static unsigned int vertexCount = 4096;

        vk::Device device;
        vma::Allocator allocator;
        vk::Extent2D frameSize;
        double aspectRatio;

        std::vector<vma::UniqueBuffer> vertexBuffers;
        std::vector<vma::UniqueAllocation> vertexBufferAllocations;
        std::vector<vma::MemoryMapping> vertexBufferMappings;
        std::vector<vertex_data*> vertexBufferPointers;
        std::vector<unsigned int> vertexCounts;

        vk::UniquePipelineLayout pipelineLayout;
        UniquePipelineMap pipelines;
};

}
