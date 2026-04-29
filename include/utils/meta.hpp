#pragma once
// ============================================================================
// meta.hpp — C++23 Reflection & Compile-Time Metaprogramming System
// ============================================================================
// Single-header library for zero-overhead reflection, structural introspection,
// and compile-time computation. No macros. No virtual dispatch. No RTTI.
// ============================================================================

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <functional>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace meta {

// ============================================================================
//  SECTION 1: COMPILE-TIME DATA STRUCTURES & UTILITIES
// ============================================================================

// ---------------------------------------------------------------------------
// 1.1 fixed_string — compile-time string literal
// ---------------------------------------------------------------------------
template <std::size_t N>
struct fixed_string {
    char data[N]{};
    static constexpr std::size_t length = N - 1;

    consteval fixed_string() = default;

    consteval fixed_string(const char (&str)[N]) noexcept {
        for (std::size_t i = 0; i < N; ++i)
            data[i] = str[i];
    }

    [[nodiscard]] consteval std::string_view view() const noexcept {
        return {data, length};
    }

    [[nodiscard]] consteval char operator[](std::size_t i) const noexcept {
        return data[i];
    }

    template <std::size_t M>
    [[nodiscard]] consteval bool
    operator==(const fixed_string<M> &other) const noexcept {
        if constexpr (N != M) {
            return false;
        } else {
            for (std::size_t i = 0; i < N; ++i) {
                if (data[i] != other.data[i])
                    return false;
            }
            return true;
        }
    }

    template <std::size_t M>
    [[nodiscard]] consteval auto
    operator+(const fixed_string<M> &other) const noexcept {
        fixed_string<N + M - 1> result{};
        for (std::size_t i = 0; i < length; ++i)
            result.data[i] = data[i];
        for (std::size_t i = 0; i < M; ++i)
            result.data[length + i] = other.data[i];
        return result;
    }
};

template <std::size_t N>
fixed_string(const char (&)[N]) -> fixed_string<N>;

// ---------------------------------------------------------------------------
// 1.2 TypeList — heterogeneous type container
// ---------------------------------------------------------------------------
template <typename... Ts>
struct TypeList {
    static constexpr std::size_t size = sizeof...(Ts);

    template <std::size_t I>
    using element = std::tuple_element_t<I, std::tuple<Ts...>>;

    template <typename T>
    static consteval bool contains() noexcept {
        return (std::same_as<T, Ts> || ...);
    }

    template <typename T>
    static consteval std::size_t index_of() noexcept {
        std::size_t idx = 0;
        bool found = false;
        (
            [&] {
                if (!found && std::same_as<T, Ts>)
                    found = true;
                else if (!found)
                    ++idx;
            }(),
            ...);
        return idx;
    }
};

// ---------------------------------------------------------------------------
// 1.3 Sequence — descriptor container (core data structure)
// ---------------------------------------------------------------------------
template <typename... Descriptors>
struct Sequence {
    static constexpr std::size_t size = sizeof...(Descriptors);

    template <std::size_t I>
        requires(I < size)
    using element = std::tuple_element_t<I, std::tuple<Descriptors...>>;

    using as_type_list = TypeList<Descriptors...>;

    // Convenient index sequence spanning [0, size).
    using as_index_sequence = std::make_index_sequence<size>;

    // Compile-time bounds guard: has_element<I> is true iff element<I> exists.
    template <std::size_t I>
    static constexpr bool has_element = (I < size);

    // -----------------------------------------------------------------------
    // Member algorithm bridges
    //
    // These give callers a uniform object-style calling convention:
    //   Seq::for_each(fn)         — instead of  meta::for_each<Seq>(fn)
    //   Seq::transform(fn)        — instead of  meta::transform<Seq>(fn)
    //   Seq::fold(init, fn)       — instead of  meta::fold<Seq>(init, fn)
    //   Seq::count_if(pred)       — instead of  meta::count_if<Seq>(pred)
    //   Seq::find_if(pred)        — instead of  meta::find_if<Seq>(pred)
    //   Seq::find_by_name(sv)     — instead of  meta::find_by_name<Seq>(sv)
    //   Seq::contains_named(sv)   — (no direct free-function equivalent)
    //
    // The implementations mirror the fold-expression patterns in Section 7.
    // They do not forward-call those free functions to avoid a dependency on
    // code defined later in the file.  All empty-pack cases are safe:
    // comma-fold over an empty pack yields void (for_each, fold, count_if),
    // pack-expansion into a braced-init-list gives std::tuple<> (transform).
    // -----------------------------------------------------------------------

    /// Invoke fn(D{}) for each descriptor D, left-to-right.
    template <typename Fn>
    static constexpr void for_each(Fn &&fn) {
        (fn(Descriptors{}), ...);
    }

    /// Return std::tuple{fn(D0{}), fn(D1{}), ...}.
    template <typename Fn>
    static constexpr auto transform(Fn &&fn) {
        return std::tuple{fn(Descriptors{})...};
    }

    /// Left-fold: accumulate result = fn(result, D{}) for each descriptor.
    template <typename Init, typename Fn>
    static constexpr auto fold(Init init, Fn &&fn) {
        auto result = std::move(init);
        ((result = fn(std::move(result), Descriptors{})), ...);
        return result;
    }

    /// Count descriptors for which pred(D{}) is true.
    template <typename Pred>
    static consteval std::size_t count_if(Pred pred) {
        std::size_t n = 0;
        ((pred(Descriptors{}) ? ++n : n), ...);
        return n;
    }

    /// Index of the first descriptor for which pred(D{}) is true.
    /// Returns `size` as the not-found sentinel.
    template <typename Pred>
    static consteval std::size_t find_if(Pred pred) {
        std::size_t result = size;
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            bool found = false;
            (((!found &&
               pred(std::tuple_element_t<I, std::tuple<Descriptors...>>{}))
                  ? (result = I, found = true)
                  : false),
             ...);
        }(as_index_sequence{});
        return result;
    }

    /// Index of the first descriptor whose name() == sv.
    /// Returns `size` as the not-found sentinel.
    static consteval std::size_t find_by_name(std::string_view sv) {
        std::size_t result = size;
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            bool found = false;
            (((!found &&
               std::tuple_element_t<I, std::tuple<Descriptors...>>::name() == sv)
                  ? (result = I, found = true)
                  : false),
             ...);
        }(as_index_sequence{});
        return result;
    }

    /// True if any descriptor has name() == sv.
    static consteval bool contains_named(std::string_view sv) {
        return find_by_name(sv) != size;
    }
};

// ---------------------------------------------------------------------------
// 1.4 compile-time pair and map
// ---------------------------------------------------------------------------
template <typename Key, typename Value>
struct ct_pair {
    using key_type   = Key;
    using value_type = Value;
};

template <fixed_string Key, typename Value>
struct named_entry {
    static constexpr auto key = Key;
    using value_type           = Value;
};

template <typename... Entries>
struct ct_map {
    static constexpr std::size_t size = sizeof...(Entries);

    template <std::size_t I>
        requires(I < size)
    using entry_at = std::tuple_element_t<I, std::tuple<Entries...>>;

    template <fixed_string Key>
    static consteval bool contains() noexcept {
        return ((Entries::key == Key) || ...);
    }

    // lookup by string key — returns the value_type of the first match
    template <fixed_string Key>
    struct lookup {
    private:
        template <typename Entry, typename... Rest>
        struct finder;

        template <typename Entry>
            requires(Entry::key == Key)
        struct finder<Entry> {
            using type = typename Entry::value_type;
        };

        template <typename Entry>
            requires(Entry::key != Key)
        struct finder<Entry> {
            // static_assert(false) would fail; this branch shouldn't be reached
        };

        template <typename Entry, typename Next, typename... Rest>
            requires(Entry::key == Key)
        struct finder<Entry, Next, Rest...> {
            using type = typename Entry::value_type;
        };

        template <typename Entry, typename Next, typename... Rest>
            requires(Entry::key != Key)
        struct finder<Entry, Next, Rest...> : finder<Next, Rest...> {};

    public:
        using type = typename finder<Entries...>::type;
    };

    template <fixed_string Key>
    using lookup_t = typename lookup<Key>::type;
};

// ---------------------------------------------------------------------------
// 1.5 value_list — compile-time value container
// ---------------------------------------------------------------------------
template <auto... Vs>
struct value_list {
    static constexpr std::size_t size = sizeof...(Vs);

    template <std::size_t I>
    static consteval auto get() noexcept {
        constexpr auto arr =
            std::array{static_cast<std::common_type_t<decltype(Vs)...>>(Vs)...};
        return arr[I];
    }
};

// ---------------------------------------------------------------------------
// 1.6 ct_array — constexpr fixed-capacity array
// ---------------------------------------------------------------------------
template <typename T, std::size_t Capacity>
struct ct_array {
    T        elems[Capacity]{};
    std::size_t count = 0;

    consteval void push_back(const T &v) noexcept {
        elems[count++] = v;
    }

    [[nodiscard]] consteval T operator[](std::size_t i) const noexcept {
        return elems[i];
    }

    [[nodiscard]] consteval std::size_t size() const noexcept {
        return count;
    }

    [[nodiscard]] consteval const T *begin() const noexcept {
        return elems;
    }

    [[nodiscard]] consteval const T *end() const noexcept {
        return elems + count;
    }
};

// ============================================================================
//  SECTION 2: CONCEPTS & CORE TRAITS
// ============================================================================

// ---------------------------------------------------------------------------
// 2.1 Core concepts
// ---------------------------------------------------------------------------
template <typename T>
concept Aggregate = std::is_aggregate_v<std::remove_cvref_t<T>>;

template <typename T>
concept StandardLayout =
    std::is_standard_layout_v<std::remove_cvref_t<T>>;

template <typename T>
concept TriviallyCopyable =
    std::is_trivially_copyable_v<std::remove_cvref_t<T>>;

template <typename E>
concept MetaEnum = std::is_enum_v<E>;

// concept for checking if a type has custom reflection
template <typename T>
struct type_tag {};

namespace detail {
// Fallback reflect_members — returns a sentinel type when no custom reflection
// exists. This ensures the name is visible for unqualified lookup in the
// concept check. User-defined ADL-discovered reflect_members (e.g., hidden
// friends) will be preferred by overload resolution as non-template matches.
struct no_custom_reflection {};

template <typename T>
consteval no_custom_reflection reflect_members(type_tag<T>) { return {}; }

template <typename T>
concept HasCustomReflection = requires {
    { reflect_members(type_tag<T>{}) };
    requires !std::same_as<decltype(reflect_members(type_tag<T>{})),
                           no_custom_reflection>;
};

// Helper to invoke reflect_members with proper ADL + fallback visibility
template <typename T>
consteval auto invoke_reflect_members() {
    return reflect_members(type_tag<T>{});
}
} // namespace detail

template <typename T>
concept Reflectable =
    Aggregate<T> || detail::HasCustomReflection<T>;

