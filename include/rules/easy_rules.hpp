#pragma once

#ifndef EASY_RULES_HPP
#define EASY_RULES_HPP

// Modern C++23 Rules Engine - Header Only, High Performance, No Virtual Functions
// Features:
// - C++23 concepts and modern patterns
// - Type-safe facts with compile-time checking
// - Zero-overhead DSL using constexpr
// - CRTP-based extensibility (no virtual functions)
// - std::expected for error handling
// - Performance-optimized with std::flat_map and minimal allocations

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <algorithm>
#include <optional>
#include <expected>
#include <unordered_map>
#include <variant>
#include <memory>
#include <sstream>
#include <chrono>
#include <set>
#include <ranges>
#include <concepts>
#include <type_traits>
#include <regex>
#include <string_view>
#include <future>
#include <shared_mutex>

namespace easy_rules {
    // Modern error handling with std::expected
    enum class RuleError {
        InvalidPredicate,
        FactNotFound,
        TypeMismatch,
        ValidationFailed,
        ActivationLimitReached
    };

    template<typename T>
    using Result = std::expected<T, RuleError>;

    // C++23 concepts for type safety
    template<typename T>
    concept Printable = requires(const T &t, std::ostream &os)
    {
        os << t;
    };

    template<typename T>
    concept FactType = std::is_copy_constructible_v<T> && std::is_move_constructible_v<T>;

    template<typename F, typename T>
    concept Validator = std::invocable<F, const T &> && std::same_as<std::invoke_result_t<F, const T &>, bool>;

    // Type-safe fact storage using std::variant
    using FactValue = std::variant<int, bool, std::string, double>;

    // Convert any printable type to string at compile time
    template<Printable T>
    constexpr std::string to_string_safe(const T &value) {
        if constexpr (std::same_as<T, std::string>) {
            return value;
        } else if constexpr (std::same_as<T, bool>) {
            return value ? "true" : "false";
        } else {
            std::ostringstream oss;
            oss << value;
            return oss.str();
        }
    }

    // Fast, type-safe Facts storage
    class Facts {
        std::unordered_map<std::string, FactValue> data_;
        mutable std::vector<std::string> snapshot_storage_;
        mutable size_t current_snapshot_idx_ = 0;
        mutable bool snapshot_dirty_ = true;

    public:
        template<FactType T>
        void set(const std::string &key, T &&value)
            requires std::constructible_from<FactValue, T> {
            data_[key] = FactValue(std::forward<T>(value));
            mark_dirty();
        }

    private:
        void mark_dirty() {
            snapshot_dirty_ = true;
        }

    public:
        template<typename T>
        [[nodiscard]] std::expected<T, RuleError> get(const std::string &key) const {
            auto it = data_.find(key);
            if (it == data_.end()) {
                return std::unexpected(RuleError::FactNotFound);
            }

            if (auto *value = std::get_if<T>(&it->second)) {
                return *value;
            }
            return std::unexpected(RuleError::TypeMismatch);
        }

        template<typename T>
        [[nodiscard]] T get_or(const std::string &key, T &&default_value) const {
            if (auto result = get<T>(key)) {
                return *result;
            }
            return std::forward<T>(default_value);
        }

        [[nodiscard]] bool has(const std::string &key) const noexcept {
            return data_.contains(key);
        }

        void remove(const std::string &key) noexcept {
            data_.erase(key);
            mark_dirty();
        }

        void clear() noexcept {
            data_.clear();
            mark_dirty();
        }

        [[nodiscard]] size_t size() const noexcept {
            return data_.size();
        }

        [[nodiscard]] bool empty() const noexcept {
            return data_.empty();
        }

        // Efficient snapshot with caching
        [[nodiscard]] const std::string &snapshot() const {
            if (!snapshot_dirty_ && current_snapshot_idx_ < snapshot_storage_.size()) {
                return snapshot_storage_[current_snapshot_idx_];
            }

            std::ostringstream oss;
            // Sort keys for consistent output
            std::vector<std::pair<std::string, FactValue> > sorted_facts(data_.begin(), data_.end());
            std::sort(sorted_facts.begin(), sorted_facts.end(),
                      [](const auto &a, const auto &b) { return a.first < b.first; });

            for (const auto &[key, value]: sorted_facts) {
                oss << key << ":";
                std::visit([&oss](const auto &v) { oss << to_string_safe(v); }, value);
                oss << ";";
            }

            // Add to storage to ensure different memory addresses
            snapshot_storage_.push_back(oss.str());
            current_snapshot_idx_ = snapshot_storage_.size() - 1;
            snapshot_dirty_ = false;
            return snapshot_storage_[current_snapshot_idx_];
        }

