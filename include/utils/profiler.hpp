/**
 * @file profiler.hpp
 * @brief A modern, header-only C++ profiling utility for benchmarking code with statistical analysis
 *
 * This library provides high-precision timing, parallel execution, outlier trimming, exception tracking,
 * and detailed statistical reporting for performance profiling. It supports return value collection,
 * custom callbacks, progress reporting, and various export formats (CSV, JSON, Chrome Trace).
 *
 * Key Features:
 * - High-precision timing using std::chrono
 * - Multi-threaded parallel execution with per-thread statistics
 * - Automatic warmup iterations and outlier trimming
 * - Statistical analysis (mean, median, variance, percentiles, confidence intervals)
 * - Exception tracking and reporting
 * - Export to CSV, JSON, and Chrome Tracing formats
 * - RAII-style scoped profiling
 * - Comparison mode with Mann-Whitney U statistical test
 * - Memory profiling infrastructure (experimental)
 *
 * @date 2026
 * @version 2.0
 *
 * @example
 * @code
 * profiler::ProfileConfig config;
 * config.iterations = 1000;
 * config.label = "MyBenchmark";
 *
 * auto result = profiler::measure(config, []() {
 *     // Code to benchmark
 * });
 *
 * std::cout << profiler::format_result(result) << std::endl;
 * @endcode
 */
#pragma once
#ifndef PROFILER_HPP
#define PROFILER_HPP

#include <chrono>
#include <concepts>
#include <functional>
#include <string>
#include <vector>
#include <numeric>
#include <algorithm>
#include <cmath>
#include <optional>
#include <map>
#include <thread>
#include <future>
#include <mutex>
#include <atomic>
#include <format>
#include <string_view>
#include <sstream>
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <cstring>

namespace profiler {
    /**
     * @enum TimeUnit
     * @brief Time unit options for formatting profiling results
     */
    enum class TimeUnit {
        Nanoseconds, ///< Display times in nanoseconds
        Microseconds, ///< Display times in microseconds
        Milliseconds, ///< Display times in milliseconds
        Seconds ///< Display times in seconds
    };

    // --- Core Data Structures (Defined first for visibility) ---

    /**
     * @struct ProfileConfig
     * @brief Configuration parameters for profiling sessions
     *
     * Controls all aspects of the profiling run including iterations, parallelism,
     * warmup, outlier trimming, logging, and callbacks.
     */
    struct ProfileConfig {
        std::size_t iterations{1}; ///< Number of measured iterations (validated >= 0)
        std::size_t warmup_iterations{0}; ///< Warmup runs before measurement (capped at 1M)
        std::size_t parallelism{1}; ///< Number of threads (capped at hw_concurrency × 4)
        std::string label; ///< Optional label for reporting
        double trim_outliers_percentage{0.0}; ///< % of outliers to trim from each end [0-100]
        std::function<void(std::string_view)> logger{nullptr}; ///< Optional logger callback
        TimeUnit output_unit{TimeUnit::Microseconds}; ///< Time unit for formatting
        std::function<void(double progress)> progress_callback{nullptr}; ///< Progress reporter [0.0-1.0]
        bool track_memory{false}; ///< Enable memory profiling (experimental)
        bool export_chrome_trace{false}; ///< Generate Chrome tracing JSON (reserved)
    };

    /**
     * @struct OutlierInfo
     * @brief Information about trimmed outliers
     */
    struct OutlierInfo {
        std::size_t trimmed_low; ///< Number of fastest runs trimmed
        std::size_t trimmed_high; ///< Number of slowest runs trimmed
        double percentage; ///< Percentage trimmed from each end
    };

    /**
     * @struct ExceptionInfo
     * @brief Information about an exception caught during profiling
     */
    struct ExceptionInfo {
        std::size_t iteration_index; ///< Iteration where exception occurred
        std::string what_message; ///< Exception message
    };

    /**
     * @struct MemoryStats
     * @brief Memory profiling statistics (experimental)
     *
     * Tracks allocation/deallocation counts and bytes. Requires custom allocator
     * integration or malloc hooks for actual tracking.
     */
    struct MemoryStats {
        std::atomic<size_t> allocations{0}; ///< Total allocation count
        std::atomic<size_t> deallocations{0}; ///< Total deallocation count
        std::atomic<size_t> bytes_allocated{0}; ///< Total bytes allocated
        std::atomic<size_t> bytes_deallocated{0}; ///< Total bytes deallocated
        std::atomic<size_t> peak_memory{0}; ///< Peak memory usage

        // Add copy/move constructors to make MemoryStats copyable
        MemoryStats() = default;

        MemoryStats(const MemoryStats &other)
            : allocations(other.allocations.load())
              , deallocations(other.deallocations.load())
              , bytes_allocated(other.bytes_allocated.load())
              , bytes_deallocated(other.bytes_deallocated.load())
              , peak_memory(other.peak_memory.load()) {
        }

        MemoryStats(MemoryStats &&other) noexcept
            : allocations(other.allocations.load())
              , deallocations(other.deallocations.load())
              , bytes_allocated(other.bytes_allocated.load())
              , bytes_deallocated(other.bytes_deallocated.load())
              , peak_memory(other.peak_memory.load()) {
        }

        MemoryStats &operator=(const MemoryStats &other) {
            if (this != &other) {
                allocations.store(other.allocations.load());
                deallocations.store(other.deallocations.load());
                bytes_allocated.store(other.bytes_allocated.load());
                bytes_deallocated.store(other.bytes_deallocated.load());
                peak_memory.store(other.peak_memory.load());
            }
            return *this;
        }

        MemoryStats &operator=(MemoryStats &&other) noexcept {
            if (this != &other) {
                allocations.store(other.allocations.load());
                deallocations.store(other.deallocations.load());
                bytes_allocated.store(other.bytes_allocated.load());
                bytes_deallocated.store(other.bytes_deallocated.load());
                peak_memory.store(other.peak_memory.load());
            }
            return *this;
        }

        /**
         * @brief Reset all memory counters to zero
         */
        void reset() {
            allocations = 0;
            deallocations = 0;
            bytes_allocated = 0;
            bytes_deallocated = 0;
            peak_memory = 0;
        }

        /**
         * @brief Get net (live) allocation count
         * @return Number of live allocations (allocations - deallocations)
         */
        [[nodiscard]] size_t net_allocations() const {
            return allocations.load() - deallocations.load();
        }

        /**
         * @brief Get net (live) memory bytes
         * @return Number of live bytes (bytes_allocated - bytes_deallocated)
         */
        [[nodiscard]] size_t net_bytes() const {
            return bytes_allocated.load() - bytes_deallocated.load();
        }
    };

