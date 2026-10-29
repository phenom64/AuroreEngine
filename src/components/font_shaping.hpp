#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <harfbuzz/hb.h>
#include <harfbuzz/hb-ft.h>

namespace auroreengine::detail {

// Internal CPU shaping shared by atlas preparation, measurement and drawing.
// Font IDs remain stable for this object's lifetime, including missing optional files.
class font_shaper {
    struct library {
        FT_Library value{};
        library() {
            if(FT_Init_FreeType(&value)) throw std::runtime_error("Failed to initialize font shaping library");
        }
        ~library() { FT_Done_FreeType(value); }
        library(const library&) = delete;
        library& operator=(const library&) = delete;
    } library_;
    struct font_face {
        FT_Face ft{};
        hb_font_t* hb{};
        font_face(FT_Library lib, const std::string& path, int pixels) {
            if(FT_New_Face(lib, path.c_str(), 0, &ft)) throw std::runtime_error("Failed to load font face: " + path);
            if(FT_Set_Pixel_Sizes(ft, 0, pixels)) {
                FT_Done_Face(ft); ft = nullptr;
                throw std::runtime_error("Failed to set font size: " + path);
            }
            hb = hb_ft_font_create_referenced(ft);
        }
        ~font_face() { hb_font_destroy(hb); FT_Done_Face(ft); }
        font_face(const font_face&) = delete;
        font_face& operator=(const font_face&) = delete;
    };
    struct buffer_deleter { void operator()(hb_buffer_t* b) const { hb_buffer_destroy(b); } };
    std::vector<std::unique_ptr<font_face>> faces_;
    std::vector<std::string> paths_;
    std::unique_ptr<hb_buffer_t, buffer_deleter> buffer_{hb_buffer_create()};
    struct scalar { std::uint32_t cp; std::size_t begin, end; hb_script_t script; };

