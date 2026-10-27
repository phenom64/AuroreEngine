// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

#include <aurore/core/result.hpp>

namespace aurore {

using AnimationDuration = std::chrono::nanoseconds;
using MonotonicClock = std::chrono::steady_clock;
using MonotonicTimePoint = MonotonicClock::time_point;
using WallTimePoint = std::chrono::system_clock::time_point;

struct FrameTime final {
  MonotonicTimePoint monotonic_now{};
  AnimationDuration animation_time{};
  AnimationDuration delta{};
  AnimationDuration unclamped_delta{};
  std::uint64_t tick{0};

  [[nodiscard]] bool operator==(const FrameTime &) const = default;
};

enum class AnimationClockMode : std::uint8_t {
  realtime = 0,
  fixed_step = 1,
  paused = 2,
};

struct AnimationClockConfig final {
  AnimationDuration fixed_step{std::chrono::nanoseconds{16'666'667}};
  AnimationDuration maximum_realtime_delta{std::chrono::milliseconds{100}};
};

// AnimationClock never reads civil time. The owner supplies a monotonic sample,
// making replay, tests, and platform event-loop integration deterministic.
class AnimationClock final {
public:
  AnimationClock() noexcept = default;

  [[nodiscard]] static Result<AnimationClock>
  create(AnimationClockConfig config);

  [[nodiscard]] FrameTime sample(MonotonicTimePoint now) noexcept;

  void set_mode(AnimationClockMode mode) noexcept;
  void pause() noexcept;
  void resume() noexcept;

  [[nodiscard]] Result<void> set_fixed_step(AnimationDuration fixed_step);
  [[nodiscard]] Result<void>
  set_maximum_realtime_delta(AnimationDuration maximum_delta);
  [[nodiscard]] Result<void> seek(AnimationDuration animation_time);
  [[nodiscard]] Result<void> seek(AnimationDuration animation_time,
                                  std::uint64_t tick);

  // Clears the monotonic anchor. The first subsequent sample has zero delta.
  [[nodiscard]] Result<void>
  reset(AnimationDuration animation_time = AnimationDuration::zero());

  [[nodiscard]] AnimationClockMode mode() const noexcept { return mode_; }
  [[nodiscard]] AnimationDuration fixed_step() const noexcept {
    return config_.fixed_step;
  }
  [[nodiscard]] AnimationDuration maximum_realtime_delta() const noexcept {
    return config_.maximum_realtime_delta;
  }
  [[nodiscard]] AnimationDuration animation_time() const noexcept {
    return animation_time_;
  }
  [[nodiscard]] std::uint64_t tick() const noexcept { return tick_; }

private:
  explicit AnimationClock(AnimationClockConfig config) noexcept
      : config_(config) {}

  AnimationClockConfig config_{};
  AnimationClockMode mode_{AnimationClockMode::realtime};
  AnimationClockMode resume_mode_{AnimationClockMode::realtime};
  std::optional<MonotonicTimePoint> previous_sample_{};
  AnimationDuration animation_time_{};
  std::uint64_t tick_{0};
};

class WallClock {
public:
  virtual ~WallClock() = default;
  [[nodiscard]] virtual WallTimePoint now() const noexcept = 0;
};

class SystemWallClock final : public WallClock {
public:
  [[nodiscard]] WallTimePoint now() const noexcept override;
};

// Construction is explicit; the engine does not create a process-global clock.
[[nodiscard]] std::unique_ptr<WallClock> make_system_wall_clock();

} // namespace aurore