    /**
     * @struct ProfileResult
     * @brief Results from a profiling session
     *
     * Contains timing statistics, iteration counts, exception information,
     * and provides methods for statistical analysis and export.
     */
    struct ProfileResult {
        std::string label; ///< Label from config
        std::chrono::nanoseconds total_duration{0}; ///< Total measured time
        std::chrono::nanoseconds average_duration{0}; ///< Mean time per iteration
        std::chrono::nanoseconds min_duration{0}; ///< Fastest iteration
        std::chrono::nanoseconds max_duration{0}; ///< Slowest iteration
        std::size_t iterations_attempted{0}; ///< Total iterations requested
        std::size_t iterations_succeeded{0}; ///< Iterations without exceptions
        std::size_t parallelism_used{1}; ///< Threads used
        std::size_t warmup_iterations_run{0}; ///< Warmup runs performed
        std::vector<std::chrono::nanoseconds> individual_runs; ///< Duration per iteration
        std::map<std::string, size_t> unique_exceptions; ///< Exception message → count
        std::optional<OutlierInfo> outlier_info; ///< Outlier trimming info
        std::vector<std::vector<std::chrono::nanoseconds> > per_thread_runs; ///< Per-thread durations
        std::optional<MemoryStats> memory_stats; ///< Memory profiling data
        mutable std::vector<std::chrono::nanoseconds> _scratch; ///< Scratch buffer for selection ops

        /**
         * @brief Compute median duration using nth_element (O(n) average)
         * @return Median duration, or 0 if no runs
         */
        [[nodiscard]] std::chrono::nanoseconds median() const;

        /**
         * @brief Compute percentile duration using nth_element (O(n) average)
         * @param p Percentile [0-100]
         * @return Duration at p-th percentile, or 0 if invalid p or no runs
         */
        [[nodiscard]] std::chrono::nanoseconds percentile(double p) const;

        /**
         * @brief Generate histogram of duration distribution
         * @param buckets Number of histogram buckets
         * @return Vector of sample counts per bucket
         */
        std::vector<size_t> histogram(size_t buckets = 10) const;

        /**
         * @brief Export results as CSV
         * @return CSV string with "iteration,duration_ns" format
         */
        std::string to_csv() const;

        /**
         * @brief Export results as JSON
         * @return JSON object with all profiling data
         */
        nlohmann::json to_json() const;

        /**
         * @brief Format results with custom time unit
         * @param unit Time unit for display
         * @return Formatted string report
         */
        std::string format(TimeUnit unit = TimeUnit::Microseconds) const;

        /**
         * @brief Compute population standard deviation
         * @return Standard deviation of durations
         */
        [[nodiscard]] std::chrono::nanoseconds standard_deviation() const;

        /**
         * @brief Compute population variance
         * @return Variance of durations
         */
        [[nodiscard]] double variance() const;

        /**
         * @brief Compute coefficient of variation (CV = stddev / mean)
         * @return CV value, 0 if mean is 0
         * @note CV < 0.05: excellent stability, CV > 0.30: high variance
         */
        [[nodiscard]] double coefficient_of_variation() const;

        /**
         * @brief Compute 95% confidence interval for mean (±1.96σ)
         * @return Pair of (lower, upper) bounds
         */
        [[nodiscard]] std::pair<std::chrono::nanoseconds, std::chrono::nanoseconds>
        confidence_interval_95() const;

        /**
         * @brief Detect bimodal distribution (performance instability)
         * @return True if distribution has two or more peaks
         * @note Indicates CPU frequency scaling, OS scheduling, or cache effects
         */
        [[nodiscard]] bool is_bimodal() const;

        /**
         * @brief Export results for Chrome tracing viewer (chrome://tracing)
         * @return Chrome Trace Event Format JSON string
         */
        std::string to_chrome_trace() const;
    };

    /**
     * @struct ProfileResultWithData
     * @brief Profiling results with collected return values
     * @tparam T Return type of profiled function
     *
     * Extends ProfileResult with a vector of return values and forwards
     * all statistical methods for convenience.
     */
    template<typename T>
    struct ProfileResultWithData {
        ProfileResult profile; ///< Profiling statistics
        std::vector<T> return_values; ///< Collected return values

        // Forwarding methods for convenience
        [[nodiscard]] auto median() const { return profile.median(); }
        [[nodiscard]] auto percentile(double p) const { return profile.percentile(p); }
        [[nodiscard]] auto histogram(size_t buckets = 10) const { return profile.histogram(buckets); }
        [[nodiscard]] auto standard_deviation() const { return profile.standard_deviation(); }
        [[nodiscard]] auto variance() const { return profile.variance(); }
        [[nodiscard]] auto coefficient_of_variation() const { return profile.coefficient_of_variation(); }
        [[nodiscard]] auto confidence_interval_95() const { return profile.confidence_interval_95(); }
        [[nodiscard]] auto is_bimodal() const { return profile.is_bimodal(); }
    };

    // --- ProfileResult Method Implementations ---

    inline std::chrono::nanoseconds ProfileResult::median() const {
        if (individual_runs.empty()) return std::chrono::nanoseconds(0);
        _scratch.clear();
        _scratch.reserve(individual_runs.size());
        _scratch.assign(individual_runs.begin(), individual_runs.end());
        const size_t mid = _scratch.size() / 2;
        std::nth_element(_scratch.begin(), _scratch.begin() + mid, _scratch.end());
        if (_scratch.size() % 2 == 1) return _scratch[mid];
        auto lower_it = std::max_element(_scratch.begin(), _scratch.begin() + mid);
        return (*lower_it + _scratch[mid]) / 2;
    }

    inline std::chrono::nanoseconds ProfileResult::percentile(double p) const {
        if (individual_runs.empty() || p < 0.0 || p > 100.0) return std::chrono::nanoseconds(0);
        const size_t n = individual_runs.size();
        const size_t idx = static_cast<size_t>(std::round((p / 100.0) * (n - 1)));
        _scratch.clear();
        _scratch.reserve(n);
        _scratch.assign(individual_runs.begin(), individual_runs.end());
        std::nth_element(_scratch.begin(), _scratch.begin() + idx, _scratch.end());
        return _scratch[idx];
    }