    static bool weak_script(hb_script_t script) {
        return script == HB_SCRIPT_COMMON || script == HB_SCRIPT_INHERITED || script == HB_SCRIPT_UNKNOWN;
    }
    static bool ignorable(std::uint32_t cp) {
        return hb_unicode_general_category(hb_unicode_funcs_get_default(), cp) == HB_UNICODE_GENERAL_CATEGORY_FORMAT ||
               (cp >= 0xfe00 && cp <= 0xfe0f) || (cp >= 0xe0100 && cp <= 0xe01ef);
    }
    static bool mark(std::uint32_t cp) {
        const auto category = hb_unicode_general_category(hb_unicode_funcs_get_default(), cp);
        return category == HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK ||
               category == HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK ||
               category == HB_UNICODE_GENERAL_CATEGORY_ENCLOSING_MARK;
    }
    static std::vector<scalar> decode(std::string_view text) {
        std::vector<scalar> out;
        for(std::size_t i=0; i<text.size();) {
            const auto begin = i;
            const auto lead = static_cast<unsigned char>(text[i++]);
            std::uint32_t cp = lead;
            const int continuation = lead < 0x80 ? 0 : lead >= 0xc2 && lead <= 0xdf ? 1 :
                                     lead >= 0xe0 && lead <= 0xef ? 2 : lead >= 0xf0 && lead <= 0xf4 ? 3 : -1;
            if(continuation > 0) {
                cp = lead & (0x7f >> continuation);
                bool valid = i + continuation <= text.size();
                for(int n=0; valid && n<continuation; ++n) {
                    const auto next = static_cast<unsigned char>(text[i+n]);
                    if((next & 0xc0) != 0x80) valid = false;
                    else cp = (cp << 6) | (next & 0x3f);
                }
                const auto minimum = continuation == 1 ? 0x80u : continuation == 2 ? 0x800u : 0x10000u;
                if(valid && cp >= minimum && cp <= 0x10ffff && !(cp >= 0xd800 && cp <= 0xdfff)) i += continuation;
                else cp = 0xfffd;
            } else if(continuation < 0) cp = 0xfffd;
            out.push_back({cp, begin, i, hb_unicode_script(hb_unicode_funcs_get_default(), cp)});
        }
        // Inherited marks and punctuation travel with their script. In particular,
        // an Indic syllable is never split into independently shaped characters.
        hb_script_t previous = HB_SCRIPT_UNKNOWN;
        for(auto& item : out) {
            if(weak_script(item.script)) item.script = previous;
            else previous = item.script;
        }
        hb_script_t next = HB_SCRIPT_UNKNOWN;
        for(auto it=out.rbegin(); it!=out.rend(); ++it) {
            if(weak_script(it->script)) it->script = next;
            else next = it->script;
        }
        return out;
    }
    bool covers(std::size_t face, const std::vector<scalar>& text, std::size_t begin, std::size_t end) const {
        for(auto i=begin; i<end; ++i)
            if(!ignorable(text[i].cp) && !FT_Get_Char_Index(faces_[face]->ft, text[i].cp)) return false;
        return true;
    }
    std::size_t select(const std::vector<scalar>& text, std::size_t begin, std::size_t end) const {
        for(std::size_t i=0; i<faces_.size(); ++i) if(covers(i, text, begin, end)) return i;
        return faces_.size();
    }
public:
    using glyph_id = std::uint64_t;
    static constexpr glyph_id glyph_key(std::uint32_t face, std::uint32_t glyph) {
        return (static_cast<glyph_id>(face) << 32) | glyph;
    }
    static constexpr std::uint32_t face_index(glyph_id key) { return static_cast<std::uint32_t>(key >> 32); }
    static constexpr std::uint32_t glyph_index(glyph_id key) { return static_cast<std::uint32_t>(key); }
    struct positioned_glyph { glyph_id key; std::uint32_t cluster; float x_offset, y_offset, advance; };
    struct shaped_text { std::vector<positioned_glyph> glyphs; float advance{}; std::size_t missing_glyphs{}; };
    font_shaper(const std::string& primary, int pixels, const std::vector<std::string>& fallbacks) {
        faces_.push_back(std::make_unique<font_face>(library_.value, primary, pixels));
        paths_.push_back(primary);
        for(const auto& path : fallbacks) {
            if(std::find(paths_.begin(), paths_.end(), path) != paths_.end()) continue;
            // Optional package/system fonts can be absent. The primary remains required.
            try {
                auto face = std::make_unique<font_face>(library_.value, path, pixels);
                faces_.push_back(std::move(face)); paths_.push_back(path);
            } catch(const std::runtime_error&) {}
        }
    }
    FT_Face face(std::uint32_t index) const { return faces_.at(index)->ft; }
    const std::vector<std::string>& loaded_paths() const { return paths_; }
    float line_height() const { return static_cast<float>(face(0)->size->metrics.height >> 6); }
    int baseline() const { return face(0)->size->metrics.ascender >> 6; }
    shaped_text shape(std::string_view text) {
        if(text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) throw std::length_error("Text exceeds the shaping limit");
        shaped_text out;
        const auto scalars = decode(text);
        auto append = [&](std::size_t faceIndex, std::size_t begin, std::size_t end) {
            hb_buffer_reset(buffer_.get());
            hb_buffer_set_cluster_level(buffer_.get(), HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);
            hb_buffer_add_utf8(buffer_.get(), text.data(), static_cast<int>(text.size()),
                               static_cast<unsigned int>(scalars[begin].begin),
                               static_cast<int>(scalars[end-1].end - scalars[begin].begin));
            hb_buffer_set_script(buffer_.get(), scalars[begin].script);
            hb_buffer_guess_segment_properties(buffer_.get());
            // Preserve existing Play text metrics while retaining mandatory script shaping.
            constexpr std::array features{
                hb_feature_t{HB_TAG('l','i','g','a'), 0, HB_FEATURE_GLOBAL_START, HB_FEATURE_GLOBAL_END},
                hb_feature_t{HB_TAG('c','l','i','g'), 0, HB_FEATURE_GLOBAL_START, HB_FEATURE_GLOBAL_END}};
            hb_shape(faces_[faceIndex]->hb, buffer_.get(), features.data(), static_cast<unsigned int>(features.size()));
            unsigned int count{};
            const auto* infos = hb_buffer_get_glyph_infos(buffer_.get(), &count);
            const auto* positions = hb_buffer_get_glyph_positions(buffer_.get(), &count);
            for(unsigned int i=0; i<count; ++i) {
                const float advance = positions[i].x_advance / 64.0f;
                out.glyphs.push_back({glyph_key(static_cast<std::uint32_t>(faceIndex), infos[i].codepoint), infos[i].cluster,
                                     positions[i].x_offset / 64.0f, -positions[i].y_offset / 64.0f, advance});
                out.advance += advance;
                if(infos[i].codepoint == 0) ++out.missing_glyphs;
            }
        };
        for(std::size_t begin=0; begin<scalars.size();) {
            auto end = begin+1;
            while(end<scalars.size() && scalars[end].script == scalars[begin].script) ++end;
            const auto selected = select(scalars, begin, end);
            if(selected == 0) append(0, begin, end);
            else {
                // Keep primary-supported clusters even when a fallback covers this
                // whole script run. Combining sequences, viramas and joiners stay together.
                // Adjacent clusters selecting the same face are shaped together for kerning.
                auto runBegin = begin, runFace = faces_.size();
                for(auto cluster=begin; cluster<end;) {
                    auto clusterEnd = cluster+1;
                    while(clusterEnd<end && (mark(scalars[clusterEnd].cp) || ignorable(scalars[clusterEnd].cp) ||
                          scalars[clusterEnd-1].cp == 0x200d ||
                          hb_unicode_combining_class(hb_unicode_funcs_get_default(), scalars[clusterEnd-1].cp) == HB_UNICODE_COMBINING_CLASS_VIRAMA)) ++clusterEnd;
                    auto faceIndex = select(scalars, cluster, clusterEnd);
                    if(faceIndex == faces_.size()) faceIndex = 0; // Visible .notdef if no font can cover a cluster.
                    if(runFace != faceIndex) {
                        if(cluster != runBegin) append(runFace, runBegin, cluster);
                        runBegin = cluster; runFace = faceIndex;
                    }
                    cluster = clusterEnd;
                }
                append(runFace, runBegin, end);
            }
            begin = end;
        }
        return out;
    }
};

} // namespace auroreengine::detail
