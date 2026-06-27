// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace aurore {

// Values are explicit because errors may cross logging, IPC, and persistence
// boundaries. Append new domains; do not renumber existing ones.
enum class ErrorDomain : std::uint16_t {
  generic = 0,
  system = 1,
  input_output = 2,
  asset = 3,
  rendering = 4,
  audio = 5,
  media = 6,
  text = 7,
  platform = 8,
  state = 9,
  timing = 10,
};

enum class SuggestedRecovery : std::uint8_t {
  none = 0,
  retry = 1,
  retry_later = 2,
  ignore = 3,
  reconfigure = 4,
  reload_resource = 5,
  restart_subsystem = 6,
  restart_application = 7,
};

using ErrorCode = std::int32_t;
using ErrorContext = std::vector<std::pair<std::string, std::string>>;

struct Error final {
  ErrorDomain domain{ErrorDomain::generic};
  ErrorCode code{0};
  std::string message{};
  ErrorContext context{};
  SuggestedRecovery suggested_recovery{SuggestedRecovery::none};

  [[nodiscard]] bool operator==(const Error &) const = default;
};

} // namespace aurore