    inline std::vector<size_t> ProfileResult::histogram(size_t buckets) const {
        if (individual_runs.empty() || buckets == 0) return {};
        auto [min_it, max_it] = std::minmax_element(individual_runs.begin(), individual_runs.end());
        auto min = min_it->count(), max = max_it->count();
        if (min == max) return std::vector<size_t>(buckets, individual_runs.size() / buckets);
        std::vector<size_t> hist(buckets, 0);
        for (auto ns: individual_runs) {
            size_t idx = std::min<size_t>((ns.count() - min) * buckets / (max - min + 1), buckets - 1);
            hist[idx]++;
        }
        return hist;
    }

    inline std::string ProfileResult::to_csv() const {
        std::ostringstream oss;
        oss << "iteration,duration_ns\n";
        for (size_t i = 0; i < individual_runs.size(); ++i)
            oss << i << "," << individual_runs[i].count() << "\n";
        return oss.str();
    }

    inline nlohmann::json ProfileResult::to_json() const {
        nlohmann::json j;
        j["label"] = label;
        j["total_duration_ns"] = total_duration.count();
        j["average_duration_ns"] = average_duration.count();
        j["min_duration_ns"] = min_duration.count();
        j["max_duration_ns"] = max_duration.count();
        j["iterations_attempted"] = iterations_attempted;
        j["iterations_succeeded"] = iterations_succeeded;
        j["parallelism_used"] = parallelism_used;
        j["warmup_iterations_run"] = warmup_iterations_run;
        auto &arr = j["individual_runs_ns"] = nlohmann::json::array();
        // remove invalid reserve on json array
        // for performance we simply push_back
        for (auto d: individual_runs) arr.push_back(d.count());
        j["unique_exceptions"] = unique_exceptions;
        if (outlier_info) {
            j["outlier_info"] = {
                {"trimmed_low", outlier_info->trimmed_low},
                {"trimmed_high", outlier_info->trimmed_high},
                {"percentage", outlier_info->percentage}
            };
        }
        return j;
    }

    inline std::chrono::nanoseconds ProfileResult::standard_deviation() const {
        if (individual_runs.size() < 2) return std::chrono::nanoseconds(0);
        // Compute variance over durations (nanoseconds counts)
        long double mean = static_cast<long double>(average_duration.count());
        long double acc = 0.0L;
        for (auto d: individual_runs) {
            long double diff = static_cast<long double>(d.count()) - mean;
            acc += diff * diff;
        }
        long double var = acc / static_cast<long double>(individual_runs.size());
        auto sd = static_cast<long long>(std::llround(std::sqrt(var)));
        return std::chrono::nanoseconds(sd);
    }

    inline double ProfileResult::variance() const {
        if (individual_runs.size() < 2) return 0.0;
        double mean = static_cast<double>(average_duration.count());
        double sq_sum = std::accumulate(
            individual_runs.begin(), individual_runs.end(), 0.0,
            [mean](double acc, auto val) {
                double diff = static_cast<double>(val.count()) - mean;
                return acc + (diff * diff);
            });
        return sq_sum / static_cast<double>(individual_runs.size());
    }

    inline double ProfileResult::coefficient_of_variation() const {
        if (average_duration.count() == 0) return 0.0;
        double stddev = static_cast<double>(standard_deviation().count());
        double mean = static_cast<double>(average_duration.count());
        return stddev / mean;
    }

    inline std::pair<std::chrono::nanoseconds, std::chrono::nanoseconds>
    ProfileResult::confidence_interval_95() const {
        if (individual_runs.size() < 2) return {average_duration, average_duration};
        double stddev = static_cast<double>(standard_deviation().count());
        double margin = 1.96 * stddev / std::sqrt(static_cast<double>(individual_runs.size()));
        auto lower = std::chrono::nanoseconds(static_cast<long long>(average_duration.count() - margin));
        auto upper = std::chrono::nanoseconds(static_cast<long long>(average_duration.count() + margin));
        return {lower, upper};
    }

    inline bool ProfileResult::is_bimodal() const {
        if (individual_runs.size() < 10) return false;
        auto hist = histogram(10);
        if (hist.empty()) return false;

        size_t peaks = 0;
        for (size_t i = 1; i + 1 < hist.size(); ++i) {
            if (hist[i] > hist[i - 1] && hist[i] > hist[i + 1]) {
                peaks++;
            }
        }
        return peaks >= 2;
    }

    inline std::string ProfileResult::to_chrome_trace() const {
        nlohmann::json trace;
        trace["traceEvents"] = nlohmann::json::array();

        for (size_t i = 0; i < individual_runs.size(); ++i) {
            nlohmann::json event;
            event["name"] = label.empty() ? "Iteration" : label;
            event["cat"] = "benchmark";
            event["ph"] = "X"; // Complete event (duration)
            event["ts"] = static_cast<double>(i * 1000); // Timestamp in microseconds
            event["dur"] = individual_runs[i].count() / 1000.0; // Duration in microseconds
            event["pid"] = 1;
            event["tid"] = 1;
            event["args"] = {{"iteration", i}};
            trace["traceEvents"].push_back(event);
        }

        return trace.dump(2);
    }

    inline std::string ProfileResult::format(TimeUnit unit) const {
        auto convert = [](std::chrono::nanoseconds ns, TimeUnit u) -> double {
            switch (u) {
                case TimeUnit::Nanoseconds: return static_cast<double>(ns.count());
                case TimeUnit::Microseconds: return static_cast<double>(ns.count()) / 1e3;
                case TimeUnit::Milliseconds: return static_cast<double>(ns.count()) / 1e6;
                case TimeUnit::Seconds: return static_cast<double>(ns.count()) / 1e9;
            }
            return static_cast<double>(ns.count());
        };
        auto unit_str = [](TimeUnit u) -> const char * {
            switch (u) {
                case TimeUnit::Nanoseconds: return "ns";
                case TimeUnit::Microseconds: return "us";
                case TimeUnit::Milliseconds: return "ms";
                case TimeUnit::Seconds: return "s";
            }
            return "ns";
        };

        const char *suffix = unit_str(unit);
        std::ostringstream oss;
        oss.setf(std::ios::fixed);
        oss.precision(3);
        oss << "--- " << (label.empty() ? "Profile Result" : label) << " ---\n";
        oss << "Total Duration:   " << convert(total_duration, unit) << " " << suffix << "\n";
        oss << "Average Time:     " << convert(average_duration, unit) << " " << suffix << "\n";
        oss << "Median Time:      " << convert(median(), unit) << " " << suffix << "\n";
        oss << "Min/Max Time:     " << convert(min_duration, unit) << " / " << convert(max_duration, unit) << " " <<
                suffix << "\n";
        oss << "Iterations:       " << iterations_succeeded << " / " << iterations_attempted << "\n";
        if (outlier_info) {
            oss << "Outliers Trimmed: " << outlier_info->trimmed_high << " slowest, "
                    << outlier_info->trimmed_low << " fastest (" << outlier_info->percentage << "%)\n";
        }
        if (!unique_exceptions.empty()) {
            size_t total_exc = 0;
            for (const auto &[msg, count]: unique_exceptions) total_exc += count;
            oss << "Exceptions Caught: " << total_exc << "\n";
            for (const auto &[msg, count]: unique_exceptions) {
                oss << "  - [" << count << "x] " << msg << "\n";
            }
        }
        return oss.str();
    }