// concept for field descriptor
template <typename D>
concept FieldDescriptorLike = requires {
    { D::index() } -> std::convertible_to<std::size_t>;
    typename D::owner_type;
    { D::name() } -> std::convertible_to<std::string_view>;
    typename D::declared_type;
    typename D::value_type;
    { D::is_synthetic() } -> std::convertible_to<bool>;
};

// concept for a sequence of descriptors
template <typename S>
concept DescriptorSequence = requires {
    { S::size } -> std::convertible_to<std::size_t>;
};

// ============================================================================
//  SECTION 3: COMPILER ADAPTATION LAYER
// ============================================================================

namespace detail::compiler {

enum class compiler_id { clang, gcc, msvc, unknown };

consteval compiler_id detect() noexcept {
#if defined(__clang__)
    return compiler_id::clang;
#elif defined(__GNUC__)
    return compiler_id::gcc;
#elif defined(_MSC_VER)
    return compiler_id::msvc;
#else
    return compiler_id::unknown;
#endif
}

inline constexpr compiler_id current = detect();

// Extract type name from compiler-specific function signature
template <typename T>
consteval std::string_view type_name_raw() noexcept {
#if defined(__clang__)
    // __PRETTY_FUNCTION__ = "std::string_view meta::detail::compiler::type_name_raw() [T = ...]"
    std::string_view sv = __PRETTY_FUNCTION__;
    auto start          = sv.find("T = ") + 4;
    auto end            = sv.rfind(']');
    return sv.substr(start, end - start);
#elif defined(__GNUC__)
    std::string_view sv = __PRETTY_FUNCTION__;
    auto start          = sv.find("T = ") + 4;
    auto end            = sv.rfind(';');
    if (end == std::string_view::npos)
        end = sv.rfind(']');
    return sv.substr(start, end - start);
#elif defined(_MSC_VER)
    std::string_view sv = __FUNCSIG__;
    auto start          = sv.find("type_name_raw<") + 14;
    auto end            = sv.rfind(">(void)");
    return sv.substr(start, end - start);
#else
    return "unknown";
#endif
}

// Extract enum value name from compiler-specific function signature
template <auto V>
    requires std::is_enum_v<decltype(V)>
consteval std::string_view enum_name_raw() noexcept {
#if defined(__clang__)
    std::string_view sv = __PRETTY_FUNCTION__;
    auto start          = sv.find("V = ") + 4;
    auto end            = sv.rfind(']');
    return sv.substr(start, end - start);
#elif defined(__GNUC__)
    std::string_view sv = __PRETTY_FUNCTION__;
    auto start          = sv.find("V = ") + 4;
    auto end            = sv.rfind(';');
    if (end == std::string_view::npos)
        end = sv.rfind(']');
    return sv.substr(start, end - start);
#elif defined(_MSC_VER)
    std::string_view sv = __FUNCSIG__;
    auto start          = sv.find("enum_name_raw<") + 14;
    auto end            = sv.rfind(">(void)");
    return sv.substr(start, end - start);
#else
    return "unknown";
#endif
}

// Clean enum name: strip scope prefix to get just the enumerator name
consteval std::string_view clean_enum_name(std::string_view raw) noexcept {
    // Trim lightweight compiler formatting noise around pretty-function payloads.
    while (!raw.empty() && (raw.front() == ' ' || raw.front() == '\t'))
        raw.remove_prefix(1);
    while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t'))
        raw.remove_suffix(1);

    auto pos = raw.rfind("::");
    if (pos != std::string_view::npos)
        raw = raw.substr(pos + 2);

    while (!raw.empty() && (raw.front() == ' ' || raw.front() == '\t'))
        raw.remove_prefix(1);
    while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t'))
        raw.remove_suffix(1);

    return raw;
}

consteval bool looks_like_cast_spelling(std::string_view raw) noexcept {
    // Common cast-like spellings include parentheses or explicit cast tokens.
    if (raw.find('(') != std::string_view::npos ||
        raw.find(')') != std::string_view::npos)
        return true;
    if (raw.find("static_cast") != std::string_view::npos)
        return true;
    if (raw.find("reinterpret_cast") != std::string_view::npos)
        return true;
    if (raw.find("const_cast") != std::string_view::npos)
        return true;

    // Heuristic: if the raw spelling contains digits but no alphabetic
    // characters (excluding underscore), it is likely a numeric probe such as
    // "(E)123" or "123" rather than a valid identifier. Treat that as a
    // cast-like/spelling artifact.
    bool has_digit = false;
    bool has_alpha = false;
    for (char c : raw) {
        if (c >= '0' && c <= '9')
            has_digit = true;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_')
            has_alpha = true;
    }
    if (has_digit && !has_alpha)
        return true;

    return false;
}

// Check if an enum value has a valid name (not a cast expression)
// Enhanced validation for Apple Clang and other compilers
consteval bool is_valid_enum_name(std::string_view name) noexcept {
    if (name.empty())
        return false;

    // Invalid names typically start with '(' for cast expressions
    if (name[0] == '(')
        return false;

    // Clang-family pretty-function output can spell out-of-range probes as
    // cast-like forms (e.g. "(E)123") instead of identifiers. Use a stricter
    // heuristic on Clang to reject these artifacts (parentheses, explicit
    // cast tokens, or numeric probes lacking alphabetic characters). This
    // preserves normal behavior on GCC/MSVC while avoiding false positives
    // on Clang-family toolchains.
#if defined(__clang__)
    if (looks_like_cast_spelling(name))
        return false;
#endif

    // Extract the identifier portion after scope resolution
    auto clean = clean_enum_name(name);

    if (clean.empty())
        return false;

    // Check for invalid patterns that may appear in compiler output
    // - Starts with a digit (invalid C++ identifier)
    // - Contains only digits (likely a numeric cast representation)
    if (clean[0] >= '0' && clean[0] <= '9')
        return false;

    // A valid C++ enumerator name starts with letter or underscore
    if (!((clean[0] >= 'A' && clean[0] <= 'Z') ||
          (clean[0] >= 'a' && clean[0] <= 'z') ||
          clean[0] == '_'))
        return false;

    // Additional validation: ensure it's a valid identifier throughout
    // (not just checking the first character)
    for (char c : clean) {
        bool is_valid_char = (c >= 'A' && c <= 'Z') ||
                            (c >= 'a' && c <= 'z') ||
                            (c >= '0' && c <= '9') ||
                            c == '_';
        if (!is_valid_char)
            return false;
    }

    return true;
}

} // namespace detail::compiler

// Public API for type name
template <typename T>
consteval std::string_view type_name() noexcept {
    return detail::compiler::type_name_raw<T>();
}

// ============================================================================
//  SECTION 4: REFLECTION — AGGREGATE DECOMPOSITION ENGINE
// ============================================================================

namespace detail::aggregate {

// Phase 1: Field Counting
// -----------------------
// Uses brace-init probing over all arities and picks the largest constructible
// arity. This stays valid for aggregates with reference members where the
// constructibility predicate is not monotonic.

struct AnyType {
    // Single lvalue-reference conversion avoids ambiguous value conversions
    // while still allowing probes for value and reference fields.
    template <typename T>
        requires(!std::is_aggregate_v<T>)
    consteval operator T &() const noexcept; // not defined; used in unevaluated context
};

template <typename T, std::size_t... I>
consteval bool is_constructible_n(std::index_sequence<I...>) {
    AnyType any{};
    return requires { T{(void(I), any)...}; };
}

// Binary search for field count (O(log N) instantiation depth)
template <typename T, std::size_t Lo, std::size_t Hi>
consteval std::size_t count_fields_binary() {
    if constexpr (Lo == Hi) {
        return Lo;
    } else {
        constexpr std::size_t Mid = Lo + (Hi - Lo + 1) / 2;
        if constexpr (is_constructible_n<T>(
                           std::make_index_sequence<Mid>{})) {
            return count_fields_binary<T, Mid, Hi>();
        } else {
            return count_fields_binary<T, Lo, Mid - 1>();
        }
    }
}

// ---------------------------------------------------------------------------
// aggregate_limits — single source of truth for aggregate field limits.
// Increase aggregate_limits::max_fields here (and extend apply_aggregate /
// exact_types to match) to support aggregates with more members.
// ---------------------------------------------------------------------------
struct aggregate_limits {
    static constexpr std::size_t max_fields = 32;
};

// Backward-compatible aliases — prefer aggregate_limits::max_fields in new code.
inline constexpr std::size_t max_aggregate_fields = aggregate_limits::max_fields;
inline constexpr std::size_t max_fields            = aggregate_limits::max_fields; // backward compat

template <typename T>
consteval std::size_t count_fields();

template <typename T, std::size_t N = 0>
consteval std::size_t count_fields_linear() {
    if constexpr (N > aggregate_limits::max_fields) {
        return 0;
    } else {
        constexpr std::size_t tail = count_fields_linear<T, N + 1>();
        if constexpr (is_constructible_n<T>(std::make_index_sequence<N>{})) {
            return N > tail ? N : tail;
        } else {
            return tail;
        }
    }
}

// ---------------------------------------------------------------------------
// Phase 2: Unified Structured Binding Engine — apply_aggregate<N>
// ---------------------------------------------------------------------------
// NOTE:
// This is the ONLY place where structured binding expansion is implemented.
// All aggregate decomposition logic must go through apply_aggregate<N>.
// Do NOT add structured-binding N-expansion logic anywhere else in this file.
// ---------------------------------------------------------------------------

template <std::size_t N, typename T, typename F>
constexpr decltype(auto) apply_aggregate(T &&obj, F &&fn) {
    static_assert(N <= aggregate_limits::max_fields,
        "meta::reflect failed: aggregate field count exceeds supported limit. "
        "Increase limit or use custom reflection.");
    if constexpr (N == 0) {
        return std::forward<F>(fn)();
    } else if constexpr (N == 1) {
        auto &&[f0] = obj;
        return std::forward<F>(fn)(f0);
    } else if constexpr (N == 2) {
        auto &&[f0, f1] = obj;
        return std::forward<F>(fn)(f0, f1);
    } else if constexpr (N == 3) {
        auto &&[f0, f1, f2] = obj;
        return std::forward<F>(fn)(f0, f1, f2);
    } else if constexpr (N == 4) {
        auto &&[f0, f1, f2, f3] = obj;
        return std::forward<F>(fn)(f0, f1, f2, f3);
    } else if constexpr (N == 5) {
        auto &&[f0, f1, f2, f3, f4] = obj;
        return std::forward<F>(fn)(f0, f1, f2, f3, f4);
    } else if constexpr (N == 6) {
        auto &&[f0, f1, f2, f3, f4, f5] = obj;
        return std::forward<F>(fn)(f0, f1, f2, f3, f4, f5);
    } else if constexpr (N == 7) {
        auto &&[f0, f1, f2, f3, f4, f5, f6] = obj;
        return std::forward<F>(fn)(f0, f1, f2, f3, f4, f5, f6);
    } else if constexpr (N == 8) {
        auto &&[f0, f1, f2, f3, f4, f5, f6, f7] = obj;
        return std::forward<F>(fn)(f0, f1, f2, f3, f4, f5, f6, f7);
    } else if constexpr (N == 9) {
        auto &&[f0, f1, f2, f3, f4, f5, f6, f7, f8] = obj;
        return std::forward<F>(fn)(f0, f1, f2, f3, f4, f5, f6, f7, f8);
    } else if constexpr (N == 10) {
        auto &&[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9] = obj;
        return std::forward<F>(fn)(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9);
    } else if constexpr (N == 11) {
        auto &&[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10] = obj;
        return std::forward<F>(fn)(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10);
    } else if constexpr (N == 12) {
        auto &&[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11] = obj;
        return std::forward<F>(fn)(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11);
    } else if constexpr (N == 13) {
        auto &&[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12] = obj;
        return std::forward<F>(fn)(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12);
    } else if constexpr (N == 14) {
        auto &&[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13] = obj;
        return std::forward<F>(fn)(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13);
    } else if constexpr (N == 15) {
        auto &&[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14] = obj;
        return std::forward<F>(fn)(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14);
    } else if constexpr (N == 16) {
        auto &&[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15] = obj;
        return std::forward<F>(fn)(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15);
    } else if constexpr (N == 17) {
        auto &&[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16] = obj;
        return std::forward<F>(fn)(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16);
    } else if constexpr (N == 18) {
        auto &&[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17] = obj;
        return std::forward<F>(fn)(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17);
    } else if constexpr (N == 19) {
        auto &&[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18] = obj;
        return std::forward<F>(fn)(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18);
    } else if constexpr (N == 20) {
        auto &&[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19] = obj;
        return std::forward<F>(fn)(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19);
    } else if constexpr (N == 21) {
        auto &&[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20] = obj;
        return std::forward<F>(fn)(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20);
    } else if constexpr (N == 22) {
        auto &&[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21] = obj;
        return std::forward<F>(fn)(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21);
    } else if constexpr (N == 23) {
        auto &&[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22] = obj;
        return std::forward<F>(fn)(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22);
    } else if constexpr (N == 24) {
        auto &&[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23] = obj;
        return std::forward<F>(fn)(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23);
    } else if constexpr (N == 25) {
        auto &&[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24] = obj;
        return std::forward<F>(fn)(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24);
    } else if constexpr (N == 26) {
        auto &&[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25] = obj;
        return std::forward<F>(fn)(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25);
    } else if constexpr (N == 27) {
        auto &&[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26] = obj;
        return std::forward<F>(fn)(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26);
    } else if constexpr (N == 28) {
        auto &&[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27] = obj;
        return std::forward<F>(fn)(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27);
    } else if constexpr (N == 29) {
        auto &&[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28] = obj;
        return std::forward<F>(fn)(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28);
    } else if constexpr (N == 30) {
        auto &&[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28, f29] = obj;
        return std::forward<F>(fn)(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28, f29);
    } else if constexpr (N == 31) {
        auto &&[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28, f29, f30] = obj;
        return std::forward<F>(fn)(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28, f29, f30);
    } else if constexpr (N == 32) {
        auto &&[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28, f29, f30, f31] = obj;
        return std::forward<F>(fn)(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28, f29, f30, f31);
    }
}

// Tuple of forwarded references — thin wrapper over apply_aggregate.
// Replaces the former tie_impl; do NOT add N-expansion logic here.
template <std::size_t N, typename T>
constexpr auto aggregate_as_tuple(T &&obj) {
    return apply_aggregate<N>(std::forward<T>(obj),
        []<typename... Fs>(Fs &&...fs) -> decltype(auto) {
            return std::forward_as_tuple(std::forward<Fs>(fs)...);
        });
}


template <typename T>
consteval std::size_t count_fields() {
    static_assert(std::is_aggregate_v<T>,
                  "meta: count_fields requires an aggregate type");

    constexpr std::size_t result = count_fields_binary<T, 0, aggregate_limits::max_fields>();
    static_assert(result < aggregate_limits::max_fields,
                  "meta::reflect failed: aggregate field count exceeds supported limit. "
                  "Increase limit or use custom reflection.");
    return result;
}

} // namespace detail::aggregate

