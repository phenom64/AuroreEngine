/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
module;

#include <spdlog/spdlog.h>
#include <spdlog/cfg/env.h>
#include <spdlog/cfg/argv.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/ranges.h>

export module spdlog;

export namespace spdlog {
    using spdlog::trace;
    using spdlog::debug;
    using spdlog::info;
    using spdlog::warn;
    using spdlog::error;
    using spdlog::critical;
    using spdlog::set_level;
    using spdlog::should_log;

    using spdlog::set_default_logger;
    using spdlog::default_logger;
    using spdlog::create;

    using spdlog::stdout_logger_st;
    using spdlog::stdout_logger_mt;
    using spdlog::stderr_logger_st;
    using spdlog::stderr_logger_mt;
    using spdlog::stdout_color_st;
    using spdlog::stdout_color_mt;
    using spdlog::stderr_color_st;
    using spdlog::stderr_color_mt;

    namespace level {
        using spdlog::level::trace;
        using spdlog::level::debug;
        using spdlog::level::info;
        using spdlog::level::warn;
        using spdlog::level::err;
        using spdlog::level::critical;
    }
    namespace cfg {
        using spdlog::cfg::load_env_levels;
        using spdlog::cfg::load_argv_levels;
    }
};

export namespace fmt {
    using fmt::underlying;
    using fmt::join;
}
