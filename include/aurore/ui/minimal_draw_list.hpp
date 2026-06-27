// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace aurore::ui {

template <typename Tag> struct StableId final {
  std::uint64_t value{0};

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return value != 0;
  }
  [[nodiscard]] bool operator==(const StableId &) const = default;
};

struct TextureAssetTag;
struct FontAssetTag;
struct MeshAssetTag;
struct MaterialTag;

using TextureAssetId = StableId<TextureAssetTag>;
using FontAssetId = StableId<FontAssetTag>;
using MeshAssetId = StableId<MeshAssetTag>;
using MaterialId = StableId<MaterialTag>;

struct Vec2 final {
  float x{0.0F};
  float y{0.0F};
  [[nodiscard]] bool operator==(const Vec2 &) const = default;
};

struct Colour final {
  float red{1.0F};
  float green{1.0F};
  float blue{1.0F};
  float alpha{1.0F};
  [[nodiscard]] bool operator==(const Colour &) const = default;
};

struct Rect final {
  float x{0.0F};
  float y{0.0F};
  float width{0.0F};
  float height{0.0F};
  [[nodiscard]] bool operator==(const Rect &) const = default;
};

// Row-major 2D affine transform:
// [m00 m01 tx]
// [m10 m11 ty]
struct Transform2D final {
  float m00{1.0F};
  float m01{0.0F};
  float m10{0.0F};
  float m11{1.0F};
  float tx{0.0F};
  float ty{0.0F};
  [[nodiscard]] bool operator==(const Transform2D &) const = default;
};

struct DrawState final {
  MaterialId material{};
  float z{0.0F};
  float opacity{1.0F};
  Transform2D transform{};
  std::optional<Rect> clip{};
  [[nodiscard]] bool operator==(const DrawState &) const = default;
};

struct TexturedQuadCommand final {
  DrawState state{};
  TextureAssetId texture{};
  Vec2 size{};
  Rect texture_coordinates{0.0F, 0.0F, 1.0F, 1.0F};
  Colour tint{};
  [[nodiscard]] bool operator==(const TexturedQuadCommand &) const = default;
};

enum class HorizontalTextAlignment : std::uint8_t {
  leading = 0,
  centre = 1,
  trailing = 2,
};

struct LatinTextCommand final {
  DrawState state{};
  FontAssetId font{};
  std::string utf8_text{};
  float point_size{16.0F};
  float maximum_width{0.0F};
  Colour colour{};
  HorizontalTextAlignment alignment{HorizontalTextAlignment::leading};
  [[nodiscard]] bool operator==(const LatinTextCommand &) const = default;
};

struct MeshVertex final {
  Vec2 position{};
  Vec2 texture_coordinate{};
  Colour colour{};
  [[nodiscard]] bool operator==(const MeshVertex &) const = default;
};

struct IndexedMeshCommand final {
  DrawState state{};
  MeshAssetId mesh{};
  std::optional<TextureAssetId> texture{};
  std::vector<MeshVertex> vertices{};
  std::vector<std::uint32_t> indices{};
  [[nodiscard]] bool operator==(const IndexedMeshCommand &) const = default;
};

using DrawCommand =
    std::variant<TexturedQuadCommand, LatinTextCommand, IndexedMeshCommand>;

struct DrawEntry final {
  std::uint64_t insertion_index{0};
  DrawCommand command{};

  [[nodiscard]] float z() const noexcept {
    return std::visit([](const auto &value) { return value.state.z; }, command);
  }
};

class MinimalDrawList final {
public:
  [[nodiscard]] std::uint64_t add(TexturedQuadCommand command) {
    return append(std::move(command));
  }

  [[nodiscard]] std::uint64_t add(LatinTextCommand command) {
    return append(std::move(command));
  }

  [[nodiscard]] std::uint64_t add(IndexedMeshCommand command) {
    return append(std::move(command));
  }

  void clear() noexcept {
    entries_.clear();
    next_insertion_index_ = 0;
  }

  [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
  [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
  [[nodiscard]] std::span<const DrawEntry> entries() const noexcept {
    return entries_;
  }

  // Returns references ordered by ascending z. Equal or unordered (NaN) z
  // values retain insertion order through the explicit insertion index.
  [[nodiscard]] std::vector<std::reference_wrapper<const DrawEntry>>
  render_order() const {
    std::vector<std::reference_wrapper<const DrawEntry>> ordered{};
    ordered.reserve(entries_.size());
    for (const auto &entry : entries_) {
      ordered.emplace_back(entry);
    }

    std::stable_sort(
        ordered.begin(), ordered.end(), [](const auto lhs, const auto rhs) {
          const auto lhs_z = lhs.get().z();
          const auto rhs_z = rhs.get().z();
          const auto lhs_is_nan = std::isnan(lhs_z);
          const auto rhs_is_nan = std::isnan(rhs_z);
          if (lhs_is_nan != rhs_is_nan) {
            return !lhs_is_nan;
          }
          if (!lhs_is_nan) {
            if (lhs_z < rhs_z) {
              return true;
            }
            if (rhs_z < lhs_z) {
              return false;
            }
          }
          return lhs.get().insertion_index < rhs.get().insertion_index;
        });
    return ordered;
  }

private:
  template <typename Command>
  [[nodiscard]] std::uint64_t append(Command command) {
    const auto insertion_index = next_insertion_index_++;
    entries_.push_back(
        DrawEntry{insertion_index, DrawCommand{std::move(command)}});
    return insertion_index;
  }

  std::vector<DrawEntry> entries_{};
  std::uint64_t next_insertion_index_{0};
};

} // namespace aurore::ui