// Phase 3: Descriptor Integration
// --------------------------------
namespace detail {

// Extract the declared member type directly from a pointer-to-member.
template <typename>
struct member_pointer_value;

template <typename C, typename M>
struct member_pointer_value<M C::*> {
    using type = M;
};

// Aggregate decomposition uses synthetic positional names in v1.
//
// These names are placeholders generated by the reflection engine to refer to
// positional fields (field_0, field_1, ...). They are NOT the real source
// identifiers and should not be treated as semantic field names. Users who
// require stable or semantic names should provide custom (ADL) reflection via
// `reflect_members`.
template <std::size_t Index>
consteval std::string_view synthetic_field_name() noexcept {
    // Compile-time array lookup: replaces the 32-branch if constexpr chain.
    // The compiler can reduce in-range accesses to a simple offset into the
    // read-only data section, avoiding repeated template instantiation overhead.
    constexpr std::array<std::string_view, 32> names = {
        "field_0",  "field_1",  "field_2",  "field_3",
        "field_4",  "field_5",  "field_6",  "field_7",
        "field_8",  "field_9",  "field_10", "field_11",
        "field_12", "field_13", "field_14", "field_15",
        "field_16", "field_17", "field_18", "field_19",
        "field_20", "field_21", "field_22", "field_23",
        "field_24", "field_25", "field_26", "field_27",
        "field_28", "field_29", "field_30", "field_31"
    };
    if constexpr (Index < names.size()) return names[Index];
    else return "field_N";
}

// ---------------------------------------------------------------------------
// impl::exact_types — private implementation detail of aggregate_info
// ---------------------------------------------------------------------------
// Used ONLY by aggregate_info<T>::exact_type_list.
// Do NOT call this function directly from outside aggregate_info.
// Access field types exclusively via:
//     aggregate_info<T>::exact_type_list
//
// Uses auto& binding so that decltype(fi) gives the exact declared type:
//   - reference member  int& ref  → decltype(fi) = int&
//   - const value member const int c → decltype(fi) = const int
// Do NOT replace this ladder with the lambda/apply_aggregate approach;
// forwarding-reference deduction cannot distinguish these two cases.
namespace impl {
template <std::size_t N, typename T>
consteval auto exact_types(T &sample) {
    if constexpr (N == 0) {
        return TypeList<>{};
    } else if constexpr (N == 1) {
        auto &[f0] = sample;
        return TypeList<decltype(f0)>{};
    } else if constexpr (N == 2) {
        auto &[f0, f1] = sample;
        return TypeList<decltype(f0), decltype(f1)>{};
    } else if constexpr (N == 3) {
        auto &[f0, f1, f2] = sample;
        return TypeList<decltype(f0), decltype(f1), decltype(f2)>{};
    } else if constexpr (N == 4) {
        auto &[f0, f1, f2, f3] = sample;
        return TypeList<decltype(f0), decltype(f1), decltype(f2), decltype(f3)>{};
    } else if constexpr (N == 5) {
        auto &[f0, f1, f2, f3, f4] = sample;
        return TypeList<decltype(f0), decltype(f1), decltype(f2), decltype(f3), decltype(f4)>{};
    } else if constexpr (N == 6) {
        auto &[f0, f1, f2, f3, f4, f5] = sample;
        return TypeList<decltype(f0), decltype(f1), decltype(f2), decltype(f3), decltype(f4), decltype(f5)>{};
    } else if constexpr (N == 7) {
        auto &[f0, f1, f2, f3, f4, f5, f6] = sample;
        return TypeList<decltype(f0), decltype(f1), decltype(f2), decltype(f3), decltype(f4), decltype(f5), decltype(f6)>{};
    } else if constexpr (N == 8) {
        auto &[f0, f1, f2, f3, f4, f5, f6, f7] = sample;
        return TypeList<decltype(f0), decltype(f1), decltype(f2), decltype(f3), decltype(f4), decltype(f5), decltype(f6), decltype(f7)>{};
    } else if constexpr (N == 9) {
        auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8] = sample;
        return TypeList<decltype(f0), decltype(f1), decltype(f2), decltype(f3), decltype(f4), decltype(f5), decltype(f6), decltype(f7), decltype(f8)>{};
    } else if constexpr (N == 10) {
        auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9] = sample;
        return TypeList<decltype(f0), decltype(f1), decltype(f2), decltype(f3), decltype(f4), decltype(f5), decltype(f6), decltype(f7), decltype(f8), decltype(f9)>{};
    } else if constexpr (N == 11) {
        auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10] = sample;
        return TypeList<decltype(f0), decltype(f1), decltype(f2), decltype(f3), decltype(f4), decltype(f5), decltype(f6), decltype(f7), decltype(f8), decltype(f9), decltype(f10)>{};
    } else if constexpr (N == 12) {
        auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11] = sample;
        return TypeList<decltype(f0), decltype(f1), decltype(f2), decltype(f3), decltype(f4), decltype(f5), decltype(f6), decltype(f7), decltype(f8), decltype(f9), decltype(f10), decltype(f11)>{};
    } else if constexpr (N == 13) {
        auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12] = sample;
        return TypeList<decltype(f0), decltype(f1), decltype(f2), decltype(f3), decltype(f4), decltype(f5), decltype(f6), decltype(f7), decltype(f8), decltype(f9), decltype(f10), decltype(f11), decltype(f12)>{};
    } else if constexpr (N == 14) {
        auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13] = sample;
        return TypeList<decltype(f0), decltype(f1), decltype(f2), decltype(f3), decltype(f4), decltype(f5), decltype(f6), decltype(f7), decltype(f8), decltype(f9), decltype(f10), decltype(f11), decltype(f12), decltype(f13)>{};
    } else if constexpr (N == 15) {
        auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14] = sample;
        return TypeList<decltype(f0), decltype(f1), decltype(f2), decltype(f3), decltype(f4), decltype(f5), decltype(f6), decltype(f7), decltype(f8), decltype(f9), decltype(f10), decltype(f11), decltype(f12), decltype(f13), decltype(f14)>{};
    } else if constexpr (N == 16) {
        auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15] = sample;
        return TypeList<decltype(f0), decltype(f1), decltype(f2), decltype(f3), decltype(f4), decltype(f5), decltype(f6), decltype(f7), decltype(f8), decltype(f9), decltype(f10), decltype(f11), decltype(f12), decltype(f13), decltype(f14), decltype(f15)>{};
    } else if constexpr (N == 17) {
        auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16] = sample;
        return TypeList<decltype(f0), decltype(f1), decltype(f2), decltype(f3), decltype(f4), decltype(f5), decltype(f6), decltype(f7), decltype(f8), decltype(f9), decltype(f10), decltype(f11), decltype(f12), decltype(f13), decltype(f14), decltype(f15), decltype(f16)>{};
    } else if constexpr (N == 18) {
        auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17] = sample;
        return TypeList<decltype(f0), decltype(f1), decltype(f2), decltype(f3), decltype(f4), decltype(f5), decltype(f6), decltype(f7), decltype(f8), decltype(f9), decltype(f10), decltype(f11), decltype(f12), decltype(f13), decltype(f14), decltype(f15), decltype(f16), decltype(f17)>{};
    } else if constexpr (N == 19) {
        auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18] = sample;
        return TypeList<decltype(f0), decltype(f1), decltype(f2), decltype(f3), decltype(f4), decltype(f5), decltype(f6), decltype(f7), decltype(f8), decltype(f9), decltype(f10), decltype(f11), decltype(f12), decltype(f13), decltype(f14), decltype(f15), decltype(f16), decltype(f17), decltype(f18)>{};
    } else if constexpr (N == 20) {
        auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19] = sample;
        return TypeList<decltype(f0), decltype(f1), decltype(f2), decltype(f3), decltype(f4), decltype(f5), decltype(f6), decltype(f7), decltype(f8), decltype(f9), decltype(f10), decltype(f11), decltype(f12), decltype(f13), decltype(f14), decltype(f15), decltype(f16), decltype(f17), decltype(f18), decltype(f19)>{};
    } else if constexpr (N == 21) {
        auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20] = sample;
        return TypeList<decltype(f0), decltype(f1), decltype(f2), decltype(f3), decltype(f4), decltype(f5), decltype(f6), decltype(f7), decltype(f8), decltype(f9), decltype(f10), decltype(f11), decltype(f12), decltype(f13), decltype(f14), decltype(f15), decltype(f16), decltype(f17), decltype(f18), decltype(f19), decltype(f20)>{};
    } else if constexpr (N == 22) {
        auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21] = sample;
        return TypeList<decltype(f0), decltype(f1), decltype(f2), decltype(f3), decltype(f4), decltype(f5), decltype(f6), decltype(f7), decltype(f8), decltype(f9), decltype(f10), decltype(f11), decltype(f12), decltype(f13), decltype(f14), decltype(f15), decltype(f16), decltype(f17), decltype(f18), decltype(f19), decltype(f20), decltype(f21)>{};
    } else if constexpr (N == 23) {
        auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22] = sample;
        return TypeList<decltype(f0), decltype(f1), decltype(f2), decltype(f3), decltype(f4), decltype(f5), decltype(f6), decltype(f7), decltype(f8), decltype(f9), decltype(f10), decltype(f11), decltype(f12), decltype(f13), decltype(f14), decltype(f15), decltype(f16), decltype(f17), decltype(f18), decltype(f19), decltype(f20), decltype(f21), decltype(f22)>{};
    } else if constexpr (N == 24) {
        auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23] = sample;
        return TypeList<decltype(f0), decltype(f1), decltype(f2), decltype(f3), decltype(f4), decltype(f5), decltype(f6), decltype(f7), decltype(f8), decltype(f9), decltype(f10), decltype(f11), decltype(f12), decltype(f13), decltype(f14), decltype(f15), decltype(f16), decltype(f17), decltype(f18), decltype(f19), decltype(f20), decltype(f21), decltype(f22), decltype(f23)>{};
    } else if constexpr (N == 25) {
        auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24] = sample;
        return TypeList<decltype(f0), decltype(f1), decltype(f2), decltype(f3), decltype(f4), decltype(f5), decltype(f6), decltype(f7), decltype(f8), decltype(f9), decltype(f10), decltype(f11), decltype(f12), decltype(f13), decltype(f14), decltype(f15), decltype(f16), decltype(f17), decltype(f18), decltype(f19), decltype(f20), decltype(f21), decltype(f22), decltype(f23), decltype(f24)>{};
    } else if constexpr (N == 26) {
        auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25] = sample;
        return TypeList<decltype(f0), decltype(f1), decltype(f2), decltype(f3), decltype(f4), decltype(f5), decltype(f6), decltype(f7), decltype(f8), decltype(f9), decltype(f10), decltype(f11), decltype(f12), decltype(f13), decltype(f14), decltype(f15), decltype(f16), decltype(f17), decltype(f18), decltype(f19), decltype(f20), decltype(f21), decltype(f22), decltype(f23), decltype(f24), decltype(f25)>{};
    } else if constexpr (N == 27) {
        auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26] = sample;
        return TypeList<decltype(f0), decltype(f1), decltype(f2), decltype(f3), decltype(f4), decltype(f5), decltype(f6), decltype(f7), decltype(f8), decltype(f9), decltype(f10), decltype(f11), decltype(f12), decltype(f13), decltype(f14), decltype(f15), decltype(f16), decltype(f17), decltype(f18), decltype(f19), decltype(f20), decltype(f21), decltype(f22), decltype(f23), decltype(f24), decltype(f25), decltype(f26)>{};
    } else if constexpr (N == 28) {
        auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27] = sample;
        return TypeList<decltype(f0), decltype(f1), decltype(f2), decltype(f3), decltype(f4), decltype(f5), decltype(f6), decltype(f7), decltype(f8), decltype(f9), decltype(f10), decltype(f11), decltype(f12), decltype(f13), decltype(f14), decltype(f15), decltype(f16), decltype(f17), decltype(f18), decltype(f19), decltype(f20), decltype(f21), decltype(f22), decltype(f23), decltype(f24), decltype(f25), decltype(f26), decltype(f27)>{};
    } else if constexpr (N == 29) {
        auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28] = sample;
        return TypeList<decltype(f0), decltype(f1), decltype(f2), decltype(f3), decltype(f4), decltype(f5), decltype(f6), decltype(f7), decltype(f8), decltype(f9), decltype(f10), decltype(f11), decltype(f12), decltype(f13), decltype(f14), decltype(f15), decltype(f16), decltype(f17), decltype(f18), decltype(f19), decltype(f20), decltype(f21), decltype(f22), decltype(f23), decltype(f24), decltype(f25), decltype(f26), decltype(f27), decltype(f28)>{};
    } else if constexpr (N == 30) {
        auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28, f29] = sample;
        return TypeList<decltype(f0), decltype(f1), decltype(f2), decltype(f3), decltype(f4), decltype(f5), decltype(f6), decltype(f7), decltype(f8), decltype(f9), decltype(f10), decltype(f11), decltype(f12), decltype(f13), decltype(f14), decltype(f15), decltype(f16), decltype(f17), decltype(f18), decltype(f19), decltype(f20), decltype(f21), decltype(f22), decltype(f23), decltype(f24), decltype(f25), decltype(f26), decltype(f27), decltype(f28), decltype(f29)>{};
    } else if constexpr (N == 31) {
        auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28, f29, f30] = sample;
        return TypeList<decltype(f0), decltype(f1), decltype(f2), decltype(f3), decltype(f4), decltype(f5), decltype(f6), decltype(f7), decltype(f8), decltype(f9), decltype(f10), decltype(f11), decltype(f12), decltype(f13), decltype(f14), decltype(f15), decltype(f16), decltype(f17), decltype(f18), decltype(f19), decltype(f20), decltype(f21), decltype(f22), decltype(f23), decltype(f24), decltype(f25), decltype(f26), decltype(f27), decltype(f28), decltype(f29), decltype(f30)>{};
    } else if constexpr (N == 32) {
        auto &[f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28, f29, f30, f31] = sample;
        return TypeList<decltype(f0), decltype(f1), decltype(f2), decltype(f3), decltype(f4), decltype(f5), decltype(f6), decltype(f7), decltype(f8), decltype(f9), decltype(f10), decltype(f11), decltype(f12), decltype(f13), decltype(f14), decltype(f15), decltype(f16), decltype(f17), decltype(f18), decltype(f19), decltype(f20), decltype(f21), decltype(f22), decltype(f23), decltype(f24), decltype(f25), decltype(f26), decltype(f27), decltype(f28), decltype(f29), decltype(f30), decltype(f31)>{};
    }
}

} // namespace impl

