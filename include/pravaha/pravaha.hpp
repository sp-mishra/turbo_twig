#pragma once
// ============================================================================
// pravaha.hpp — C++23 Task-Graph Orchestration Engine (Foundational Skeleton)
// ============================================================================
// Single-header library for declarative task-graph definition, validation,
// and execution orchestration. No macros. No virtual functions.
// ============================================================================

#include <concepts>
#include <cstddef>
#include <expected>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

// Project dependencies
#include "containers/graph/LiteGraph.hpp"
#include "containers/graph/LiteGraphAlgorithms.hpp"
#include "containers/tree/NAryTree.hpp"
#include "edsl/lithe.hpp"
#include "utils/meta.hpp"

namespace pravaha {

// ============================================================================
//  SECTION 1: ERROR HANDLING
// ============================================================================

// ---------------------------------------------------------------------------
// 1.1 ErrorKind — enumeration of all error categories
// ---------------------------------------------------------------------------
enum class ErrorKind {
    ParseError,
    ValidationError,
    CycleDetected,
    SymbolNotFound,
    TypeMismatch,
    ExecutorUnavailable,
    DomainConstraintViolation,
    PayloadNotSerializable,
    PayloadNotTransferable,
    TaskFailed,
    TaskCanceled,
    QueueRejected,
    Timeout,
    InternalError
};

// ---------------------------------------------------------------------------
// 1.2 PravahaError — structured error type
// ---------------------------------------------------------------------------
struct PravahaError {
    ErrorKind          kind;
    std::string        message;
    std::string        task_identity;
    std::source_location location;

    // Primary constructor
    constexpr PravahaError(
        ErrorKind k,
        std::string msg,
        std::string task_id = {},
        std::source_location loc = std::source_location::current())
        : kind{k}
        , message{std::move(msg)}
        , task_identity{std::move(task_id)}
        , location{loc}
    {}

    // Convenience factory: error without task identity
    static constexpr PravahaError make(
        ErrorKind k,
        std::string msg,
        std::source_location loc = std::source_location::current())
    {
        return PravahaError{k, std::move(msg), {}, loc};
    }

    // Convenience factory: error with task identity
    static constexpr PravahaError make_for_task(
        ErrorKind k,
        std::string msg,
        std::string task_id,
        std::source_location loc = std::source_location::current())
    {
        return PravahaError{k, std::move(msg), std::move(task_id), loc};
    }
};

// ---------------------------------------------------------------------------
// 1.3 Outcome — result-or-error alias using std::expected
// ---------------------------------------------------------------------------
template <class T>
using Outcome = std::expected<T, PravahaError>;

// ---------------------------------------------------------------------------
// 1.4 Unit — void-equivalent value type
// ---------------------------------------------------------------------------
using Unit = std::monostate;

// ============================================================================
//  SECTION 2: TASK STATE & POLICY ENUMERATIONS
// ============================================================================

// ---------------------------------------------------------------------------
// 2.1 TaskState — lifecycle states of a task node
// ---------------------------------------------------------------------------
enum class TaskState {
    Created,
    Ready,
    Scheduled,
    Running,
    Succeeded,
    Failed,
    Canceled,
    Skipped
};

// ---------------------------------------------------------------------------
// 2.2 JoinPolicyKind — how multiple upstream results are combined
// ---------------------------------------------------------------------------
enum class JoinPolicyKind {
    AllOrNothing,
    CollectAll,
    AnySuccess,
    Quorum
};

// ---------------------------------------------------------------------------
// 2.3 ExecutionDomain — where a task executes
// ---------------------------------------------------------------------------
enum class ExecutionDomain {
    Inline,
    CPU,
    IO,
    Fiber,
    Coroutine,
    External
};

// ============================================================================
//  SECTION 3: PAYLOAD CONCEPTS
// ============================================================================

// ---------------------------------------------------------------------------
// 3.1 Payload — minimal requirement for data flowing between tasks
// ---------------------------------------------------------------------------
// A Payload must be movable (at minimum) so tasks can produce/consume values.
template <typename T>
concept Payload =
    std::move_constructible<T> &&
    std::destructible<T>;

// ---------------------------------------------------------------------------
// 3.2 LocalPayload — payload that is only valid within a single process
// ---------------------------------------------------------------------------
// Local payloads are move-constructible but make no serializability guarantees.
// They may contain pointers, references, or non-trivial types.
template <typename T>
concept LocalPayload =
    Payload<T> &&
    std::move_constructible<T>;

// ---------------------------------------------------------------------------
// 3.3 TransferablePayload — payload that can move across thread boundaries
// ---------------------------------------------------------------------------
// Transferable payloads must be both move-constructible and copy-constructible
// to allow queuing, buffering, or fan-out to multiple consumers.
template <typename T>
concept TransferablePayload =
    Payload<T> &&
    std::copy_constructible<T>;

// ---------------------------------------------------------------------------
// 3.4 SerializablePayload — payload suitable for zero-copy or binary transfer
// ---------------------------------------------------------------------------
// Uses meta::is_zero_copy_serializable when the type satisfies the required
// constraints (aggregate, reflectable). Falls back to trivially_copyable +
// standard_layout for non-aggregate types.
namespace detail {

template <typename T>
consteval bool is_serializable_payload_check() {
    // First check basic trivial copyability and standard layout
    if constexpr (!std::is_trivially_copyable_v<T> || !std::is_standard_layout_v<T>) {
        return false;
    } else if constexpr (meta::Reflectable<T>) {
        // Use meta::is_zero_copy_serializable for reflectable types
        return meta::is_zero_copy_serializable<T>();
    } else {
        // Fallback: trivially copyable + standard layout is sufficient
        return true;
    }
}

} // namespace detail

template <typename T>
concept SerializablePayload =
    Payload<T> &&
    detail::is_serializable_payload_check<T>();

// ---------------------------------------------------------------------------
// 3.5 InvocableTask — a callable that produces an Outcome
// ---------------------------------------------------------------------------
// An InvocableTask is a callable object that, when invoked with Args...,
// returns an Outcome<Result>. This is the fundamental building block
// for task nodes in the execution graph.
template <typename F, typename Result, typename... Args>
concept InvocableTask =
    std::invocable<F, Args...> &&
    std::same_as<std::invoke_result_t<F, Args...>, Outcome<Result>>;

} // namespace pravaha