#include "../../src/components/font_shaping.hpp"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <unordered_map>
#include <unordered_set>

using auroreengine::detail::font_shaper;

int main(int argc, char** argv) {
    if(argc != 2) {
        std::cerr << "Usage: font_fallback_tests <directory containing Play-Regular.ttf and fonts/>\n";
        return 2;
    }
    const std::filesystem::path root = argv[1];
    const auto primary = (root / "Play-Regular.ttf").string();
    const std::vector<std::string> fallback{
        (root / "fonts/NotoSans-Regular.ttf").string(),
        (root / "fonts/NotoSansDevanagari-Regular.ttf").string(),
        (root / "fonts/NotoSansCJK-Regular.ttc").string()};
    font_shaper play(primary, 48, {}), mixed(primary, 48, fallback);
    const std::string latin = "British English Deutsch Français Polski Bahasa Indonesia Español";
    const auto before = play.shape(latin), after = mixed.shape(latin);
    // Compare against the renderer's original single-face HarfBuzz configuration.
    hb_font_t* legacyFont = hb_ft_font_create_referenced(play.face(0));
    hb_buffer_t* legacyBuffer = hb_buffer_create();
    hb_buffer_set_cluster_level(legacyBuffer, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS);
    hb_buffer_add_utf8(legacyBuffer, latin.data(), static_cast<int>(latin.size()), 0, static_cast<int>(latin.size()));
    hb_buffer_guess_segment_properties(legacyBuffer);
    constexpr std::array legacyFeatures{
        hb_feature_t{HB_TAG('l','i','g','a'), 0, HB_FEATURE_GLOBAL_START, HB_FEATURE_GLOBAL_END},
        hb_feature_t{HB_TAG('c','l','i','g'), 0, HB_FEATURE_GLOBAL_START, HB_FEATURE_GLOBAL_END}};
    hb_shape(legacyFont, legacyBuffer, legacyFeatures.data(), static_cast<unsigned int>(legacyFeatures.size()));
    unsigned int legacyCount{};
    const auto* legacyInfos = hb_buffer_get_glyph_infos(legacyBuffer, &legacyCount);
    const auto* legacyPositions = hb_buffer_get_glyph_positions(legacyBuffer, &legacyCount);
    assert(legacyCount == before.glyphs.size());
    for(unsigned int i=0; i<legacyCount; ++i) {
        assert(legacyInfos[i].codepoint == font_shaper::glyph_index(before.glyphs[i].key));
        assert(legacyPositions[i].x_advance / 64.0f == before.glyphs[i].advance);
        assert(legacyPositions[i].x_offset / 64.0f == before.glyphs[i].x_offset);
    }
    hb_buffer_destroy(legacyBuffer);
    hb_font_destroy(legacyFont);
    assert(before.glyphs.size() == after.glyphs.size());
    assert(before.advance == after.advance);
    for(std::size_t i=0; i<before.glyphs.size(); ++i) {
        assert(before.glyphs[i].key == after.glyphs[i].key);
        assert(before.glyphs[i].x_offset == after.glyphs[i].x_offset);
        assert(before.glyphs[i].advance == after.glyphs[i].advance);
        assert(font_shaper::face_index(after.glyphs[i].key) == 0);
    }
    assert(play.line_height() == mixed.line_height());
    assert(play.baseline() == mixed.baseline());
    const auto uncommonLatin = mixed.shape("Play Ḅ");
    const auto supportedPrefix = play.shape("Play ");
    assert(uncommonLatin.missing_glyphs == 0);
    for(std::size_t i=0; i<supportedPrefix.glyphs.size(); ++i) {
        assert(uncommonLatin.glyphs[i].key == supportedPrefix.glyphs[i].key);
        assert(uncommonLatin.glyphs[i].advance == supportedPrefix.glyphs[i].advance);
    }
    assert(font_shaper::face_index(uncommonLatin.glyphs.back().key) == 1);

    const std::string hindi = "हिन्दी";
    assert(play.shape(hindi).missing_glyphs > 0);
    const auto shapedHindi = mixed.shape(hindi);
    assert(shapedHindi.missing_glyphs == 0 && shapedHindi.advance > 0);
    // Compare the complete Indic run against HarfBuzz using that actual face.
    font_shaper devanagari(fallback[1], 48, {});
    const auto directHindi = devanagari.shape(hindi);
    assert(shapedHindi.glyphs.size() == directHindi.glyphs.size());
    for(std::size_t i=0; i<directHindi.glyphs.size(); ++i) {
        const auto& actual = shapedHindi.glyphs[i];
        const auto& direct = directHindi.glyphs[i];
        assert(font_shaper::glyph_index(actual.key) == font_shaper::glyph_index(direct.key));
        assert(actual.cluster == direct.cluster);
        assert(actual.x_offset == direct.x_offset && actual.y_offset == direct.y_offset);
        assert(actual.advance == direct.advance);
    }
    // Hindi's pre-base vowel and conjunct must not be shaped as separate scalars.
    assert(shapedHindi.glyphs.front().cluster == shapedHindi.glyphs[1].cluster);
    hb_font_t* indicFont = hb_ft_font_create_referenced(devanagari.face(0));
    hb_buffer_t* indicBuffer = hb_buffer_create();
    hb_buffer_add_utf8(indicBuffer, hindi.data(), static_cast<int>(hindi.size()), 0, static_cast<int>(hindi.size()));
    hb_buffer_guess_segment_properties(indicBuffer);
    hb_shape(indicFont, indicBuffer, nullptr, 0);
    unsigned int indicCount{};
    const auto* indicInfos = hb_buffer_get_glyph_infos(indicBuffer, &indicCount);
    const auto* indicPositions = hb_buffer_get_glyph_positions(indicBuffer, &indicCount);
    assert(indicCount == shapedHindi.glyphs.size());
    for(unsigned int i=0; i<indicCount; ++i) {
        assert(indicInfos[i].codepoint == font_shaper::glyph_index(shapedHindi.glyphs[i].key));
        assert(indicInfos[i].cluster == shapedHindi.glyphs[i].cluster);
        assert(indicPositions[i].x_advance / 64.0f == shapedHindi.glyphs[i].advance);
    }
    hb_buffer_destroy(indicBuffer);
    hb_font_destroy(indicFont);

    const auto autonyms = mixed.shape(latin + " हिन्दी 日本語 简体中文 繁體中文 한국어 Русский");
    assert(autonyms.missing_glyphs == 0 && autonyms.advance > after.advance);
    std::unordered_set<std::uint64_t> identities;
    for(const auto& g : autonyms.glyphs) identities.insert(g.key);
    assert(identities.size() > 40);
    // Every face can legally have glyph ID 1: these must occupy distinct atlas entries.
    assert(font_shaper::glyph_key(0, 1) != font_shaper::glyph_key(1, 1));
    for(const auto& g : autonyms.glyphs) {
        FT_Face face = mixed.face(font_shaper::face_index(g.key));
        assert(FT_Load_Glyph(face, font_shaper::glyph_index(g.key), FT_LOAD_DEFAULT) == 0);
    }
    // Exercise actual overlapping glyph numbers from the packaged faces.
    // Each face's low glyph IDs are unrelated outlines, even for the same number.
    std::unordered_set<std::uint64_t> atlasKeys;
    for(std::uint32_t face=0; face<mixed.loaded_paths().size(); ++face)
        for(std::uint32_t glyph=1; glyph<20; ++glyph) {
            assert(FT_Load_Glyph(mixed.face(face), glyph, FT_LOAD_DEFAULT) == 0);
            atlasKeys.insert(font_shaper::glyph_key(face, glyph));
        }
    assert(atlasKeys.size() == 19 * mixed.loaded_paths().size());

    font_shaper missing(primary, 48, {(root / "absent-optional.ttf").string(), fallback[1]});
    assert(missing.shape(hindi).missing_glyphs == 0);
    assert(missing.shape(latin).advance == before.advance);
    // Each shaper owns its library and faces; a second instance may die first.
    for(int i=0; i<20; ++i) {
        font_shaper temporary(primary, 48, fallback);
        assert(temporary.shape(hindi).missing_glyphs == 0);
    }
    assert(mixed.shape(hindi).advance == shapedHindi.advance);
    const auto mixedText = mixed.shape("Play हिन्दी 日本語");
    const auto playPrefix = play.shape("Play ");
    for(std::size_t i=0; i<playPrefix.glyphs.size(); ++i) {
        assert(mixedText.glyphs[i].key == playPrefix.glyphs[i].key);
        assert(mixedText.glyphs[i].advance == playPrefix.glyphs[i].advance);
    }
    assert(mixed.shape("हिन्दी\xff").missing_glyphs <= 1);
    const auto unsupported = missing.shape("日本語");
    assert(unsupported.missing_glyphs > 0 && !unsupported.glyphs.empty());
    std::cout << "font fallback: Latin metrics, Indic clusters, mixed autonyms, glyph identity, missing files and lifetime passed\n";
}
