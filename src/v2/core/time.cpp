// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <aurore/core/time.hpp>

#include <algorithm>
#include <limits>

namespace aurore {
namespace {

constexpr ErrorCode invalid_duration = 1;
constexpr ErrorCode invalid_seek = 2;

[[nodiscard]] Error timing_error(ErrorCode code, const char *message,
                                 const char *field) {
  return Error{
      .domain = ErrorDomain::timing,
      .code = code,
      .message = message,
      .context = {{"field", field}},
      .suggested_recovery = SuggestedRecovery::reconfigure,
  };
}

[[nodiscard]] AnimationDuration saturating_add(AnimationDuration lhs,
                                               AnimationDuration rhs) noexcept {
  using Representation = AnimationDuration::rep;
  constexpr auto maximum = std::numeric_limits<Representation>::max();
  constexpr auto minimum = std::numeric_limits<Representation>::min();

  if (rhs.count() > 0 && lhs.count() > maximum - rhs.count()) {
    return AnimationDuration{maximum};
  }
  if (rhs.count() < 0 && lhs.count() < minimum - rhs.count()) {
    return AnimationDuration{minimum};
  }
  return lhs + rhs;
}

} // namespace

Result<AnimationClock> AnimationClock::create(AnimationClockConfig config) {
  if (config.fixed_step <= AnimationDuration::zero()) {
    return std::unexpected(timing_error(invalid_duration,
                                        "fixed step must be greater than zero",
                                        "fixed_step"));
  }
  if (config.maximum_realtime_delta <= AnimationDuration::zero()) {
    return std::unexpected(timing_error(
        invalid_duration, "maximum realtime delta must be greater than zero",
        "maximum_realtime_delta"));
  }
  return AnimationClock{config};
}

FrameTime AnimationClock::sample(MonotonicTimePoint now) noexcept {
  const auto sample_tick = tick_;
  if (!previous_sample_) {
    previous_sample_ = now;
    if (tick_ != std::numeric_limits<std::uint64_t>::max()) {
      ++tick_;
    }
    return FrameTime{now, animation_time_, AnimationDuration::zero(),
                     AnimationDuration::zero(), sample_tick};
  }

  auto elapsed =
      std::chrono::duration_cast<AnimationDuration>(now - *previous_sample_);
  if (elapsed < AnimationDuration::zero()) {
    elapsed = AnimationDuration::zero();
  }
  previous_sample_ = now;

  AnimationDuration animation_delta{};
  switch (mode_) {
  case AnimationClockMode::realtime:
    animation_delta = std::min(elapsed, config_.maximum_realtime_delta);
    break;
  case AnimationClockMode::fixed_step:
    animation_delta = config_.fixed_step;
    break;
  case AnimationClockMode::paused:
    animation_delta = AnimationDuration::zero();
    break;
  }

  animation_time_ = saturating_add(animation_time_, animation_delta);
  if (tick_ != std::numeric_limits<std::uint64_t>::max()) {
    ++tick_;
  }
  return FrameTime{now, animation_time_, animation_delta, elapsed, sample_tick};
}

void AnimationClock::set_mode(AnimationClockMode mode) noexcept {
  if (mode == AnimationClockMode::paused) {
    pause();
    return;
  }
  mode_ = mode;
  resume_mode_ = mode;
}

void AnimationClock::pause() noexcept {
  if (mode_ != AnimationClockMode::paused) {
    resume_mode_ = mode_;
    mode_ = AnimationClockMode::paused;
  }
}

void AnimationClock::resume() noexcept {
  if (mode_ == AnimationClockMode::paused) {
    mode_ = resume_mode_;
  }
}

Result<void> AnimationClock::set_fixed_step(AnimationDuration fixed_step) {
  if (fixed_step <= AnimationDuration::zero()) {
    return std::unexpected(timing_error(invalid_duration,
                                        "fixed step must be greater than zero",
                                        "fixed_step"));
  }
  config_.fixed_step = fixed_step;
  return {};
}

Result<void>
AnimationClock::set_maximum_realtime_delta(AnimationDuration maximum_delta) {
  if (maximum_delta <= AnimationDuration::zero()) {
    return std::unexpected(timing_error(
        invalid_duration, "maximum realtime delta must be greater than zero",
        "maximum_realtime_delta"));
  }
  config_.maximum_realtime_delta = maximum_delta;
  return {};
}

Result<void> AnimationClock::seek(AnimationDuration animation_time) {
  if (animation_time < AnimationDuration::zero()) {
    return std::unexpected(timing_error(
        invalid_seek, "animation time must not be negative", "animation_time"));
  }
  animation_time_ = animation_time;
  return {};
}

Result<void> AnimationClock::seek(AnimationDuration animation_time,
                                  std::uint64_t tick) {
  auto result = seek(animation_time);
  if (!result) {
    return result;
  }
  tick_ = tick;
  return {};
}

Result<void> AnimationClock::reset(AnimationDuration animation_time) {
  auto result = seek(animation_time, 0);
  if (!result) {
    return result;
  }
  previous_sample_.reset();
  return {};
}

WallTimePoint SystemWallClock::now() const noexcept {
  return std::chrono::system_clock::now();
}

std::unique_ptr<WallClock> make_system_wall_clock() {
  return std::make_unique<SystemWallClock>();
}

} // namespace aurore