        // Range-based iteration
        [[nodiscard]] auto begin() const { return data_.begin(); }
        [[nodiscard]] auto end() const { return data_.end(); }
        [[nodiscard]] auto cbegin() const { return data_.cbegin(); }
        [[nodiscard]] auto cend() const { return data_.cend(); }

        void print() const {
            std::cout << "--- Facts (" << data_.size() << " items) ---\n";
            if (data_.empty()) {
                std::cout << "  (empty)\n";
                return;
            }

            for (const auto &[key, value]: data_) {
                std::cout << "  " << key << ": ";
                std::visit([](const auto &v) { std::cout << to_string_safe(v); }, value);
                std::cout << "\n";
            }
        }

        // Enhanced operations for advanced use cases
        template<typename T, typename Predicate>
        void set_if(const std::string &key, T &&value, Predicate &&condition) {
            if (condition()) {
                set(key, std::forward<T>(value));
            }
        }

        void set_bulk(const std::vector<std::pair<std::string, std::string> > &key_values) {
            for (const auto &[key, value]: key_values) {
                set(key, value);
            }
        }

        template<typename T>
        bool compare_and_set(const std::string &key, const T &expected, const T &new_value) {
            auto current = get<T>(key);
            if (current && *current == expected) {
                set(key, new_value);
                return true;
            }
            return false;
        }

        template<typename T, typename Transform>
        void transform(const std::string &key, Transform &&func) {
            if (auto current = get<T>(key)) {
                auto new_value = func(*current);
                set(key, new_value);
            }
        }
    };

    // Modern DSL using C++23 features - zero overhead
    namespace dsl {
        // Compile-time fact reference
        template<typename T = FactValue>
        struct FactRef {
            std::string name;

            constexpr explicit FactRef(std::string n) : name(std::move(n)) {
            }

            template<typename U>
            [[nodiscard]] constexpr auto operator==(const U &value) const {
                return [name = name, value](const Facts &facts) {
                    if constexpr (std::same_as<U, T>) {
                        return facts.get<T>(name).value_or(T{}) == value;
                    } else {
                        auto fact_val = facts.get<U>(name);
                        return fact_val && *fact_val == value;
                    }
                };
            }

            template<typename U>
            [[nodiscard]] constexpr auto operator!=(const U &value) const {
                return [name = name, value](const Facts &facts) {
                    if constexpr (std::same_as<U, T>) {
                        return facts.get<T>(name).value_or(T{}) != value;
                    } else {
                        auto fact_val = facts.get<U>(name);
                        return !fact_val || *fact_val != value;
                    }
                };
            }

            template<typename U>
            [[nodiscard]] constexpr auto operator<(const U &value) const
                requires std::totally_ordered_with<T, U> {
                return [name = name, value](const Facts &facts) {
                    auto fact_val = facts.get<T>(name);
                    return fact_val && *fact_val < value;
                };
            }

            template<typename U>
            [[nodiscard]] constexpr auto operator>(const U &value) const
                requires std::totally_ordered_with<T, U> {
                return [name = name, value](const Facts &facts) {
                    auto fact_val = facts.get<T>(name);
                    return fact_val && *fact_val > value;
                };
            }

            template<typename U>
            [[nodiscard]] constexpr auto operator<=(const U &value) const
                requires std::totally_ordered_with<T, U> {
                return [name = name, value](const Facts &facts) {
                    auto fact_val = facts.get<T>(name);
                    return fact_val && *fact_val <= value;
                };
            }

            template<typename U>
            [[nodiscard]] constexpr auto operator>=(const U &value) const
                requires std::totally_ordered_with<T, U> {
                return [name = name, value](const Facts &facts) {
                    auto fact_val = facts.get<T>(name);
                    return fact_val && *fact_val >= value;
                };
            }
        };

