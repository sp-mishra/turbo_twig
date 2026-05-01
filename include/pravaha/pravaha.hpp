#pragma once
// ============================================================================
// pravaha.hpp - C++23 Task-Graph Orchestration Engine
// ============================================================================
// Single-header. No macros. No virtual functions. No std::function.
// ============================================================================

#include <atomic>
#include <algorithm>
#include <cctype>
#include <compare>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <deque>
#include <exception>
#include <expected>
#include <memory>
#include <mutex>
#include <new>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>
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

template<class T>
struct is_outcome : std::false_type {};

template<class T>
struct is_outcome<Outcome<T>> : std::true_type {};

template<class T>
inline constexpr bool is_outcome_v = is_outcome<std::remove_cvref_t<T>>::value;

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
concept TransferablePayload = Payload<T>
    && std::is_trivially_copyable_v<T>
    && std::is_standard_layout_v<T>;

template <typename T>
concept CopyablePayload = Payload<T> && std::copy_constructible<T>;

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
    std::string debug_name_{};

    template <typename F>
    static Outcome<Unit> invoke_impl(void* s) noexcept {
        try {
            F& fn = *std::launder(reinterpret_cast<F*>(s));
            using R = std::invoke_result_t<F&>;

            if constexpr (std::is_void_v<R>) {
                fn();
                return Outcome<Unit>{Unit{}};
            } else if constexpr (std::same_as<std::remove_cvref_t<R>, Unit>) {
                (void)fn();
                return Outcome<Unit>{Unit{}};
            } else if constexpr (std::same_as<std::remove_cvref_t<R>, Outcome<Unit>>) {
                return fn();
            } else if constexpr (is_outcome_v<R>) {
                auto out = fn();
                if (!out.has_value()) {
                    return std::unexpected(out.error());
                }
                return Outcome<Unit>{Unit{}};
            } else {
                // v0.1 compatibility: invoke unsupported return types and treat as success.
                (void)fn();
                return Outcome<Unit>{Unit{}};
            }
        }
        catch (const std::exception& e) {
            return std::unexpected(PravahaError{ErrorKind::TaskFailed, e.what()});
        }
        catch (...) {
            return std::unexpected(PravahaError{ErrorKind::TaskFailed, "unknown exception"});
        }
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
    TaskCommand(TaskCommand&& o) noexcept : invoke_fn_{o.invoke_fn_}, move_fn_{o.move_fn_}, destroy_fn_{o.destroy_fn_}, debug_name_{std::move(o.debug_name_)} {
        if (move_fn_) { move_fn_(storage_, o.storage_); o.invoke_fn_ = nullptr; o.move_fn_ = nullptr; o.destroy_fn_ = nullptr; o.debug_name_.clear(); }
    }
    TaskCommand& operator=(TaskCommand&& o) noexcept {
        if (this != &o) { destroy_current(); invoke_fn_ = o.invoke_fn_; move_fn_ = o.move_fn_; destroy_fn_ = o.destroy_fn_; debug_name_ = std::move(o.debug_name_);
            if (move_fn_) { move_fn_(storage_, o.storage_); o.invoke_fn_ = nullptr; o.move_fn_ = nullptr; o.destroy_fn_ = nullptr; o.debug_name_.clear(); } }
        return *this;
    }
    ~TaskCommand() noexcept { destroy_current(); }

    template <typename F> requires std::move_constructible<std::decay_t<F>> && std::invocable<std::decay_t<F>>
    static TaskCommand make(F&& f, std::string_view debug_name = {}) {
        using S = std::decay_t<F>;
        static_assert(sizeof(S) <= buffer_size, "TaskCommand: callable exceeds inline storage capacity (128 bytes).");
        static_assert(alignof(S) <= buffer_align, "TaskCommand: callable alignment exceeds buffer alignment.");
        TaskCommand cmd; ::new(cmd.storage_) S(std::forward<F>(f));
        cmd.invoke_fn_ = &invoke_impl<S>; cmd.move_fn_ = &move_impl<S>; cmd.destroy_fn_ = &destroy_impl<S>; cmd.debug_name_ = std::string{debug_name};
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

struct PayloadMeta {
    bool output_checked{false};
    bool output_transferable{false};
    bool output_serializable{false};
    std::string output_type_name;
};

struct IrNode {
    TaskId id;
    std::string name;
    ExecutionDomain domain{ExecutionDomain::CPU};
    TaskState state{TaskState::Created};
    TaskCommand command;
    PayloadMeta payload_meta;
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

struct IrJoinGroup {
    std::vector<TaskId> members;
    JoinPolicyKind policy{JoinPolicyKind::AllOrNothing};
};

struct TaskIr {
    std::vector<IrNode> nodes;
    std::vector<IrEdge> edges;
    std::vector<IrJoinGroup> join_groups;

    TaskId add_node(std::string name, ExecutionDomain domain, TaskCommand cmd) {
        TaskId id{nodes.size()};
        nodes.emplace_back(id, std::move(name), domain, std::move(cmd));
        return id;
    }
    void add_edge(TaskId from, TaskId to, EdgeKind kind) {
        edges.emplace_back(from, to, kind);
    }
    void add_join_group(std::vector<TaskId> members, JoinPolicyKind policy) {
        join_groups.push_back(IrJoinGroup{std::move(members), policy});
    }
    [[nodiscard]] std::size_t node_count() const noexcept { return nodes.size(); }
    [[nodiscard]] std::size_t edge_count() const noexcept { return edges.size(); }
};

// ============================================================================
//  SECTION 7.5: LOWERING
// ============================================================================

namespace detail {

template <class T>
struct unwrap_outcome {
    using type = T;
};

template <class T>
struct unwrap_outcome<Outcome<T>> {
    using type = T;
};

template <class T>
using unwrap_outcome_t = typename unwrap_outcome<std::remove_cvref_t<T>>::type;

template <class F>
using callable_payload_t = std::conditional_t<
    std::is_void_v<std::invoke_result_t<F>>,
    Unit,
    unwrap_outcome_t<std::invoke_result_t<F>>
>;

struct LowerResult {
    std::vector<TaskId> starts;
    std::vector<TaskId> terminals;
    TaskIr ir;
};

inline LowerResult merge_into(LowerResult& dst, LowerResult src) {
    const std::size_t offset = dst.ir.nodes.size();
    for (auto& node : src.ir.nodes) { node.id = TaskId{node.id.value + offset}; dst.ir.nodes.push_back(std::move(node)); }
    for (auto& edge : src.ir.edges) { edge.from = TaskId{edge.from.value + offset}; edge.to = TaskId{edge.to.value + offset}; dst.ir.edges.push_back(std::move(edge)); }
    for (auto& jg : src.ir.join_groups) {
        for (auto& m : jg.members) m = TaskId{m.value + offset};
        dst.ir.join_groups.push_back(std::move(jg));
    }
    std::vector<TaskId> rs; rs.reserve(src.starts.size());
    for (auto id : src.starts) rs.emplace_back(id.value + offset);
    std::vector<TaskId> rt; rt.reserve(src.terminals.size());
    for (auto id : src.terminals) rt.emplace_back(id.value + offset);
    return LowerResult{std::move(rs), std::move(rt), {}};
}

template <typename F> LowerResult lower_impl(TaskExpr<F> expr);
template <typename L, typename R> LowerResult lower_impl(SequenceExpr<L, R> expr);
template <typename L, typename R> LowerResult lower_impl(ParallelExpr<L, R> expr);

template <typename T>
PayloadMeta make_payload_meta() {
    using LogicalT = unwrap_outcome_t<T>;
    PayloadMeta pm;
    pm.output_checked = true;
    if constexpr (std::is_trivially_copyable_v<LogicalT> && std::is_standard_layout_v<LogicalT>) {
        if constexpr (meta::Reflectable<LogicalT>) {
            pm.output_transferable = meta::is_binary_stable<LogicalT>();
            pm.output_serializable = meta::is_zero_copy_serializable<LogicalT>();
        } else {
            pm.output_transferable = true;
            pm.output_serializable = true;
        }
    } else {
        pm.output_transferable = false;
        pm.output_serializable = false;
    }
    pm.output_type_name = std::string(meta::type_name<LogicalT>());
    return pm;
}

template <typename F>
LowerResult lower_impl(TaskExpr<F> expr) {
    LowerResult result;
    auto cmd = TaskCommand::make(std::move(expr.callable()), expr.name());
    TaskId id = result.ir.add_node(expr.name(), expr.domain(), std::move(cmd));
    using OutputT = callable_payload_t<F>;
    result.ir.nodes.back().payload_meta = make_payload_meta<OutputT>();
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
    JoinPolicyKind policy = expr.policy;
    auto left_result = lower_impl(std::move(expr.left));
    auto right_result = lower_impl(std::move(expr.right));
    // Capture left terminals before merge (they are the left branch members)
    std::vector<TaskId> left_terminals = left_result.terminals;
    auto right_remap = merge_into(left_result, std::move(right_result));
    for (auto s : right_remap.starts) left_result.starts.push_back(s);
    for (auto t : right_remap.terminals) left_result.terminals.push_back(t);
    // Record join group if CollectAll
    if (policy == JoinPolicyKind::CollectAll) {
        std::vector<TaskId> members;
        for (auto t : left_terminals) members.push_back(t);
        for (auto t : right_remap.terminals) members.push_back(t);
        left_result.ir.add_join_group(std::move(members), policy);
    }
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
    std::vector<std::vector<std::size_t>> successors;
    std::vector<PravahaError> errors;
    std::vector<IrJoinGroup> const* join_groups{nullptr};
    bool canceled{false};

    static RuntimeState build(const TaskIr& ir) {
        RuntimeState rs;
        std::size_t n = ir.nodes.size();
        rs.node_states.resize(n, TaskState::Created);
        rs.remaining_deps.resize(n, 0);
        rs.successors.resize(n);
        rs.join_groups = &ir.join_groups;

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

    [[nodiscard]] bool is_in_collect_all_group(std::size_t idx) const {
        if (!join_groups) return false;
        TaskId tid{idx};
        for (const auto& jg : *join_groups) {
            if (jg.policy != JoinPolicyKind::CollectAll) continue;
            for (const auto& m : jg.members) {
                if (m == tid) return true;
            }
        }
        return false;
    }

    void mark_succeeded(std::size_t idx) {
        node_states[idx] = TaskState::Succeeded;
        decrement_downstream(idx);
    }

    void mark_failed_collect_all(std::size_t idx, PravahaError err) {
        node_states[idx] = TaskState::Failed;
        errors.push_back(std::move(err));
        // Don't skip siblings — decrement downstream dep counters so group can complete
        decrement_downstream(idx);
    }

    void mark_failed(std::size_t idx, PravahaError err) {
        if (is_in_collect_all_group(idx)) {
            mark_failed_collect_all(idx, std::move(err));
        } else {
            node_states[idx] = TaskState::Failed;
            errors.push_back(std::move(err));
            skip_downstream(idx);
        }
    }

    void decrement_downstream(std::size_t idx) {
        for (auto succ : successors[idx]) {
            if (remaining_deps[succ] > 0) {
                remaining_deps[succ]--;
                if (remaining_deps[succ] == 0 && node_states[succ] == TaskState::Created) {
                    // Check if any predecessor of succ has failed — if so, skip
                    if (has_failed_predecessor(succ)) {
                        node_states[succ] = TaskState::Skipped;
                        skip_downstream(succ);
                    } else {
                        node_states[succ] = TaskState::Ready;
                    }
                }
            }
        }
    }

    [[nodiscard]] bool has_failed_predecessor(std::size_t succ_idx) const {
        // Check all nodes that have succ_idx as a successor
        for (std::size_t i = 0; i < successors.size(); ++i) {
            for (auto s : successors[i]) {
                if (s == succ_idx && node_states[i] == TaskState::Failed) {
                    return true;
                }
            }
        }
        return false;
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
//  SECTION 9.5: DOMAIN CONSTRAINT VALIDATION (uses meta.hpp)
// ============================================================================

namespace domain_traits {

template <class T>
consteval bool pravaha_zero_copy_serializable() {
    if constexpr (!std::is_trivially_copyable_v<T> || !std::is_standard_layout_v<T>) return false;
    else if constexpr (meta::Reflectable<T>) return meta::is_zero_copy_serializable<T>();
    else return true;
}

template <class T>
consteval bool pravaha_binary_stable() {
    if constexpr (!std::is_trivially_copyable_v<T> || !std::is_standard_layout_v<T>) return false;
    else if constexpr (meta::Reflectable<T>) return meta::is_binary_stable<T>();
    else return std::is_standard_layout_v<T> && std::is_trivially_copyable_v<T>;
}

template <class T>
consteval bool is_transferable() {
    return pravaha_binary_stable<T>();
}

template <class T>
consteval bool is_serializable_for_external() {
    return pravaha_zero_copy_serializable<T>() || pravaha_binary_stable<T>();
}

} // namespace domain_traits

namespace detail {

template <typename F>
struct infer_output_type {
    using type = callable_payload_t<F>;
};

template <typename F>
using inferred_output_t = typename infer_output_type<F>::type;


} // namespace detail

inline Outcome<Unit> validate_domain_constraints(const TaskIr& ir) {
    for (const auto& node : ir.nodes) {
        if (node.domain == ExecutionDomain::External) {
            if (node.payload_meta.output_checked) {
                if (!node.payload_meta.output_transferable && !node.payload_meta.output_serializable) {
                    return std::unexpected(PravahaError{
                        ErrorKind::DomainConstraintViolation,
                        "External domain requires transferable or serializable output: " + node.payload_meta.output_type_name,
                        node.name
                    });
                }
            }
        }
    }
    return Unit{};
}

// ============================================================================
//  SECTION 10: INLINE BACKEND & RUNNER
// ============================================================================

class InlineBackend {
    bool stop_requested_{false};
public:
    InlineBackend() = default;

    void submit(TaskCommand cmd) noexcept {
        if (!stop_requested_) {
            cmd.run();
        }
    }

    void drain() noexcept {}
    void request_stop() noexcept { stop_requested_ = true; }
    [[nodiscard]] bool stopped() const noexcept { return stop_requested_; }
};

// ============================================================================
//  SECTION 10.5: JTHREAD BACKEND
// ============================================================================

class JThreadBackend {
    mutable std::mutex mutex_;
    std::condition_variable_any cv_work_;
    std::condition_variable_any cv_drain_;
    std::deque<TaskCommand> queue_;
    std::atomic<bool> stop_requested_{false};
    std::atomic<std::size_t> in_flight_{0};
    std::vector<std::jthread> workers_;

    void worker_loop(std::stop_token stoken) {
        while (true) {
            TaskCommand cmd;
            {
                std::unique_lock lock(mutex_);
                cv_work_.wait(lock, stoken, [this]() { return !queue_.empty(); });
                if (stoken.stop_requested() && queue_.empty()) return;
                if (queue_.empty()) return;
                cmd = std::move(queue_.front());
                queue_.pop_front();
            }
            cmd.run();
            in_flight_.fetch_sub(1, std::memory_order_release);
            cv_drain_.notify_all();
        }
    }

public:
    explicit JThreadBackend(std::size_t worker_count = 0) {
        if (worker_count == 0) {
            worker_count = std::thread::hardware_concurrency();
            if (worker_count == 0) worker_count = 1;
        }
        workers_.reserve(worker_count);
        for (std::size_t i = 0; i < worker_count; ++i) {
            workers_.emplace_back([this](std::stop_token st) { worker_loop(st); });
        }
    }

    ~JThreadBackend() {
        stop_requested_.store(true, std::memory_order_release);
        for (auto& w : workers_) w.request_stop();
        cv_work_.notify_all();
        workers_.clear(); // joins all threads before mutex/cv destroyed
    }

    JThreadBackend(const JThreadBackend&) = delete;
    JThreadBackend& operator=(const JThreadBackend&) = delete;
    JThreadBackend(JThreadBackend&&) = delete;
    JThreadBackend& operator=(JThreadBackend&&) = delete;

    bool submit(TaskCommand cmd) {
        {
            std::lock_guard lock(mutex_);
            if (stop_requested_.load(std::memory_order_acquire)) {
                return false;
            }
            in_flight_.fetch_add(1, std::memory_order_release);
            queue_.push_back(std::move(cmd));
        }
        cv_work_.notify_one();
        return true;
    }

    void drain() {
        std::unique_lock lock(mutex_);
        cv_drain_.wait(lock, [this]() {
            return in_flight_.load(std::memory_order_acquire) == 0 && queue_.empty();
        });
    }

    void request_stop() noexcept {
        stop_requested_.store(true, std::memory_order_release);
        for (auto& w : workers_) w.request_stop();
        cv_work_.notify_all();
        cv_drain_.notify_all();
    }

    [[nodiscard]] bool stopped() const noexcept {
        return stop_requested_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::size_t worker_count() const noexcept { return workers_.size(); }
};

namespace detail {

template <class Backend>
[[nodiscard]] bool backend_is_stopped(Backend& backend) {
    if constexpr (requires(Backend& b) {
        { b.stopped() } -> std::convertible_to<bool>;
    }) {
        return static_cast<bool>(backend.stopped());
    } else {
        return false;
    }
}

template <class Backend>
Outcome<Unit> submit_to_backend(Backend& backend, TaskCommand cmd) {
    if (backend_is_stopped(backend)) {
        return std::unexpected(PravahaError{ErrorKind::QueueRejected, "backend rejected task submission: stopped"});
    }

    using submit_result_t = decltype(std::declval<Backend&>().submit(std::declval<TaskCommand>()));

    if constexpr (std::same_as<submit_result_t, Outcome<Unit>>) {
        return backend.submit(std::move(cmd));
    } else if constexpr (std::same_as<submit_result_t, bool>) {
        if (!backend.submit(std::move(cmd))) {
            return std::unexpected(PravahaError{ErrorKind::QueueRejected, "backend rejected task submission"});
        }
        return Outcome<Unit>{Unit{}};
    } else {
        backend.submit(std::move(cmd));
        if (backend_is_stopped(backend)) {
            return std::unexpected(PravahaError{ErrorKind::QueueRejected, "backend rejected task submission: stopped"});
        }
        return Outcome<Unit>{Unit{}};
    }
}

struct SharedSchedulerState {
    RuntimeState rt;
    std::mutex mutex;
    std::condition_variable cv_done;
    std::size_t total_nodes{0};
    std::size_t terminal_count{0};
    std::size_t completed_count{0};
    std::size_t scheduled_last_pass{0};

    [[nodiscard]] bool all_terminal() const { return terminal_count >= total_nodes; }

    static bool is_terminal(TaskState s) {
        return s == TaskState::Succeeded || s == TaskState::Failed ||
               s == TaskState::Skipped || s == TaskState::Canceled;
    }

    void count_terminals() {
        terminal_count = 0;
        for (auto s : rt.node_states)
            if (is_terminal(s)) ++terminal_count;
        completed_count = terminal_count;
    }

    void note_scheduled_in_last_pass(std::size_t count) {
        scheduled_last_pass = count;
    }

    [[nodiscard]] std::size_t active_count() const {
        std::size_t active = 0;
        for (auto s : rt.node_states) {
            if (s == TaskState::Scheduled || s == TaskState::Running) {
                ++active;
            }
        }
        return active;
    }
};

} // namespace detail

// GraphAlgorithmPolicy owns DAG validation and ordering strategy.
// Default implementation delegates to LiteGraph/LiteGraphAlgorithms helpers.
// Alternative policies may add custom cycle checks, incremental validation,
// or critical-path-aware ordering while keeping Runner unchanged.
struct DefaultGraphAlgorithmPolicy {
    static Outcome<Unit> validate(const TaskIr& ir) {
        return validate_ir_with_litegraph(ir);
    }

    static Outcome<std::vector<TaskId>> topological_order(const TaskIr& ir) {
        return pravaha::topological_order(ir);
    }
};

// Backward-compatible alias during transition to algorithm-policy naming.
using DefaultGraphValidationPolicy = DefaultGraphAlgorithmPolicy;

// ReadyPolicy owns schedulability checks for each node.
// Default implementation uses dependency count, cancellation flag, and node
// state; alternative policies may add priority/resource/domain-aware readiness.
struct DefaultReadyPolicy {
    template <class RuntimeStateLike>
    static bool is_ready(const RuntimeStateLike& state, std::size_t index) {
        if (state.canceled) return false;
        if (index >= state.node_states.size() || index >= state.remaining_deps.size()) return false;
        if (state.remaining_deps[index] != 0) return false;

        // Accept either state model: precomputed Ready nodes or Created+deps==0 nodes.
        const auto node_state = state.node_states[index];
        const bool schedulable_state =
            node_state == TaskState::Ready || node_state == TaskState::Created;

        return schedulable_state;
    }
};

// NoProgressPolicy owns deadlock/no-progress handling.
// Default implementation force-fails unresolved non-terminal nodes and records
// InternalError; alternative policies may add diagnostics, wait-for analysis,
// or timeout-based handling.
struct DefaultNoProgressPolicy {
    template <class SharedSchedulerStateLike>
    static bool handle_no_progress(SharedSchedulerStateLike& sstate) {
        if (sstate.completed_count >= sstate.total_nodes) return false;
        if (sstate.active_count() > 0) return false;
        if (sstate.scheduled_last_pass > 0) return false;

        bool changed = false;
        for (auto& s : sstate.rt.node_states) {
            if (!SharedSchedulerStateLike::is_terminal(s)) {
                s = TaskState::Failed;
                changed = true;
            }
        }
        if (!changed) return false;

        sstate.rt.errors.push_back(PravahaError{
            ErrorKind::InternalError,
            "scheduler made no progress; unresolved non-terminal nodes remain"
        });
        sstate.count_terminals();
        return true;
    }
};

template <
    typename Backend = InlineBackend,
    typename GraphAlgorithmPolicy = DefaultGraphAlgorithmPolicy,
    typename ReadyPolicy = DefaultReadyPolicy,
    typename NoProgressPolicy = DefaultNoProgressPolicy>
class Runner {
    Backend* backend_{nullptr};
    Backend owned_backend_;

public:
    Runner() : owned_backend_{}, backend_{&owned_backend_} {}
    explicit Runner(Backend& b) : backend_{&b} {}

    template <IsPravahaExpr Expr>
    Outcome<RunResult> submit(Expr&& expr) {
        auto ir_result = lower_to_ir(std::forward<Expr>(expr));
        if (!ir_result.has_value()) return std::unexpected(ir_result.error());
        auto& ir = ir_result.value();

        auto validation = GraphAlgorithmPolicy::validate(ir);
        if (!validation.has_value()) return std::unexpected(validation.error());

        auto domain_check = validate_domain_constraints(ir);
        if (!domain_check.has_value()) return std::unexpected(domain_check.error());

        return execute(ir);
    }

    void request_stop() noexcept { backend_->request_stop(); }

    // Expose backend reference for parallel_reduce and other algorithms
    Backend& backend_ref() noexcept { return *backend_; }

private:
    Outcome<RunResult> execute(TaskIr& ir) {
        auto sstate = std::make_shared<detail::SharedSchedulerState>();
        sstate->rt = RuntimeState::build(ir);
        sstate->total_nodes = ir.nodes.size();
        sstate->count_terminals();

        // Submit initially ready nodes
        schedule_ready(ir, sstate);

        // Deadlock guard: no terminal completion possible if scheduler has no live work.
        bool no_progress_forced = false;
        {
            std::lock_guard lock(sstate->mutex);
            no_progress_forced = NoProgressPolicy::handle_no_progress(*sstate);
        }
        if (no_progress_forced) {
            sstate->cv_done.notify_all();
        }

        // Wait for all nodes to reach terminal state
        {
            std::unique_lock lock(sstate->mutex);
            sstate->cv_done.wait(lock, [&]() { return sstate->all_terminal(); });
        }

        return sstate->rt.finalize();
    }

    void schedule_ready(TaskIr& ir, std::shared_ptr<detail::SharedSchedulerState>& sstate) {
        // Collect ready indices under lock
        std::vector<std::size_t> ready_indices;
        {
            std::lock_guard lock(sstate->mutex);
            for (std::size_t i = 0; i < sstate->rt.node_states.size(); ++i) {
                if (ReadyPolicy::is_ready(sstate->rt, i)) {
                    sstate->rt.node_states[i] = TaskState::Scheduled;
                    ready_indices.push_back(i);
                }
            }
            sstate->note_scheduled_in_last_pass(ready_indices.size());
            sstate->count_terminals();
        }

        for (auto idx : ready_indices) {
            submit_node(ir, idx, sstate);
        }
    }

    void submit_node(TaskIr& ir, std::size_t idx, std::shared_ptr<detail::SharedSchedulerState> sstate) {
        // Wrap the node execution in a TaskCommand that calls back into scheduler
        auto* node_cmd_ptr = &ir.nodes[idx].command;
        auto* ir_ptr = &ir;
        auto wrapped = TaskCommand::make([this, idx, node_cmd_ptr, ir_ptr, sstate]() mutable {
            // Run the actual node command
            auto result = node_cmd_ptr->run();

            // Update scheduler state
            std::vector<std::size_t> newly_ready;
            bool no_progress_forced = false;
            {
                std::lock_guard lock(sstate->mutex);
                if (result.has_value()) {
                    sstate->rt.mark_succeeded(idx);
                } else {
                    sstate->rt.mark_failed(idx, std::move(result.error()));
                }
                sstate->count_terminals();

                // Collect newly ready nodes
                for (std::size_t i = 0; i < sstate->rt.node_states.size(); ++i) {
                    if (ReadyPolicy::is_ready(sstate->rt, i)) {
                        sstate->rt.node_states[i] = TaskState::Scheduled;
                        newly_ready.push_back(i);
                    }
                }
                sstate->note_scheduled_in_last_pass(newly_ready.size());
                sstate->count_terminals();

                // Deadlock guard after completion scheduling pass.
                no_progress_forced = NoProgressPolicy::handle_no_progress(*sstate);
            }

            // Submit newly ready nodes
            for (auto nidx : newly_ready) {
                submit_node(*ir_ptr, nidx, sstate);
            }

            // Notify if all done
            if (no_progress_forced) {
                sstate->cv_done.notify_all();
            }
            sstate->cv_done.notify_all();
        });

        auto submit_result = detail::submit_to_backend(*backend_, std::move(wrapped));
        if (!submit_result.has_value()) {
            {
                std::lock_guard lock(sstate->mutex);
                sstate->rt.mark_failed(idx, PravahaError{
                    ErrorKind::QueueRejected,
                    submit_result.error().message,
                    ir.nodes[idx].name
                });
                sstate->count_terminals();
                (void)NoProgressPolicy::handle_no_progress(*sstate);
            }
            sstate->cv_done.notify_all();
        }
    }
};

// Specialization behavior: InlineBackend submit returns Outcome, JThreadBackend submit is void.
// We need the Runner to work with both. The wrapped TaskCommand handles everything internally.

// ============================================================================
//  SECTION 11: TEXTUAL PIPELINE PARSING (v0.1 bridge using Lithe)
// ============================================================================
// Lithe (edsl/lithe.hpp) is a compile-time expression template EDSL.
// For v0.1, we use Lithe's SymbolicExpression/expr<> infrastructure for
// keyword/identifier validation, and implement a minimal recursive descent
// parser for the textual pipeline syntax.

namespace symbolic {

namespace lithe_frontend {

struct pipeline_tag {};
struct task_ref_tag {};
struct sequence_tag {};
struct parallel_tag {};
struct collect_all_tag {};
struct keyword_tag {};
struct identifier_tag {};
struct token_tag {};

struct TokenCapture {
    std::string kind;
    std::string text;
    std::size_t begin{};
    std::size_t end{};

    bool operator==(const TokenCapture&) const = default;
};

struct CapturedToken {
    TokenCapture capture;
    std::string lithe_dump;
    std::size_t lithe_hash{};
};

inline auto token_expr(TokenCapture capture) {
    return lithe::make_node<token_tag>(lithe::as_expr(std::move(capture)));
}

inline auto keyword_expr(std::string text, std::size_t begin, std::size_t end) {
    return lithe::make_node<keyword_tag>(
        token_expr(TokenCapture{"keyword", std::move(text), begin, end})
    );
}

inline auto identifier_expr(std::string text, std::size_t begin, std::size_t end) {
    return lithe::make_node<identifier_tag>(
        token_expr(TokenCapture{"identifier", std::move(text), begin, end})
    );
}

inline auto task_ref_expr(std::string name, std::size_t begin, std::size_t end) {
    return lithe::make_node<task_ref_tag>(identifier_expr(std::move(name), begin, end));
}

inline bool keyword_matches(std::string_view token, std::string_view keyword) {
    return !token.empty() && !keyword.empty() && token == keyword;
}

inline bool is_pipeline_keyword(std::string_view token) {
    return keyword_matches(token, "pipeline");
}

inline bool is_then_keyword(std::string_view token) {
    return keyword_matches(token, "then");
}

inline bool is_parallel_keyword(std::string_view token) {
    return keyword_matches(token, "parallel");
}

inline bool is_collect_all_keyword(std::string_view token) {
    return keyword_matches(token, "collect_all");
}

inline bool is_reserved_keyword(std::string_view token) {
    return is_pipeline_keyword(token)
        || is_then_keyword(token)
        || is_parallel_keyword(token)
        || is_collect_all_keyword(token);
}

inline bool identifier_matches(std::string_view token) {
    if (token.empty() || is_reserved_keyword(token)) return false;
    if (!std::isalpha(static_cast<unsigned char>(token[0])) && token[0] != '_') return false;
    for (const auto c : token) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
    }
    return true;
}

inline Outcome<CapturedToken> capture_keyword(
    std::string_view text,
    std::size_t offset,
    std::string_view keyword,
    std::string_view kind
) {
    if (keyword.empty()) {
        return std::unexpected(PravahaError{ErrorKind::ParseError, "expected non-empty keyword"});
    }

    std::size_t pos = offset;
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
    if (pos >= text.size()) {
        return std::unexpected(PravahaError{ErrorKind::ParseError, "expected keyword '" + std::string(keyword) + "'"});
    }

    const std::size_t begin = pos;
    while (pos < text.size() && !std::isspace(static_cast<unsigned char>(text[pos]))
           && text[pos] != '{' && text[pos] != '}' && text[pos] != ',') {
        ++pos;
    }
    const std::size_t end = pos;
    const auto token = text.substr(begin, end - begin);

    if (!keyword_matches(token, keyword)) {
        return std::unexpected(PravahaError{ErrorKind::ParseError, "expected keyword '" + std::string(keyword) + "'"});
    }

    CapturedToken captured;
    captured.capture = TokenCapture{std::string(kind), std::string(token), begin, end};
    const auto expr = lithe::make_node<keyword_tag>(lithe::as_expr(std::string(token)));
    captured.lithe_dump = lithe::emit::dump(expr);
    captured.lithe_hash = lithe::emit::structural_hash(expr);
    return captured;
}

inline Outcome<CapturedToken> capture_identifier(
    std::string_view text,
    std::size_t offset,
    std::string_view kind
) {
    std::size_t pos = offset;
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
    if (pos >= text.size()) {
        return std::unexpected(PravahaError{ErrorKind::ParseError, "expected identifier"});
    }

    const std::size_t begin = pos;
    while (pos < text.size() && !std::isspace(static_cast<unsigned char>(text[pos]))
           && text[pos] != '{' && text[pos] != '}' && text[pos] != ',') {
        ++pos;
    }
    const std::size_t end = pos;
    const auto token = text.substr(begin, end - begin);

    if (!identifier_matches(token)) {
        return std::unexpected(PravahaError{ErrorKind::ParseError, "expected identifier"});
    }

    CapturedToken captured;
    captured.capture = TokenCapture{std::string(kind), std::string(token), begin, end};
    const auto expr = lithe::make_node<identifier_tag>(lithe::as_expr(std::string(token)));
    captured.lithe_dump = lithe::emit::dump(expr);
    captured.lithe_hash = lithe::emit::structural_hash(expr);
    return captured;
}

} // namespace lithe_frontend

namespace lithe_bridge {

// v0.1: Lithe owns keyword/token exact matching; identifier character
// classes remain local fallback until a dedicated character-class grammar is added.
inline bool keyword_matches(std::string_view token, std::string_view keyword) {
    auto pm = lithe::patterns::match(keyword, token);
    return pm.apply([](auto&& p, auto&& v) {
        return std::string_view{p} == std::string_view{v};
    });
}

inline bool is_pipeline_keyword(std::string_view token) {
    return keyword_matches(token, "pipeline");
}

inline bool is_then_keyword(std::string_view token) {
    return keyword_matches(token, "then");
}

inline bool is_parallel_keyword(std::string_view token) {
    return keyword_matches(token, "parallel");
}

inline bool is_reserved_keyword(std::string_view token) {
    return is_pipeline_keyword(token)
        || is_then_keyword(token)
        || is_parallel_keyword(token)
        || keyword_matches(token, "collect_all");
}

inline bool identifier_matches(std::string_view token) {
    if (token.empty() || is_reserved_keyword(token)) return false;
    if (!std::isalpha(static_cast<unsigned char>(token[0])) && token[0] != '_') return false;
    for (auto c : token) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
    }
    return true;
}

struct PipelineHeaderParse {
    std::string pipeline_name;
    std::size_t body_start_offset{0};
    std::size_t open_brace_offset{0};
};

inline Outcome<PipelineHeaderParse> parse_pipeline_header(std::string_view text) {
    std::size_t pos = 0;
    auto skip_ws = [&]() {
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
            ++pos;
        }
    };

    auto read_token = [&]() -> std::string_view {
        skip_ws();
        if (pos >= text.size()) return {};
        std::size_t start = pos;
        while (pos < text.size() && !std::isspace(static_cast<unsigned char>(text[pos]))
               && text[pos] != '{' && text[pos] != '}' && text[pos] != ',') {
            ++pos;
        }
        return text.substr(start, pos - start);
    };

    auto kw = read_token();
    if (!is_pipeline_keyword(kw)) {
        return std::unexpected(PravahaError{ErrorKind::ParseError, "expected keyword 'pipeline'"});
    }

    auto name_tok = read_token();
    if (name_tok.empty()) {
        return std::unexpected(PravahaError{ErrorKind::ParseError, "expected identifier after pipeline"});
    }
    if (is_reserved_keyword(name_tok)) {
        return std::unexpected(PravahaError{
            ErrorKind::ParseError,
            "reserved keyword cannot be used as task identifier: " + std::string(name_tok)
        });
    }
    if (!identifier_matches(name_tok)) {
        return std::unexpected(PravahaError{ErrorKind::ParseError, "expected identifier after pipeline"});
    }

    skip_ws();
    if (pos >= text.size() || text[pos] != '{') {
        return std::unexpected(PravahaError{ErrorKind::ParseError, "expected '{' after pipeline name"});
    }

    const std::size_t brace = pos;
    ++pos;
    return PipelineHeaderParse{std::string(name_tok), pos, brace};
}

inline Outcome<std::size_t> parse_parallel_intro(std::string_view text, std::size_t offset) {
    std::size_t pos = offset;
    auto skip_ws = [&]() {
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
            ++pos;
        }
    };

    auto read_token = [&]() -> std::string_view {
        skip_ws();
        if (pos >= text.size()) return {};
        std::size_t start = pos;
        while (pos < text.size() && !std::isspace(static_cast<unsigned char>(text[pos]))
               && text[pos] != '{' && text[pos] != '}' && text[pos] != ',') {
            ++pos;
        }
        return text.substr(start, pos - start);
    };

    auto kw = read_token();
    if (!is_parallel_keyword(kw)) {
        return std::unexpected(PravahaError{ErrorKind::ParseError, "expected keyword 'parallel'"});
    }

    skip_ws();
    if (pos >= text.size() || text[pos] != '{') {
        return std::unexpected(PravahaError{ErrorKind::ParseError, "expected '{' after parallel"});
    }

    ++pos;
    return pos;
}

} // namespace lithe_bridge

struct SymbolicTaskExpr { std::string name; };
struct SymbolicSequenceExpr;
struct SymbolicParallelExpr;

using SymbolicExpr = std::variant<
    SymbolicTaskExpr,
    std::unique_ptr<SymbolicSequenceExpr>,
    std::unique_ptr<SymbolicParallelExpr>
>;

struct SymbolicSequenceExpr { SymbolicExpr left; SymbolicExpr right; };
struct SymbolicParallelExpr { std::vector<SymbolicExpr> branches; };

struct SymbolicPipeline {
    std::string name;
    SymbolicExpr root;
};

// Lithe-validated keyword set
inline bool is_keyword(std::string_view token) {
    return lithe_bridge::is_reserved_keyword(token);
}

inline bool is_valid_identifier(std::string_view token) {
    return lithe_bridge::identifier_matches(token);
}

namespace detail {

struct Parser {
    std::string_view src;
    std::size_t pos{0};

    void skip_ws() { while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos]))) ++pos; }

    std::string_view peek_token() {
        skip_ws();
        if (pos >= src.size()) return {};
        if (src[pos] == '{' || src[pos] == '}' || src[pos] == ',') return src.substr(pos, 1);
        std::size_t start = pos;
        while (pos < src.size() && !std::isspace(static_cast<unsigned char>(src[pos]))
               && src[pos] != '{' && src[pos] != '}' && src[pos] != ',') ++pos;
        auto tok = src.substr(start, pos - start);
        pos = start; // don't consume
        return tok;
    }

    std::string_view consume_token() {
        skip_ws();
        if (pos >= src.size()) return {};
        if (src[pos] == '{' || src[pos] == '}' || src[pos] == ',') return src.substr(pos++, 1);
        std::size_t start = pos;
        while (pos < src.size() && !std::isspace(static_cast<unsigned char>(src[pos]))
               && src[pos] != '{' && src[pos] != '}' && src[pos] != ',') ++pos;
        return src.substr(start, pos - start);
    }

    bool expect(std::string_view expected) {
        skip_ws();
        auto tok = consume_token();
        return tok == expected;
    }

    Outcome<SymbolicExpr> parse_parallel() {
        std::vector<SymbolicExpr> branches;
        while (true) {
            skip_ws();
            auto tok = peek_token();
            if (tok == "}") { consume_token(); break; }
            if (tok.empty()) return std::unexpected(PravahaError{ErrorKind::ParseError, "Unexpected end in parallel block"});
            if (!branches.empty()) {
                if (tok == ",") consume_token();
                else return std::unexpected(PravahaError{ErrorKind::ParseError, "Expected ',' between parallel branches"});
                tok = peek_token();
            }
            if (!is_valid_identifier(tok)) return std::unexpected(PravahaError{ErrorKind::ParseError, "Invalid identifier in parallel: " + std::string(tok)});
            consume_token();
            branches.push_back(SymbolicTaskExpr{std::string(tok)});
        }
        if (branches.empty()) return std::unexpected(PravahaError{ErrorKind::ParseError, "Empty parallel block"});
        auto par = std::make_unique<SymbolicParallelExpr>();
        par->branches = std::move(branches);
        return SymbolicExpr{std::move(par)};
    }

    Outcome<SymbolicExpr> parse_step() {
        skip_ws();
        auto tok = peek_token();
        if (lithe_bridge::is_parallel_keyword(tok)) {
            auto intro = lithe_bridge::parse_parallel_intro(src, pos);
            if (!intro.has_value()) return std::unexpected(intro.error());
            pos = intro.value();
            return parse_parallel();
        }
        if (!is_valid_identifier(tok)) return std::unexpected(PravahaError{ErrorKind::ParseError, "Expected identifier, got: " + std::string(tok)});
        consume_token();
        return SymbolicExpr{SymbolicTaskExpr{std::string(tok)}};
    }

    Outcome<SymbolicExpr> parse_sequence() {
        auto first = parse_step();
        if (!first.has_value()) return first;
        auto current = std::move(first.value());
        while (true) {
            skip_ws();
            auto tok = peek_token();
            if (!lithe_bridge::is_then_keyword(tok)) break;
            consume_token();
            auto next = parse_step();
            if (!next.has_value()) return next;
            auto seq = std::make_unique<SymbolicSequenceExpr>();
            seq->left = std::move(current);
            seq->right = std::move(next.value());
            current = SymbolicExpr{std::move(seq)};
        }
        return current;
    }

    Outcome<SymbolicPipeline> parse_pipeline() {
        auto header = lithe_bridge::parse_pipeline_header(src);
        if (!header.has_value()) return std::unexpected(header.error());
        pos = header->body_start_offset;

        auto body = parse_sequence();
        if (!body.has_value()) return std::unexpected(body.error());
        if (!expect("}")) return std::unexpected(PravahaError{ErrorKind::ParseError, "Expected '}'"});
        return SymbolicPipeline{std::move(header->pipeline_name), std::move(body.value())};
    }
};

} // namespace detail
} // namespace symbolic

