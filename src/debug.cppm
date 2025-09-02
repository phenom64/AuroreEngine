/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;

#include <cstdint>
#include <stdexcept>
#include <string>
export module dreamrender:debug;

// Minimal debug utilities module. For environments without the Vulkan DebugUtils
// extension or where names are not essential, provide no-op overloads so call
// sites compile without extra dependency or platform conditionals.

export namespace dreamrender {
    template<typename... Args>
    inline void debugName(Args&&...) noexcept {}
}