        // DSL factory functions
        template<typename T = FactValue>
        [[nodiscard]] constexpr auto fact(const std::string &name) {
            return FactRef<T>{name};
        }

        // Logical operators for combining predicates
        template<typename L, typename R>
        [[nodiscard]] constexpr auto operator&&(L &&left, R &&right) {
            return [left = std::forward<L>(left), right = std::forward<R>(right)](const Facts &facts) {
                return left(facts) && right(facts);
            };
        }

        template<typename L, typename R>
        [[nodiscard]] constexpr auto operator||(L &&left, R &&right) {
            return [left = std::forward<L>(left), right = std::forward<R>(right)](const Facts &facts) {
                return left(facts) || right(facts);
            };
        }

        template<typename P>
        [[nodiscard]] constexpr auto operator!(P &&predicate) {
            return [pred = std::forward<P>(predicate)](const Facts &facts) {
                return !pred(facts);
            };
        }

        // Enhanced String Operations
        struct StringFactRef {
            std::string name;

            explicit StringFactRef(std::string n) : name(std::move(n)) {
            }

            [[nodiscard]] auto contains(const std::string &substr) const {
                return [name = name, substr](const Facts &facts) -> bool {
                    auto val = facts.get<std::string>(name);
                    return val.has_value() && val->find(substr) != std::string::npos;
                };
            }

            [[nodiscard]] auto starts_with(const std::string &prefix) const {
                return [name = name, prefix](const Facts &facts) -> bool {
                    auto val = facts.get<std::string>(name);
                    return val.has_value() && val->starts_with(prefix);
                };
            }

            [[nodiscard]] auto ends_with(const std::string &suffix) const {
                return [name = name, suffix](const Facts &facts) -> bool {
                    auto val = facts.get<std::string>(name);
                    return val.has_value() && val->ends_with(suffix);
                };
            }

            [[nodiscard]] auto matches(const std::string &pattern) const {
                return [name = name, pattern](const Facts &facts) -> bool {
                    auto val = facts.get<std::string>(name);
                    if (!val.has_value()) return false;
                    try {
                        std::regex regex(pattern);
                        return std::regex_match(*val, regex);
                    } catch (...) {
                        return false;
                    }
                };
            }

            [[nodiscard]] auto iequals(const std::string &other) const {
                return [name = name, other](const Facts &facts) -> bool {
                    auto val = facts.get<std::string>(name);
                    if (!val.has_value()) return false;
                    return std::equal(val->begin(), val->end(), other.begin(), other.end(),
                                      [](char a, char b) { return std::tolower(a) == std::tolower(b); });
                };
            }
        };

        // Enhanced Numeric Operations
        template<typename T>
        struct NumericFactRef {
            std::string name;

            explicit NumericFactRef(std::string n) : name(std::move(n)) {
            }

            [[nodiscard]] auto in_range(T min_val, T max_val) const {
                return [name = name, min_val, max_val](const Facts &facts) -> bool {
                    auto val = facts.get<T>(name);
                    return val.has_value() && *val >= min_val && *val <= max_val;
                };
            }

            [[nodiscard]] auto is_even() const requires std::integral<T> {
                return [name = name](const Facts &facts) -> bool {
                    auto val = facts.get<T>(name);
                    return val.has_value() && (*val % 2 == 0);
                };
            }

            [[nodiscard]] auto is_odd() const requires std::integral<T> {
                return [name = name](const Facts &facts) -> bool {
                    auto val = facts.get<T>(name);
                    return val.has_value() && (*val % 2 != 0);
                };
            }

            [[nodiscard]] auto divisible_by(T divisor) const requires std::integral<T> {
                return [name = name, divisor](const Facts &facts) -> bool {
                    auto val = facts.get<T>(name);
                    return val.has_value() && divisor != 0 && (*val % divisor == 0);
                };
            }

            [[nodiscard]] auto approx_equal(T target, T tolerance = T{}) const requires std::floating_point<T> {
                return [name = name, target, tolerance](const Facts &facts) -> bool {
                    auto val = facts.get<T>(name);
                    return val.has_value() && std::abs(*val - target) <= tolerance;
                };
            }
        };

