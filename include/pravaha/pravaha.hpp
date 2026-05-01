#pragma once
// ============================================================================
// pravaha.hpp — C++23 Task-Graph Orchestration Engine (Foundational Skeleton)
// ============================================================================
// Single-header library for declarative task-graph definition, validation,
// and execution orchestration. No macros. No virtual functions.
// ============================================================================

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <exception>
#include <expected>
#include <memory>
#include <new>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
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

// ============================================================================
//  SECTION 4: CANCELLATION PRIMITIVES
// ============================================================================

// ---------------------------------------------------------------------------
// 4.1 CancellationState — shared atomic boolean representing cancellation
// ---------------------------------------------------------------------------
// Internal state object shared between CancellationSource and CancellationToken.
// Stores a single atomic_bool that transitions from false→true exactly once.
struct CancellationState {
    std::atomic<bool> requested{false};

    // Request cancellation. Idempotent: multiple calls are safe.
    void request() noexcept {
        requested.store(true, std::memory_order_release);
    }

    // Check if cancellation has been requested.
    [[nodiscard]] bool is_requested() const noexcept {
        return requested.load(std::memory_order_acquire);
    }
};

// ---------------------------------------------------------------------------
// 4.2 CancellationToken — lightweight non-owning handle to CancellationState
// ---------------------------------------------------------------------------
// Tokens are cheap to copy and pass to tasks. A default-constructed token
// (empty token) always returns false for stop_requested().
class CancellationToken {
    std::shared_ptr<CancellationState> state_{};

public:
    // Default: empty token, never canceled
    CancellationToken() noexcept = default;

    // Construct from shared state
    explicit CancellationToken(std::shared_ptr<CancellationState> state) noexcept
        : state_{std::move(state)}
    {}

    // Check if stop has been requested. Empty token always returns false.
    [[nodiscard]] bool stop_requested() const noexcept {
        return state_ && state_->is_requested();
    }

    // Check if this token is associated with a cancellation state
    [[nodiscard]] bool has_state() const noexcept {
        return state_ != nullptr;
    }
};

// ---------------------------------------------------------------------------
// 4.3 CancellationSource — owns a CancellationState, produces tokens
// ---------------------------------------------------------------------------
// The source is the authority that can trigger cancellation. It owns the
// shared state and hands out lightweight tokens to observers.
class CancellationSource {
    std::shared_ptr<CancellationState> state_;

public:
    // Construct with a fresh cancellation state
    CancellationSource()
        : state_{std::make_shared<CancellationState>()}
    {}

    // Obtain a token that observes this source's cancellation state
    [[nodiscard]] CancellationToken token() const noexcept {
        return CancellationToken{state_};
    }

    // Request cancellation. Idempotent: safe to call multiple times.
    void request_stop() noexcept {
        state_->request();
    }

    // Check if stop has been requested on this source
    [[nodiscard]] bool stop_requested() const noexcept {
        return state_->is_requested();
    }
};

// ---------------------------------------------------------------------------
// 4.4 CancellationScope — scoped cancellation with optional parent
// ---------------------------------------------------------------------------
// A CancellationScope owns a local CancellationSource and optionally
// observes a parent CancellationToken. Cancellation is requested if either
// the local source or the parent has been canceled.
class CancellationScope {
    CancellationSource local_source_;
    CancellationToken  parent_token_;

public:
    // Construct a root scope (no parent)
    CancellationScope() = default;

    // Construct a child scope that observes a parent token
    explicit CancellationScope(CancellationToken parent) noexcept
        : parent_token_{std::move(parent)}
    {}

    // Check if stop has been requested (local OR parent)
    [[nodiscard]] bool stop_requested() const noexcept {
        return local_source_.stop_requested() || parent_token_.stop_requested();
    }

    // Get a token for this scope's local cancellation state
    [[nodiscard]] CancellationToken token() const noexcept {
        return local_source_.token();
    }

