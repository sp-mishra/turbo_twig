#pragma once
// ============================================================================
// pravaha.hpp - C++23 Task-Graph Orchestration Engine
// ============================================================================
// Single-header. No macros. No virtual functions. No std::function.
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
#include <vector>
#include <unordered_map>

#include "containers/graph/LiteGraph.hpp"
#include "containers/graph/LiteGraphAlgorithms.hpp"
#include "containers/tree/NAryTree.hpp"
#include "edsl/lithe.hpp"
#include "utils/meta.hpp"

namespace pravaha {

// ============================================================================
//  SECTION 1: ERROR HANDLING
// ============================================================================

enum class ErrorKind {
    ParseError, ValidationError, CycleDetected, SymbolNotFound,
    TypeMismatch, ExecutorUnavailable, DomainConstraintViolation,
    PayloadNotSerializable, PayloadNotTransferable, TaskFailed,
    TaskCanceled, QueueRejected, Timeout, InternalError
};

struct PravahaError {
    ErrorKind kind;
    std::string message;
    std::string task_identity;
    std::source_location location;

    constexpr PravahaError(ErrorKind k, std::string msg, std::string task_id = {},
        std::source_location loc = std::source_location::current())
        : kind{k}, message{std::move(msg)}, task_identity{std::move(task_id)}, location{loc} {}

    static constexpr PravahaError make(ErrorKind k, std::string msg,
        std::source_location loc = std::source_location::current()) {
        return PravahaError{k, std::move(msg), {}, loc};
    }

    static constexpr PravahaError make_for_task(ErrorKind k, std::string msg, std::string task_id,
        std::source_location loc = std::source_location::current()) {
        return PravahaError{k, std::move(msg), std::move(task_id), loc};
    }
};

template <class T>
using Outcome = std::expected<T, PravahaError>;

using Unit = std::monostate;

// ============================================================================
//  SECTION 2: ENUMERATIONS
// ============================================================================

enum class TaskState { Created, Ready, Scheduled, Running, Succeeded, Failed, Canceled, Skipped };
enum class JoinPolicyKind { AllOrNothing, CollectAll, AnySuccess, Quorum };
enum class ExecutionDomain { Inline, CPU, IO, Fiber, Coroutine, External };

// ============================================================================
//  SECTION 3: PAYLOAD CONCEPTS
// ============================================================================

template <typename T>
concept Payload = std::move_constructible<T> && std::destructible<T>;

template <typename T>
concept LocalPayload = Payload<T> && std::move_constructible<T>;

template <typename T>
concept TransferablePayload = Payload<T> && std::copy_constructible<T>;

namespace detail {
template <typename T>
consteval bool is_serializable_payload_check() {
    if constexpr (!std::is_trivially_copyable_v<T> || !std::is_standard_layout_v<T>) return false;
    else if constexpr (meta::Reflectable<T>) return meta::is_zero_copy_serializable<T>();
    else return true;
}
} // namespace detail

template <typename T>
concept SerializablePayload = Payload<T> && detail::is_serializable_payload_check<T>();

template <typename F, typename Result, typename... Args>
concept InvocableTask = std::invocable<F, Args...> &&
    std::same_as<std::invoke_result_t<F, Args...>, Outcome<Result>>;

// ============================================================================
//  SECTION 4: CANCELLATION PRIMITIVES
// ============================================================================

struct CancellationState {
    std::atomic<bool> requested{false};
    void request() noexcept { requested.store(true, std::memory_order_release); }
    [[nodiscard]] bool is_requested() const noexcept { return requested.load(std::memory_order_acquire); }
};

class CancellationToken {
    std::shared_ptr<CancellationState> state_{};
public:
    CancellationToken() noexcept = default;
    explicit CancellationToken(std::shared_ptr<CancellationState> state) noexcept : state_{std::move(state)} {}
    [[nodiscard]] bool stop_requested() const noexcept { return state_ && state_->is_requested(); }
    [[nodiscard]] bool has_state() const noexcept { return state_ != nullptr; }
};

class CancellationSource {
    std::shared_ptr<CancellationState> state_;
public:
    CancellationSource() : state_{std::make_shared<CancellationState>()} {}
    [[nodiscard]] CancellationToken token() const noexcept { return CancellationToken{state_}; }
    void request_stop() noexcept { state_->request(); }
    [[nodiscard]] bool stop_requested() const noexcept { return state_->is_requested(); }
};

class CancellationScope {
    CancellationSource local_source_;
    CancellationToken parent_token_;
public:
    CancellationScope() = default;
    explicit CancellationScope(CancellationToken parent) noexcept : parent_token_{std::move(parent)} {}
    [[nodiscard]] bool stop_requested() const noexcept { return local_source_.stop_requested() || parent_token_.stop_requested(); }
    [[nodiscard]] CancellationToken token() const noexcept { return local_source_.token(); }
    void request_stop() noexcept { local_source_.request_stop(); }
};

// ============================================================================
//  SECTION 5: TASK COMMAND - STATIC TYPE ERASURE
// ============================================================================

class TaskCommand {
    static constexpr std::size_t buffer_size = 128;
    static constexpr std::size_t buffer_align = alignof(std::max_align_t);
    using invoke_fn_t = Outcome<Unit>(*)(void*) noexcept;
    using move_fn_t = void(*)(void*, void*) noexcept;
    using destroy_fn_t = void(*)(void*) noexcept;

