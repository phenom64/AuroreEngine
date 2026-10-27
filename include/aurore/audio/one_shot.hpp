// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include <aurore/core/result.hpp>

namespace aurore::audio {

// Product code owns the mapping from semantic actions to these stable IDs.
struct SemanticCueId final {
  std::uint64_t value{0};
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return value != 0;
  }
  [[nodiscard]] bool operator==(const SemanticCueId &) const = default;
};

struct OneShotRequest final {
  SemanticCueId cue{};
  float gain{1.0F};
  std::chrono::milliseconds fade_in{};
  std::chrono::milliseconds fade_out{};
};

enum class OneShotAvailability : std::uint8_t {
  played = 0,
  degraded = 1,
  unavailable = 2,
};

struct OneShotOutcome final {
  OneShotAvailability availability{OneShotAvailability::unavailable};
  std::string detail{};
};

class OneShotPlayer {
public:
  virtual ~OneShotPlayer() = default;
  [[nodiscard]] virtual Result<OneShotOutcome>
  play_one_shot(const OneShotRequest &request) = 0;
};

} // namespace aurore::audio