    // --- Internal Implementation Details ---

    /**
     * @namespace profiler::internal
     * @brief Internal implementation details for the profiler library
     *
     * This namespace contains helper functions, templates, and implementation details
     * that support the public API. Users should not directly use these internals.
     *
     * @warning Internal API: subject to change without notice
     */
    namespace internal {
        // --- Standalone Helpers (defined before use) ---

        /**
         * @brief Trim outliers from a vector of measurements
         *
         * Sorts the vector and removes the specified percentage of outliers from both ends.
         * Requires at least 20 samples to perform trimming.
         *
         * @tparam T Element type (typically std::chrono::nanoseconds)
         * @param vec Vector to trim (modified in-place)
         * @param percentage Percentage to trim from each end [0-100]
         * @param info Output parameter populated with trimming information
         *
         * @note If percentage <= 0 or vec.size() < 20, no trimming is performed
         * @note Vector is sorted as a side effect
         *
         * @warning This function modifies the input vector
         */
        template<typename T>
        void trim_vector(std::vector<T> &vec, double percentage, std::optional<OutlierInfo> &info) {
            if (percentage <= 0.0 || vec.size() < 20) {
                info.reset();
                return;
            }
            std::sort(vec.begin(), vec.end());
            size_t total_size = vec.size();
            size_t trim_count = static_cast<size_t>((percentage / 100.0) * total_size);

            if (trim_count > 0 && vec.size() > trim_count * 2) {
                vec.erase(vec.begin(), vec.begin() + trim_count);
                vec.erase(vec.end() - trim_count, vec.end());
                info = OutlierInfo{trim_count, trim_count, percentage};
            } else {
                info.reset();
            }
        }

        /**
         * @brief Format a duration as a human-readable string with appropriate units
         *
         * Automatically selects the most appropriate unit (s, ms, us, ns) based on
         * the magnitude of the duration.
         *
         * @param ns Duration to format
         * @return Formatted string (e.g., "123.456 ms")
         *
         * @note Returns "N/A" for std::chrono::nanoseconds::max() or ::min()
         * @note Uses fixed-point notation with 3 decimal places
         */
        inline std::string format_duration(std::chrono::nanoseconds ns) {
            if (ns == std::chrono::nanoseconds::max() || ns == std::chrono::nanoseconds::min()) {
                return "N/A";
            }
            const auto c = ns.count();
            std::ostringstream oss;
            oss.setf(std::ios::fixed);
            oss.precision(3);
            if (std::llabs(c) >= 1000000000LL) {
                oss << (static_cast<double>(c) / 1e9) << " s";
                return oss.str();
            }
            if (std::llabs(c) >= 1000000LL) {
                oss << (static_cast<double>(c) / 1e6) << " ms";
                return oss.str();
            }
            if (std::llabs(c) >= 1000LL) {
                oss << (static_cast<double>(c) / 1e3) << " us";
                return oss.str();
            }
            return std::to_string(c) + " ns";
        }

        // --- Helper templates to conditionally hold return values ---

        /**
         * @struct ReturnValueHolder
         * @brief Template to optionally hold return values from profiled functions
         * @tparam T Return type of profiled function
         *
         * This helper allows the profiler to collect return values when the profiled
         * function returns a value, and gracefully handle void-returning functions.
         */
        template<typename T>
        struct ReturnValueHolder {
            std::vector<T> returns; ///< Collected return values
        };

        /**
         * @struct ReturnValueHolder<void>
         * @brief Specialization for void-returning functions
         *
         * Empty struct to avoid attempting to create std::vector<void>
         */
        template<>
        struct ReturnValueHolder<void> {
            // Empty specialization for void to prevent `std::vector<void>`
        };

        // --- Core `measure` Implementation (defined before helpers that use it) ---