    alignas(buffer_align) unsigned char storage_[buffer_size]{};
    invoke_fn_t invoke_fn_{nullptr};
    move_fn_t move_fn_{nullptr};
    destroy_fn_t destroy_fn_{nullptr};
    std::string_view debug_name_{};

    template <typename F>
    static Outcome<Unit> invoke_impl(void* s) noexcept {
        try { (*std::launder(reinterpret_cast<F*>(s)))(); return Unit{}; }
        catch (const std::exception& e) { return std::unexpected(PravahaError{ErrorKind::TaskFailed, std::string{"TaskCommand exception: "} + e.what()}); }
        catch (...) { return std::unexpected(PravahaError{ErrorKind::TaskFailed, "TaskCommand: unknown exception"}); }
    }
    template <typename F>
    static void move_impl(void* d, void* s) noexcept { ::new(d) F(std::move(*std::launder(reinterpret_cast<F*>(s)))); std::launder(reinterpret_cast<F*>(s))->~F(); }
    template <typename F>
    static void destroy_impl(void* s) noexcept { std::launder(reinterpret_cast<F*>(s))->~F(); }
    void destroy_current() noexcept { if (destroy_fn_) { destroy_fn_(storage_); invoke_fn_ = nullptr; move_fn_ = nullptr; destroy_fn_ = nullptr; } }

public:
    TaskCommand() noexcept = default;
    TaskCommand(const TaskCommand&) = delete;
    TaskCommand& operator=(const TaskCommand&) = delete;
    TaskCommand(TaskCommand&& o) noexcept : invoke_fn_{o.invoke_fn_}, move_fn_{o.move_fn_}, destroy_fn_{o.destroy_fn_}, debug_name_{o.debug_name_} {
        if (move_fn_) { move_fn_(storage_, o.storage_); o.invoke_fn_ = nullptr; o.move_fn_ = nullptr; o.destroy_fn_ = nullptr; o.debug_name_ = {}; }
    }
    TaskCommand& operator=(TaskCommand&& o) noexcept {
        if (this != &o) { destroy_current(); invoke_fn_ = o.invoke_fn_; move_fn_ = o.move_fn_; destroy_fn_ = o.destroy_fn_; debug_name_ = o.debug_name_;
            if (move_fn_) { move_fn_(storage_, o.storage_); o.invoke_fn_ = nullptr; o.move_fn_ = nullptr; o.destroy_fn_ = nullptr; o.debug_name_ = {}; } }
        return *this;
    }
    ~TaskCommand() noexcept { destroy_current(); }

