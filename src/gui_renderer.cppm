/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;

#include <cstdint>
#include <span>
#include <cmath>
#include <string_view>
#include <vector>
#include <algorithm>

export module auroreengine:gui_renderer;

import :components.font_renderer;
import :components.image_renderer;
import :components.simple_renderer;

import glm;
import vulkan_hpp;

namespace auroreengine {

export class gui_renderer {
    public:
        gui_renderer(vk::CommandBuffer commandBuffer, int frame, vk::RenderPass renderPass,
            vk::Extent2D frameSize,
            font_renderer* fontRenderer, image_renderer* imageRenderer, simple_renderer* simpleRenderer)
            :
            commandBuffer(commandBuffer), frame(frame), renderPass(renderPass),
            frame_size(frameSize), aspect_ratio(static_cast<double>(frameSize.width) / frameSize.height),
            font_renderer(fontRenderer), image_renderer(imageRenderer), simple_renderer(simpleRenderer)
        {}

        const vk::Extent2D frame_size;
        const double aspect_ratio;
        // Per-context theme typography. Fresh contexts and reset() use neutral
        // defaults so ordinary rectangles/images and existing callers are unchanged.
        glm::vec4 text_color{1.0f};
        float text_scale{1.0f};

        [[deprecated("ignores the color stack; use push_color_direct instead")]]
        void set_color(glm::vec4 color) {
            this->color = color;
        }
        [[deprecated("ignores the color stack; use pop_color instead")]]
        void reset_color() {
            this->color = glm::vec4(1.0f);
            color_stack.clear();
        }
        void push_color(glm::vec4 color) {
            color_stack.push_back(this->color);
            set_color(this->color*color);
        }
        void push_color_direct(glm::vec4 color) {
            color_stack.push_back(this->color);
            set_color(color);
        }
        void pop_color() {
            if(color_stack.empty())
                return;
            set_color(color_stack.back());
            color_stack.pop_back();
        }

        void set_clip(float x, float y, float width, float height) {
            vk::Rect2D scissor{
                vk::Offset2D{static_cast<int32_t>(x*frame_size.width), static_cast<int32_t>(y*frame_size.height)},
                vk::Extent2D{static_cast<uint32_t>(width*frame_size.width), static_cast<uint32_t>(height*frame_size.height)}
            };
            commandBuffer.setScissor(0, scissor);
        }
        void set_clip(vk::Rect2D scissor) {
            commandBuffer.setScissor(0, scissor);
        }
        void reset_clip() {
            vk::Rect2D scissor{
                vk::Offset2D{0, 0},
                frame_size
            };
            commandBuffer.setScissor(0, scissor);
        }

        // Zoom helpers: apply a temporary viewport/scissor scale centered on screen
        void push_zoom(float scale) {
            scale = std::clamp(scale, 0.1f, 1.0f);
            vk::Viewport vp(0.0f, 0.0f,
                static_cast<float>(frame_size.width),
                static_cast<float>(frame_size.height), 0.0f, 1.0f);
            float nw = vp.width * scale;
            float nh = vp.height * scale;
            vp.x = (vp.width - nw) * 0.5f;
            vp.y = (vp.height - nh) * 0.5f;
            vp.width = nw;
            vp.height = nh;
            viewport_stack.push_back(vp);
            commandBuffer.setViewport(0, vp);

            vk::Rect2D scissor({0,0}, frame_size);
            scissor.offset.x = static_cast<int32_t>(vp.x);
            scissor.offset.y = static_cast<int32_t>(vp.y);
            scissor.extent.width  = static_cast<uint32_t>(vp.width);
            scissor.extent.height = static_cast<uint32_t>(vp.height);
            scissor_stack.push_back(scissor);
            commandBuffer.setScissor(0, scissor);
        }
        void pop_zoom() {
            if(!viewport_stack.empty()) viewport_stack.pop_back();
            if(!scissor_stack.empty()) scissor_stack.pop_back();
            // Restore to previous or full
            if(!viewport_stack.empty()) {
                commandBuffer.setViewport(0, viewport_stack.back());
            } else {
                vk::Viewport vp(0.0f, 0.0f,
                    static_cast<float>(frame_size.width),
                    static_cast<float>(frame_size.height), 0.0f, 1.0f);
                commandBuffer.setViewport(0, vp);
            }
            if(!scissor_stack.empty()) {
                commandBuffer.setScissor(0, scissor_stack.back());
            } else {
                reset_clip();
            }
        }