// ---------------------------------------------------------------------------
// aggregate_info<T> — centralized metadata cache for aggregate type T
// ---------------------------------------------------------------------------
// Instantiated at most once per T.  All aggregate reflection paths use this
// struct rather than calling count_fields or exact_types directly.
template <typename T>
    requires std::is_aggregate_v<T>
struct aggregate_info {
    // Cached field count — computed via binary/linear constructibility search.
    static constexpr std::size_t field_count =
        aggregate::count_fields<T>();

    // Cached declared-type list — value types of all fields.
    // Computed via impl::exact_types, which is the sole owner of the
    // structured-binding type extraction logic.  All external code must
    // access field types through this alias, never by calling impl::exact_types
    // directly.
    using exact_type_list =
        decltype(impl::exact_types<field_count>(std::declval<T &>()));
};

// Deduce the exact type of the I-th field of aggregate T
template <typename T, std::size_t I>
struct aggregate_field_type {
    using type = typename aggregate_info<T>::exact_type_list::template element<I>;
};

template <typename T, std::size_t I>
using aggregate_field_type_t = typename aggregate_field_type<T, I>::type;

template <std::size_t Index, typename T>
constexpr decltype(auto) get_field(T &&obj) noexcept {
    using U = std::remove_cvref_t<T>;
    // Delegate directly to apply_aggregate so that projection and
    // decomposition share the same single structured-binding backend.
    // The lambda receives all fields as forwarding references and picks
    // element Index via std::forward_as_tuple, preserving value category
    // without materialising an intermediate aggregate-as-tuple object.
    return aggregate::apply_aggregate<aggregate_info<U>::field_count>(
        std::forward<T>(obj),
        []<typename... Fs>(Fs &&...fs) -> decltype(auto) {
            return std::get<Index>(
                std::forward_as_tuple(std::forward<Fs>(fs)...));
        });
}

} // namespace detail

// ---------------------------------------------------------------------------
// AggregateDecomposable — validates a type can be decomposed as an aggregate
// ---------------------------------------------------------------------------
// Concept that fires at the earliest possible point to give a clean,
// single-line diagnostic instead of a cascade of template errors.
// A type satisfies AggregateDecomposable if:
//   (a) it satisfies Aggregate (std::is_aggregate_v<remove_cvref_t<T>>), AND
//   (b) detail::aggregate_info<T>::exact_type_list is a well-formed type,
//       meaning the structured-binding decomposition succeeded.
// ---------------------------------------------------------------------------
template <typename T>
concept AggregateDecomposable =
    Aggregate<T> &&
    requires {
        typename detail::aggregate_info<std::remove_cvref_t<T>>::exact_type_list;
    };

// ---------------------------------------------------------------------------
// 4.1 AggregateFieldDescriptor
// ---------------------------------------------------------------------------
template <typename T, std::size_t Index>
    requires Aggregate<T>
struct AggregateFieldDescriptor {
    using owner_type    = T;
    using declared_type = detail::aggregate_field_type_t<T, Index>;
    using value_type    = std::remove_cvref_t<declared_type>;

    static consteval std::size_t index() noexcept { return Index; }

    // Synthetic field name generation for positional aggregate fields
    // Note: These are placeholder names, not semantic identifiers.
    // Use custom reflection for true field names.
    static consteval std::string_view name() noexcept {
        return detail::synthetic_field_name<Index>();
    }