    template <typename F> requires std::move_constructible<std::decay_t<F>> && std::invocable<std::decay_t<F>>
    static TaskCommand make(F&& f, std::string_view debug_name = {}) {
        using S = std::decay_t<F>;
        static_assert(sizeof(S) <= buffer_size, "TaskCommand: callable exceeds inline storage capacity (128 bytes).");
        static_assert(alignof(S) <= buffer_align, "TaskCommand: callable alignment exceeds buffer alignment.");
        TaskCommand cmd; ::new(cmd.storage_) S(std::forward<F>(f));
        cmd.invoke_fn_ = &invoke_impl<S>; cmd.move_fn_ = &move_impl<S>; cmd.destroy_fn_ = &destroy_impl<S>; cmd.debug_name_ = debug_name;
        return cmd;
    }

    Outcome<Unit> run() noexcept { if (!invoke_fn_) return std::unexpected(PravahaError{ErrorKind::TaskFailed, "TaskCommand::run() called on empty command"}); return invoke_fn_(storage_); }
    [[nodiscard]] bool has_value() const noexcept { return invoke_fn_ != nullptr; }
    [[nodiscard]] bool empty() const noexcept { return invoke_fn_ == nullptr; }
    [[nodiscard]] std::string_view name() const noexcept { return debug_name_; }
    [[nodiscard]] explicit operator bool() const noexcept { return has_value(); }
};

// ============================================================================
//  SECTION 6: LAZY EXPRESSION DSL
// ============================================================================

template <typename F> class TaskExpr;
template <typename L, typename R> struct SequenceExpr;
template <typename L, typename R> struct ParallelExpr;

namespace detail {
template <typename T> struct is_pravaha_expr_impl : std::false_type {};
template <typename F> struct is_pravaha_expr_impl<TaskExpr<F>> : std::true_type {};
template <typename L, typename R> struct is_pravaha_expr_impl<SequenceExpr<L, R>> : std::true_type {};
template <typename L, typename R> struct is_pravaha_expr_impl<ParallelExpr<L, R>> : std::true_type {};
} // namespace detail

template <typename T>
concept IsPravahaExpr = detail::is_pravaha_expr_impl<std::remove_cvref_t<T>>::value;

template <typename F>
class TaskExpr {
    std::string name_; F callable_; ExecutionDomain domain_;
public:
    TaskExpr(std::string name, F callable, ExecutionDomain domain = ExecutionDomain::CPU)
        : name_{std::move(name)}, callable_{std::move(callable)}, domain_{domain} {}
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] ExecutionDomain domain() const noexcept { return domain_; }
    [[nodiscard]] const F& callable() const noexcept { return callable_; }
    [[nodiscard]] F& callable() noexcept { return callable_; }
};

template <typename L, typename R>
struct SequenceExpr {
    L left; R right;
    SequenceExpr(L l, R r) : left{std::move(l)}, right{std::move(r)} {}
};

template <typename L, typename R>
struct ParallelExpr {
    L left; R right; JoinPolicyKind policy;
    ParallelExpr(L l, R r, JoinPolicyKind p = JoinPolicyKind::AllOrNothing)
        : left{std::move(l)}, right{std::move(r)}, policy{p} {}
};

template <typename F> requires std::move_constructible<std::decay_t<F>>
[[nodiscard]] auto task(std::string name, F&& callable) {
    return TaskExpr<std::decay_t<F>>{std::move(name), std::forward<F>(callable)};
}

template <typename F> requires std::move_constructible<std::decay_t<F>>
[[nodiscard]] auto task_on(ExecutionDomain domain, std::string name, F&& callable) {
    return TaskExpr<std::decay_t<F>>{std::move(name), std::forward<F>(callable), domain};
}

template <IsPravahaExpr L, IsPravahaExpr R>
[[nodiscard]] auto operator|(L&& lhs, R&& rhs) {
    return SequenceExpr<std::decay_t<L>, std::decay_t<R>>{std::forward<L>(lhs), std::forward<R>(rhs)};
}

template <IsPravahaExpr L, IsPravahaExpr R>
[[nodiscard]] auto operator&(L&& lhs, R&& rhs) {
    return ParallelExpr<std::decay_t<L>, std::decay_t<R>>{std::forward<L>(lhs), std::forward<R>(rhs)};
}

template <typename L, typename R>
[[nodiscard]] auto collect_all(ParallelExpr<L, R> expr) { expr.policy = JoinPolicyKind::CollectAll; return expr; }