        void draw_text(std::string_view text, float x, float y, float scale = 1.0f,
            glm::vec4 color = glm::vec4(1.0, 1.0, 1.0, 1.0),
            bool centerH = false, bool centerV = false)
        {
            if(centerH || centerV) {
                glm::vec2 size = measure_text(text, scale * 0.5f);
                if(centerH)
                    x -= size.x / 2.0f;
                if(centerV)
                    y -= size.y / 2.0f;
            }
            apply_view();
            font_renderer->renderText(commandBuffer, frame, renderPass, text, x, y,
                scale*effective_text_scale(), color*text_color*this->color);
        }
        // Pixel font sizes and a baseline anchor avoid guessed line-height
        // multipliers. Coordinates use framebuffer pixels; no extra typography scale.
        void draw_text_pixels(std::string_view text,float x,float baseline,float pixels,
            glm::vec4 tint=glm::vec4(1),float shadow=0.65f,float glow=0.0f,bool centered=false,glm::vec3 opacity_gradient=glm::vec3(1)) {
            const float scale=pixels/static_cast<float>(frame_size.height)*font_renderer->emScale();
            float nx=x/static_cast<float>(frame_size.width);
            if(centered) nx-=font_renderer->measureText(text,scale*0.5f).x*0.5f;
            const float ny=(baseline-pixels*font_renderer->baselineEm())/static_cast<float>(frame_size.height);
            apply_view();
            const bool fading=opacity_gradient!=glm::vec3(1);
            const glm::vec4 bounds=fading?glm::vec4(nx*frame_size.width,nx*frame_size.width+font_renderer->measureText(text,scale*0.5f).x*frame_size.width,0.172f,0.438f):glm::vec4(0);
            font_renderer->renderText(commandBuffer,frame,renderPass,text,nx,ny,scale,tint*text_color*color,shadow,glow,bounds,glm::vec4(opacity_gradient,0.812f));
        }
        float text_width_pixels(std::string_view text,float pixels) const {
            return font_renderer->measureText(text,pixels/static_cast<float>(frame_size.height)*font_renderer->emScale()*0.5f).x*frame_size.width;
        }
        // Ascender and positive descender distances from the baseline.
        glm::vec2 text_line_metrics_pixels(float pixels) const noexcept {
            return font_renderer->lineMetricsEm()*pixels;
        }
        glm::vec2 measure_text(std::string_view text, float scale) const {
            return font_renderer->measureText(text, scale*effective_text_scale());
        }

        void draw_image(const texture& texture, float x, float y, float scaleX = 1.0f, float scaleY = 1.0f,
            glm::vec4 color = glm::vec4(1.0, 1.0, 1.0, 1.0))
        {
            apply_view();
            image_renderer->renderImage(commandBuffer, frame, renderPass, texture, x, y, scaleX, scaleY, color*this->color);
        }
        // Explicit screen-space mesh geometry, including perspective-correct
        // UVs, vertex tint, optional additive blending and depth testing.
        // Depth testing requires a depth attachment in the selected render pass.
        void draw_textured_triangles(const texture& image,
            std::span<const auroreengine::image_renderer::textured_vertex> vertices,
            bool additive=false,bool depth=false) {
            apply_view();
            image_renderer->renderTexturedTriangles(commandBuffer,frame,renderPass,image,vertices,color,additive,depth);
        }
        void draw_textured_triangles(vk::ImageView image,
            std::span<const auroreengine::image_renderer::textured_vertex> vertices,
            bool additive=false,bool depth=false) {
            apply_view();
            image_renderer->renderTexturedTriangles(commandBuffer,frame,renderPass,image,vertices,color,additive,depth);
        }