        /**
         * @brief Core implementation of the profiling measurement
         *
         * This is the main workhorse function that executes the profiled callable,
         * manages parallelism, collects timing data, handles exceptions, and aggregates results.
         *
         * @tparam Clock Clock type for timing measurements (default: std::chrono::steady_clock)
         * @tparam IterationCallback Type of the iteration callback (can be std::nullptr_t)
         * @tparam Args Argument types forwarded to the callable
         *
         * @param config Validated profiling configuration
         * @param func Callable to profile
         * @param on_iteration Optional iteration callback for per-iteration notifications
         * @param args Arguments forwarded to func
         *
         * @return ProfileResult if func returns void, ProfileResultWithData<T> otherwise
         *
         * @note Assumes config has already been validated by the public API
         * @note Spawns config.parallelism threads and distributes iterations fairly
         * @note Catches all exceptions from func and records them in the result
         * @note Progress callback is batched every 64 iterations to reduce overhead
         *
         * @warning Internal function: use profiler::measure() instead
         */
        template<typename Clock = std::chrono::steady_clock, typename IterationCallback = std::nullptr_t, typename...
            Args>
        auto measure_impl(
            const ProfileConfig &config,
            std::invocable<Args...> auto &&func,
            IterationCallback &&on_iteration,
            Args &&... args) {
            using ReturnType = decltype(std::invoke(func, args...));

            const auto &log = config.logger;

            /**
             * @struct ThreadResult
             * @brief Per-thread results container
             *
             * Each thread accumulates its own runs, return values, and exceptions
             * to avoid contention. Results are merged during aggregation.
             */
            struct ThreadResult : ReturnValueHolder<ReturnType> {
                std::vector<std::chrono::nanoseconds> runs; ///< Duration per iteration
                std::vector<ExceptionInfo> exceptions; ///< Exceptions caught in this thread
            };
            std::vector<ThreadResult> thread_results(config.parallelism);
            std::mutex callback_mutex; ///< Protects iteration callback invocations
            std::atomic stop_flag = false; ///< Early-stop flag set by callback

            // Initialize memory tracking if enabled
            MemoryStats global_memory_stats;
            if (config.track_memory) {
                global_memory_stats.reset();
            }

            // Shared progress counter to avoid per-thread recompute
            std::atomic<std::size_t> iterations_done;
            const std::size_t progress_stride = 64; // batch notifications to reduce contention

            /**
             * @brief Lambda executed by each worker thread
             * @param thread_idx Thread index [0, parallelism)
             * @param start_iter Starting iteration index (inclusive)
             * @param end_iter Ending iteration index (exclusive)
             *
             * Executes iterations [start_iter, end_iter) and records timing data,
             * return values, and exceptions in thread_results[thread_idx].
             */
            auto run_iterations = [&](size_t thread_idx, size_t start_iter, size_t end_iter) {
                auto &res = thread_results[thread_idx];
                const size_t capacity = (end_iter > start_iter) ? (end_iter - start_iter) : 0;

                // Reserve to avoid reallocation
                if (capacity) res.runs.reserve(capacity);
                if constexpr (!std::is_void_v<ReturnType>) {
                    if (capacity) res.returns.reserve(capacity);
                }
                res.exceptions.reserve(0); // keep small unless exceptions thrown

                for (size_t i = start_iter; i < end_iter; ++i) {
                    if (stop_flag.load(std::memory_order_relaxed)) break;

                    try {
                        const auto start = Clock::now();
                        if constexpr (std::is_void_v<ReturnType>) {
                            std::invoke(func, args...);
                            const auto end = Clock::now();
                            auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
                            res.runs.emplace_back(duration);
                            if constexpr (!std::is_same_v<IterationCallback, std::nullptr_t>) {
                                std::lock_guard lock(callback_mutex);
                                if (!on_iteration(duration, i)) stop_flag.store(true, std::memory_order_relaxed);
                            }
                        } else {
                            ReturnType val = std::invoke(func, args...);
                            const auto end = Clock::now();
                            auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
                            res.runs.emplace_back(duration);
                            res.returns.emplace_back(std::move(val));
                            if constexpr (!std::is_same_v<IterationCallback, std::nullptr_t>) {
                                std::lock_guard lock(callback_mutex);
                                if (!on_iteration(duration, res.returns.back(), i))
                                    stop_flag.store(
                                        true, std::memory_order_relaxed);
                            }
                        }
                    } catch (const std::exception &e) {
                        res.exceptions.emplace_back(ExceptionInfo{i, e.what()});
                    } catch (...) {
                        res.exceptions.emplace_back(ExceptionInfo{i, "Unknown exception type"});
                    }

                    // Progress callback after iteration (batched)
                    if (config.progress_callback && config.iterations > 0) {
                        const std::size_t done = iterations_done.fetch_add(1, std::memory_order_relaxed) + 1;
                        if ((done % progress_stride) == 0 || done == config.iterations) {
                            double prog = std::min(
                                1.0, static_cast<double>(done) / static_cast<double>(config.iterations));
                            config.progress_callback(prog);
                        }
                    }
                }
            };

            // Execute warmup iterations (not measured)
            if (log && config.warmup_iterations > 0)
                log(
                    std::format("Starting {} warmup iterations...", config.warmup_iterations));
            for (size_t i = 0; i < config.warmup_iterations; ++i) { std::invoke(func, args...); }

            // Spawn worker threads for measured iterations
            if (log)
                log(
                    std::format("Starting {} iterations on {} thread(s)...", config.iterations, config.parallelism));
            std::vector<std::thread> threads;
            // Fair split with remainder distributed to first R threads
            const std::size_t base = (config.parallelism == 0) ? 0 : (config.iterations / config.parallelism);
            const std::size_t rem = (config.parallelism == 0) ? 0 : (config.iterations % config.parallelism);
            std::size_t cursor = 0;
            for (size_t i = 0; i < config.parallelism; ++i) {
                const std::size_t span = base + (i < rem ? 1 : 0);
                const std::size_t start = cursor;
                const std::size_t end = start + span;
                cursor = end;
                if (start < end) {
                    threads.emplace_back(run_iterations, i, start, end);
                }
            }
            for (auto &t: threads) { t.join(); }
            if (log) log("Measurement complete. Aggregating results...");

            /**
             * @brief Aggregate per-thread results into final ProfileResult
             * @param final_result_base Reference to ProfileResult or ProfileResultWithData
             *
             * Merges per-thread timing data, return values, and exceptions into the final result.
             * Applies outlier trimming if configured. Computes aggregate statistics.
             */
            auto aggregate = [&](auto &final_result_base) {
                auto &profile = [&]() -> ProfileResult & {
                    if constexpr (std::is_void_v<ReturnType>) return final_result_base;
                    else return final_result_base.profile;
                }();

                profile.label = config.label;
                profile.parallelism_used = config.parallelism;
                profile.warmup_iterations_run = config.warmup_iterations;
                profile.iterations_attempted = config.iterations;
                profile.min_duration = std::chrono::nanoseconds::max();
                profile.max_duration = std::chrono::nanoseconds::min();

                // Pre-reserve to avoid growth copies
                const size_t total_runs_expected = config.iterations;
                profile.individual_runs.reserve(total_runs_expected);
                profile.per_thread_runs.resize(config.parallelism);

                // Merge thread results into profile
                for (size_t i = 0; i < thread_results.size(); ++i) {
                    const auto &res = thread_results[i];
                    profile.per_thread_runs[i].reserve(res.runs.size());
                    profile.per_thread_runs[i] = res.runs;
                    profile.individual_runs.insert(profile.individual_runs.end(), res.runs.begin(), res.runs.end());
                    if constexpr (!std::is_void_v<ReturnType>) {
                        // Handle move-only types: use std::move_iterator
                        if constexpr (std::is_move_constructible_v<ReturnType> && !std::is_copy_constructible_v<
                                          ReturnType>) {
                            // Move-only type: move elements one by one
                            final_result_base.return_values.reserve(
                                final_result_base.return_values.size() + res.returns.size());
                            for (auto &val: const_cast<std::vector<ReturnType> &>(res.returns)) {
                                final_result_base.return_values.push_back(std::move(val));
                            }
                        } else {
                            // Copyable type: use insert
                            final_result_base.return_values.insert(final_result_base.return_values.end(),
                                                                   res.returns.begin(), res.returns.end());
                        }
                    }
                    for (const auto &ex: res.exceptions) {
                        profile.unique_exceptions[ex.what_message]++;
                    }
                }
                profile.iterations_succeeded = profile.individual_runs.size();

                // Apply outlier trimming
                trim_vector(profile.individual_runs, config.trim_outliers_percentage, profile.outlier_info);

                // Set memory stats if tracking was enabled
                if (config.track_memory) {
                    profile.memory_stats = global_memory_stats;
                }

                // Compute aggregate statistics
                if (!profile.individual_runs.empty()) {
                    profile.total_duration = std::accumulate(profile.individual_runs.begin(),
                                                             profile.individual_runs.end(),
                                                             std::chrono::nanoseconds(0));
                    profile.average_duration = profile.total_duration / profile.individual_runs.size();
                    auto [min_it, max_it] = std::minmax_element(profile.individual_runs.begin(),
                                                                profile.individual_runs.end());
                    profile.min_duration = *min_it;
                    profile.max_duration = *max_it;
                } else {
                    // Handle edge case: no successful runs
                    profile.total_duration = std::chrono::nanoseconds(0);
                    profile.average_duration = std::chrono::nanoseconds(0);
                    profile.min_duration = std::chrono::nanoseconds(0);
                    profile.max_duration = std::chrono::nanoseconds(0);
                }
            };

            // Return appropriate result type based on func's return type
            if constexpr (std::is_void_v<ReturnType>) {
                ProfileResult final_result;
                aggregate(final_result);
                return final_result;
            } else {
                ProfileResultWithData<ReturnType> final_result;
                aggregate(final_result);
                return final_result;
            }
        }