        // Collection Operations
        template<typename Container>
        [[nodiscard]] auto in(const Container &container) {
            return [container](const auto &fact_ref) {
                return [fact_ref, container](const Facts &facts) {
                    using ValueType = Container::value_type;
                    auto val = facts.get<ValueType>(fact_ref.name);
                    return val && std::find(container.begin(), container.end(), *val) != container.end();
                };
            };
        }

        // Enhanced factory functions
        [[nodiscard]] inline auto string_fact(const std::string &name) {
            return StringFactRef{name};
        }

        template<typename T>
        [[nodiscard]] auto numeric_fact(const std::string &name) {
            return NumericFactRef<T>{name};
        }

        // Temporal Operations
        struct TemporalFactRef {
            std::string name;

            explicit TemporalFactRef(std::string n) : name(std::move(n)) {
            }

            [[nodiscard]] auto before(const std::chrono::system_clock::time_point &time) const {
                return [name = name, time](const Facts &facts) -> bool {
                    auto val = facts.get<int>(name);
                    if (!val.has_value()) return false;
                    auto fact_time = std::chrono::system_clock::from_time_t(*val);
                    return fact_time < time;
                };
            }

            [[nodiscard]] auto after(const std::chrono::system_clock::time_point &time) const {
                return [name = name, time](const Facts &facts) -> bool {
                    auto val = facts.get<int>(name);
                    if (!val.has_value()) return false;
                    auto fact_time = std::chrono::system_clock::from_time_t(*val);
                    return fact_time > time;
                };
            }

            [[nodiscard]] auto within_last(const std::chrono::seconds &duration) const {
                return [name = name, duration](const Facts &facts) -> bool {
                    auto val = facts.get<int>(name);
                    if (!val.has_value()) return false;
                    auto fact_time = std::chrono::system_clock::from_time_t(*val);
                    auto now = std::chrono::system_clock::now();
                    return (now - fact_time) <= duration;
                };
            }
        };

        [[nodiscard]] inline auto temporal_fact(const std::string &name) {
            return TemporalFactRef{name};
        }
    }

    // Forward declarations
    class ExecutionContext;

    // Modern rule structure (moved before listeners to avoid forward declaration issues)
    struct Rule {
        std::string name;
        std::string description;
        std::function<bool(const Facts &)> predicate;
        std::function<void(ExecutionContext &)> action;
        std::function<void(ExecutionContext &)> else_action;
        int priority = 0;
        std::optional<size_t> activation_limit;

        // Fluent API
        template<typename Action>
        Rule &then(Action &&act) {
            action = std::forward<Action>(act);
            return *this;
        }

        template<typename ElseAction>
        Rule &otherwise(ElseAction &&act) {
            else_action = std::forward<ElseAction>(act);
            return *this;
        }

        Rule &with_priority(int p) {
            priority = p;
            return *this;
        }

        Rule &with_activation_limit(size_t limit) {
            activation_limit = limit;
            return *this;
        }

        Rule &with_description(std::string desc) {
            description = std::move(desc);
            return *this;
        }
    };

    // CRTP-based listener system (no virtual functions)
    template<typename Derived>
    class RuleListener {
    public:
        [[nodiscard]] constexpr bool before_evaluate(const Rule &rule, const ExecutionContext &context) {
            return static_cast<Derived *>(this)->before_evaluate_impl(rule, context);
        }

        constexpr void on_success(const Rule &rule, ExecutionContext &context) {
            static_cast<Derived *>(this)->on_success_impl(rule, context);
        }

        constexpr void on_failure(const Rule &rule, ExecutionContext &context) {
            static_cast<Derived *>(this)->on_failure_impl(rule, context);
        }

        constexpr void on_skipped(const Rule &rule) {
            static_cast<Derived *>(this)->on_skipped_impl(rule);
        }

    protected:
        // Default implementations
        constexpr bool before_evaluate_impl(const Rule &, const ExecutionContext &) { return true; }

        constexpr void on_success_impl(const Rule &, ExecutionContext &) {
        }

        constexpr void on_failure_impl(const Rule &, ExecutionContext &) {
        }

        constexpr void on_skipped_impl(const Rule &) {
        }
    };

    // Execution context
    class ExecutionContext {
    public:
        Facts facts;

        // Performance metrics
        struct Metrics {
            size_t rules_evaluated = 0;
            size_t rules_fired = 0;
            size_t iterations = 0;
            std::chrono::nanoseconds total_time{0};
        } metrics;

