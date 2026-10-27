#pragma once

// Documented xmb-web icon glass material constants (ICON_MATERIAL_DAY).
// Source: xmb-web/index.html @ ICON_MATERIAL_DAY (~line 2727) and FS_ICON_GLASS
// composite (~line 4331). Values are the web port's boosted day-theme tuning.

#include <array>
#include <cmath>
#include <numbers>

namespace aurore::tests::shaders {

inline constexpr std::array<float, 3> kIconGlassLight1 = {
    -0.5358268F, 0.3875156F, 0.7500925F};
inline constexpr std::array<float, 3> kIconGlassLight2 = {
    0.7071068F, 0.8660254F, -0.3535534F};
inline constexpr std::array<float, 3> kIconGlassAmbient = {0.74F, 0.80F, 0.90F};

// (spec1_pow, spec2_pow, spec1_attn, spec2_attn)
inline constexpr std::array<float, 4> kIconGlassSpecParams = {
    100.0F, 30.0F, 2.4F, 1.6F};

// Boosted 3x from PS3 .mnu REFR offsets for visible chromatic dispersion.
inline constexpr std::array<float, 3> kIconGlassRefr = {
    -0.096F, -0.114F, -0.144F};
inline constexpr float kIconGlassRefrScl = 12.0F;

// (attn_diff, attn_env)
inline constexpr std::array<float, 2> kIconGlassAttnDiffEnv = {2.0F, 0.74F};

inline constexpr float kIconGlassRefractedWeight = 0.35F;
inline constexpr float kIconGlassAmbientWeight = 0.55F;
inline constexpr float kIconGlassEnvWeight = 0.60F;
inline constexpr float kIconGlassSpecWeight = 1.80F;
inline constexpr float kIconGlassFresnelWeight = 0.75F;
inline constexpr float kIconGlassShadowBias = 0.30F;
inline constexpr float kIconGlassTonemapExposure = 0.95F;
inline constexpr float kIconGlassBrightnessScale = 1.08F;
inline constexpr float kIconGlassFresAlphaBase = 0.55F;
inline constexpr float kIconGlassFresAlphaRange = 0.40F;

inline constexpr std::array<float, 3> kIconGlassViewSkew = {-0.30F, -0.30F, 1.0F};

} // namespace aurore::tests::shaders