    // Projection: get field value preserving value category
    template <typename O>
        requires std::same_as<std::remove_cvref_t<O>, owner_type>
    [[nodiscard]] static constexpr decltype(auto) get(O &&obj) noexcept(
        noexcept(detail::get_field<Index>(std::forward<O>(obj)))) {
        return detail::get_field<Index>(std::forward<O>(obj));
    }

    // Traits
    static consteval bool is_reference() noexcept {
        return std::is_reference_v<declared_type>;
    }
    static consteval bool is_pointer() noexcept {
        return std::is_pointer_v<value_type>;
    }
    static consteval bool is_const() noexcept {
        return std::is_const_v<std::remove_reference_t<declared_type>>;
    }
    // Synthetic: aggregate-decomposed descriptors use positional placeholder names.
    static consteval bool is_synthetic() noexcept { return true; }

    // Capability flags make backend layout limits explicit.
    // Aggregate-backed descriptors are produced from structured bindings and
    // therefore cannot reliably report member offsets. They do, however,
    // provide size/alignment information only when the aggregate type is
    // standard-layout (so sizeof/alignof of the field is meaningful).
    static consteval bool has_offset() noexcept { return false; }
    static consteval bool has_size() noexcept { return StandardLayout<T>; }
    static consteval bool has_alignment() noexcept { return StandardLayout<T>; }
    static consteval bool has_layout_metadata() noexcept {
        return has_size() && has_alignment();
    }

    // Layout properties (only for standard layout types)
    static consteval std::size_t size() noexcept
        requires StandardLayout<T>
    {
        return sizeof(value_type);
    }
    static consteval std::size_t alignment() noexcept
        requires StandardLayout<T>
    {
        return alignof(value_type);
    }
};

// Master decompose entry point
namespace detail {

template <typename T, std::size_t... I>
consteval auto decompose_impl(std::index_sequence<I...>) {
    return Sequence<AggregateFieldDescriptor<T, I>...>{};
}

} // namespace detail

// decompose<T>() is constrained on AggregateDecomposable (not merely Aggregate)
// so that unsatisfied decomposition fires a concept diagnostic at the call site
// rather than deep inside template instantiation.
template <typename T>
    requires AggregateDecomposable<T>
consteval auto decompose() {
    using U = std::remove_cvref_t<T>;
    return detail::decompose_impl<U>(
        std::make_index_sequence<detail::aggregate_info<U>::field_count>{});
}

// field_count — number of fields in an aggregate
template <Aggregate T>
inline constexpr std::size_t field_count =
    detail::aggregate_info<std::remove_cvref_t<T>>::field_count;

// ---------------------------------------------------------------------------
// is_small_aggregate_v — compile-time size classification hook
// ---------------------------------------------------------------------------
// True when T has at most 8 reflected fields.  Purely informational; does not
// alter any existing code path.  Intended as a future-facing tag for:
//   - selecting lighter compile-time algorithms for small aggregates
//   - enabling branch-free specialisations in serialisation or hashing
//   - providing a guard in static_assert-based diagnostics
//
// The threshold of 8 is a conventional "fits in a cache line" heuristic and
// may be overridden per-type via explicit specialisation if needed.
// ---------------------------------------------------------------------------
template <Aggregate T>
inline constexpr bool is_small_aggregate_v =
    detail::aggregate_info<std::remove_cvref_t<T>>::field_count <= 8;

// ============================================================================
//  SECTION 5: REFLECTION — CUSTOM (ADL-BASED) REFLECTION
// ============================================================================

// ---------------------------------------------------------------------------
// 5.1 MemberFieldDescriptor — pointer-to-member based
// ---------------------------------------------------------------------------
template <typename T, std::size_t Index, auto Ptr, fixed_string Name>
struct MemberFieldDescriptor {
public:
    using owner_type    = T;
    using declared_type = typename detail::member_pointer_value<decltype(Ptr)>::type;
    using value_type    = std::remove_cvref_t<declared_type>;

    static consteval std::size_t index() noexcept { return Index; }

    static consteval std::string_view name() noexcept {
        return Name.view();
    }

    // Expose the backing member pointer for compile-time introspection by consumers.
    // Enables descriptor-based lookup: compare D::member_ptr() == MPtr at compile time
    // to map a member pointer NTTP to its semantic field name without runtime hacks.
    static consteval auto member_ptr() noexcept { return Ptr; }

    template <typename O>
        requires std::same_as<std::remove_cvref_t<O>, owner_type>
    [[nodiscard]] static constexpr decltype(auto) get(O &&obj) noexcept(
        noexcept(std::forward<O>(obj).*Ptr)) {
        return std::forward<O>(obj).*Ptr;
    }

    // Traits
    static consteval bool is_reference() noexcept {
        return std::is_reference_v<declared_type>;
    }
    static consteval bool is_pointer() noexcept {
        return std::is_pointer_v<value_type>;
    }
    static consteval bool is_const() noexcept {
        return std::is_const_v<std::remove_reference_t<declared_type>>;
    }
    // Not synthetic: MemberFieldDescriptor carries user-assigned semantic names.
    static consteval bool is_synthetic() noexcept { return false; }

    // Member-pointer-backed descriptors are able to provide size/alignment
    // unconditionally (these come from the member's value type). Offsets are
    // backend-specific and may not be universally available; reserve the
    // has_offset capability for backends that can compute or expose offsets.
    // By default we conservatively report no offset to avoid exposing layout
    // details that may not be portable across platforms/compilers.
    static consteval bool has_offset() noexcept { return false; }
    static consteval bool has_size() noexcept { return true; }
    static consteval bool has_alignment() noexcept { return true; }
    static consteval bool has_layout_metadata() noexcept { return has_size() && has_alignment(); }

    static consteval std::size_t size() noexcept {
        return sizeof(value_type);
    }
    static consteval std::size_t alignment() noexcept {
        return alignof(value_type);
    }
};

// ---------------------------------------------------------------------------
// 5.2 field() — factory for MemberFieldDescriptor
// ---------------------------------------------------------------------------
namespace detail {

// Extract class type from pointer-to-member type
template <typename>
struct member_pointer_class;

template <typename C, typename M>
struct member_pointer_class<M C::*> {
    using type = C;
};

template <typename T>
using member_pointer_class_t = typename member_pointer_class<T>::type;

} // namespace detail

template <std::size_t Index, auto Ptr, fixed_string Name>
consteval auto field() {
    using owner = detail::member_pointer_class_t<decltype(Ptr)>;
    return MemberFieldDescriptor<owner, Index, Ptr, Name>{};
}

// ---------------------------------------------------------------------------
// 5.3 make_sequence — build Sequence from descriptors
// ---------------------------------------------------------------------------
template <typename... Descriptors>
consteval auto make_sequence(Descriptors...) {
    return Sequence<Descriptors...>{};
}

// ---------------------------------------------------------------------------
// 5.4 reflect<T>() — unified reflection entry point
// ---------------------------------------------------------------------------
// Dispatches to custom reflection (ADL) or aggregate decomposition.

template <typename T>
consteval auto reflect() {
    using Clean = std::remove_cvref_t<T>;
    if constexpr (detail::HasCustomReflection<Clean>) {
        return detail::invoke_reflect_members<Clean>();
    } else if constexpr (AggregateDecomposable<Clean>) {
        return decompose<Clean>();
    } else {
        static_assert(detail::HasCustomReflection<Clean> || AggregateDecomposable<Clean>,
                      "meta::reflect<T>: T must be an aggregate (satisfying "
                      "AggregateDecomposable) or provide a reflect_members() "
                      "ADL overload for custom reflection.");
    }
}

// Type alias for the reflection result
template <typename T>
using reflect_t = decltype(reflect<T>());

// ============================================================================
//  SECTION 6: ENUM REFLECTION
// ============================================================================

namespace detail::enums {

// Probe a single enum value
template <MetaEnum E, E Value>
consteval std::string_view enum_value_name() noexcept {
    constexpr auto raw = compiler::enum_name_raw<Value>();
    if constexpr (compiler::is_valid_enum_name(raw)) {
        return compiler::clean_enum_name(raw);
    } else {
        return {};
    }
}

// Default range for enum scanning
inline constexpr int enum_min = -128;
inline constexpr int enum_max = 256;

// Check single enum value validity via index_sequence expansion
template <MetaEnum E, int V>
consteval bool is_valid_enum_value() noexcept {
    return compiler::is_valid_enum_name(
        compiler::enum_name_raw<static_cast<E>(V)>());
}

// Count valid enum values in range using index_sequence
template <MetaEnum E, int Min, int... Is>
consteval std::size_t count_enum_values_impl(
    std::integer_sequence<int, Is...>) noexcept {
    return (static_cast<std::size_t>(
                is_valid_enum_value<E, Min + Is>()) +
            ...);
}

template <MetaEnum E, int Min, int Max>
consteval std::size_t count_enum_values() noexcept {
    if constexpr (Min > Max) {
        return 0;
    } else {
        return count_enum_values_impl<E, Min>(
            std::make_integer_sequence<int, Max - Min + 1>{});
    }
}

// Collect valid enum values using index_sequence
template <MetaEnum E, int Min, int... Is>
consteval auto collect_enum_values_impl(
    std::integer_sequence<int, Is...>) noexcept {
    constexpr std::size_t N = (static_cast<std::size_t>(
                                   is_valid_enum_value<E, Min + Is>()) +
                               ...);
    ct_array<int, (N > 0 ? N : 1)> result{};
    ((is_valid_enum_value<E, Min + Is>()
          ? (result.push_back(Min + Is), 0)
          : 0),
     ...);
    return result;
}

template <MetaEnum E, int Min, int Max>
consteval auto collect_enum_values() noexcept {
    return collect_enum_values_impl<E, Min>(
        std::make_integer_sequence<int, Max - Min + 1>{});
}

} // namespace detail::enums

// ---------------------------------------------------------------------------
// 6.1 EnumeratorDescriptor
// ---------------------------------------------------------------------------
template <MetaEnum E, E Value>
struct EnumeratorDescriptor {
    using enum_type = E;
    using constant  = std::integral_constant<E, Value>;

    static consteval E value() noexcept { return Value; }

    static consteval std::string_view name() noexcept {
        return detail::enums::enum_value_name<E, Value>();
    }

    static consteval constant as_constant() noexcept { return {}; }
};

// ---------------------------------------------------------------------------
// 6.2 enum_name<V>() — get name of a single enumerator
// ---------------------------------------------------------------------------
template <auto V>
    requires MetaEnum<decltype(V)>
consteval std::string_view enum_name() noexcept {
    return detail::enums::enum_value_name<decltype(V), V>();
}