inline Outcome<symbolic::SymbolicPipeline> parse_pipeline(std::string_view text) {
    symbolic::detail::Parser parser{text};
    return parser.parse_pipeline();
}

// ============================================================================
//  SECTION 11.5: SYMBOL REGISTRY & SYMBOLIC LOWERING
// ============================================================================

class SymbolRegistry {
    struct Entry { std::string name; TaskCommand cmd; };
    std::vector<Entry> entries_;
public:
    SymbolRegistry() = default;

    template <typename F> requires std::move_constructible<std::decay_t<F>> && std::invocable<std::decay_t<F>>
    void register_task(std::string name, F&& f) {
        std::string debug_name{name};
        auto cmd = TaskCommand::make(std::forward<F>(f), debug_name);
        entries_.push_back(Entry{std::move(name), std::move(cmd)});
    }

    void register_command(std::string name, TaskCommand cmd) {
        entries_.push_back(Entry{std::move(name), std::move(cmd)});
    }

    TaskCommand* find(const std::string& name) {
        for (auto& e : entries_) if (e.name == name) return &e.cmd;
        return nullptr;
    }
};

namespace detail {

inline Outcome<Unit> lower_symbolic_expr(const symbolic::SymbolicExpr& expr, SymbolRegistry& reg,
    TaskIr& ir, std::vector<TaskId>& starts, std::vector<TaskId>& terminals) {

    if (auto* task = std::get_if<symbolic::SymbolicTaskExpr>(&expr)) {
        auto* cmd_ptr = reg.find(task->name);
        if (!cmd_ptr) return std::unexpected(PravahaError{ErrorKind::SymbolNotFound, "Symbol not found: " + task->name, task->name});
        // Create a placeholder command that delegates to the registered one
        auto* raw = cmd_ptr;
        auto wrapper = TaskCommand::make([raw]() -> Outcome<Unit> { return raw->run(); });
        TaskId id = ir.add_node(task->name, ExecutionDomain::CPU, std::move(wrapper));
        starts.push_back(id);
        terminals.push_back(id);
        return Unit{};
    }

    if (auto* seq_ptr = std::get_if<std::unique_ptr<symbolic::SymbolicSequenceExpr>>(&expr)) {
        auto& seq = **seq_ptr;
        std::vector<TaskId> left_starts, left_terminals, right_starts, right_terminals;
        auto lr = lower_symbolic_expr(seq.left, reg, ir, left_starts, left_terminals);
        if (!lr.has_value()) return lr;
        auto rr = lower_symbolic_expr(seq.right, reg, ir, right_starts, right_terminals);
        if (!rr.has_value()) return rr;
        for (auto t : left_terminals) for (auto s : right_starts) ir.add_edge(t, s, EdgeKind::Sequence);
        starts = std::move(left_starts);
        terminals = std::move(right_terminals);
        return Unit{};
    }

    if (auto* par_ptr = std::get_if<std::unique_ptr<symbolic::SymbolicParallelExpr>>(&expr)) {
        auto& par = **par_ptr;
        for (auto& branch : par.branches) {
            std::vector<TaskId> bs, bt;
            auto br = lower_symbolic_expr(branch, reg, ir, bs, bt);
            if (!br.has_value()) return br;
            for (auto s : bs) starts.push_back(s);
            for (auto t : bt) terminals.push_back(t);
        }
        return Unit{};
    }

    return std::unexpected(PravahaError{ErrorKind::ParseError, "Unknown symbolic expression type"});
}

} // namespace detail