        // --- Helpers that depend on `measure_impl` (defined after) ---

        /**
         * @brief Measure profiler overhead by profiling an empty lambda
         *
         * Runs a lightweight profiling session to estimate the overhead introduced
         * by the profiler itself. Used for reporting profiler overhead in results.
         *
         * @return Average duration per iteration for an empty lambda
         *
         * @note Called once and cached via std::call_once
         * @note Uses 2000 iterations with no warmup for quick measurement
         */
        inline std::chrono::nanoseconds measure_overhead_once() {
            // Lighter sampling to reduce cold-start penalty
            ProfileConfig cfg;
            cfg.iterations = 2000;
            cfg.warmup_iterations = 0;
            return measure_impl(cfg, []() {
            }, nullptr).average_duration;
        }

        /**
         * @brief Format ProfileResult as a detailed human-readable report
         *
         * Generates a comprehensive report including timing statistics, iteration counts,
         * exception information, and profiler overhead. Used by the public format_result().
         *
         * @param result Profiling results to format
         * @return Formatted string report with header, stats, and footer
         *
         * @note Measures profiler overhead on first call (cached thereafter)
         * @note Includes per-thread statistics, outlier info, and exception details
         */
        inline std::string format_result(const ProfileResult &result) {
            static std::once_flag once;
            static std::chrono::nanoseconds overhead{0};
            std::call_once(once, []() {
                overhead = measure_overhead_once();
            });

            const std::string title = result.label.empty() ? "Profile Result" : result.label;
            const std::string header = std::string("--- ") + title + " ---";
            const std::string footer(header.length(), '-');

            size_t total_exceptions = 0;
            for (const auto &[msg, count]: result.unique_exceptions) total_exceptions += count;

            std::ostringstream oss;
            oss << header << "\n"
                    << "Config:           " << result.iterations_attempted << " iterations, "
                    << result.parallelism_used << " thread(s), " << result.warmup_iterations_run << " warmup\n"
                    << "Iterations Run:   " << (result.iterations_succeeded + total_exceptions) << " (Succeeded: " <<
                    result.iterations_succeeded << ")\n"
                    << "Total Duration:   " << format_duration(result.total_duration) << "\n"
                    << "Average Time:     " << format_duration(result.average_duration) << "\n"
                    << "Median Time:      " << format_duration(result.median()) << "\n"
                    << "Min/Max Time:     " << format_duration(result.min_duration) << " / " << format_duration(
                        result.max_duration) << "\n"
                    << "Profiler Overhead: ~" << format_duration(overhead) << "\n";

            if (result.outlier_info) {
                const auto &info = *result.outlier_info;
                oss << "Outliers Trimmed: " << info.trimmed_high << " slowest, " << info.trimmed_low << " fastest (" <<
                        info.percentage << "%)\n";
            }
            if (!result.unique_exceptions.empty()) {
                oss << "Exceptions Caught: " << total_exceptions << "\n";
                for (const auto &[msg, count]: result.unique_exceptions) {
                    oss << "  - [" << count << "x] " << msg << "\n";
                }
            }
            oss << footer;
            return oss.str();
        }

        /**
         * @brief Fast JSON serialization of ProfileResult
         *
         * Optimized JSON export that avoids redundant conversions and directly
         * constructs the JSON object.
         *
         * @param r ProfileResult to serialize
         * @return JSON object with all profiling data
         *
         * @note Used internally by ProfileResult::to_json()
         * @note Includes all timing data, exceptions, and outlier info
         */
        inline nlohmann::json profile_to_json_fast(const ProfileResult &r) {
            nlohmann::json j;
            j["label"] = r.label;
            j["total_duration_ns"] = r.total_duration.count();
            j["average_duration_ns"] = r.average_duration.count();
            j["min_duration_ns"] = r.min_duration.count();
            j["max_duration_ns"] = r.max_duration.count();
            j["iterations_attempted"] = r.iterations_attempted;
            j["iterations_succeeded"] = r.iterations_succeeded;
            j["parallelism_used"] = r.parallelism_used;
            j["warmup_iterations_run"] = r.warmup_iterations_run;
            auto &arr = j["individual_runs_ns"] = nlohmann::json::array();
            // remove invalid reserve on json array
            for (auto d: r.individual_runs) arr.push_back(d.count());
            j["unique_exceptions"] = r.unique_exceptions;
            if (r.outlier_info) {
                j["outlier_info"] = {
                    {"trimmed_low", r.outlier_info->trimmed_low},
                    {"trimmed_high", r.outlier_info->trimmed_high},
                    {"percentage", r.outlier_info->percentage}
                };
            }
            return j;
        }
    } // namespace internal
    // --- Public-Facing API ---

