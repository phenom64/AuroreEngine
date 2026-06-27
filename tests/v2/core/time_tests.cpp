// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <aurore/core/time.hpp>

#include <array>
#include <cassert>
#include <chrono>
#include <cstring>
#include <type_traits>

namespace {

using namespace std::chrono_literals;

aurore::MonotonicTimePoint at(std::chrono::nanoseconds value) {
  return aurore::MonotonicTimePoint{value};
}

class JumpingWallClock final : public aurore::WallClock {
public:
  aurore::WallTimePoint value{};
  [[nodiscard]] aurore::WallTimePoint now() const noexcept override {
    return value;
  }
};

void fixed_step_is_byte_stable() {
  auto make_clock = [] {
    auto result = aurore::AnimationClock::create({20ms, 75ms});
    assert(result);
    auto clock = *result;
    clock.set_mode(aurore::AnimationClockMode::fixed_step);
    return clock;
  };

  auto first = make_clock();
  auto second = make_clock();
  constexpr std::array<std::chrono::nanoseconds, 4> inputs{0ns, 1ns, 300ms, 9s};
  for (const auto input : inputs) {
    const auto lhs = first.sample(at(input));
    const auto rhs = second.sample(at(input));
    static_assert(std::is_trivially_copyable_v<aurore::FrameTime>);
    assert(std::memcmp(&lhs, &rhs, sizeof(aurore::FrameTime)) == 0);
  }
  assert(first.animation_time() == 60ms);
}

void transitions_are_predictable_and_resume_is_clamped() {
  auto result = aurore::AnimationClock::create({10ms, 50ms});
  assert(result);
  auto clock = *result;

  assert(clock.sample(at(0ns)).delta == 0ns);
  assert(clock.sample(at(20ms)).delta == 20ms);

  clock.pause();
  const auto paused = clock.sample(at(40ms));
  assert(paused.delta == 0ns);
  assert(paused.unclamped_delta == 20ms);
  assert(paused.animation_time == 20ms);

  assert(clock.seek(2s));
  clock.resume();
  const auto resumed = clock.sample(at(10s));
  assert(resumed.unclamped_delta == 9'960ms);
  assert(resumed.delta == 50ms);
  assert(resumed.animation_time == 2'050ms);

  clock.set_mode(aurore::AnimationClockMode::fixed_step);
  const auto fixed = clock.sample(at(10s + 1ns));
  assert(fixed.unclamped_delta == 1ns);
  assert(fixed.delta == 10ms);
  assert(fixed.animation_time == 2'060ms);

  assert(clock.seek(5s, 90));
  const auto sought = clock.sample(at(11s));
  assert(sought.tick == 90);
  assert(sought.animation_time == 5'010ms);
}

void wall_clock_jumps_do_not_affect_animation() {
  JumpingWallClock wall_clock{};
  aurore::AnimationClock animation{};
  assert(animation.sample(at(0ns)).delta == 0ns);

  wall_clock.value = aurore::WallTimePoint{500h};
  const auto before_jump = animation.sample(at(16ms));
  wall_clock.value = aurore::WallTimePoint{-500h};
  const auto after_jump = animation.sample(at(32ms));

  assert(before_jump.delta == 16ms);
  assert(after_jump.delta == 16ms);
  assert(after_jump.animation_time == 32ms);
}

void invalid_configuration_returns_structured_errors() {
  const auto invalid = aurore::AnimationClock::create({0ns, 1ms});
  assert(!invalid);
  assert(invalid.error().domain == aurore::ErrorDomain::timing);
  assert(invalid.error().code != 0);
  assert(invalid.error().suggested_recovery ==
         aurore::SuggestedRecovery::reconfigure);

  aurore::AnimationClock clock{};
  const auto invalid_seek = clock.seek(-1ns);
  assert(!invalid_seek);
}

} // namespace

int main() {
  fixed_step_is_byte_stable();
  transitions_are_predictable_and_resume_is_clamped();
  wall_clock_jumps_do_not_affect_animation();
  invalid_configuration_returns_structured_errors();
}
