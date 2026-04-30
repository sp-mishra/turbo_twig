#ifndef TESTFW_STATIC_EXAMPLE_REGISTRY_HPP
#define TESTFW_STATIC_EXAMPLE_REGISTRY_HPP

#include <algorithm>
#include <array>
#include <concepts>
#include <expected>
#include <format>
#include <iostream>
#include <source_location>
#include <span>
#include <string_view>

namespace testfw {

// ─── Result/Error model ─────────────────────────────────────────────────────

struct Error {
    std::string_view message;
    std::source_location where;
};

using Result = std::expected<void, Error>;

[[nodiscard]] inline constexpr auto fail(
    std::string_view message,
    std::source_location where = std::source_location::current()
) -> std::unexpected<Error> {
    return std::unexpected<Error>{Error{message, where}};
}

// ─── Concepts ───────────────────────────────────────────────────────────────

template <typename T>
concept HasStaticRun = requires {
    { T::run() } -> std::convertible_to<Result>;
};

template <typename T>
concept HasInstanceRun = !HasStaticRun<T> && requires(T t) {
    { t.run() } -> std::convertible_to<Result>;
};

template <typename T>
concept HasStaticSetup = requires {
    { T::setup() } -> std::convertible_to<Result>;
};

template <typename T>
concept HasInstanceSetup = !HasStaticSetup<T> && requires(T t) {
    { t.setup() } -> std::convertible_to<Result>;
};

template <typename T>
concept HasStaticTeardown = requires {
    { T::teardown() } -> std::convertible_to<Result>;
};

template <typename T>
concept HasInstanceTeardown = !HasStaticTeardown<T> && requires(T t) {
    { t.teardown() } -> std::convertible_to<Result>;
};

template <typename T>
concept ExampleType = requires {
    { T::name() } -> std::convertible_to<std::string_view>;
    { T::description() } -> std::convertible_to<std::string_view>;
    { T::tags() } -> std::convertible_to<std::span<const std::string_view>>;
} && (HasStaticRun<T> || HasInstanceRun<T>);

// ─── Registry ───────────────────────────────────────────────────────────────

template <ExampleType... Examples>
class Registry {
public:
    static int run_all() {
        int failed = 0;
        ((execute<Examples>() ? void() : void(++failed)), ...);
        return failed > 0 ? 2 : 0;
    }

    static int run_by_name(std::string_view target) {
        bool passed = true;
        bool found = (match_and_run<Examples>(target, passed) || ...);
        if (!found) {
            std::cout << std::format("Error: example '{}' not found\n", target);
            return 1;
        }
        return passed ? 0 : 2;
    }

    static int run_by_tag(std::string_view tag) {
        int failed = 0;
        bool found = false;
        (tag_run<Examples>(tag, failed, found), ...);
        if (!found) {
            std::cout << std::format("Error: no examples with tag '{}'\n", tag);
            return 1;
        }
        return failed > 0 ? 2 : 0;
    }

    static void print_list() {
        (print_entry<Examples>(), ...);
    }

private:
    // ─── Unified invocation with lifecycle ──────────────────────────────

    template <ExampleType T>
    static auto invoke() -> Result {
        if constexpr (HasStaticRun<T>) {
            return invoke_static<T>();
        } else {
            return invoke_instance<T>();
        }
    }

    template <ExampleType T>
    static auto invoke_static() -> Result {
        // Setup
        if constexpr (HasStaticSetup<T>) {
            Result setup_result = T::setup();
            if (!setup_result) return setup_result;
        }

        // Run
        Result run_result = T::run();

        // Teardown
        if constexpr (HasStaticTeardown<T>) {
            Result teardown_result = T::teardown();
            if (!run_result) return run_result;
            if (!teardown_result) return teardown_result;
            return {};
        } else {
            return run_result;
        }
    }

    template <ExampleType T>
    static auto invoke_instance() -> Result {
        T instance{};

        // Setup
        if constexpr (HasInstanceSetup<T>) {
            Result setup_result = instance.setup();
            if (!setup_result) return setup_result;
        }

        // Run
        Result run_result = instance.run();

        // Teardown
        if constexpr (HasInstanceTeardown<T>) {
            Result teardown_result = instance.teardown();
            if (!run_result) return run_result;
            if (!teardown_result) return teardown_result;
            return {};
        } else {
            return run_result;
        }
    }

    // ─── Execution with reporting ───────────────────────────────────────

    template <ExampleType T>
    static auto execute() -> bool {
        std::cout << std::format("[RUNNING] {}\n", std::string_view{T::name()});
        Result result = invoke<T>();
        if (result) {
            std::cout << std::format("[PASSED]  {}\n", std::string_view{T::name()});
            return true;
        } else {
            const auto& err = result.error();
            std::cout << std::format("[FAILED]  {}: {} ({}:{})\n",
                std::string_view{T::name()},
                err.message,
                err.where.file_name(),
                err.where.line());
            return false;
        }
    }

    // ─── Helpers ────────────────────────────────────────────────────────

    template <ExampleType T>
    static auto match_and_run(std::string_view target, bool& passed) -> bool {
        if (std::string_view{T::name()} == target) {
            if (!execute<T>()) passed = false;
            return true;
        }
        return false;
    }

    template <ExampleType T>
    static auto has_tag(std::string_view tag) -> bool {
        auto tags = std::span<const std::string_view>{T::tags()};
        return std::ranges::find(tags, tag) != tags.end();
    }

    template <ExampleType T>
    static void tag_run(std::string_view tag, int& failed, bool& found) {
        if (has_tag<T>(tag)) {
            found = true;
            if (!execute<T>()) ++failed;
        }
    }

    template <ExampleType T>
    static void print_entry() {
        auto tags = std::span<const std::string_view>{T::tags()};
        std::cout << std::format("  {:20s} - {} [", std::string_view{T::name()}, std::string_view{T::description()});
        for (std::size_t i = 0; i < tags.size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::cout << tags[i];
        }
        std::cout << "]\n";
    }
};

} // namespace testfw

#endif // TESTFW_STATIC_EXAMPLE_REGISTRY_HPP