template <IsPravahaExpr E>
    requires (!requires { typename std::remove_cvref_t<E>::left; typename std::remove_cvref_t<E>::right; } ||
              !std::same_as<std::remove_cvref_t<E>,
                            ParallelExpr<typename std::remove_cvref_t<E>::left,
                                         typename std::remove_cvref_t<E>::right>>)
[[nodiscard]] auto collect_all(E&& expr) { return std::forward<E>(expr); }

// ============================================================================
//  SECTION 7: TASK IR
// ============================================================================

struct TaskId {
    std::size_t value;
    static constexpr std::size_t invalid_value = ~std::size_t{0};
    constexpr TaskId() noexcept : value{invalid_value} {}
    constexpr explicit TaskId(std::size_t v) noexcept : value{v} {}
    [[nodiscard]] constexpr bool is_valid() const noexcept { return value != invalid_value; }
    constexpr bool operator==(const TaskId&) const noexcept = default;
    constexpr auto operator<=>(const TaskId&) const noexcept = default;
};

inline constexpr TaskId invalid_task_id{};

enum class EdgeKind { Sequence, Data, Cancellation, Join };

struct IrNode {
    TaskId id;
    std::string name;
    ExecutionDomain domain{ExecutionDomain::CPU};
    TaskState state{TaskState::Created};
    TaskCommand command;
    IrNode() = default;
    IrNode(TaskId id_, std::string name_, ExecutionDomain dom_, TaskCommand cmd_)
        : id{id_}, name{std::move(name_)}, domain{dom_}, state{TaskState::Created}, command{std::move(cmd_)} {}
};

struct IrEdge {
    TaskId from;
    TaskId to;
    EdgeKind kind;
    IrEdge() = default;
    IrEdge(TaskId f, TaskId t, EdgeKind k) noexcept : from{f}, to{t}, kind{k} {}
};

struct TaskIr {
    std::vector<IrNode> nodes;
    std::vector<IrEdge> edges;

    TaskId add_node(std::string name, ExecutionDomain domain, TaskCommand cmd) {
        TaskId id{nodes.size()};
        nodes.emplace_back(id, std::move(name), domain, std::move(cmd));
        return id;
    }
    void add_edge(TaskId from, TaskId to, EdgeKind kind) {
        edges.emplace_back(from, to, kind);
    }
    [[nodiscard]] std::size_t node_count() const noexcept { return nodes.size(); }
    [[nodiscard]] std::size_t edge_count() const noexcept { return edges.size(); }
};

// ============================================================================
//  SECTION 7.5: LOWERING
// ============================================================================

namespace detail {

struct LowerResult {
    std::vector<TaskId> starts;
    std::vector<TaskId> terminals;
    TaskIr ir;
};

inline LowerResult merge_into(LowerResult& dst, LowerResult src) {
    const std::size_t offset = dst.ir.nodes.size();
    for (auto& node : src.ir.nodes) { node.id = TaskId{node.id.value + offset}; dst.ir.nodes.push_back(std::move(node)); }
    for (auto& edge : src.ir.edges) { edge.from = TaskId{edge.from.value + offset}; edge.to = TaskId{edge.to.value + offset}; dst.ir.edges.push_back(std::move(edge)); }
    std::vector<TaskId> rs; rs.reserve(src.starts.size());
    for (auto id : src.starts) rs.emplace_back(id.value + offset);
    std::vector<TaskId> rt; rt.reserve(src.terminals.size());
    for (auto id : src.terminals) rt.emplace_back(id.value + offset);
    return LowerResult{std::move(rs), std::move(rt), {}};
}

template <typename F> LowerResult lower_impl(TaskExpr<F> expr);
template <typename L, typename R> LowerResult lower_impl(SequenceExpr<L, R> expr);
template <typename L, typename R> LowerResult lower_impl(ParallelExpr<L, R> expr);

template <typename F>
LowerResult lower_impl(TaskExpr<F> expr) {
    LowerResult result;
    auto cmd = TaskCommand::make(std::move(expr.callable()), expr.name());
    TaskId id = result.ir.add_node(expr.name(), expr.domain(), std::move(cmd));
    result.starts.push_back(id);
    result.terminals.push_back(id);
    return result;
}

template <typename L, typename R>
LowerResult lower_impl(SequenceExpr<L, R> expr) {
    auto left_result = lower_impl(std::move(expr.left));
    auto right_result = lower_impl(std::move(expr.right));
    auto right_remap = merge_into(left_result, std::move(right_result));
    for (auto t : left_result.terminals)
        for (auto s : right_remap.starts)
            left_result.ir.add_edge(t, s, EdgeKind::Sequence);
    left_result.terminals = std::move(right_remap.terminals);
    return left_result;
}

template <typename L, typename R>
LowerResult lower_impl(ParallelExpr<L, R> expr) {
    auto left_result = lower_impl(std::move(expr.left));
    auto right_result = lower_impl(std::move(expr.right));
    auto right_remap = merge_into(left_result, std::move(right_result));
    for (auto s : right_remap.starts) left_result.starts.push_back(s);
    for (auto t : right_remap.terminals) left_result.terminals.push_back(t);
    return left_result;
}

} // namespace detail