// ---------------------------------------------------------------------------
// 6.3 enum_name(E v) — runtime enum-to-string (via constexpr table)
// ---------------------------------------------------------------------------
namespace detail::enums {

template <MetaEnum E, int Min, int Max, std::size_t... I>
consteval auto make_enum_entries(std::index_sequence<I...>) noexcept {
    constexpr auto values = collect_enum_values<E, Min, Max>();
    struct entry {
        E                val;
        std::string_view name;
    };
    return std::array<entry, sizeof...(I)>{entry{
        static_cast<E>(values[I]),
        compiler::clean_enum_name(
            compiler::enum_name_raw<static_cast<E>(values[I])>())}...};
}

template <MetaEnum E, int Min = enum_min, int Max = enum_max>
consteval auto enum_entries() noexcept {
    constexpr std::size_t N = count_enum_values<E, Min, Max>();
    return make_enum_entries<E, Min, Max>(std::make_index_sequence<N>{});
}

} // namespace detail::enums

template <MetaEnum E, int Min = detail::enums::enum_min,
          int Max = detail::enums::enum_max>
constexpr std::string_view enum_name(E value) noexcept {
    constexpr auto entries = detail::enums::enum_entries<E, Min, Max>();
    for (const auto &e : entries) {
        if (e.val == value)
            return e.name;
    }
    return {};
}

// ---------------------------------------------------------------------------
// 6.4 enum_count<E>() — number of enumerators
// ---------------------------------------------------------------------------
template <MetaEnum E, int Min = detail::enums::enum_min,
          int Max = detail::enums::enum_max>
inline constexpr std::size_t enum_count =
    detail::enums::count_enum_values<E, Min, Max>();

// ---------------------------------------------------------------------------
// 6.5 enum_values<E>() — array of all valid enum values
// ---------------------------------------------------------------------------
template <MetaEnum E, int Min = detail::enums::enum_min,
          int Max = detail::enums::enum_max>
consteval auto enum_values() noexcept {
    constexpr auto entries = detail::enums::enum_entries<E, Min, Max>();
    std::array<E, entries.size()> result{};
    for (std::size_t i = 0; i < entries.size(); ++i)
        result[i] = entries[i].val;
    return result;
}

// ---------------------------------------------------------------------------
// 6.6 Parse error type for enum_from_string
// ---------------------------------------------------------------------------
struct parse_error {
    std::string_view message;
};

// ---------------------------------------------------------------------------
// 6.7 enum_from_string<E>(string_view) — string-to-enum with std::expected
// ---------------------------------------------------------------------------
template <MetaEnum E, int Min = detail::enums::enum_min,
          int Max = detail::enums::enum_max>
constexpr std::expected<E, parse_error> enum_from_string(std::string_view sv) noexcept {
    constexpr auto entries = detail::enums::enum_entries<E, Min, Max>();
    for (const auto &e : entries) {
        if (e.name == sv)
            return e.val;
    }
    return std::unexpected(parse_error{"Invalid enum value"});
}

// ============================================================================
//  SECTION 7: META ALGORITHMS
// ============================================================================

// All algorithms operate on Sequence types and are fold-expression based
// (no recursion). They work at compile time and are fully inlined at runtime.

// ---------------------------------------------------------------------------
// 7.1 for_each — apply function to each descriptor
// ---------------------------------------------------------------------------
template <typename Seq, typename Fn>
constexpr void for_each(Fn &&fn) {
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        (fn(typename Seq::template element<I>{}), ...);
    }(std::make_index_sequence<Seq::size>{});
}

// for_each with instance (runtime object access)
// Two safe overloads are provided for runtime field iteration:
//  - T& obj       -> mutable lvalues
//  - const T& obj -> const lvalues
// These overloads intentionally accept lvalue references and call
// descriptor::get(obj) directly inside the fold to avoid repeatedly
// std::forward-ing the same object expression (which can cause
// unexpected moves/dangling when the caller passed an rvalue).
// A deleted forwarding-reference overload prevents accidental calls
// with rvalues (it participates only for rvalue calls and is =delete).
template <typename Seq, typename T, typename Fn>
constexpr void for_each(T &obj, Fn &&fn) {
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        (fn(typename Seq::template element<I>{},
            Seq::template element<I>::get(obj)),
         ...);
    }(std::make_index_sequence<Seq::size>{});
}

template <typename Seq, typename T, typename Fn>
constexpr void for_each(const T &obj, Fn &&fn) {
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        (fn(typename Seq::template element<I>{},
            Seq::template element<I>::get(obj)),
         ...);
    }(std::make_index_sequence<Seq::size>{});
}

template <typename Seq, typename T, typename Fn>
    requires(!std::is_lvalue_reference_v<T>)
constexpr void for_each(T &&, Fn &&) = delete;

// ---------------------------------------------------------------------------
// 7.2 transform — map descriptors to a new sequence via trait
// ---------------------------------------------------------------------------
template <typename Seq, template <typename> class Transform>
struct transform_sequence;

template <typename... Ds, template <typename> class Transform>
struct transform_sequence<Sequence<Ds...>, Transform> {
    using type = Sequence<typename Transform<Ds>::type...>;
};

template <typename Seq, template <typename> class Transform>
using transform_t = typename transform_sequence<Seq, Transform>::type;

// transform with a callable returning values — returns std::tuple of results
template <typename Seq, typename Fn>
constexpr auto transform(Fn &&fn) {
    return [&]<std::size_t... I>(std::index_sequence<I...>) {
        return std::tuple{fn(typename Seq::template element<I>{})...};
    }(std::make_index_sequence<Seq::size>{});
}

// ---------------------------------------------------------------------------
// 7.3 filter — select descriptors matching a predicate
// ---------------------------------------------------------------------------
namespace detail {

template <typename Seq, template <typename> class Pred,
          typename Result = Sequence<>>
struct filter_impl;

// Base case: empty input
template <template <typename> class Pred, typename Result>
struct filter_impl<Sequence<>, Pred, Result> {
    using type = Result;
};

// Recursive case: matching
template <typename Head, typename... Tail,
          template <typename> class Pred, typename... Rs>
    requires(Pred<Head>::value)
struct filter_impl<Sequence<Head, Tail...>, Pred, Sequence<Rs...>> {
    using type =
        typename filter_impl<Sequence<Tail...>, Pred,
                             Sequence<Rs..., Head>>::type;
};

// Recursive case: not matching
template <typename Head, typename... Tail,
          template <typename> class Pred, typename... Rs>
    requires(!Pred<Head>::value)
struct filter_impl<Sequence<Head, Tail...>, Pred, Sequence<Rs...>> {
    using type =
        typename filter_impl<Sequence<Tail...>, Pred,
                             Sequence<Rs...>>::type;
};

} // namespace detail

template <typename Seq, template <typename> class Pred>
using filter_t = typename detail::filter_impl<Seq, Pred>::type;

// ---------------------------------------------------------------------------
// 7.4 fold — reduce descriptors to a single value
// ---------------------------------------------------------------------------
template <typename Seq, typename Init, typename Fn>
constexpr auto fold(Init init, Fn &&fn) {
    auto result = init;
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        ((result = fn(result, typename Seq::template element<I>{})), ...);
    }(std::make_index_sequence<Seq::size>{});
    return result;
}

// ---------------------------------------------------------------------------
// 7.5 find — find first descriptor matching predicate
// ---------------------------------------------------------------------------
template <typename Seq, typename Pred>
consteval std::size_t find_if(Pred pred) {
    std::size_t result = Seq::size; // sentinel: not found
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        bool found = false;
        (((!found && pred(typename Seq::template element<I>{}))
              ? (result = I, found = true)
              : false),
         ...);
    }(std::make_index_sequence<Seq::size>{});
    return result;
}

// find by name
template <typename Seq>
consteval std::size_t find_by_name(std::string_view name) {
    std::size_t result = Seq::size;
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        bool found = false;
        ((((!found) &&
           Seq::template element<I>::name() == name)
              ? (result = I, found = true)
              : false),
         ...);
    }(std::make_index_sequence<Seq::size>{});
    return result;
}

// ---------------------------------------------------------------------------
// 7.6 contains — check if any descriptor matches
// ---------------------------------------------------------------------------
template <typename Seq, typename Pred>
consteval bool contains(Pred pred) {
    return find_if<Seq>(pred) != Seq::size;
}

// ---------------------------------------------------------------------------
// 7.7 count_if — count descriptors matching predicate
// ---------------------------------------------------------------------------
template <typename Seq, typename Pred>
consteval std::size_t count_if(Pred pred) {
    std::size_t count = 0;
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        ((pred(typename Seq::template element<I>{}) ? ++count : count), ...);
    }(std::make_index_sequence<Seq::size>{});
    return count;
}

// ---------------------------------------------------------------------------
// 7.8 apply — apply a function object to each field value
// ---------------------------------------------------------------------------
template <typename T, typename Fn>
constexpr void apply(T &&obj, Fn &&fn) {
    using Seq = reflect_t<std::remove_cvref_t<T>>;
    // Use lvalue to avoid repeated forwarding
    auto &obj_ref = obj;
    for_each<Seq>(obj_ref, std::forward<Fn>(fn));
}

// ---------------------------------------------------------------------------
// 7.9 zip — iterate two objects field-by-field
// ---------------------------------------------------------------------------
template <typename T, typename Fn>
constexpr void zip(T &&a, T &&b, Fn &&fn) {
    using Seq = reflect_t<std::remove_cvref_t<T>>;
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        (fn(typename Seq::template element<I>{},
            Seq::template element<I>::get(std::forward<T>(a)),
            Seq::template element<I>::get(std::forward<T>(b))),
         ...);
    }(std::make_index_sequence<Seq::size>{});
}

// ============================================================================
//  SECTION 8: TUPLE INTEROP
// ============================================================================

// ---------------------------------------------------------------------------
// 8.1 tie_members — lvalue references to all fields
// ---------------------------------------------------------------------------
// MemberTie<Tup> — lightweight wrapper used by forward_members to make the
// ephemeral forwarding view explicit. It is a thin wrapper around the tuple
// produced by `detail::aggregate::tie_impl` and provides tuple-like access
// via standard tuple customization points below.
template <typename Tup>
struct MemberTie {
    Tup tup;
    constexpr explicit MemberTie(Tup t) noexcept(std::is_nothrow_move_constructible_v<Tup>)
        : tup(std::move(t)) {}
};

// ============================================================================
//  SECTION 14: REFLECTION VIEWS
// ============================================================================
// Views are compile-time filters and projections applied on top of a reflected
// Sequence<Descriptors...>.  All view aliases are pure type transformations;
// zero runtime overhead.
//
// Architecture:
//
//   reflect<T>() → full Sequence<Descriptors...>
//        │
//        ▼  view filter predicates
//   structural_view_t<Seq>    = Seq (identity)
//   semantic_view_t<Seq>      = filter_t<Seq, detail::is_semantic_field>
//   layout_view_t<Seq>        = filter_t<Seq, detail::has_layout_metadata_pred>
//   serialization_view_t<Seq> = filter_t<Seq, detail::is_serializable_field>
//        │
//        ▼  type-level descriptor transformation
//   projected_view_t<Seq, Proj> = Sequence<Proj<D0>, Proj<D1>, ...>
//   (example: projections::as_const_descriptor<D>)

