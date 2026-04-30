#pragma once

#ifndef LOG_HPP
#define LOG_HPP

// groklab::(info|warn|error|debug|critical)
//
// Goals:
// - C++23 friendly, header-only, no virtuals, no project-wide logging macros required at call sites.
// - Preserve the existing call pattern: groklab::info("... {}", x);
// - Allow compile-time disable via compiler flags (recommended) or a single macro.
//
// Compile-time switch:
//   -DLG_LOG_ENABLED=0   // hard-disables all logging calls (they become no-ops)
//   -DLG_LOG_ENABLED=1   // enables logging (default)
//
// Note on argument evaluation:
// With function-based logging, arguments are evaluated before the call.
// If you need *zero* overhead AND zero argument evaluation, you must wrap at the call site
// (e.g., macros or lazy lambdas). This header keeps call sites unchanged.

#include <utility>
#include <string_view>
#include <type_traits>
#include <string>

#ifndef LG_LOG_ENABLED
#define LG_LOG_ENABLED 1
#endif


namespace lg::log {
    inline constexpr bool enabled = (LG_LOG_ENABLED != 0);

    // Small helper for "unused" suppression without generating code.
    template<class... Ts>
    constexpr void swallow(Ts &&...) noexcept {
    }

    // Optional init/config surface (only meaningful when enabled).
    // Kept tiny so existing users don't need to adopt anything.
    inline void set_level(int /*spdlog::level::level_enum*/ level) noexcept;

    inline void set_pattern(std::string_view pattern) noexcept;

    inline void flush_on(int /*spdlog::level::level_enum*/ level) noexcept;
} // namespace lg::log


// Include spdlog only when logging is enabled.
#if LG_LOG_ENABLED
#include <spdlog/spdlog.h>
#endif

namespace lg {
    // --- Logging API (preserves existing names) ---

    template<typename... Args>
    inline void info(
#if LG_LOG_ENABLED
        spdlog::format_string_t<Args...> fmt,
#else
        std::string_view fmt,
#endif
        Args &&... args) {
        if constexpr (log::enabled) {
#if LG_LOG_ENABLED
            spdlog::info(fmt, std::forward<Args>(args)...);
#else
            log::swallow(fmt, std::forward<Args>(args)...);
#endif
        } else {
            log::swallow(fmt, std::forward<Args>(args)...);
        }
    }

    template<typename... Args>
    inline void warn(
#if LG_LOG_ENABLED
        spdlog::format_string_t<Args...> fmt,
#else
        std::string_view fmt,
#endif
        Args &&... args) {
        if constexpr (log::enabled) {
#if LG_LOG_ENABLED
            spdlog::warn(fmt, std::forward<Args>(args)...);
#else
            log::swallow(fmt, std::forward<Args>(args)...);
#endif
        } else {
            log::swallow(fmt, std::forward<Args>(args)...);
        }
    }

    template<typename... Args>
    inline void error(
#if LG_LOG_ENABLED
        spdlog::format_string_t<Args...> fmt,
#else
        std::string_view fmt,
#endif
        Args &&... args) {
        if constexpr (log::enabled) {
#if LG_LOG_ENABLED
            spdlog::error(fmt, std::forward<Args>(args)...);
#else
            log::swallow(fmt, std::forward<Args>(args)...);
#endif
        } else {
            log::swallow(fmt, std::forward<Args>(args)...);
        }
    }

    template<typename... Args>
    inline void debug(
#if LG_LOG_ENABLED
        spdlog::format_string_t<Args...> fmt,
#else
        std::string_view fmt,
#endif
        Args &&... args) {
        if constexpr (log::enabled) {
#if LG_LOG_ENABLED
            spdlog::debug(fmt, std::forward<Args>(args)...);
#else
            log::swallow(fmt, std::forward<Args>(args)...);
#endif
        } else {
            log::swallow(fmt, std::forward<Args>(args)...);
        }
    }

    template<typename... Args>
    inline void critical(
#if LG_LOG_ENABLED
        spdlog::format_string_t<Args...> fmt,
#else
        std::string_view fmt,
#endif
        Args &&... args) {
        if constexpr (log::enabled) {
#if LG_LOG_ENABLED
            spdlog::critical(fmt, std::forward<Args>(args)...);
#else
            log::swallow(fmt, std::forward<Args>(args)...);
#endif
        } else {
            log::swallow(fmt, std::forward<Args>(args)...);
        }
    }


    // --- Optional configuration (no-ops when disabled) ---

    namespace log {
        inline void set_level(int level) noexcept {
            if constexpr (enabled) {
#if LG_LOG_ENABLED
                spdlog::set_level(static_cast<spdlog::level::level_enum>(level));
#else
                (void) level;
#endif
            } else {
                (void) level;
            }
        }

        inline void set_pattern(std::string_view pattern) noexcept {
            if constexpr (enabled) {
#if LG_LOG_ENABLED
                spdlog::set_pattern(std::string(pattern));
#else
                (void) pattern;
#endif
            } else {
                (void) pattern;
            }
        }

        inline void flush_on(int level) noexcept {
            if constexpr (enabled) {
#if LG_LOG_ENABLED
                spdlog::flush_on(static_cast<spdlog::level::level_enum>(level));
#else
                (void) level;
#endif
            } else {
                (void) level;
            }
        }
    } // namespace log
} // namespace lg

#endif // LOG_HPP