template <IsPravahaExpr Expr>
Outcome<TaskIr> lower_to_ir(Expr&& expr) {
    auto result = detail::lower_impl(std::move(expr));
    return std::move(result.ir);
}

// ============================================================================
//  SECTION 8: LITEGRAPH VALIDATION LAYER
// ============================================================================

// ---------------------------------------------------------------------------
// 8.1 Graph payload types
// ---------------------------------------------------------------------------

struct GraphNodePayload {
    TaskId task_id;
    std::string name;
    bool operator==(const GraphNodePayload& o) const noexcept { return task_id == o.task_id && name == o.name; }
};

struct GraphEdgePayload {
    EdgeKind kind;
    bool operator==(const GraphEdgePayload& o) const noexcept { return kind == o.kind; }
};

// ============================================================================
//  SECTION 9: RUNTIME STATE & SCHEDULER
// ============================================================================

struct RunResult {
    TaskState final_state{TaskState::Succeeded};
    std::vector<TaskState> node_states;
    std::vector<PravahaError> errors;
};

struct RuntimeState {
    std::vector<TaskState> node_states;
    std::vector<std::size_t> remaining_deps;
    std::vector<std::vector<std::size_t>> successors; // outgoing dep adjacency
    std::vector<PravahaError> errors;
    bool canceled{false};

    static RuntimeState build(const TaskIr& ir) {
        RuntimeState rs;
        std::size_t n = ir.nodes.size();
        rs.node_states.resize(n, TaskState::Created);
        rs.remaining_deps.resize(n, 0);
        rs.successors.resize(n);

        for (const auto& edge : ir.edges) {
            if (edge.kind != EdgeKind::Sequence && edge.kind != EdgeKind::Data) continue;
            auto from = edge.from.value;
            auto to = edge.to.value;
            if (from < n && to < n) {
                rs.remaining_deps[to]++;
                rs.successors[from].push_back(to);
            }
        }

        for (std::size_t i = 0; i < n; ++i) {
            if (rs.remaining_deps[i] == 0) {
                rs.node_states[i] = TaskState::Ready;
            }
        }

        return rs;
    }

    void mark_succeeded(std::size_t idx) {
        node_states[idx] = TaskState::Succeeded;
        for (auto succ : successors[idx]) {
            if (remaining_deps[succ] > 0) {
                remaining_deps[succ]--;
                if (remaining_deps[succ] == 0 && node_states[succ] == TaskState::Created) {
                    node_states[succ] = TaskState::Ready;
                }
            }
        }
    }

    void mark_failed(std::size_t idx, PravahaError err) {
        node_states[idx] = TaskState::Failed;
        errors.push_back(std::move(err));
        skip_downstream(idx);
    }

    void skip_downstream(std::size_t idx) {
        for (auto succ : successors[idx]) {
            if (node_states[succ] == TaskState::Created || node_states[succ] == TaskState::Ready) {
                node_states[succ] = TaskState::Skipped;
                skip_downstream(succ);
            }
        }
    }

    [[nodiscard]] bool has_ready() const {
        for (auto s : node_states)
            if (s == TaskState::Ready) return true;
        return false;
    }