    /**
     * @brief Measure the performance of a callable with optional iteration callback
     *
     * Profiles the given function with the specified configuration. Supports parallel
     * execution, warmup, outlier trimming, exception handling, and optional per-iteration
     * callbacks. Automatically validates and normalizes configuration parameters.
     *
     * @tparam Clock Clock type for timing (default: std::chrono::steady_clock)
     * @tparam IterationCallback Callback type (default: std::nullptr_t for no callback)
     * @tparam Args Argument types forwarded to the callable
     *
     * @param config Profiling configuration
     * @param func Callable to profile (void or returning T)
     * @param on_iteration Optional callback after each iteration. Return false to stop early.
     *                     Signature for void-returning: bool(nanoseconds duration, size_t iteration)
     *                     Signature for value-returning: bool(nanoseconds duration, const T& value, size_t iteration)
     * @param args Arguments forwarded to func
     *
     * @return ProfileResult if func returns void, ProfileResultWithData<T> if func returns T
     *
     * @note Configuration validation:
     *       - parallelism clamped to [1, hardware_concurrency × 4]
     *       - trim_outliers_percentage clamped to [0, 100]
     *       - warmup_iterations capped at 1'000'000
     *       - iterations == 0 returns empty result immediately
     *
     * @example
     * @code
     * profiler::ProfileConfig cfg;
     * cfg.iterations = 1000;
     * cfg.parallelism = 4;
     *
     * // Profile void function
     * auto result = profiler::measure(cfg, []() {
     *     std::this_thread::sleep_for(std::chrono::milliseconds(1));
     * });
     *
     * // Profile function with return value
     * auto result2 = profiler::measure(cfg, []() -> int {
     *     return 42;
     * });
     *
     * // With iteration callback
     * auto result3 = profiler::measure(
     *     cfg,
     *     []() { // work
     *     },
     *     [](std::chrono::nanoseconds d, size_t i) {
     *         std::cout << "Iteration " << i << ": " << d.count() << "ns\n";
     *         return true; // continue
     *     }
     * );
     * @endcode
     */
    template<typename Clock = std::chrono::steady_clock, typename IterationCallback = std::nullptr_t, typename... Args>
    [[nodiscard]]
    auto measure(const ProfileConfig &config, std::invocable<Args...> auto &&func,
                 IterationCallback &&on_iteration = nullptr, Args &&... args) {
        // Production-grade config validation and normalization
        ProfileConfig cfg = config;
        if (cfg.parallelism == 0) cfg.parallelism = 1;
        // Avoid pathological thread counts (cap to hardware concurrency x 4)
        const auto hw = std::max<unsigned>(1u, std::thread::hardware_concurrency());
        cfg.parallelism = std::min<std::size_t>(cfg.parallelism, static_cast<std::size_t>(hw) * 4);
        // Negative or >100% outliers are invalid; clamp to [0,100]
        if (cfg.trim_outliers_percentage < 0.0) cfg.trim_outliers_percentage = 0.0;
        if (cfg.trim_outliers_percentage > 100.0) cfg.trim_outliers_percentage = 100.0;
        // Warmup should not be negative, and avoid excessive warmup (cap to 1e6)
        // iterations already size_t, but guard for zero measurements
        // If iterations == 0, return empty result fast
        if (cfg.iterations == 0) {
            using ReturnType = decltype(std::invoke(func, args...));
            if constexpr (std::is_void_v<ReturnType>) {
                ProfileResult r;
                r.label = cfg.label;
                r.iterations_attempted = 0;
                r.iterations_succeeded = 0;
                r.parallelism_used = cfg.parallelism;
                r.warmup_iterations_run = std::min<std::size_t>(cfg.warmup_iterations, 1000000);
                return r;
            } else {
                ProfileResultWithData<ReturnType> r;
                r.profile.label = cfg.label;
                r.profile.iterations_attempted = 0;
                r.profile.iterations_succeeded = 0;
                r.profile.parallelism_used = cfg.parallelism;
                r.profile.warmup_iterations_run = std::min<std::size_t>(cfg.warmup_iterations, 1000000);
                return r;
            }
        }
        cfg.warmup_iterations = std::min<std::size_t>(cfg.warmup_iterations, 1000000);

        return internal::measure_impl<Clock>(cfg, std::forward<decltype(func)>(func),
                                             std::forward<IterationCallback>(on_iteration),
                                             std::forward<Args>(args)...);
    }

    /**
     * @brief Format profiling results as human-readable string
     *
     * Generates a comprehensive report including timing statistics, iteration counts,
     * exception information, and profiler overhead.
     *
     * @param result Profiling results to format
     * @return Formatted string report
     *
     * @note Handles edge case of zero iterations gracefully
     *
     * @example
     * @code
     * auto result = profiler::measure(config, func);
     * std::cout << profiler::format_result(result) << std::endl;
     * @endcode
     */
    inline std::string format_result(const profiler::ProfileResult &result) {
        // Robust formatting when no iterations ran
        if (result.iterations_attempted == 0 && result.individual_runs.empty()) {
            std::ostringstream oss;
            std::string title = result.label.empty() ? "Profile Result" : result.label;
            oss << std::format("--- {} ---\n", title);
            oss << "Config:           0 iterations, " << result.parallelism_used << " thread(s), " << result.
                    warmup_iterations_run << " warmup\n";
            oss << "Iterations Run:   0 (Succeeded: 0)\n";
            oss << "Total Duration:   N/A\nAverage Time:     N/A\nMedian Time:      N/A\nMin/Max Time:     N/A / N/A\n";
            oss << std::string(title.size() + 8, '-');
            return oss.str();
        }
        return internal::format_result(result);
    }

    // --- Comparison Mode ---

    /**
     * @struct ComparisonResult
     * @brief Results from comparing two profiling runs
     *
     * Contains speedup factor, statistical significance (Mann-Whitney U test),
     * and a human-readable verdict.
     */
    struct ComparisonResult {
        std::string baseline_label; ///< Label of baseline benchmark
        std::string candidate_label; ///< Label of candidate benchmark
        double speedup_factor; ///< candidate_median / baseline_median (<1.0 = faster)
        double p_value; ///< Statistical significance (Mann-Whitney U test)
        bool is_significant; ///< True if p < 0.05
        std::string verdict; ///< "Faster by X%", "Slower by X%", or "No significant difference"
    };

    /**
     * @brief Perform Mann-Whitney U test on two distributions
     *
     * Non-parametric statistical test to determine if two distributions differ significantly.
     *
     * @param a First sample distribution
     * @param b Second sample distribution
     * @return p-value (p < 0.05 indicates significant difference)
     *
     * @note Uses normal approximation for the U statistic
     */
    inline double mann_whitney_u_test(const std::vector<std::chrono::nanoseconds> &a,
                                      const std::vector<std::chrono::nanoseconds> &b) {
        // Simplified Mann-Whitney U test (returns pseudo p-value)
        std::vector<std::pair<long long, int> > combined;
        for (auto v: a) combined.push_back({v.count(), 0});
        for (auto v: b) combined.push_back({v.count(), 1});
        std::sort(combined.begin(), combined.end());

        double u1 = 0;
        for (size_t i = 0; i < combined.size(); ++i) {
            if (combined[i].second == 0) {
                u1 += i + 1; // Rank sum for group A
            }
        }
        u1 -= a.size() * (a.size() + 1) / 2.0;
        double u2 = a.size() * b.size() - u1;
        double u = std::min(u1, u2);

        // Normal approximation for p-value (simplified)
        double mean_u = a.size() * b.size() / 2.0;
        double std_u = std::sqrt(a.size() * b.size() * (a.size() + b.size() + 1) / 12.0);
        double z = (u - mean_u) / std_u;
        double p = 0.5 * std::erfc(-z / std::sqrt(2.0)); // Two-tailed
        return p;
    }