// ---------------------------------------------------------------------------
// 14.1 View tag types
// ---------------------------------------------------------------------------
struct structural_view_tag {};
struct semantic_view_tag {};
struct layout_view_tag {};
struct serialization_view_tag {};

// ---------------------------------------------------------------------------
// 14.2 Named field predicates (type-level, for use with filter_t)
// ---------------------------------------------------------------------------
namespace detail {

template <typename D>
struct is_semantic_field : std::bool_constant<!D::is_synthetic()> {};

template <typename D>
struct has_layout_metadata_pred
    : std::bool_constant<D::has_layout_metadata()> {};

template <typename D>
struct is_serializable_field
    : std::bool_constant<!D::is_pointer() &&
                         !D::is_reference() &&
                         std::is_trivially_copyable_v<typename D::value_type>> {};

} // namespace detail

// ---------------------------------------------------------------------------
// 14.3 View type aliases
// ---------------------------------------------------------------------------
template <typename Seq>
using structural_view_t = Seq;

template <typename Seq>
using semantic_view_t = filter_t<Seq, detail::is_semantic_field>;

template <typename Seq>
using layout_view_t = filter_t<Seq, detail::has_layout_metadata_pred>;

template <typename Seq>
using serialization_view_t = filter_t<Seq, detail::is_serializable_field>;

// ---------------------------------------------------------------------------
// 14.4 reflect_as<ViewTag, T>() — view-gated reflection entry point
// ---------------------------------------------------------------------------
template <typename ViewTag, typename T>
consteval auto reflect_as() {
    using Seq = reflect_t<T>;
    if constexpr (std::same_as<ViewTag, structural_view_tag>)
        return Seq{};
    else if constexpr (std::same_as<ViewTag, semantic_view_tag>)
        return semantic_view_t<Seq>{};
    else if constexpr (std::same_as<ViewTag, layout_view_tag>)
        return layout_view_t<Seq>{};
    else if constexpr (std::same_as<ViewTag, serialization_view_tag>)
        return serialization_view_t<Seq>{};
    else
        static_assert(!std::is_same_v<ViewTag, ViewTag>,
                      "meta::reflect_as: unknown view tag");
}

template <typename ViewTag, typename T>
using reflect_as_t = decltype(reflect_as<ViewTag, T>());

// ---------------------------------------------------------------------------
// 14.5 projected_sequence — type-level descriptor transformation
// ---------------------------------------------------------------------------
// Proj must be a unary template where Proj<D> is itself a descriptor type.
// Usage: projected_view_t<Seq, projections::as_const_descriptor>
template <typename Seq, template <typename> class Proj>
struct projected_sequence;

template <typename... Ds, template <typename> class Proj>
struct projected_sequence<Sequence<Ds...>, Proj> {
    using type = Sequence<Proj<Ds>...>;
};

template <typename Seq, template <typename> class Proj>
using projected_view_t = typename projected_sequence<Seq, Proj>::type;

// ---------------------------------------------------------------------------
// 14.6 value_types_of — extract field value types as a TypeList
// ---------------------------------------------------------------------------
template <typename Seq>
struct value_types_of;

template <typename... Ds>
struct value_types_of<Sequence<Ds...>> {
    using type = TypeList<typename Ds::value_type...>;
};

template <typename Seq>
using value_types_of_t = typename value_types_of<Seq>::type;

// ---------------------------------------------------------------------------
// 14.7 Concrete projection: projections::as_const_descriptor<D>
// ---------------------------------------------------------------------------
namespace projections {

template <typename D>
struct as_const_descriptor {
    using owner_type    = typename D::owner_type;
    using declared_type = std::add_const_t<
                              std::remove_reference_t<typename D::declared_type>>;
    using value_type    = typename D::value_type;

    static consteval std::size_t      index()        noexcept { return D::index(); }
    static consteval std::string_view name()         noexcept { return D::name(); }
    static consteval bool             is_synthetic() noexcept { return D::is_synthetic(); }

    template <typename O>
        requires std::same_as<std::remove_cvref_t<O>, owner_type>
    [[nodiscard]] static constexpr const value_type &
    get(O &&obj) noexcept(noexcept(D::get(std::forward<O>(obj)))) {
        return D::get(std::forward<O>(obj));
    }

    static consteval bool is_reference()        noexcept { return false; }
    static consteval bool is_pointer()          noexcept { return D::is_pointer(); }
    static consteval bool is_const()            noexcept { return true; }

    static consteval bool has_offset()          noexcept { return D::has_offset(); }
    static consteval bool has_size()            noexcept { return D::has_size(); }
    static consteval bool has_alignment()       noexcept { return D::has_alignment(); }
    static consteval bool has_layout_metadata() noexcept { return D::has_layout_metadata(); }

    static consteval std::size_t size()         noexcept { return sizeof(value_type); }
    static consteval std::size_t alignment()    noexcept { return alignof(value_type); }
};

} // namespace projections

} // namespace meta

// Allow structured access via std::tuple_size/std::tuple_element for
// meta::MemberTie<Tup>. Only template *specializations* are permitted in
// namespace std for user-defined types ([namespace.std]). New function
// overloads must NOT be placed there — they belong in namespace meta where
// ADL will find them when std::get<I>(membertie) is used.
namespace std {
template <typename Tup>
struct tuple_size<meta::MemberTie<Tup>>
    : tuple_size<Tup> {};

template <size_t I, typename Tup>
struct tuple_element<I, meta::MemberTie<Tup>>
    : tuple_element<I, Tup> {};
} // namespace std

namespace meta {

// get<I> overloads for MemberTie — placed in namespace meta so that ADL
// finds them when callers write std::get<I>(membertie) or get<I>(membertie).
// Using explicit std::get<I> avoids any risk of recursion.
template <std::size_t I, typename Tup>
constexpr decltype(auto) get(MemberTie<Tup> &m) noexcept {
    return std::get<I>(m.tup);
}

template <std::size_t I, typename Tup>
constexpr decltype(auto) get(const MemberTie<Tup> &m) noexcept {
    return std::get<I>(m.tup);
}

template <std::size_t I, typename Tup>
constexpr decltype(auto) get(MemberTie<Tup> &&m) noexcept {
    return std::get<I>(std::move(m.tup));
}


// ---------------------------------------------------------------------------
// 8.1 tie_members — tuple of lvalue references to every reflected field
// ---------------------------------------------------------------------------
// Returns std::tuple<Field0&, Field1&, ...>.  Mutations through the tuple
// write back to the original object.  Works for both plain aggregates and
// custom-reflected types (those providing a hidden-friend reflect_members).
//
// DANGER: storing the returned tuple after the source object's lifetime ends
// produces dangling references.  The rvalue overload is =delete'd to catch
// the most common accidental case at compile time.
template <typename T>
    requires Reflectable<std::remove_cvref_t<T>>
constexpr auto tie_members(T &obj) noexcept {
    using Seq = reflect_t<std::remove_cvref_t<T>>;
    return [&]<std::size_t... I>(std::index_sequence<I...>) {
        // std::tie binds each expression by lvalue reference; the field
        // accessors return lvalue refs when called on an lvalue, so the
        // resulting tuple element types are exactly Field&.
        return std::tie(Seq::template element<I>::get(obj)...);
    }(std::make_index_sequence<Seq::size>{});
}

// Deleted: binding to a temporary yields an immediately-dangling tuple.
template <typename T>
    requires Reflectable<std::remove_cvref_t<T>> && (!std::is_lvalue_reference_v<T>)
constexpr auto tie_members(T &&) = delete;

// ---------------------------------------------------------------------------
// 8.2 forward_members — category-preserving named wrapper (MemberTie)
// ---------------------------------------------------------------------------
// Returns MemberTie<tuple<Field0&, Field1&, ...>>.  The named wrapper type
// prevents the result from silently decaying into a plain tuple and makes
// the non-owning, ephemeral nature of the view visible at call sites.
//
// Single forwarding-reference overload constrained to lvalue references.
// When T = SomeType&  (mutable lvalue):  is_lvalue_reference_v<T> = true → accepted.
// When T = const SomeType& (const lvalue): is_lvalue_reference_v<T> = true → accepted.
// When T = SomeType   (rvalue/forwarded): is_lvalue_reference_v<T> = false → deleted overload wins.
// This prevents the const T& overload from silently binding to rvalues.
template <typename T>
    requires Reflectable<std::remove_cvref_t<T>> && std::is_lvalue_reference_v<T>
constexpr auto forward_members(T &&obj) noexcept {
    using Clean = std::remove_cvref_t<T>;
    using Seq = reflect_t<Clean>;
    auto tup = [&]<std::size_t... I>(std::index_sequence<I...>) {
        return std::forward_as_tuple(Seq::template element<I>::get(std::forward<T>(obj))...);
    }(std::make_index_sequence<Seq::size>{});
    // Do NOT store MemberTie beyond the lifetime of obj.
    return MemberTie<decltype(tup)>{std::move(tup)};
}

// Rvalue forwarding ties dangle immediately after the full expression.
template <typename T>
    requires Reflectable<std::remove_cvref_t<T>> && (!std::is_lvalue_reference_v<T>)
constexpr auto forward_members(T &&) = delete;

// ---------------------------------------------------------------------------
// 8.3 to_value_tuple — deep copy all fields into an owning value tuple
// ---------------------------------------------------------------------------
// Returns std::tuple<Field0, Field1, ...> (by value).  The result owns its
// data and is safe to store, return, and pass freely.  Modifications to the
// tuple do NOT propagate back to the source object.
//
// Works for both plain aggregates and custom-reflected types.
template <typename T>
    requires Reflectable<std::remove_cvref_t<T>>
constexpr auto to_value_tuple(const T &obj) {
    using Seq = reflect_t<std::remove_cvref_t<T>>;
    return [&]<std::size_t... I>(std::index_sequence<I...>) {
        // Explicitly copy-construct each value_type; never binds a reference.
        return std::tuple{
            typename Seq::template element<I>::value_type(
                Seq::template element<I>::get(obj))...};
    }(std::make_index_sequence<Seq::size>{});
}

// ---------------------------------------------------------------------------
// 8.4 from_tuple — construct aggregate from tuple
// ---------------------------------------------------------------------------
template <Aggregate T, typename Tuple>
constexpr T from_tuple(Tuple &&tup) {
    return std::apply(
        [](auto &&...args) -> T {
            return T{std::forward<decltype(args)>(args)...};
        },
        std::forward<Tuple>(tup));
}

// ============================================================================
//  SECTION 9: LAYOUT & ZERO-COPY POLICIES
// ============================================================================

// ---------------------------------------------------------------------------
// 9.1 Structural facts
// ---------------------------------------------------------------------------
namespace detail {

template <typename D>
struct is_pointer_field : std::bool_constant<D::is_pointer()> {};

template <typename D>
struct is_reference_field : std::bool_constant<D::is_reference()> {};

template <typename D>
struct is_const_field : std::bool_constant<D::is_const()> {};

} // namespace detail

// has_pointer_field<T> — true if any field is a pointer
template <typename T>
    requires Reflectable<T>
consteval bool has_pointer_field() {
    using Seq = reflect_t<T>;
    bool result = false;
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        // Unary left fold: (... || E) evaluates to false for an empty pack,
        // which is the correct identity for ||. Using the left-fold form
        // makes the empty-pack base case explicit and avoids reliance on
        // compiler-specific behaviour for the unary right fold.
        result = (... || Seq::template element<I>::is_pointer());
    }(std::make_index_sequence<Seq::size>{});
    return result;
}