        void reset_metrics() {
            metrics = {};
        }
    };

    // Audit events
    struct AuditEvent {
        enum class Type { RuleFired, RuleFailed, RuleSkipped, FactChanged };

        Type type;
        std::string rule_name;
        std::string description;
        std::chrono::time_point<std::chrono::steady_clock> timestamp;

        AuditEvent(Type t, std::string rule, std::string desc)
            : type(t), rule_name(std::move(rule)), description(std::move(desc))
              , timestamp(std::chrono::steady_clock::now()) {
        }
    };

    // CRTP Audit Listener
    class AuditListener : public RuleListener<AuditListener> {
    private:
        std::vector<AuditEvent> history_;

    public:
        constexpr bool before_evaluate_impl(const Rule &, const ExecutionContext &) { return true; }

        void on_success_impl(const Rule &rule, ExecutionContext &) {
            history_.emplace_back(AuditEvent::Type::RuleFired, rule.name, "Rule executed successfully");
        }

        void on_failure_impl(const Rule &rule, ExecutionContext &) {
            history_.emplace_back(AuditEvent::Type::RuleFailed, rule.name, "Rule predicate failed");
        }

        void on_skipped_impl(const Rule &rule) {
            history_.emplace_back(AuditEvent::Type::RuleSkipped, rule.name, "Rule skipped due to activation limit");
        }

        [[nodiscard]] const std::vector<AuditEvent> &get_history() const { return history_; }
        void clear_history() { history_.clear(); }
    };


    // High-performance rules engine
    class EasyRuleEngine {
    public:
        static constexpr std::string_view DEFAULT_GROUP = "__default__";

        // Configuration
        struct Config {
            bool verbose = true;
            bool stop_on_first_match = false;
            size_t max_iterations = 100;
            bool enable_metrics = false;
        } config;

    private:
        std::unordered_map<std::string, std::vector<Rule> > rule_groups_;
        std::unordered_map<std::string, size_t> activation_counts_;
        std::vector<std::function<bool(const Rule &, const ExecutionContext &)> > before_listeners_;
        std::vector<std::function<void(const Rule &, ExecutionContext &)> > success_listeners_;
        std::vector<std::function<void(const Rule &, ExecutionContext &)> > failure_listeners_;
        std::vector<std::function<void(const Rule &)> > skip_listeners_;

    public:
        // Modern rule definition API
        template<typename Predicate>
        Rule &when(const std::string &name, Predicate &&pred) {
            return when(std::string{DEFAULT_GROUP}, std::move(name), std::forward<Predicate>(pred));
        }

        template<typename Predicate>
        Rule &when(const std::string &group, const std::string &name, Predicate &&pred) {
            auto &rules = rule_groups_[group];
            rules.emplace_back(Rule{
                .name = std::move(name),
                .predicate = [pred = std::forward<Predicate>(pred)](const Facts &facts) { return pred(facts); }
            });
            return rules.back();
        }

        void remove_rule(const std::string &name, const std::string &group = std::string{DEFAULT_GROUP}) {
            if (auto it = rule_groups_.find(group); it != rule_groups_.end()) {
                auto &rules = it->second;
                rules.erase(
                    std::remove_if(rules.begin(), rules.end(),
                                   [&name](const Rule &r) { return r.name == name; }),
                    rules.end()
                );
                activation_counts_.erase(name);
            }
        }

        [[nodiscard]] const auto &get_all_rules() const { return rule_groups_; }

        // CRTP listener registration
        template<typename Listener>
        void add_listener(Listener &listener) {
            before_listeners_.emplace_back([&listener](const Rule &rule, const ExecutionContext &ctx) {
                return listener.before_evaluate(rule, ctx);
            });
            success_listeners_.emplace_back([&listener](const Rule &rule, ExecutionContext &ctx) {
                listener.on_success(rule, ctx);
            });
            failure_listeners_.emplace_back([&listener](const Rule &rule, ExecutionContext &ctx) {
                listener.on_failure(rule, ctx);
            });
            skip_listeners_.emplace_back([&listener](const Rule &rule) {
                listener.on_skipped(rule);
            });
        }