    [[nodiscard]] std::size_t next_ready() const {
        for (std::size_t i = 0; i < node_states.size(); ++i)
            if (node_states[i] == TaskState::Ready) return i;
        return ~std::size_t{0};
    }

    RunResult finalize() const {
        RunResult result;
        result.node_states = node_states;
        result.errors = errors;
        result.final_state = TaskState::Succeeded;
        for (auto s : node_states) {
            if (s == TaskState::Failed) { result.final_state = TaskState::Failed; break; }
            if (s == TaskState::Canceled) { result.final_state = TaskState::Canceled; break; }
        }
        return result;
    }
};

// ============================================================================
//  SECTION 10: INLINE BACKEND & RUNNER
// ============================================================================

class InlineBackend {
    bool stop_requested_{false};
public:
    InlineBackend() = default;

    Outcome<Unit> submit(TaskCommand& cmd) noexcept {
        if (stop_requested_) {
            return std::unexpected(PravahaError{ErrorKind::TaskCanceled, "Backend stop requested"});
        }
        return cmd.run();
    }

    void drain() noexcept {}
    void request_stop() noexcept { stop_requested_ = true; }
    [[nodiscard]] bool stopped() const noexcept { return stop_requested_; }
};

template <typename Backend = InlineBackend>
class Runner {
    Backend backend_;

public:
    Runner() = default;
    explicit Runner(Backend b) : backend_{std::move(b)} {}

    template <IsPravahaExpr Expr>
    Outcome<RunResult> submit(Expr&& expr) {
        auto ir_result = lower_to_ir(std::forward<Expr>(expr));
        if (!ir_result.has_value()) return std::unexpected(ir_result.error());
        auto& ir = ir_result.value();

        auto validation = validate_ir_with_litegraph(ir);
        if (!validation.has_value()) return std::unexpected(validation.error());

        return execute(ir);
    }

    void request_stop() noexcept { backend_.request_stop(); }

private:
    Outcome<RunResult> execute(TaskIr& ir) {
        auto state = RuntimeState::build(ir);

        while (state.has_ready()) {
            auto idx = state.next_ready();
            if (idx >= ir.nodes.size()) break;

            if (backend_.stopped()) {
                state.node_states[idx] = TaskState::Canceled;
                state.canceled = true;
                continue;
            }

            state.node_states[idx] = TaskState::Scheduled;
            state.node_states[idx] = TaskState::Running;

            auto cmd_result = backend_.submit(ir.nodes[idx].command);

            if (cmd_result.has_value()) {
                state.mark_succeeded(idx);
            } else {
                state.mark_failed(idx, std::move(cmd_result.error()));
            }
        }

        return state.finalize();
    }
};

} // namespace pravaha

// std::hash specializations for LiteGraph Hashable concept
template<> struct std::hash<pravaha::GraphNodePayload> {
    std::size_t operator()(const pravaha::GraphNodePayload& p) const noexcept {
        return std::hash<std::size_t>{}(p.task_id.value) ^ (std::hash<std::string>{}(p.name) << 1);
    }
};
template<> struct std::hash<pravaha::GraphEdgePayload> {
    std::size_t operator()(const pravaha::GraphEdgePayload& p) const noexcept {
        return std::hash<int>{}(static_cast<int>(p.kind));
    }
};

