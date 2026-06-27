// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <aurore/audio/one_shot.hpp>
#include <aurore/ui/minimal_draw_list.hpp>

#include <cassert>
#include <cstdint>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace {

void commands_own_payloads() {
  aurore::ui::MinimalDrawList list{};
  std::string source_text = "Settings";
  aurore::ui::LatinTextCommand text{};
  text.state.z = 2.0F;
  text.font = aurore::ui::FontAssetId{42};
  text.utf8_text = source_text;
  const auto text_index = list.add(text);

  std::vector<aurore::ui::MeshVertex> source_vertices(3);
  std::vector<std::uint32_t> source_indices{0, 1, 2};
  aurore::ui::IndexedMeshCommand mesh{};
  mesh.state.z = 1.0F;
  mesh.vertices = source_vertices;
  mesh.indices = source_indices;
  const auto mesh_index = list.add(mesh);

  source_text.assign("mutated");
  source_vertices.clear();
  source_indices.clear();
  text.utf8_text.assign("also mutated");
  mesh.vertices.clear();
  mesh.indices.clear();

  assert(text_index == 0);
  assert(mesh_index == 1);
  const auto &stored_text =
      std::get<aurore::ui::LatinTextCommand>(list.entries()[0].command);
  const auto &stored_mesh =
      std::get<aurore::ui::IndexedMeshCommand>(list.entries()[1].command);
  assert(stored_text.utf8_text == "Settings");
  assert(stored_mesh.vertices.size() == 3);
  assert((stored_mesh.indices == std::vector<std::uint32_t>{0, 1, 2}));
}

void render_order_is_z_sorted_and_insertion_stable() {
  aurore::ui::MinimalDrawList list{};
  aurore::ui::TexturedQuadCommand first{};
  first.state.z = 3.0F;
  aurore::ui::LatinTextCommand second{};
  second.state.z = 1.0F;
  aurore::ui::IndexedMeshCommand third{};
  third.state.z = 3.0F;

  static_cast<void>(list.add(first));
  static_cast<void>(list.add(second));
  static_cast<void>(list.add(third));

  const auto order = list.render_order();
  assert(order.size() == 3);
  assert(order[0].get().insertion_index == 1);
  assert(order[1].get().insertion_index == 0);
  assert(order[2].get().insertion_index == 2);

  // Sorting a view must not disturb backend-neutral submission order.
  assert(list.entries()[0].insertion_index == 0);
  assert(list.entries()[1].insertion_index == 1);
  assert(list.entries()[2].insertion_index == 2);
}

void public_contracts_remain_backend_neutral() {
  static_assert(
      std::is_same_v<aurore::Result<void>, std::expected<void, aurore::Error>>);
  static_assert(std::is_abstract_v<aurore::audio::OneShotPlayer>);

  const aurore::audio::OneShotRequest request{
      .cue = aurore::audio::SemanticCueId{7},
      .gain = 0.75F,
      .fade_in = std::chrono::milliseconds{5},
      .fade_out = std::chrono::milliseconds{25},
  };
  assert(request.cue.value == 7);
}

} // namespace

int main() {
  commands_own_payloads();
  render_order_is_z_sorted_and_insertion_stable();
  public_contracts_remain_backend_neutral();
}