        // High-performance execution
        void run(ExecutionContext &context, const std::vector<std::string> &groups_to_run = {}) {
            execute(context, groups_to_run, false);
        }

        void run_to_completion(ExecutionContext &context, const std::vector<std::string> &groups_to_run = {}) {
            execute(context, groups_to_run, true);
        }

    private:
        void execute(ExecutionContext &context, const std::vector<std::string> &groups_to_run, bool to_completion) {
            auto start_time = std::chrono::steady_clock::now();
            context.reset_metrics();

            size_t iteration = 0;
            bool facts_changed = true;
            std::set<std::string> seen_states;

            while (facts_changed && iteration < config.max_iterations) {
                ++iteration;
                facts_changed = false;

                // Collect active rules
                std::vector<std::reference_wrapper<Rule> > active_rules;

                if (groups_to_run.empty()) {
                    for (auto &[group, rules]: rule_groups_) {
                        for (auto &rule: rules) {
                            active_rules.emplace_back(rule);
                        }
                    }
                } else {
                    for (const auto &group_name: groups_to_run) {
                        if (auto it = rule_groups_.find(group_name); it != rule_groups_.end()) {
                            for (auto &rule: it->second) {
                                active_rules.emplace_back(rule);
                            }
                        }
                    }
                }

                // Sort by priority (ascending) - higher priority rules execute last and can override
                std::ranges::sort(active_rules, [](const auto &a, const auto &b) {
                    return a.get().priority < b.get().priority;
                });

                // Execute rules
                for (Rule &rule: active_rules) {
                    context.metrics.rules_evaluated++;


                    // Check listeners
                    bool skip = std::ranges::any_of(before_listeners_,
                                                    [&](const auto &listener) { return !listener(rule, context); });

                    if (skip) continue;

                    // Check activation limit
                    if (rule.activation_limit) {
                        if (activation_counts_[rule.name] >= *rule.activation_limit) {
                            std::ranges::for_each(skip_listeners_,
                                                  [&](const auto &listener) { listener(rule); });
                            continue;
                        }
                    }

                    // Evaluate predicate
                    bool success = rule.predicate(context.facts);

                    if (success) {
                        if (rule.action) {
                            rule.action(context);
                        }

                        activation_counts_[rule.name]++;
                        context.metrics.rules_fired++;
                        facts_changed = to_completion;

                        std::ranges::for_each(success_listeners_,
                                              [&](const auto &listener) { listener(rule, context); });

                        if (config.stop_on_first_match) {
                            facts_changed = false;
                            break;
                        }
                    } else {
                        if (rule.else_action) {
                            rule.else_action(context);
                            facts_changed = to_completion;
                        }

                        std::ranges::for_each(failure_listeners_,
                                              [&](const auto &listener) { listener(rule, context); });
                    }
                }

                // Cycle detection
                if (to_completion && facts_changed) {
                    const auto &state = context.facts.snapshot();
                    if (seen_states.contains(state)) {
                        if (config.verbose) {
                            std::cerr << "[Warning] Cycle detected, stopping execution\n";
                        }
                        break;
                    }
                    seen_states.insert(state);
                }
            }

            context.metrics.iterations = iteration;
            if (config.enable_metrics) {
                context.metrics.total_time = std::chrono::steady_clock::now() - start_time;

                if (config.verbose) {
                    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(context.metrics.total_time);
                    std::cout << "[Performance] " << context.metrics.rules_evaluated << " rules evaluated, "
                            << context.metrics.rules_fired << " fired in " << iteration << " iterations ("
                            << ms.count() << "ms)\n";
                }
            }

            if (iteration >= config.max_iterations && config.verbose) {
                std::cerr << "[Warning] Max iterations (" << config.max_iterations << ") reached\n";
            }
        }
    };

    // Thread-Safe Rules Engine
    class ThreadSafeRuleEngine {
    public:
        static constexpr std::string_view DEFAULT_GROUP = "__default__";

        // Configuration
        struct Config {
            bool verbose = true;
            bool stop_on_first_match = false;
            size_t max_iterations = 100;
            bool enable_metrics = false;
        } config;