namespace pravaha {

// ---------------------------------------------------------------------------
// 8.2 ExecutionGraph alias
// ---------------------------------------------------------------------------
using ExecutionGraph = litegraph::Graph<GraphNodePayload, GraphEdgePayload, litegraph::Directed>;

// ---------------------------------------------------------------------------
// 8.3 to_litegraph - convert TaskIr to ExecutionGraph
// ---------------------------------------------------------------------------
inline Outcome<ExecutionGraph> to_litegraph(const TaskIr& ir) {
    ExecutionGraph graph;
    graph.reserve_nodes(ir.nodes.size());
    graph.reserve_edges(ir.edges.size());

    // Map TaskId -> litegraph::NodeId
    std::unordered_map<std::size_t, litegraph::NodeId> id_map;
    id_map.reserve(ir.nodes.size());

    for (const auto& node : ir.nodes) {
        litegraph::NodeId nid = graph.add_node(GraphNodePayload{node.id, node.name});
        id_map[node.id.value] = nid;
    }

    for (const auto& edge : ir.edges) {
        auto from_it = id_map.find(edge.from.value);
        auto to_it = id_map.find(edge.to.value);
        if (from_it == id_map.end() || to_it == id_map.end()) {
            return std::unexpected(PravahaError{
                ErrorKind::ValidationError,
                "Invalid edge endpoint: TaskId not found in IR nodes"
            });
        }
        graph.add_edge(from_it->second, to_it->second, GraphEdgePayload{edge.kind});
    }

    return graph;
}

// ---------------------------------------------------------------------------
// 8.4 validate_ir_with_litegraph - cycle detection and structural validation
// ---------------------------------------------------------------------------
inline Outcome<Unit> validate_ir_with_litegraph(const TaskIr& ir) {
    // Build a graph containing only dependency edges (Sequence, Data)
    ExecutionGraph dep_graph;
    dep_graph.reserve_nodes(ir.nodes.size());

    std::unordered_map<std::size_t, litegraph::NodeId> id_map;
    id_map.reserve(ir.nodes.size());

    for (const auto& node : ir.nodes) {
        litegraph::NodeId nid = dep_graph.add_node(GraphNodePayload{node.id, node.name});
        id_map[node.id.value] = nid;
    }

    for (const auto& edge : ir.edges) {
        // Only include dependency edges for cycle validation
        if (edge.kind != EdgeKind::Sequence && edge.kind != EdgeKind::Data)
            continue;

        auto from_it = id_map.find(edge.from.value);
        auto to_it = id_map.find(edge.to.value);
        if (from_it == id_map.end() || to_it == id_map.end()) {
            return std::unexpected(PravahaError{
                ErrorKind::ValidationError,
                "Invalid edge endpoint: TaskId not found in IR nodes"
            });
        }
        dep_graph.add_edge(from_it->second, to_it->second, GraphEdgePayload{edge.kind});
    }

    // Use LiteGraph cycle detection
    if (litegraph::has_cycle(dep_graph)) {
        return std::unexpected(PravahaError{
            ErrorKind::CycleDetected,
            "Cycle detected in task dependency graph"
        });
    }

    return Unit{};
}

// ---------------------------------------------------------------------------
// 8.5 topological_order - compute execution order using LiteGraph
// ---------------------------------------------------------------------------
inline Outcome<std::vector<TaskId>> topological_order(const TaskIr& ir) {
    // Build dependency graph
    ExecutionGraph dep_graph;
    dep_graph.reserve_nodes(ir.nodes.size());

    std::unordered_map<std::size_t, litegraph::NodeId> task_to_lite;
    task_to_lite.reserve(ir.nodes.size());

    for (const auto& node : ir.nodes) {
        litegraph::NodeId nid = dep_graph.add_node(GraphNodePayload{node.id, node.name});
        task_to_lite[node.id.value] = nid;
    }

    for (const auto& edge : ir.edges) {
        if (edge.kind != EdgeKind::Sequence && edge.kind != EdgeKind::Data)
            continue;
        auto from_it = task_to_lite.find(edge.from.value);
        auto to_it = task_to_lite.find(edge.to.value);
        if (from_it == task_to_lite.end() || to_it == task_to_lite.end()) {
            return std::unexpected(PravahaError{
                ErrorKind::ValidationError,
                "Invalid edge endpoint in topological_order"
            });
        }
        dep_graph.add_edge(from_it->second, to_it->second, GraphEdgePayload{edge.kind});
    }

    // Check for cycles first
    if (litegraph::has_cycle(dep_graph)) {
        return std::unexpected(PravahaError{
            ErrorKind::CycleDetected,
            "Cycle detected - topological order undefined"
        });
    }

    // Use LiteGraph topological sort
    std::vector<litegraph::NodeId> topo = litegraph::topological_sort(dep_graph);

    // Map back to TaskIds
    std::vector<TaskId> result;
    result.reserve(topo.size());
    for (const auto& nid : topo) {
        const auto& payload = dep_graph.node_data(nid);
        result.push_back(payload.task_id);
    }

    return result;
}

} // namespace pravaha