// has_reference_field<T> — true if any field is a reference
template <typename T>
    requires Reflectable<T>
consteval bool has_reference_field() {
    using Seq = reflect_t<T>;
    bool result = false;
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        // Unary left fold: (... || E) evaluates to false for an empty pack,
        // which is the correct identity for ||. Using the left-fold form
        // makes the empty-pack base case explicit and avoids reliance on
        // compiler-specific behaviour for the unary right fold.
        result = (... || Seq::template element<I>::is_reference());
    }(std::make_index_sequence<Seq::size>{});
    return result;
}

// is_trivially_copyable_deep<T> — all fields are trivially copyable
template <typename T>
    requires Reflectable<T>
consteval bool is_trivially_copyable_deep() {
    if constexpr (!std::is_trivially_copyable_v<T>)
        return false;
    using Seq = reflect_t<T>;
    bool result = true;
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        result = (std::is_trivially_copyable_v<
                      typename Seq::template element<I>::value_type> &&
                  ...);
    }(std::make_index_sequence<Seq::size>{});
    return result;
}

// ---------------------------------------------------------------------------
// 9.2 Policies
// ---------------------------------------------------------------------------
struct DefaultPolicy {};
struct StrictPolicy {};

template <typename T, typename Policy = DefaultPolicy>
consteval bool is_zero_copy_serializable() {
    if constexpr (!std::is_trivially_copyable_v<T>)
        return false;
    if constexpr (has_pointer_field<T>())
        return false;
    if constexpr (has_reference_field<T>())
        return false;
    if constexpr (std::same_as<Policy, StrictPolicy>) {
        return std::is_standard_layout_v<T> &&
               is_trivially_copyable_deep<T>();
    }
    return true;
}

template <typename T, typename Policy = DefaultPolicy>
consteval bool is_binary_stable() {
    if constexpr (!is_zero_copy_serializable<T, Policy>())
        return false;
    return std::is_standard_layout_v<T>;
}

// ============================================================================
//  SECTION 10: COMPILE-TIME ALGORITHMS (TYPE-LEVEL)
// ============================================================================

// ---------------------------------------------------------------------------
// 10.1 concat — concatenate two sequences
// ---------------------------------------------------------------------------
template <typename S1, typename S2>
struct concat;

template <typename... A, typename... B>
struct concat<Sequence<A...>, Sequence<B...>> {
    using type = Sequence<A..., B...>;
};

template <typename S1, typename S2>
using concat_t = typename concat<S1, S2>::type;

// ---------------------------------------------------------------------------
// 10.2 head / tail — sequence decomposition
// ---------------------------------------------------------------------------
template <typename Seq>
struct head;

template <typename H, typename... Tail>
struct head<Sequence<H, Tail...>> {
    using type = H;
};

template <typename Seq>
using head_t = typename head<Seq>::type;

template <typename Seq>
struct tail;

template <typename H, typename... Tail>
struct tail<Sequence<H, Tail...>> {
    using type = Sequence<Tail...>;
};

template <typename Seq>
using tail_t = typename tail<Seq>::type;

// ---------------------------------------------------------------------------
// 10.3 reverse — reverse a sequence
// ---------------------------------------------------------------------------
template <typename Seq>
struct reverse;

template <>
struct reverse<Sequence<>> {
    using type = Sequence<>;
};

template <typename H, typename... Tail>
struct reverse<Sequence<H, Tail...>> {
    using type = concat_t<typename reverse<Sequence<Tail...>>::type,
                          Sequence<H>>;
};

template <typename Seq>
using reverse_t = typename reverse<Seq>::type;

// ---------------------------------------------------------------------------
// 10.4 take_n — first N elements of a sequence
// ---------------------------------------------------------------------------
namespace detail {

template <typename Seq, std::size_t N, typename Acc = Sequence<>>
struct take_impl;

template <typename... Acc>
struct take_impl<Sequence<>, 0, Sequence<Acc...>> {
    using type = Sequence<Acc...>;
};

template <typename... Rest, typename... Acc>
struct take_impl<Sequence<Rest...>, 0, Sequence<Acc...>> {
    using type = Sequence<Acc...>;
};

template <typename H, typename... Tail, std::size_t N, typename... Acc>
    requires(N > 0)
struct take_impl<Sequence<H, Tail...>, N, Sequence<Acc...>> {
    using type =
        typename take_impl<Sequence<Tail...>, N - 1,
                           Sequence<Acc..., H>>::type;
};

} // namespace detail

template <typename Seq, std::size_t N>
using take_t = typename detail::take_impl<Seq, N>::type;

// ---------------------------------------------------------------------------
// 10.5 drop_n — remove first N elements from a sequence
// ---------------------------------------------------------------------------
namespace detail {

template <typename Seq, std::size_t N>
struct drop_impl;

template <typename... Ts>
struct drop_impl<Sequence<Ts...>, 0> {
    using type = Sequence<Ts...>;
};

template <typename H, typename... Tail, std::size_t N>
    requires(N > 0)
struct drop_impl<Sequence<H, Tail...>, N> {
    using type = typename drop_impl<Sequence<Tail...>, N - 1>::type;
};

} // namespace detail

template <typename Seq, std::size_t N>
using drop_t = typename detail::drop_impl<Seq, N>::type;

// ---------------------------------------------------------------------------
// 10.6 unique — remove duplicate types from a sequence
// ---------------------------------------------------------------------------
namespace detail {

template <typename Seq, typename Acc = Sequence<>>
struct unique_impl;

template <typename... Acc>
struct unique_impl<Sequence<>, Sequence<Acc...>> {
    using type = Sequence<Acc...>;
};

template <typename H, typename... Tail, typename... Acc>
struct unique_impl<Sequence<H, Tail...>, Sequence<Acc...>> {
    static constexpr bool already_present = (std::same_as<H, Acc> || ...);
    using type = std::conditional_t<
        already_present,
        typename unique_impl<Sequence<Tail...>, Sequence<Acc...>>::type,
        typename unique_impl<Sequence<Tail...>,
                             Sequence<Acc..., H>>::type>;
};

} // namespace detail

template <typename Seq>
using unique_t = typename detail::unique_impl<Seq>::type;

// ---------------------------------------------------------------------------
// 10.7 index_of — find index of type in a sequence
// ---------------------------------------------------------------------------
template <typename Seq, typename T>
consteval std::size_t index_of() {
    return Seq::as_type_list::template index_of<T>();
}

// ============================================================================
//  SECTION 11: COMPILE-TIME HASHING & SCHEMA
// ============================================================================

// ---------------------------------------------------------------------------
// 11.1 FNV-1a hash — constexpr string hashing
// ---------------------------------------------------------------------------
consteval std::uint64_t fnv1a_hash(std::string_view sv) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    for (char c : sv) {
        hash ^= static_cast<std::uint64_t>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

// ---------------------------------------------------------------------------
// 11.2 type_hash<T>() — hash of type name
// ---------------------------------------------------------------------------
template <typename T>
consteval std::uint64_t type_hash() noexcept {
    return fnv1a_hash(type_name<T>());
}

// ---------------------------------------------------------------------------
// 11.3 schema_hash<T>() — structural hash of a reflected type
// ---------------------------------------------------------------------------
template <typename T>
    requires Reflectable<T>
consteval std::uint64_t schema_hash() noexcept {
    using Seq = reflect_t<T>;
    std::uint64_t hash = fnv1a_hash(type_name<T>());
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        ((hash ^= fnv1a_hash(Seq::template element<I>::name()),
          hash ^= type_hash<typename Seq::template element<I>::value_type>()),
         ...);
    }(std::make_index_sequence<Seq::size>{});
    return hash;
}

// ============================================================================
//  SECTION 12: DIAGNOSTICS & VALIDATION
// ============================================================================

// ---------------------------------------------------------------------------
// 12.1 validate_reflection<T>() — sanity check for reflected types
// ---------------------------------------------------------------------------
template <typename T>
    requires Reflectable<T>
consteval bool validate_reflection() {
    using Seq = reflect_t<T>;
    // Basic checks: all indices are unique and sequential
    bool valid = true;
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        valid = ((Seq::template element<I>::index() == I) && ...);
    }(std::make_index_sequence<Seq::size>{});
    return valid;
}

// ---------------------------------------------------------------------------
// 12.2 field_names<T>() — array of all field name string_views
// ---------------------------------------------------------------------------
template <typename T>
    requires Reflectable<T>
consteval auto field_names() {
    using Seq = reflect_t<T>;
    return []<std::size_t... I>(std::index_sequence<I...>) {
        return std::array<std::string_view, Seq::size>{
            Seq::template element<I>::name()...};
    }(std::make_index_sequence<Seq::size>{});
}

// ---------------------------------------------------------------------------
// 12.3 field_types_are<T, Pred>() — check all field types satisfy a predicate
// ---------------------------------------------------------------------------
template <typename T, template <typename> class Pred>
    requires Reflectable<T>
consteval bool field_types_are() {
    using Seq = reflect_t<T>;
    bool result = true;
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        result =
            (Pred<typename Seq::template element<I>::value_type>::value && ...);
    }(std::make_index_sequence<Seq::size>{});
    return result;
}

// ============================================================================
//  SECTION 13: COMPARISON & EQUALITY
// ============================================================================

// Compile-time structural equality — compares all reflected fields
template <typename T>
    requires Reflectable<T>
constexpr bool structural_equal(const T &a, const T &b) {
    using Seq = reflect_t<T>;
    bool eq   = true;
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        ((eq = eq && (Seq::template element<I>::get(a) ==
                      Seq::template element<I>::get(b))),
         ...);
    }(std::make_index_sequence<Seq::size>{});
    return eq;
}

// Structural less-than — lexicographic comparison
template <typename T>
    requires Reflectable<T>
constexpr bool structural_less(const T &a, const T &b) {
    using Seq      = reflect_t<T>;
    int cmp_result = 0; // -1 less, 0 equal, 1 greater
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        ((cmp_result == 0
              ? (Seq::template element<I>::get(a) <
                         Seq::template element<I>::get(b)
                     ? (cmp_result = -1)
                 : Seq::template element<I>::get(b) <
                         Seq::template element<I>::get(a)
                     ? (cmp_result = 1)
                     : 0)
              : 0),
         ...);
    }(std::make_index_sequence<Seq::size>{});
    return cmp_result < 0;
}

} // namespace meta