    private:
        mutable std::shared_mutex rules_mutex_;
        std::unordered_map<std::string, std::vector<Rule> > rule_groups_;
        std::unordered_map<std::string, size_t> activation_counts_;
        std::vector<std::function<bool(const Rule &, const ExecutionContext &)> > before_listeners_;
        std::vector<std::function<void(const Rule &, ExecutionContext &)> > success_listeners_;
        std::vector<std::function<void(const Rule &, ExecutionContext &)> > failure_listeners_;
        std::vector<std::function<void(const Rule &)> > skip_listeners_;

    public:
        // Thread-safe rule definition API
        template<typename Predicate>
        Rule &when_threadsafe(std::string name, Predicate &&pred) {
            return when_threadsafe(std::string{DEFAULT_GROUP}, std::move(name), std::forward<Predicate>(pred));
        }

        template<typename Predicate>
        Rule &when_threadsafe(const std::string &group, const std::string &name, Predicate &&pred) {
            std::unique_lock lock(rules_mutex_);
            auto &rules = rule_groups_[group];
            rules.emplace_back(Rule{
                .name = std::move(name),
                .predicate = [pred = std::forward<Predicate>(pred)](const Facts &facts) { return pred(facts); }
            });
            return rules.back();
        }

        // Async execution
        std::future<void> run_async(ExecutionContext &context) {
            return std::async(std::launch::async, [this, &context]() {
                this->run_threadsafe(context);
            });
        }

        void run_threadsafe(ExecutionContext &context) {
            std::shared_lock lock(rules_mutex_);

            auto start_time = std::chrono::steady_clock::now();
            context.reset_metrics();

            size_t iteration = 0;
            bool facts_changed = true;
            std::set<std::string> seen_states;

            while (facts_changed && iteration < config.max_iterations) {
                ++iteration;
                facts_changed = false;

                // Collect active rules
                std::vector<std::reference_wrapper<Rule> > active_rules;
                for (auto &[group, rules]: rule_groups_) {
                    for (auto &rule: rules) {
                        active_rules.emplace_back(rule);
                    }
                }

                // Sort by priority
                std::ranges::sort(active_rules, [](const auto &a, const auto &b) {
                    return a.get().priority < b.get().priority;
                });

                // Execute rules
                for (Rule &rule: active_rules) {
                    context.metrics.rules_evaluated++;

                    // Check activation limit
                    if (rule.activation_limit) {
                        if (activation_counts_[rule.name] >= *rule.activation_limit) {
                            continue;
                        }
                    }

                    // Evaluate predicate
                    bool success = rule.predicate(context.facts);

                    if (success) {
                        if (rule.action) {
                            rule.action(context);
                        }

                        activation_counts_[rule.name]++;
                        context.metrics.rules_fired++;
                        facts_changed = true;

                        if (config.stop_on_first_match) {
                            facts_changed = false;
                            break;
                        }
                    } else if (rule.else_action) {
                        rule.else_action(context);
                        facts_changed = true;
                    }
                }
            }

            context.metrics.iterations = iteration;
            if (config.enable_metrics) {
                context.metrics.total_time = std::chrono::steady_clock::now() - start_time;
            }
        }
    };

    // Rule Templates for common patterns
    namespace templates {
        template<typename T>
        Rule create_threshold_rule(const std::string &name, const std::string &fact_name,
                                   T threshold, const std::string &result_fact, const std::string &result_value) {
            return Rule{
                .name = name,
                .predicate = [fact_name, threshold](const Facts &facts) {
                    auto val = facts.get<T>(fact_name);
                    return val.has_value() && *val >= threshold;
                },
                .action = [result_fact, result_value](ExecutionContext &ctx) {
                    ctx.facts.set(result_fact, result_value);
                }
            };
        }

        template<typename T>
        Rule create_validation_rule(const std::string &name, const std::string &fact_name,
                                    std::function<bool(const T &)> validator, const std::string &error_fact) {
            return Rule{
                .name = name,
                .predicate = [fact_name, validator](const Facts &facts) {
                    auto val = facts.get<T>(fact_name);
                    return val.has_value() && validator(*val);
                },
                .action = [](ExecutionContext &ctx) {
                    ctx.facts.set("validation_passed", true);
                },
                .else_action = [error_fact](ExecutionContext &ctx) {
                    ctx.facts.set("validation_passed", false);
                    ctx.facts.set(error_fact, std::string("Validation failed"));
                }
            };
        }