inline Outcome<TaskIr> lower_symbolic_pipeline(const symbolic::SymbolicPipeline& pipeline, SymbolRegistry& reg) {
    TaskIr ir;
    std::vector<TaskId> starts, terminals;
    auto result = detail::lower_symbolic_expr(pipeline.root, reg, ir, starts, terminals);
    if (!result.has_value()) return std::unexpected(result.error());
    return ir;
}

// ============================================================================
//  SECTION 12: PARALLEL_FOR (NAryTree hierarchy)
// ============================================================================

struct AlgorithmTreeNode {
    std::string name;
    std::size_t begin{0};
    std::size_t end{0};
    bool operator==(const AlgorithmTreeNode&) const = default;
};

using AlgorithmTree = NAryTree<AlgorithmTreeNode>;

template <typename F>
struct ParallelForResult {
    TaskIr ir;
    AlgorithmTree hierarchy;
    std::size_t chunk_count{0};
};

template <typename Range, typename F>
    requires std::invocable<F, std::size_t, std::size_t>
auto parallel_for(std::string name, Range& range, std::size_t chunk_size, F&& body) {
    std::size_t total = range.size();
    if (chunk_size == 0) chunk_size = 1;
    std::size_t num_chunks = (total + chunk_size - 1) / chunk_size;

    // Build NAryTree hierarchy
    AlgorithmTree tree(AlgorithmTreeNode{name, 0, total});
    auto* root_node = tree.get_root();

    // Build TaskIr with parallel chunk tasks
    TaskIr ir;
    std::vector<TaskId> chunk_ids;
    chunk_ids.reserve(num_chunks);

    for (std::size_t i = 0; i < num_chunks; ++i) {
        std::size_t b = i * chunk_size;
        std::size_t e = std::min(b + chunk_size, total);

        // Add to NAryTree hierarchy
        tree.insert(root_node, AlgorithmTreeNode{name + "_chunk_" + std::to_string(i), b, e});

        // Create chunk task command
        auto cmd = TaskCommand::make([&body, b, e]() { body(b, e); });
        TaskId id = ir.add_node(name + "_chunk_" + std::to_string(i), ExecutionDomain::CPU, std::move(cmd));
        chunk_ids.push_back(id);
    }

    // No edges between chunks — they are all parallel (AllOrNothing)
    return ParallelForResult<F>{std::move(ir), std::move(tree), num_chunks};
}