    // Request cancellation on the local source. Idempotent.
    void request_stop() noexcept {
        local_source_.request_stop();
    }
};

// ============================================================================
//  SECTION 5: TASK COMMAND — STATIC TYPE ERASURE
// ============================================================================

// ---------------------------------------------------------------------------
// 5.1 TaskCommand — move-only, statically-erased callable for backend queues
// ---------------------------------------------------------------------------
// TaskCommand is the only erased execution object allowed into backend queues.
// It uses manual static type erasure with inline storage (no heap, no virtual).
// The callable must be invocable with no arguments and move-constructible.
class TaskCommand {
    // Inline storage configuration
    static constexpr std::size_t buffer_size  = 128;
    static constexpr std::size_t buffer_align = alignof(std::max_align_t);

    // Function pointer types for the vtable-free dispatch
    using invoke_fn_t  = Outcome<Unit>(*)(void* storage) noexcept;
    using move_fn_t    = void(*)(void* dst, void* src) noexcept;
    using destroy_fn_t = void(*)(void* storage) noexcept;

    // Aligned inline storage
    alignas(buffer_align) unsigned char storage_[buffer_size]{};

    // Function pointers (null when empty)
    invoke_fn_t  invoke_fn_{nullptr};
    move_fn_t    move_fn_{nullptr};
    destroy_fn_t destroy_fn_{nullptr};

    // Optional debug name
    std::string_view debug_name_{};

    // ---------------------------------------------------------------------------
    // Static dispatch implementations for a concrete type F
    // ---------------------------------------------------------------------------
    template <typename F>
    static Outcome<Unit> invoke_impl(void* storage) noexcept {
        try {
            F& fn = *std::launder(reinterpret_cast<F*>(storage));
            fn();
            return Unit{};
        } catch (const std::exception& e) {
            return std::unexpected(PravahaError{
                ErrorKind::TaskFailed,
                std::string{"TaskCommand exception: "} + e.what()
            });
        } catch (...) {
            return std::unexpected(PravahaError{
                ErrorKind::TaskFailed,
                "TaskCommand: unknown exception"
            });
        }
    }

    template <typename F>
    static void move_impl(void* dst, void* src) noexcept {
        F& src_fn = *std::launder(reinterpret_cast<F*>(src));
        ::new (dst) F(std::move(src_fn));
        src_fn.~F();
    }

    template <typename F>
    static void destroy_impl(void* storage) noexcept {
        F& fn = *std::launder(reinterpret_cast<F*>(storage));
        fn.~F();
    }

    // Destroy current callable if present
    void destroy_current() noexcept {
        if (destroy_fn_) {
            destroy_fn_(storage_);
            invoke_fn_  = nullptr;
            move_fn_    = nullptr;
            destroy_fn_ = nullptr;
        }
    }

public:
    // Default: empty command
    TaskCommand() noexcept = default;

    // Not copyable
    TaskCommand(const TaskCommand&) = delete;
    TaskCommand& operator=(const TaskCommand&) = delete;

    // Move constructor: transfer callable from other
    TaskCommand(TaskCommand&& other) noexcept
        : invoke_fn_{other.invoke_fn_}
        , move_fn_{other.move_fn_}
        , destroy_fn_{other.destroy_fn_}
        , debug_name_{other.debug_name_}
    {
        if (move_fn_) {
            move_fn_(storage_, other.storage_);
            other.invoke_fn_  = nullptr;
            other.move_fn_    = nullptr;
            other.destroy_fn_ = nullptr;
            other.debug_name_ = {};
        }
    }

    // Move assignment: destroy old, transfer new
    TaskCommand& operator=(TaskCommand&& other) noexcept {
        if (this != &other) {
            destroy_current();
            invoke_fn_  = other.invoke_fn_;
            move_fn_    = other.move_fn_;
            destroy_fn_ = other.destroy_fn_;
            debug_name_ = other.debug_name_;
            if (move_fn_) {
                move_fn_(storage_, other.storage_);
                other.invoke_fn_  = nullptr;
                other.move_fn_    = nullptr;
                other.destroy_fn_ = nullptr;
                other.debug_name_ = {};
            }
        }
        return *this;
    }