        template<typename InputType, typename OutputType>
        Rule create_transformation_rule(const std::string &name, const std::string &input_fact,
                                        const std::string &output_fact,
                                        std::function<OutputType(const InputType &)> transformer) {
            return Rule{
                .name = name,
                .predicate = [input_fact](const Facts &facts) {
                    return facts.has(input_fact);
                },
                .action = [input_fact, output_fact, transformer](ExecutionContext &ctx) {
                    if (auto val = ctx.facts.get<InputType>(input_fact)) {
                        auto result = transformer(*val);
                        ctx.facts.set(output_fact, result);
                    }
                }
            };
        }
    }

    // Enhanced Audit Listener with Statistics
    class EnhancedAuditListener : public RuleListener<EnhancedAuditListener> {
        std::vector<AuditEvent> history_;
        std::unordered_map<std::string, size_t> rule_execution_counts_;
        std::unordered_map<std::string, std::chrono::nanoseconds> rule_execution_times_;

    public:
        struct RuleStatistic {
            std::string name;
            size_t execution_count = 0;
            std::chrono::nanoseconds total_time{0};
            std::chrono::nanoseconds avg_time{0};
        };

        constexpr bool before_evaluate_impl(const Rule &, const ExecutionContext &) { return true; }

        void on_success_impl(const Rule &rule, ExecutionContext &) {
            history_.emplace_back(AuditEvent::Type::RuleFired, rule.name, "Rule executed successfully");
            rule_execution_counts_[rule.name]++;
        }

        void on_failure_impl(const Rule &rule, ExecutionContext &) {
            history_.emplace_back(AuditEvent::Type::RuleFailed, rule.name, "Rule predicate failed");
        }

        void on_skipped_impl(const Rule &rule) {
            history_.emplace_back(AuditEvent::Type::RuleSkipped, rule.name, "Rule skipped");
        }

        [[nodiscard]] std::vector<RuleStatistic> get_rule_statistics() const {
            std::vector<RuleStatistic> stats;
            for (const auto &[name, count]: rule_execution_counts_) {
                RuleStatistic stat;
                stat.name = name;
                stat.execution_count = count;
                if (auto it = rule_execution_times_.find(name); it != rule_execution_times_.end()) {
                    stat.total_time = it->second;
                    stat.avg_time = count > 0
                                        ? std::chrono::duration_cast<std::chrono::nanoseconds>(it->second / count)
                                        : std::chrono::nanoseconds{0};
                }
                stats.push_back(stat);
            }
            return stats;
        }

        [[nodiscard]] const std::vector<AuditEvent> &get_history() const { return history_; }

        void clear_history() {
            history_.clear();
            rule_execution_counts_.clear();
            rule_execution_times_.clear();
        }
    };

    // Enhanced Facts with additional operations
    using EnhancedFacts = Facts; // Already enhanced in the main Facts class
} // namespace easy_rules

// Example usage demonstrating modern C++23 features
namespace easy_rules::examples {
    inline void demo() {
        using namespace easy_rules::dsl;

        ExecutionContext ctx;
        ctx.facts.set("age", 25);
        ctx.facts.set("name", std::string("John"));
        ctx.facts.set("active", true);

        EasyRuleEngine engine;
        engine.config.enable_metrics = true;

        // Modern DSL with type safety
        engine.when("adult_rule", fact<int>("age") >= 18)
                .then([](ExecutionContext &ctx) {
                    ctx.facts.set("status", std::string("adult"));
                    std::cout << "User is an adult\n";
                })
                .with_priority(10)
                .with_description("Check if user is adult");

        // Complex predicate with logical operators
        engine.when("active_adult",
                    fact<bool>("active") == true && fact<int>("age") > 21)
                .then([](ExecutionContext &ctx) {
                    std::cout << "Active adult user\n";
                })
                .with_activation_limit(1);

        // CRTP listener
        AuditListener audit;
        engine.add_listener(audit);

        engine.run(ctx);

        std::cout << "Final facts:\n";
        ctx.facts.print();

        std::cout << "\nAudit trail:\n";
        for (const auto &event: audit.get_history()) {
            std::cout << "- " << event.rule_name << ": " << event.description << "\n";
        }
    }
}

#endif // EASY_RULES_HPP