template <typename F>
Outcome<TaskIr> lower_parallel_for(ParallelForResult<F>& pf) {
    return std::move(pf.ir);
}

// ============================================================================
//  SECTION 13: PARALLEL_REDUCE (NAryTree hierarchy)
// ============================================================================

// Result type for parallel_reduce capturing the NAryTree hierarchy
template <typename T>
struct ParallelReduceResult {
    T value;
    AlgorithmTree hierarchy;
    std::size_t chunk_count{0};
    bool has_error{false};
    PravahaError error{ErrorKind::InternalError, ""};
};

// parallel_reduce: splits range into chunks, reduces each chunk in parallel,
// then combines partial results. Uses NAryTree to represent the fork-join
// reduction hierarchy.
//
// API:
//   auto result = pravaha::parallel_reduce(runner, range, init, reduce_fn, combine_fn, chunk_size);
//
// - runner:     Runner<Backend>& — executes chunk tasks
// - range:      random-access container with .size() and operator[]
// - init:       initial value of type T
// - reduce_fn:  (T accum, std::size_t begin, std::size_t end) -> T
//               reduces a sub-range [begin, end) into a partial result
// - combine_fn: (T left, T right) -> T
//               combines two partial results
// - chunk_size: number of elements per chunk
//
// Returns: Outcome<ParallelReduceResult<T>>