        void draw_glass_frame(float x,float y,float width,float height,float opacity=1) {
            apply_view();image_renderer->renderGlassFrame(commandBuffer,frame,renderPass,x,y,width,height,opacity);
        }
        void draw_image_a(const texture& texture, float x, float y, float scaleX = 1.0f, float scaleY = 1.0f,
            glm::vec4 color = glm::vec4(1.0, 1.0, 1.0, 1.0), bool center = true)
        {
            if(texture.width > texture.height) {
                if(center) {
                    y += scaleY*(1.0f - static_cast<float>(texture.height) / texture.width) / 2.0f;
                }
                scaleY *= static_cast<float>(texture.height) / texture.width;
            } else if(texture.height > texture.width) {
                if(center) {
                    x += scaleX*(1.0f - static_cast<float>(texture.width) / texture.height) / 2.0f;
                }
                scaleX *= static_cast<float>(texture.width) / texture.height;
            }
            apply_view();
            image_renderer->renderImage(commandBuffer, frame, renderPass, texture, x, y, scaleX, scaleY, color*this->color);
        }
        // xmb-web-inspired translucent glass pass for icons. The texture passed
        // here should be the imported icon normal map when available; the
        // engine glass shader samples the live shell background for refraction.
        void draw_image_glass(const texture& texture, float x, float y, float scaleX = 1.0f, float scaleY = 1.0f,
            glm::vec4 color = glm::vec4(1.0, 1.0, 1.0, 1.0))
        {
            if(!texture.loaded) return;
            apply_view();
            image_renderer->renderImageGlass(commandBuffer, frame, renderPass, texture.imageView.get(), x, y, scaleX, scaleY, color*this->color);
        }
        void draw_image(vk::ImageView view, float x, float y, float scaleX = 1.0f, float scaleY = 1.0f,
            glm::vec4 color = glm::vec4(1.0, 1.0, 1.0, 1.0))
        {
            apply_view();
            image_renderer->renderImage(commandBuffer, frame, renderPass, view, x, y, scaleX, scaleY, color*this->color);
        }
        void draw_image_sized(const texture& texture, float x, float y, int width = -1, int height = -1,
            glm::vec4 color = glm::vec4(1.0, 1.0, 1.0, 1.0))
        {
            apply_view();
            image_renderer->renderImageSized(commandBuffer, frame, renderPass, texture, x, y, width, height, color*this->color);
        }
        void draw_image_sized(vk::ImageView view, float x, float y, int width, int height,
            glm::vec4 color = glm::vec4(1.0, 1.0, 1.0, 1.0))
        {
            apply_view();
            image_renderer->renderImageSized(commandBuffer, frame, renderPass, view, x, y, width, height, color*this->color);
        }

        void draw_generic(std::ranges::range auto vertices, simple_renderer::params p = {})
            requires(std::same_as<std::ranges::range_value_t<decltype(vertices)>, simple_renderer::vertex_data>)
        {
            // The overwhelmingly common steady-state path has an identity
            // colour stack. Forward that range directly so fixed-size UI
            // geometry (clock rings, hands, focus marks) does not allocate a
            // temporary vector on every draw. Preserve the copy-and-modulate
            // path only while an inherited colour/fade is active.
            if(color.r == 1.0F && color.g == 1.0F &&
               color.b == 1.0F && color.a == 1.0F) {
                apply_view();
                simple_renderer->renderGeneric(
                    commandBuffer, frame, renderPass, vertices, p);
                return;
            }
            std::vector<simple_renderer::vertex_data> vertices_vector(vertices.begin(), vertices.end());
            for(auto& v : vertices_vector) {
                v.color *= color;
            }
            apply_view();
            simple_renderer->renderGeneric(commandBuffer, frame, renderPass, vertices_vector, p);
        }
        void draw_quad(std::ranges::range auto vertices, simple_renderer::params p = {})
            requires(std::same_as<std::ranges::range_value_t<decltype(vertices)>, simple_renderer::vertex_data>)
        {
            std::vector<simple_renderer::vertex_data> vertices_vector(vertices.begin(), vertices.end());
            for(auto& v : vertices_vector) {
                v.color *= color;
            }
            apply_view();
            simple_renderer->renderQuad(commandBuffer, frame, renderPass, vertices_vector, p);
        }
        void draw_rect(glm::vec2 position, glm::vec2 size, glm::vec4 color = glm::vec4(1.0, 1.0, 1.0, 1.0), simple_renderer::params p = {}) {
            apply_view();
            simple_renderer->renderRect(commandBuffer, frame, renderPass, position, size, color*this->color, p);
        }

        void reset() {
            text_color=glm::vec4(1.0f);text_scale=1.0f;
            reset_color();
            reset_clip();
        }

        // allow users to access the raw parts as well
        font_renderer* get_font_renderer() {
            return font_renderer;
        }
        image_renderer* get_image_renderer() {
            return image_renderer;
        }
        simple_renderer* get_simple_renderer() {
            return simple_renderer;
        }
        vk::CommandBuffer get_command_buffer() {
            return commandBuffer;
        }
        int get_frame() {
            return frame;
        }
        vk::RenderPass get_render_pass() {
            return renderPass;
        }
    private:
        float effective_text_scale() const { return std::isfinite(text_scale) && text_scale>0.0f?text_scale:1.0f; }
        void apply_view() {
            if(!viewport_stack.empty()) {
                commandBuffer.setViewport(0, viewport_stack.back());
            }
            if(!scissor_stack.empty()) {
                commandBuffer.setScissor(0, scissor_stack.back());
            }
        }
        font_renderer* font_renderer;
        image_renderer* image_renderer;
        simple_renderer* simple_renderer;

        vk::CommandBuffer commandBuffer;
        int frame;
        vk::RenderPass renderPass;

        glm::vec4 color = glm::vec4(1.0f);
        std::vector<glm::vec4> color_stack;

        std::vector<vk::Viewport> viewport_stack;
        std::vector<vk::Rect2D> scissor_stack;
};

}