    // Destructor: destroy callable if present
    ~TaskCommand() noexcept {
        destroy_current();
    }

    // ---------------------------------------------------------------------------
    // Static factory
    // ---------------------------------------------------------------------------
    template <typename F>
        requires std::move_constructible<std::decay_t<F>> &&
                 std::invocable<std::decay_t<F>>
    static TaskCommand make(F&& f, std::string_view debug_name = {}) {
        using Stored = std::decay_t<F>;
        static_assert(sizeof(Stored) <= buffer_size,
            "TaskCommand: callable exceeds inline storage capacity (128 bytes). "
            "Reduce capture size or use indirection.");
        static_assert(alignof(Stored) <= buffer_align,
            "TaskCommand: callable alignment exceeds buffer alignment.");

        TaskCommand cmd;
        ::new (cmd.storage_) Stored(std::forward<F>(f));
        cmd.invoke_fn_  = &invoke_impl<Stored>;
        cmd.move_fn_    = &move_impl<Stored>;
        cmd.destroy_fn_ = &destroy_impl<Stored>;
        cmd.debug_name_ = debug_name;
        return cmd;
    }

    // ---------------------------------------------------------------------------
    // Execution
    // ---------------------------------------------------------------------------
    // Invoke the stored callable. Returns error if command is empty.
    Outcome<Unit> run() noexcept {
        if (!invoke_fn_) {
            return std::unexpected(PravahaError{
                ErrorKind::TaskFailed,
                "TaskCommand::run() called on empty command"
            });
        }
        return invoke_fn_(storage_);
    }

    // ---------------------------------------------------------------------------
    // Queries
    // ---------------------------------------------------------------------------
    // Check if this command holds a callable
    [[nodiscard]] bool has_value() const noexcept {
        return invoke_fn_ != nullptr;
    }

    // Check if this command is empty (moved-from or default)
    [[nodiscard]] bool empty() const noexcept {
        return invoke_fn_ == nullptr;
    }

    // Get the debug name (empty string_view if not set)
    [[nodiscard]] std::string_view name() const noexcept {
        return debug_name_;
    }

    // Explicit bool conversion
    [[nodiscard]] explicit operator bool() const noexcept {
        return has_value();
    }
};

// ============================================================================
//  SECTION 6: LAZY EXPRESSION DSL
// ============================================================================

// Forward declarations
template <typename F> class TaskExpr;
template <typename L, typename R> struct SequenceExpr;
template <typename L, typename R> struct ParallelExpr;

// ---------------------------------------------------------------------------
// 6.1 IsPravahaExpr — concept to identify expression tree nodes
// ---------------------------------------------------------------------------
namespace detail {

template <typename T>
struct is_pravaha_expr_impl : std::false_type {};

template <typename F>
struct is_pravaha_expr_impl<TaskExpr<F>> : std::true_type {};

template <typename L, typename R>
struct is_pravaha_expr_impl<SequenceExpr<L, R>> : std::true_type {};

template <typename L, typename R>
struct is_pravaha_expr_impl<ParallelExpr<L, R>> : std::true_type {};

} // namespace detail

template <typename T>
concept IsPravahaExpr = detail::is_pravaha_expr_impl<std::remove_cvref_t<T>>::value;

// ---------------------------------------------------------------------------
// 6.2 TaskExpr<F> — lazy single-task expression node
// ---------------------------------------------------------------------------
// Stores a name, callable, and execution domain. Does NOT execute on construction.
template <typename F>
class TaskExpr {
    std::string     name_;
    F               callable_;
    ExecutionDomain domain_;

public:
    TaskExpr(std::string name, F callable, ExecutionDomain domain = ExecutionDomain::CPU)
        : name_{std::move(name)}
        , callable_{std::move(callable)}
        , domain_{domain}
    {}