template <typename Backend, typename GraphAlgorithmPolicy, typename ReadyPolicy, typename NoProgressPolicy,
          typename Range, typename T, typename ReduceFn, typename CombineFn>
    requires std::invocable<ReduceFn, T, std::size_t, std::size_t>
          && std::invocable<CombineFn, T, T>
          && std::copy_constructible<T>
auto parallel_reduce(
    Runner<Backend, GraphAlgorithmPolicy, ReadyPolicy, NoProgressPolicy>& runner,
    Range& range,
    T init,
    ReduceFn&& reduce_fn,
    CombineFn&& combine_fn,
    std::size_t chunk_size)
-> Outcome<ParallelReduceResult<T>>
{
    std::size_t total = range.size();

    // Empty range returns init immediately
    if (total == 0) {
        AlgorithmTree tree(AlgorithmTreeNode{"reduce_root", 0, 0});
        return ParallelReduceResult<T>{init, std::move(tree), 0, false, PravahaError{ErrorKind::InternalError, ""}};
    }

    if (chunk_size == 0) chunk_size = 1;
    std::size_t num_chunks = (total + chunk_size - 1) / chunk_size;

    // Build NAryTree hierarchy: root "reduce" node with chunk children
    AlgorithmTree tree(AlgorithmTreeNode{"reduce_root", 0, total});
    auto* root_node = tree.get_root();

    // Add combine node as child of root (represents the combine step)
    auto* combine_node = tree.insert(root_node, AlgorithmTreeNode{"combine", 0, total});

    // Storage for partial results (one per chunk)
    auto partials = std::make_shared<std::vector<T>>(num_chunks, init);
    auto error_flag = std::make_shared<std::atomic<bool>>(false);
    auto first_error = std::make_shared<PravahaError>(ErrorKind::InternalError, "");
    auto error_mutex = std::make_shared<std::mutex>();

    // Build chunk tasks as a parallel expression
    // We build a TaskIr manually with all chunks as independent tasks
    TaskIr ir;
    for (std::size_t i = 0; i < num_chunks; ++i) {
        std::size_t b = i * chunk_size;
        std::size_t e = std::min(b + chunk_size, total);

        // Add chunk node to tree hierarchy under combine node
        tree.insert(combine_node, AlgorithmTreeNode{"chunk_" + std::to_string(i), b, e});

        // Capture what we need for the chunk task
        auto chunk_cmd = TaskCommand::make(
            [i, b, e, init_val = init, &reduce_fn, partials, error_flag, first_error, error_mutex]() mutable {
                try {
                    T partial = reduce_fn(init_val, b, e);
                    (*partials)[i] = std::move(partial);
                } catch (const std::exception& ex) {
                    error_flag->store(true, std::memory_order_release);
                    std::lock_guard<std::mutex> lk(*error_mutex);
                    *first_error = PravahaError{ErrorKind::TaskFailed,
                        std::string{"parallel_reduce chunk failed: "} + ex.what(),
                        "chunk_" + std::to_string(i)};
                    throw; // re-throw so TaskCommand records it as failure
                }
            });
        ir.add_node("chunk_" + std::to_string(i), ExecutionDomain::CPU, std::move(chunk_cmd));
    }

    // All chunk nodes are independent (no edges) — they run in parallel
    // No validation needed: independent chunks with no edges cannot have cycles.

    // Execute using runner's backend via shared scheduler state
    auto sstate = std::make_shared<detail::SharedSchedulerState>();
    sstate->rt = RuntimeState::build(ir);
    sstate->total_nodes = ir.nodes.size();
    sstate->count_terminals();

    // Schedule all ready nodes (all chunks are ready since no deps)
    {
        std::vector<std::size_t> ready_indices;
        {
            std::lock_guard<std::mutex> lock(sstate->mutex);
            for (std::size_t i = 0; i < sstate->rt.node_states.size(); ++i) {
                if (sstate->rt.node_states[i] == TaskState::Ready) {
                    sstate->rt.node_states[i] = TaskState::Scheduled;
                    ready_indices.push_back(i);
                }
            }
            sstate->count_terminals();
        }

        for (auto idx : ready_indices) {
            auto* node_cmd_ptr = &ir.nodes[idx].command;
            auto wrapped = TaskCommand::make(
                [idx, node_cmd_ptr, sstate]() mutable {
                    auto result = node_cmd_ptr->run();
                    {
                        std::lock_guard<std::mutex> lock(sstate->mutex);
                        if (result.has_value()) {
                            sstate->rt.mark_succeeded(idx);
                        } else {
                            sstate->rt.mark_failed(idx, std::move(result.error()));
                        }
                        sstate->count_terminals();
                    }
                    sstate->cv_done.notify_all();
                });
            runner.backend_ref().submit(std::move(wrapped));
        }
    }

    // Wait for all chunk tasks to complete
    {
        std::unique_lock<std::mutex> lock(sstate->mutex);
        sstate->cv_done.wait(lock, [&]() { return sstate->all_terminal(); });
    }

    // Check for errors (AllOrNothing semantics)
    auto run_result = sstate->rt.finalize();
    if (run_result.final_state == TaskState::Failed) {
        std::lock_guard<std::mutex> lk(*error_mutex);
        return std::unexpected(*first_error);
    }

    // Combine phase: sequentially combine all partial results
    T combined = (*partials)[0];
    for (std::size_t i = 1; i < num_chunks; ++i) {
        combined = combine_fn(std::move(combined), std::move((*partials)[i]));
    }

    return ParallelReduceResult<T>{
        std::move(combined),
        std::move(tree),
        num_chunks,
        false,
        PravahaError{ErrorKind::InternalError, ""}
    };
}

} // namespace pravaha