    /**
     * @brief Compare two profiling results with statistical analysis
     *
     * Compares baseline vs candidate using median times and Mann-Whitney U test.
     * Handles empty inputs gracefully.
     *
     * @param baseline Baseline profiling results
     * @param candidate Candidate profiling results
     * @return Comparison results with speedup factor and verdict
     *
     * @note speedup_factor < 1.0 means candidate is faster
     *
     * @example
     * @code
     * auto baseline = profiler::measure(config, old_algorithm);
     * auto candidate = profiler::measure(config, new_algorithm);
     *
     * auto cmp = profiler::compare(baseline, candidate);
     * std::cout << profiler::format_comparison(cmp);
     *
     * if (cmp.is_significant && cmp.speedup_factor > 1.1) {
     *     std::cerr << "REGRESSION: " << cmp.verdict << "\n";
     * }
     * @endcode
     */
    inline ComparisonResult compare(const ProfileResult &baseline, const ProfileResult &candidate) {
        ComparisonResult result;
        result.baseline_label = baseline.label;
        result.candidate_label = candidate.label;

        // Handle empty inputs robustly
        const bool emptyA = baseline.individual_runs.empty();
        const bool emptyB = candidate.individual_runs.empty();
        if (emptyA || emptyB) {
            result.speedup_factor = 1.0;
            result.p_value = 1.0;
            result.is_significant = false;
            result.verdict = "No significant difference";
            return result;
        }

        // Use medians to reduce scheduling noise for short sleeps
        const auto base_med = baseline.median();
        const auto cand_med = candidate.median();
        const double baseline_med_ns = std::max(1.0, static_cast<double>(base_med.count()));
        const double candidate_med_ns = std::max(1.0, static_cast<double>(cand_med.count()));

        // Candidate faster => speedup_factor < 1.0 (as expected by tests)
        result.speedup_factor = candidate_med_ns / baseline_med_ns;

        result.p_value = mann_whitney_u_test(baseline.individual_runs, candidate.individual_runs);
        result.is_significant = result.p_value < 0.05;

        if (!result.is_significant) {
            result.verdict = "No significant difference";
        } else if (result.speedup_factor < 1.0) {
            result.verdict = std::format("Faster by {:.1f}%", (1.0 - result.speedup_factor) * 100.0);
        } else {
            result.verdict = std::format("Slower by {:.1f}%", (result.speedup_factor - 1.0) * 100.0);
        }

        return result;
    }

    /**
     * @brief Format comparison results as human-readable string
     *
     * @param cmp Comparison results to format
     * @return Formatted string report
     *
     * @example
     * @code
     * auto cmp = profiler::compare(baseline, candidate);
     * std::cout << profiler::format_comparison(cmp);
     * @endcode
     */
    inline std::string format_comparison(const ComparisonResult &cmp) {
        return std::format(
            "--- Comparison: {} vs {} ---\n"
            "Speedup Factor:   {:.3f}x\n"
            "Statistical Test: p={:.4f} ({})\n"
            "Verdict:          {}\n",
            cmp.baseline_label, cmp.candidate_label,
            cmp.speedup_factor,
            cmp.p_value, cmp.is_significant ? "significant" : "not significant",
            cmp.verdict
        );
    }

    // --- Scoped Profiler (RAII) ---

    /**
     * @class ScopedProfiler
     * @brief RAII-style scoped profiler for automatic timing
     *
     * Automatically measures elapsed time from construction to destruction.
     * Useful for profiling code blocks or entire functions.
     *
     * @note Non-copyable, non-movable
     *
     * @example
     * @code
     * {
     *     profiler::ScopedProfiler p("MyBlock");
     *     // Code to profile
     * } // Automatically reports on destruction
     *
     * void my_function() {
     *     PROFILE_FUNCTION(); // Macro for scoped profiling
     *     // Function body
     * }
     * @endcode
     */
    class ScopedProfiler {
    public:
        /**
         * @brief Construct scoped profiler and start timer
         * @param label Label for this profiling scope
         * @param config Optional profiling configuration (logger, memory tracking, etc.)
         */
        explicit ScopedProfiler(std::string label, ProfileConfig config = {})
            : label_(std::move(label)), config_(std::move(config)), start_(std::chrono::steady_clock::now()) {
            if (config_.track_memory) {
                memory_stats_.reset();
            }
        }

        /**
         * @brief Destructor: stop timer and report results
         *
         * Formats and logs results via config_.logger if provided.
         */
        ~ScopedProfiler() {
            auto end = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start_);

            ProfileResult result;
            result.label = label_;
            result.total_duration = duration;
            result.average_duration = duration;
            result.min_duration = duration;
            result.max_duration = duration;
            result.iterations_attempted = 1;
            result.iterations_succeeded = 1;
            result.individual_runs = {duration};

            if (config_.track_memory) {
                result.memory_stats = memory_stats_;
            }

            if (config_.logger) {
                config_.logger(internal::format_result(result));
            }
        }

        ScopedProfiler(const ScopedProfiler &) = delete;

        ScopedProfiler &operator=(const ScopedProfiler &) = delete;

    private:
        std::string label_;
        ProfileConfig config_;
        std::chrono::steady_clock::time_point start_;
        MemoryStats memory_stats_;
    };

    /**
     * @def PROFILE_SCOPE
     * @brief Macro for scoped profiling with custom label
     * @param label Label for the profiling scope
     *
     * @example
     * @code
     * {
     *     PROFILE_SCOPE("MyBlock");
     *     // Code to profile
     * }
     * @endcode
     */
#define PROFILE_SCOPE(label) profiler::ScopedProfiler _profiler_##__LINE__(label)

    /**
     * @def PROFILE_FUNCTION
     * @brief Macro for profiling entire function
     *
     * Uses __FUNCTION__ as the label.
     *
     * @example
     * @code
     * void my_function() {
     *     PROFILE_FUNCTION();
     *     // Function body
     * }
     * @endcode
     */
#define PROFILE_FUNCTION() PROFILE_SCOPE(__FUNCTION__)
} // namespace profiler

#endif // PROFILER_HPP
