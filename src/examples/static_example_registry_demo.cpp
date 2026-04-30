#include "test/static_example_registry.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <string_view>

// ─── Example 1: Static example that passes ──────────────────────────────────

struct StaticPass {
    static constexpr std::string_view name() { return "static_pass"; }
    static constexpr std::string_view description() { return "A simple static example that always passes"; }

    static constexpr std::array<std::string_view, 2> tag_data{"core", "fast"};
    static constexpr std::span<const std::string_view> tags() { return tag_data; }

    static testfw::Result run() {
        // Simulate some computation
        constexpr auto expected = 42;
        constexpr auto computed = 6 * 7;
        if (computed != expected) {
            return testfw::fail("computation mismatch");
        }
        return {};
    }
};

// ─── Example 2: Stateful instance example with setup/run/teardown ───────────

struct StorageExample {
    static constexpr std::string_view name() { return "storage_lifecycle"; }
    static constexpr std::string_view description() { return "Instance example with setup, run, and teardown"; }

    static constexpr std::array<std::string_view, 2> tag_data{"storage", "lifecycle"};
    static constexpr std::span<const std::string_view> tags() { return tag_data; }

    std::unique_ptr<std::array<std::byte, 64>> buffer;

    testfw::Result setup() {
        buffer = std::make_unique<std::array<std::byte, 64>>();
        buffer->fill(std::byte{0xAB});
        if (!buffer) {
            return testfw::fail("failed to allocate buffer");
        }
        return {};
    }

    testfw::Result run() {
        // Verify setup worked correctly
        if ((*buffer)[0] != std::byte{0xAB}) {
            return testfw::fail("buffer not correctly initialized");
        }
        // Simulate writing and reading back
        (*buffer)[32] = std::byte{0xCD};
        if ((*buffer)[32] != std::byte{0xCD}) {
            return testfw::fail("buffer write/read mismatch");
        }
        return {};
    }

    testfw::Result teardown() {
        buffer.reset();
        return {};
    }
};

// ─── Example 3: Failing example for error reporting demonstration ───────────

struct FailingExample {
    static constexpr std::string_view name() { return "failing_example"; }
    static constexpr std::string_view description() { return "Demonstrates error reporting on failure"; }

    static constexpr std::array<std::string_view, 2> tag_data{"core", "negative"};
    static constexpr std::span<const std::string_view> tags() { return tag_data; }

    static testfw::Result run() {
        // Intentional failure to demonstrate error output
        return testfw::fail("intentional failure for demonstration");
    }
};

// ─── Registry & Main ────────────────────────────────────────────────────────

using ExampleRegistry = testfw::Registry<StaticPass, StorageExample, FailingExample>;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: examples [--list | --all | --filter <tag> | <name>]\n";
        return 1;
    }

    std::string_view arg{argv[1]};

    if (arg == "--list") {
        ExampleRegistry::print_list();
        return 0;
    }

    if (arg == "--all") {
        return ExampleRegistry::run_all();
    }

    if (arg == "--filter") {
        if (argc < 3) {
            std::cout << "Usage: examples [--list | --all | --filter <tag> | <name>]\n";
            return 1;
        }
        return ExampleRegistry::run_by_tag(std::string_view{argv[2]});
    }

    // Treat as example name
    return ExampleRegistry::run_by_name(arg);
}