namespace std {

template<>
struct hash<pravaha::symbolic::lithe_frontend::TokenCapture> {
    std::size_t operator()(const pravaha::symbolic::lithe_frontend::TokenCapture& tok) const noexcept {
        std::size_t h = std::hash<std::string>{}(tok.kind);
        h ^= (std::hash<std::string>{}(tok.text) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
        h ^= (std::hash<std::size_t>{}(tok.begin) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
        h ^= (std::hash<std::size_t>{}(tok.end) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
        return h;
    }
};

} // namespace std

namespace lithe::emit {

template<>
struct tag_name<pravaha::symbolic::lithe_frontend::pipeline_tag> {
    static constexpr const char* value = "pravaha.pipeline";
};

template<>
struct tag_name<pravaha::symbolic::lithe_frontend::task_ref_tag> {
    static constexpr const char* value = "pravaha.task_ref";
};

template<>
struct tag_name<pravaha::symbolic::lithe_frontend::sequence_tag> {
    static constexpr const char* value = "pravaha.sequence";
};

template<>
struct tag_name<pravaha::symbolic::lithe_frontend::parallel_tag> {
    static constexpr const char* value = "pravaha.parallel";
};

template<>
struct tag_name<pravaha::symbolic::lithe_frontend::collect_all_tag> {
    static constexpr const char* value = "pravaha.collect_all";
};

template<>
struct tag_name<pravaha::symbolic::lithe_frontend::keyword_tag> {
    static constexpr const char* value = "pravaha.keyword";
};

template<>
struct tag_name<pravaha::symbolic::lithe_frontend::identifier_tag> {
    static constexpr const char* value = "pravaha.identifier";
};

template<>
struct tag_name<pravaha::symbolic::lithe_frontend::token_tag> {
    static constexpr const char* value = "pravaha.token";
};

} // namespace lithe::emit