    // Accessors
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] ExecutionDomain domain() const noexcept { return domain_; }
    [[nodiscard]] const F& callable() const noexcept { return callable_; }
    [[nodiscard]] F& callable() noexcept { return callable_; }
};

// ---------------------------------------------------------------------------
// 6.3 SequenceExpr<L, R> — two expressions composed in sequence (L then R)
// ---------------------------------------------------------------------------
template <typename L, typename R>
struct SequenceExpr {
    L left;
    R right;

    SequenceExpr(L l, R r)
        : left{std::move(l)}
        , right{std::move(r)}
    {}
};

// ---------------------------------------------------------------------------
// 6.4 ParallelExpr<L, R> — two expressions composed in parallel (L and R)
// ---------------------------------------------------------------------------
template <typename L, typename R>
struct ParallelExpr {
    L              left;
    R              right;
    JoinPolicyKind policy;

    ParallelExpr(L l, R r, JoinPolicyKind p = JoinPolicyKind::AllOrNothing)
        : left{std::move(l)}
        , right{std::move(r)}
        , policy{p}
    {}
};

// ---------------------------------------------------------------------------
// 6.5 task() — factory for TaskExpr (lazy, does not execute)
// ---------------------------------------------------------------------------
template <typename F>
    requires std::move_constructible<std::decay_t<F>>
[[nodiscard]] auto task(std::string name, F&& callable) {
    return TaskExpr<std::decay_t<F>>{std::move(name), std::forward<F>(callable)};
}

// ---------------------------------------------------------------------------
// 6.6 task_on() — factory for TaskExpr with explicit ExecutionDomain
// ---------------------------------------------------------------------------
template <typename F>
    requires std::move_constructible<std::decay_t<F>>
[[nodiscard]] auto task_on(ExecutionDomain domain, std::string name, F&& callable) {
    return TaskExpr<std::decay_t<F>>{std::move(name), std::forward<F>(callable), domain};
}

// ---------------------------------------------------------------------------
// 6.7 operator| — sequence composition
// ---------------------------------------------------------------------------
template <IsPravahaExpr L, IsPravahaExpr R>
[[nodiscard]] auto operator|(L&& lhs, R&& rhs) {
    return SequenceExpr<std::decay_t<L>, std::decay_t<R>>{
        std::forward<L>(lhs), std::forward<R>(rhs)
    };
}

// ---------------------------------------------------------------------------
// 6.8 operator& — parallel composition
// ---------------------------------------------------------------------------
template <IsPravahaExpr L, IsPravahaExpr R>
[[nodiscard]] auto operator&(L&& lhs, R&& rhs) {
    return ParallelExpr<std::decay_t<L>, std::decay_t<R>>{
        std::forward<L>(lhs), std::forward<R>(rhs)
    };
}

// ---------------------------------------------------------------------------
// 6.9 collect_all() — marks a parallel expression with CollectAll policy
// ---------------------------------------------------------------------------
template <typename L, typename R>
[[nodiscard]] auto collect_all(ParallelExpr<L, R> expr) {
    expr.policy = JoinPolicyKind::CollectAll;
    return expr;
}

// Overload for any expression that creates a ParallelExpr wrapper with CollectAll
template <IsPravahaExpr E>
    requires (!requires { typename std::remove_cvref_t<E>::left; typename std::remove_cvref_t<E>::right; } ||
              !std::same_as<std::remove_cvref_t<E>,
                            ParallelExpr<typename std::remove_cvref_t<E>::left,
                                         typename std::remove_cvref_t<E>::right>>)
[[nodiscard]] auto collect_all(E&& expr) {
    // For non-ParallelExpr types, wrap in a parallel-with-self is not meaningful.
    // Return as-is — this overload exists for forward compatibility.
    return std::forward<E>(expr);
}

} // namespace pravaha
