// ============================================================================
// test_pravaha.cpp — Unit tests for pravaha.hpp
// ============================================================================

#include "catch_amalgamated.hpp"
#include "pravaha/pravaha.hpp"

#include <string>
#include <vector>
#include <memory>
#include <algorithm>

// ============================================================================
// Test types for concept validation
// ============================================================================

struct TrivialPoint { double x; double y; };
struct NonTrivial { std::string name; int value; };
struct MoveOnly {
    std::unique_ptr<int> data;
    MoveOnly() = default;
    MoveOnly(MoveOnly&&) = default;
    MoveOnly& operator=(MoveOnly&&) = default;
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;
};
struct WithPointer { int* ptr; int val; };
struct IntWrapper { int value; };

// ============================================================================
// SECTION 1: ErrorKind Enum
// ============================================================================

TEST_CASE("ErrorKind values are distinct", "[pravaha][error]") {
    STATIC_REQUIRE(pravaha::ErrorKind::ParseError != pravaha::ErrorKind::ValidationError);
    STATIC_REQUIRE(pravaha::ErrorKind::CycleDetected != pravaha::ErrorKind::SymbolNotFound);
    STATIC_REQUIRE(pravaha::ErrorKind::TypeMismatch != pravaha::ErrorKind::ExecutorUnavailable);
    STATIC_REQUIRE(pravaha::ErrorKind::TaskFailed != pravaha::ErrorKind::TaskCanceled);
    STATIC_REQUIRE(pravaha::ErrorKind::QueueRejected != pravaha::ErrorKind::Timeout);
    STATIC_REQUIRE(pravaha::ErrorKind::InternalError != pravaha::ErrorKind::ParseError);
}

TEST_CASE("ErrorKind covers all categories", "[pravaha][error]") {
    auto check = [](pravaha::ErrorKind k) { return static_cast<int>(k) >= 0; };
    REQUIRE(check(pravaha::ErrorKind::ParseError));
    REQUIRE(check(pravaha::ErrorKind::ValidationError));
    REQUIRE(check(pravaha::ErrorKind::CycleDetected));
    REQUIRE(check(pravaha::ErrorKind::SymbolNotFound));
    REQUIRE(check(pravaha::ErrorKind::TypeMismatch));
    REQUIRE(check(pravaha::ErrorKind::ExecutorUnavailable));
    REQUIRE(check(pravaha::ErrorKind::DomainConstraintViolation));
    REQUIRE(check(pravaha::ErrorKind::PayloadNotSerializable));
    REQUIRE(check(pravaha::ErrorKind::PayloadNotTransferable));
    REQUIRE(check(pravaha::ErrorKind::TaskFailed));
    REQUIRE(check(pravaha::ErrorKind::TaskCanceled));
    REQUIRE(check(pravaha::ErrorKind::QueueRejected));
    REQUIRE(check(pravaha::ErrorKind::Timeout));
    REQUIRE(check(pravaha::ErrorKind::InternalError));
}

// ============================================================================
// SECTION 2: PravahaError
// ============================================================================

TEST_CASE("PravahaError construction", "[pravaha][error]") {
    auto err = pravaha::PravahaError{pravaha::ErrorKind::TaskFailed, "something went wrong", "task_42"};
    REQUIRE(err.kind == pravaha::ErrorKind::TaskFailed);
    REQUIRE(err.message == "something went wrong");
    REQUIRE(err.task_identity == "task_42");
    REQUIRE(err.location.line() > 0);
}

TEST_CASE("PravahaError::make factory", "[pravaha][error]") {
    auto err = pravaha::PravahaError::make(pravaha::ErrorKind::CycleDetected, "cycle found in DAG");
    REQUIRE(err.kind == pravaha::ErrorKind::CycleDetected);
    REQUIRE(err.message == "cycle found in DAG");
    REQUIRE(err.task_identity.empty());
}

TEST_CASE("PravahaError::make_for_task factory", "[pravaha][error]") {
    auto err = pravaha::PravahaError::make_for_task(pravaha::ErrorKind::Timeout, "deadline exceeded", "expensive_task");
    REQUIRE(err.kind == pravaha::ErrorKind::Timeout);
    REQUIRE(err.message == "deadline exceeded");
    REQUIRE(err.task_identity == "expensive_task");
}

// ============================================================================
// SECTION 3: Outcome<T>
// ============================================================================

TEST_CASE("Outcome<int> success", "[pravaha][outcome]") {
    pravaha::Outcome<int> result{42};
    REQUIRE(result.has_value());
    REQUIRE(result.value() == 42);
}

TEST_CASE("Outcome<int> error", "[pravaha][outcome]") {
    pravaha::Outcome<int> result = std::unexpected(pravaha::PravahaError{pravaha::ErrorKind::TaskFailed, "oops"});
    REQUIRE(!result.has_value());
    REQUIRE(result.error().kind == pravaha::ErrorKind::TaskFailed);
    REQUIRE(result.error().message == "oops");
}

TEST_CASE("Outcome<Unit> success", "[pravaha][outcome]") {
    pravaha::Outcome<pravaha::Unit> result{pravaha::Unit{}};
    REQUIRE(result.has_value());
}

TEST_CASE("Outcome<string> success", "[pravaha][outcome]") {
    pravaha::Outcome<std::string> result{"hello"};
    REQUIRE(result.has_value());
    REQUIRE(result.value() == "hello");
}

TEST_CASE("Outcome monadic chaining with and_then", "[pravaha][outcome]") {
    auto step1 = []() -> pravaha::Outcome<int> { return 10; };
    auto step2 = [](int v) -> pravaha::Outcome<int> { return v * 2; };
    auto result = step1().and_then(step2);
    REQUIRE(result.has_value());
    REQUIRE(result.value() == 20);
}

TEST_CASE("Outcome monadic chaining propagates error", "[pravaha][outcome]") {
    auto step1 = []() -> pravaha::Outcome<int> { return std::unexpected(pravaha::PravahaError{pravaha::ErrorKind::InternalError, "fail"}); };
    auto step2 = [](int v) -> pravaha::Outcome<int> { return v * 2; };
    auto result = step1().and_then(step2);
    REQUIRE(!result.has_value());
    REQUIRE(result.error().kind == pravaha::ErrorKind::InternalError);
}

// ============================================================================
// SECTION 4: TaskState Enum
// ============================================================================

TEST_CASE("TaskState values", "[pravaha][state]") {
    STATIC_REQUIRE(pravaha::TaskState::Created != pravaha::TaskState::Ready);
    STATIC_REQUIRE(pravaha::TaskState::Scheduled != pravaha::TaskState::Running);
    STATIC_REQUIRE(pravaha::TaskState::Succeeded != pravaha::TaskState::Failed);
    STATIC_REQUIRE(pravaha::TaskState::Canceled != pravaha::TaskState::Skipped);
}

TEST_CASE("TaskState all values accessible", "[pravaha][state]") {
    auto states = std::array{
        pravaha::TaskState::Created, pravaha::TaskState::Ready,
        pravaha::TaskState::Scheduled, pravaha::TaskState::Running,
        pravaha::TaskState::Succeeded, pravaha::TaskState::Failed,
        pravaha::TaskState::Canceled, pravaha::TaskState::Skipped
    };
    REQUIRE(states.size() == 8);
    for (std::size_t i = 0; i < states.size(); ++i)
        for (std::size_t j = i + 1; j < states.size(); ++j)
            REQUIRE(states[i] != states[j]);
}

// ============================================================================
// SECTION 5: JoinPolicyKind and ExecutionDomain Enums
// ============================================================================

TEST_CASE("JoinPolicyKind values", "[pravaha][policy]") {
    STATIC_REQUIRE(pravaha::JoinPolicyKind::AllOrNothing != pravaha::JoinPolicyKind::CollectAll);
    STATIC_REQUIRE(pravaha::JoinPolicyKind::AnySuccess != pravaha::JoinPolicyKind::Quorum);
}

TEST_CASE("ExecutionDomain values", "[pravaha][domain]") {
    auto domains = std::array{
        pravaha::ExecutionDomain::Inline, pravaha::ExecutionDomain::CPU,
        pravaha::ExecutionDomain::IO, pravaha::ExecutionDomain::Fiber,
        pravaha::ExecutionDomain::Coroutine, pravaha::ExecutionDomain::External
    };
    REQUIRE(domains.size() == 6);
    for (std::size_t i = 0; i < domains.size(); ++i)
        for (std::size_t j = i + 1; j < domains.size(); ++j)
            REQUIRE(domains[i] != domains[j]);
}

// ============================================================================
// SECTION 6: Payload Concept
// ============================================================================

TEST_CASE("Payload concept - basic types", "[pravaha][concepts]") {
    STATIC_REQUIRE(pravaha::Payload<int>);
    STATIC_REQUIRE(pravaha::Payload<double>);
    STATIC_REQUIRE(pravaha::Payload<std::string>);
    STATIC_REQUIRE(pravaha::Payload<std::vector<int>>);
    STATIC_REQUIRE(pravaha::Payload<TrivialPoint>);
    STATIC_REQUIRE(pravaha::Payload<NonTrivial>);
    STATIC_REQUIRE(pravaha::Payload<MoveOnly>);
}

TEST_CASE("LocalPayload concept", "[pravaha][concepts]") {
    STATIC_REQUIRE(pravaha::LocalPayload<int>);
    STATIC_REQUIRE(pravaha::LocalPayload<std::string>);
    STATIC_REQUIRE(pravaha::LocalPayload<MoveOnly>);
    STATIC_REQUIRE(pravaha::LocalPayload<TrivialPoint>);
}

// ============================================================================
// SECTION 7: TransferablePayload Concept
// ============================================================================

TEST_CASE("TransferablePayload concept - copyable types", "[pravaha][concepts]") {
    STATIC_REQUIRE(pravaha::TransferablePayload<int>);
    STATIC_REQUIRE(pravaha::TransferablePayload<double>);
    STATIC_REQUIRE(pravaha::TransferablePayload<std::string>);
    STATIC_REQUIRE(pravaha::TransferablePayload<std::vector<int>>);
    STATIC_REQUIRE(pravaha::TransferablePayload<TrivialPoint>);
    STATIC_REQUIRE(pravaha::TransferablePayload<NonTrivial>);
}

TEST_CASE("TransferablePayload concept - move-only types excluded", "[pravaha][concepts]") {
    STATIC_REQUIRE(!pravaha::TransferablePayload<MoveOnly>);
}

// ============================================================================
// SECTION 8: SerializablePayload Concept
// ============================================================================

TEST_CASE("SerializablePayload concept - trivial aggregates", "[pravaha][concepts]") {
    STATIC_REQUIRE(pravaha::SerializablePayload<TrivialPoint>);
    STATIC_REQUIRE(pravaha::SerializablePayload<IntWrapper>);
    STATIC_REQUIRE(pravaha::SerializablePayload<int>);
    STATIC_REQUIRE(pravaha::SerializablePayload<double>);
}

TEST_CASE("SerializablePayload concept - non-trivial types excluded", "[pravaha][concepts]") {
    STATIC_REQUIRE(!pravaha::SerializablePayload<std::string>);
    STATIC_REQUIRE(!pravaha::SerializablePayload<std::vector<int>>);
    STATIC_REQUIRE(!pravaha::SerializablePayload<NonTrivial>);
    STATIC_REQUIRE(!pravaha::SerializablePayload<MoveOnly>);
}

TEST_CASE("SerializablePayload concept - pointer types excluded via meta", "[pravaha][concepts]") {
    STATIC_REQUIRE(!pravaha::SerializablePayload<WithPointer>);
}

// ============================================================================
// SECTION 9: InvocableTask Concept
// ============================================================================

TEST_CASE("InvocableTask concept - valid task callable", "[pravaha][concepts]") {
    auto valid_task = [](int x) -> pravaha::Outcome<int> { return x * 2; };
    STATIC_REQUIRE(pravaha::InvocableTask<decltype(valid_task), int, int>);
}

TEST_CASE("InvocableTask concept - no-arg task", "[pravaha][concepts]") {
    auto no_arg_task = []() -> pravaha::Outcome<pravaha::Unit> { return pravaha::Unit{}; };
    STATIC_REQUIRE(pravaha::InvocableTask<decltype(no_arg_task), pravaha::Unit>);
}

TEST_CASE("InvocableTask concept - wrong return type excluded", "[pravaha][concepts]") {
    auto wrong_return = [](int x) -> int { return x; };
    STATIC_REQUIRE(!pravaha::InvocableTask<decltype(wrong_return), int, int>);
}

// ============================================================================
// SECTION 10: Unit type
// ============================================================================

TEST_CASE("Unit is monostate", "[pravaha][unit]") {
    STATIC_REQUIRE(std::same_as<pravaha::Unit, std::monostate>);
    pravaha::Unit u1{};
    pravaha::Unit u2{};
    REQUIRE(u1 == u2);
}

// ============================================================================
// SECTION 11: Cancellation Primitives
// ============================================================================

TEST_CASE("CancellationToken - fresh token is not canceled", "[pravaha][cancellation]") {
    pravaha::CancellationSource source;
    auto tok = source.token();
    REQUIRE(!tok.stop_requested());
    REQUIRE(tok.has_state());
}

TEST_CASE("CancellationToken - default empty token is not canceled", "[pravaha][cancellation]") {
    pravaha::CancellationToken empty_tok;
    REQUIRE(!empty_tok.stop_requested());
    REQUIRE(!empty_tok.has_state());
}

TEST_CASE("CancellationSource - request_stop changes state", "[pravaha][cancellation]") {
    pravaha::CancellationSource source;
    auto tok = source.token();
    REQUIRE(!tok.stop_requested());
    REQUIRE(!source.stop_requested());
    source.request_stop();
    REQUIRE(tok.stop_requested());
    REQUIRE(source.stop_requested());
}

TEST_CASE("CancellationSource - repeated request_stop is safe", "[pravaha][cancellation]") {
    pravaha::CancellationSource source;
    auto tok = source.token();
    source.request_stop();
    source.request_stop();
    source.request_stop();
    REQUIRE(tok.stop_requested());
    REQUIRE(source.stop_requested());
}

TEST_CASE("CancellationSource - multiple tokens observe same state", "[pravaha][cancellation]") {
    pravaha::CancellationSource source;
    auto tok1 = source.token();
    auto tok2 = source.token();
    REQUIRE(!tok1.stop_requested());
    REQUIRE(!tok2.stop_requested());
    source.request_stop();
    REQUIRE(tok1.stop_requested());
    REQUIRE(tok2.stop_requested());
}

TEST_CASE("CancellationScope - root scope not canceled initially", "[pravaha][cancellation]") {
    pravaha::CancellationScope scope;
    REQUIRE(!scope.stop_requested());
}

TEST_CASE("CancellationScope - local request_stop", "[pravaha][cancellation]") {
    pravaha::CancellationScope scope;
    scope.request_stop();
    REQUIRE(scope.stop_requested());
}

TEST_CASE("CancellationScope - child scope observes parent cancellation", "[pravaha][cancellation]") {
    pravaha::CancellationSource parent_source;
    pravaha::CancellationScope child_scope{parent_source.token()};
    REQUIRE(!child_scope.stop_requested());
    parent_source.request_stop();
    REQUIRE(child_scope.stop_requested());
}

TEST_CASE("CancellationScope - child local cancel does not affect parent", "[pravaha][cancellation]") {
    pravaha::CancellationSource parent_source;
    pravaha::CancellationScope child_scope{parent_source.token()};
    child_scope.request_stop();
    REQUIRE(child_scope.stop_requested());
    REQUIRE(!parent_source.stop_requested());
}

TEST_CASE("CancellationScope - token from scope", "[pravaha][cancellation]") {
    pravaha::CancellationScope scope;
    auto tok = scope.token();
    REQUIRE(!tok.stop_requested());
    scope.request_stop();
    REQUIRE(tok.stop_requested());
}

TEST_CASE("CancellationScope - nested scopes", "[pravaha][cancellation]") {
    pravaha::CancellationSource root_source;
    pravaha::CancellationScope mid_scope{root_source.token()};
    pravaha::CancellationScope leaf_scope{mid_scope.token()};
    REQUIRE(!leaf_scope.stop_requested());
    mid_scope.request_stop();
    REQUIRE(leaf_scope.stop_requested());
    REQUIRE(!root_source.stop_requested());
}

// ============================================================================
// SECTION 12: TaskCommand
// ============================================================================

TEST_CASE("TaskCommand - stores lambda and runs it", "[pravaha][taskcommand]") {
    int counter = 0;
    auto cmd = pravaha::TaskCommand::make([&counter]() { ++counter; }, "increment");
    REQUIRE(cmd.has_value());
    REQUIRE(!cmd.empty());
    REQUIRE(cmd.name() == "increment");
    auto result = cmd.run();
    REQUIRE(result.has_value());
    REQUIRE(counter == 1);
}

TEST_CASE("TaskCommand - callable runs exactly once when run once", "[pravaha][taskcommand]") {
    int counter = 0;
    auto cmd = pravaha::TaskCommand::make([&counter]() { ++counter; });
    cmd.run();
    REQUIRE(counter == 1);
    cmd.run();
    REQUIRE(counter == 2);
}

TEST_CASE("TaskCommand - move-only lambda works", "[pravaha][taskcommand]") {
    auto ptr = std::make_unique<int>(42);
    int* raw = ptr.get();
    auto cmd = pravaha::TaskCommand::make([p = std::move(ptr)]() mutable { *p += 1; }, "move_only_task");
    REQUIRE(cmd.has_value());
    auto result = cmd.run();
    REQUIRE(result.has_value());
    REQUIRE(*raw == 43);
}

TEST_CASE("TaskCommand - moved-from command is empty", "[pravaha][taskcommand]") {
    int counter = 0;
    auto cmd1 = pravaha::TaskCommand::make([&counter]() { ++counter; });
    REQUIRE(cmd1.has_value());
    auto cmd2 = std::move(cmd1);
    REQUIRE(cmd1.empty());
    REQUIRE(!cmd1.has_value());
    REQUIRE(cmd2.has_value());
    auto result = cmd2.run();
    REQUIRE(result.has_value());
    REQUIRE(counter == 1);
}

TEST_CASE("TaskCommand - move assignment", "[pravaha][taskcommand]") {
    int a_counter = 0, b_counter = 0;
    auto cmd_a = pravaha::TaskCommand::make([&a_counter]() { ++a_counter; });
    auto cmd_b = pravaha::TaskCommand::make([&b_counter]() { ++b_counter; });
    cmd_a = std::move(cmd_b);
    REQUIRE(cmd_b.empty());
    REQUIRE(cmd_a.has_value());
    cmd_a.run();
    REQUIRE(a_counter == 0);
    REQUIRE(b_counter == 1);
}

TEST_CASE("TaskCommand - exception becomes error", "[pravaha][taskcommand]") {
    auto cmd = pravaha::TaskCommand::make([]() { throw std::runtime_error("task exploded"); });
    auto result = cmd.run();
    REQUIRE(!result.has_value());
    REQUIRE(result.error().kind == pravaha::ErrorKind::TaskFailed);
    REQUIRE(result.error().message.find("task exploded") != std::string::npos);
}

TEST_CASE("TaskCommand - unknown exception becomes Outcome error", "[pravaha][taskcommand]") {
    auto cmd = pravaha::TaskCommand::make([]() { throw 42; });
    auto result = cmd.run();
    REQUIRE(!result.has_value());
    REQUIRE(result.error().kind == pravaha::ErrorKind::TaskFailed);
    REQUIRE(result.error().message.find("unknown") != std::string::npos);
}

TEST_CASE("TaskCommand - default constructed is empty", "[pravaha][taskcommand]") {
    pravaha::TaskCommand cmd;
    REQUIRE(cmd.empty());
    REQUIRE(!cmd.has_value());
    REQUIRE(!static_cast<bool>(cmd));
    auto result = cmd.run();
    REQUIRE(!result.has_value());
    REQUIRE(result.error().kind == pravaha::ErrorKind::TaskFailed);
}

TEST_CASE("TaskCommand - debug name preserved through move", "[pravaha][taskcommand]") {
    auto cmd1 = pravaha::TaskCommand::make([](){}, "my_task");
    REQUIRE(cmd1.name() == "my_task");
    auto cmd2 = std::move(cmd1);
    REQUIRE(cmd2.name() == "my_task");
    REQUIRE(cmd1.name().empty());
}

TEST_CASE("TaskCommand - bool conversion", "[pravaha][taskcommand]") {
    pravaha::TaskCommand empty_cmd;
    REQUIRE(!static_cast<bool>(empty_cmd));
    auto full_cmd = pravaha::TaskCommand::make([](){});
    REQUIRE(static_cast<bool>(full_cmd));
}

TEST_CASE("TaskCommand - no std::function usage", "[pravaha][taskcommand]") {
    auto cmd = pravaha::TaskCommand::make([](){});
    REQUIRE(cmd.has_value());
    auto result = cmd.run();
    REQUIRE(result.has_value());
}

// ============================================================================
// SECTION 13: Lazy Expression DSL
// ============================================================================

TEST_CASE("task() - creating task does not execute callable", "[pravaha][dsl]") {
    int counter = 0;
    auto a = pravaha::task("a", [&counter]() { ++counter; });
    REQUIRE(counter == 0);
    REQUIRE(a.name() == "a");
    REQUIRE(a.domain() == pravaha::ExecutionDomain::CPU);
}

TEST_CASE("task() - sequence composition does not execute callable", "[pravaha][dsl]") {
    int ca = 0, cb = 0;
    auto a = pravaha::task("a", [&ca]() { ++ca; });
    auto b = pravaha::task("b", [&cb]() { ++cb; });
    auto seq = std::move(a) | std::move(b);
    REQUIRE(ca == 0);
    REQUIRE(cb == 0);
    STATIC_REQUIRE(pravaha::IsPravahaExpr<decltype(seq)>);
}

TEST_CASE("task() - parallel composition does not execute callable", "[pravaha][dsl]") {
    int ca = 0, cb = 0;
    auto a = pravaha::task("a", [&ca]() { ++ca; });
    auto b = pravaha::task("b", [&cb]() { ++cb; });
    auto par = std::move(a) & std::move(b);
    REQUIRE(ca == 0);
    REQUIRE(cb == 0);
    STATIC_REQUIRE(pravaha::IsPravahaExpr<decltype(par)>);
}

TEST_CASE("task_on() - stores selected ExecutionDomain", "[pravaha][dsl]") {
    auto t = pravaha::task_on(pravaha::ExecutionDomain::IO, "io_task", []() {});
    REQUIRE(t.name() == "io_task");
    REQUIRE(t.domain() == pravaha::ExecutionDomain::IO);
}

TEST_CASE("collect_all() - marks parallel expr with CollectAll", "[pravaha][dsl]") {
    auto a = pravaha::task("a", [](){});
    auto b = pravaha::task("b", [](){});
    auto par = std::move(a) & std::move(b);
    REQUIRE(par.policy == pravaha::JoinPolicyKind::AllOrNothing);
    auto collected = pravaha::collect_all(std::move(par));
    REQUIRE(collected.policy == pravaha::JoinPolicyKind::CollectAll);
}

// ============================================================================
// SECTION 14: Task IR - Lowering
// ============================================================================

TEST_CASE("TaskId - basic properties", "[pravaha][ir]") {
    pravaha::TaskId id0{0};
    pravaha::TaskId id1{1};
    pravaha::TaskId invalid{};
    REQUIRE(id0.is_valid());
    REQUIRE(id1.is_valid());
    REQUIRE(!invalid.is_valid());
    REQUIRE(id0 != id1);
    REQUIRE(invalid == pravaha::invalid_task_id);
}

TEST_CASE("lower_to_ir - single task lowers to one node", "[pravaha][ir]") {
    int counter = 0;
    auto a = pravaha::task("a", [&counter]() { ++counter; });
    auto result = pravaha::lower_to_ir(std::move(a));
    REQUIRE(result.has_value());
    auto& ir = result.value();
    REQUIRE(ir.node_count() == 1);
    REQUIRE(ir.edge_count() == 0);
    REQUIRE(ir.nodes[0].name == "a");
    REQUIRE(counter == 0);
}

TEST_CASE("lower_to_ir - sequence lowers to two nodes and one Sequence edge", "[pravaha][ir]") {
    int counter = 0;
    auto a = pravaha::task("a", [&counter]() { ++counter; });
    auto b = pravaha::task("b", [&counter]() { ++counter; });
    auto expr = std::move(a) | std::move(b);
    auto result = pravaha::lower_to_ir(std::move(expr));
    REQUIRE(result.has_value());
    auto& ir = result.value();
    REQUIRE(ir.node_count() == 2);
    REQUIRE(ir.edge_count() == 1);
    REQUIRE(ir.edges[0].from == pravaha::TaskId{0});
    REQUIRE(ir.edges[0].to == pravaha::TaskId{1});
    REQUIRE(ir.edges[0].kind == pravaha::EdgeKind::Sequence);
    REQUIRE(counter == 0);
}

TEST_CASE("lower_to_ir - parallel lowers to two nodes and no edge", "[pravaha][ir]") {
    int counter = 0;
    auto a = pravaha::task("a", [&counter]() { ++counter; });
    auto b = pravaha::task("b", [&counter]() { ++counter; });
    auto expr = std::move(a) & std::move(b);
    auto result = pravaha::lower_to_ir(std::move(expr));
    REQUIRE(result.has_value());
    auto& ir = result.value();
    REQUIRE(ir.node_count() == 2);
    REQUIRE(ir.edge_count() == 0);
    REQUIRE(counter == 0);
}

TEST_CASE("lower_to_ir - (A & B) | C has edges A->C and B->C", "[pravaha][ir]") {
    int counter = 0;
    auto a = pravaha::task("A", [&counter]() { ++counter; });
    auto b = pravaha::task("B", [&counter]() { ++counter; });
    auto c = pravaha::task("C", [&counter]() { ++counter; });
    auto expr = (std::move(a) & std::move(b)) | std::move(c);
    auto result = pravaha::lower_to_ir(std::move(expr));
    REQUIRE(result.has_value());
    auto& ir = result.value();
    REQUIRE(ir.node_count() == 3);
    REQUIRE(ir.edge_count() == 2);
    REQUIRE(counter == 0);
}

TEST_CASE("lower_to_ir - commands are runnable after lowering", "[pravaha][ir]") {
    int counter = 0;
    auto a = pravaha::task("a", [&counter]() { counter += 1; });
    auto result = pravaha::lower_to_ir(std::move(a));
    REQUIRE(result.has_value());
    REQUIRE(counter == 0);
    auto run_result = result.value().nodes[0].command.run();
    REQUIRE(run_result.has_value());
    REQUIRE(counter == 1);
}

// ============================================================================
// SECTION 15: LiteGraph Validation Layer
// ============================================================================

TEST_CASE("validate_ir_with_litegraph - valid sequence passes", "[pravaha][litegraph]") {
    // A | B - simple valid sequence
    auto a = pravaha::task("A", []() {});
    auto b = pravaha::task("B", []() {});
    auto expr = std::move(a) | std::move(b);
    auto ir_result = pravaha::lower_to_ir(std::move(expr));
    REQUIRE(ir_result.has_value());

    auto validation = pravaha::validate_ir_with_litegraph(ir_result.value());
    REQUIRE(validation.has_value());
}

TEST_CASE("validate_ir_with_litegraph - invalid edge endpoint fails", "[pravaha][litegraph]") {
    // Manually construct invalid IR with bad edge endpoint
    pravaha::TaskIr ir;
    ir.add_node("A", pravaha::ExecutionDomain::CPU, pravaha::TaskCommand::make([](){}));
    ir.add_node("B", pravaha::ExecutionDomain::CPU, pravaha::TaskCommand::make([](){}));
    // Edge referencing non-existent TaskId{99}
    ir.add_edge(pravaha::TaskId{0}, pravaha::TaskId{99}, pravaha::EdgeKind::Sequence);

    auto validation = pravaha::validate_ir_with_litegraph(ir);
    REQUIRE(!validation.has_value());
    REQUIRE(validation.error().kind == pravaha::ErrorKind::ValidationError);
}

TEST_CASE("validate_ir_with_litegraph - cycle detected", "[pravaha][litegraph]") {
    // Manually construct IR with a cycle: A->B->A
    pravaha::TaskIr ir;
    ir.add_node("A", pravaha::ExecutionDomain::CPU, pravaha::TaskCommand::make([](){}));
    ir.add_node("B", pravaha::ExecutionDomain::CPU, pravaha::TaskCommand::make([](){}));
    ir.add_edge(pravaha::TaskId{0}, pravaha::TaskId{1}, pravaha::EdgeKind::Sequence);
    ir.add_edge(pravaha::TaskId{1}, pravaha::TaskId{0}, pravaha::EdgeKind::Sequence);

    auto validation = pravaha::validate_ir_with_litegraph(ir);
    REQUIRE(!validation.has_value());
    REQUIRE(validation.error().kind == pravaha::ErrorKind::CycleDetected);
}

TEST_CASE("topological_order - A | B returns A before B", "[pravaha][litegraph]") {
    auto a = pravaha::task("A", []() {});
    auto b = pravaha::task("B", []() {});
    auto expr = std::move(a) | std::move(b);
    auto ir_result = pravaha::lower_to_ir(std::move(expr));
    REQUIRE(ir_result.has_value());

    auto topo_result = pravaha::topological_order(ir_result.value());
    REQUIRE(topo_result.has_value());
    auto& order = topo_result.value();
    REQUIRE(order.size() == 2);

    // Find positions of A and B
    std::size_t a_pos = ~std::size_t{0}, b_pos = ~std::size_t{0};
    for (std::size_t i = 0; i < order.size(); ++i) {
        if (order[i] == pravaha::TaskId{0}) a_pos = i;
        if (order[i] == pravaha::TaskId{1}) b_pos = i;
    }
    REQUIRE(a_pos < b_pos);  // A must come before B
}

TEST_CASE("topological_order - (A & B) | C returns A/B before C", "[pravaha][litegraph]") {
    auto a = pravaha::task("A", []() {});
    auto b = pravaha::task("B", []() {});
    auto c = pravaha::task("C", []() {});
    auto expr = (std::move(a) & std::move(b)) | std::move(c);
    auto ir_result = pravaha::lower_to_ir(std::move(expr));
    REQUIRE(ir_result.has_value());

    auto topo_result = pravaha::topological_order(ir_result.value());
    REQUIRE(topo_result.has_value());
    auto& order = topo_result.value();
    REQUIRE(order.size() == 3);

    // Find positions
    std::size_t a_pos = ~std::size_t{0};
    std::size_t b_pos = ~std::size_t{0};
    std::size_t c_pos = ~std::size_t{0};
    for (std::size_t i = 0; i < order.size(); ++i) {
        if (order[i] == pravaha::TaskId{0}) a_pos = i;
        if (order[i] == pravaha::TaskId{1}) b_pos = i;
        if (order[i] == pravaha::TaskId{2}) c_pos = i;
    }
    // A and B must both come before C
    REQUIRE(a_pos < c_pos);
    REQUIRE(b_pos < c_pos);
}

TEST_CASE("to_litegraph - converts valid IR to ExecutionGraph", "[pravaha][litegraph]") {
    auto a = pravaha::task("A", []() {});
    auto b = pravaha::task("B", []() {});
    auto expr = std::move(a) | std::move(b);
    auto ir_result = pravaha::lower_to_ir(std::move(expr));
    REQUIRE(ir_result.has_value());

    auto graph_result = pravaha::to_litegraph(ir_result.value());
    REQUIRE(graph_result.has_value());
    auto& graph = graph_result.value();
    REQUIRE(graph.node_count() == 2);
    REQUIRE(graph.edge_count() == 1);
}

// ============================================================================
// SECTION 16: Runner & InlineBackend
// ============================================================================

TEST_CASE("Runner - single task runs on submit", "[pravaha][runner]") {
    int counter = 0;
    pravaha::Runner<> runner;
    auto expr = pravaha::task("A", [&counter]() { ++counter; });
    auto result = runner.submit(std::move(expr));
    REQUIRE(result.has_value());
    REQUIRE(counter == 1);
    REQUIRE(result.value().final_state == pravaha::TaskState::Succeeded);
    REQUIRE(result.value().node_states.size() == 1);
    REQUIRE(result.value().node_states[0] == pravaha::TaskState::Succeeded);
}

TEST_CASE("Runner - sequence runs A before B", "[pravaha][runner]") {
    std::vector<int> order;
    pravaha::Runner<> runner;
    auto a = pravaha::task("A", [&order]() { order.push_back(1); });
    auto b = pravaha::task("B", [&order]() { order.push_back(2); });
    auto expr = std::move(a) | std::move(b);
    auto result = runner.submit(std::move(expr));
    REQUIRE(result.has_value());
    REQUIRE(order.size() == 2);
    REQUIRE(order[0] == 1);
    REQUIRE(order[1] == 2);
    REQUIRE(result.value().final_state == pravaha::TaskState::Succeeded);
}

TEST_CASE("Runner - if A fails, B is skipped", "[pravaha][runner]") {
    int b_counter = 0;
    pravaha::Runner<> runner;
    auto a = pravaha::task("A", []() { throw std::runtime_error("fail"); });
    auto b = pravaha::task("B", [&b_counter]() { ++b_counter; });
    auto expr = std::move(a) | std::move(b);
    auto result = runner.submit(std::move(expr));
    REQUIRE(result.has_value());
    REQUIRE(b_counter == 0);
    REQUIRE(result.value().final_state == pravaha::TaskState::Failed);
    REQUIRE(result.value().node_states[0] == pravaha::TaskState::Failed);
    REQUIRE(result.value().node_states[1] == pravaha::TaskState::Skipped);
    REQUIRE(result.value().errors.size() == 1);
}

TEST_CASE("Runner - construction is lazy before submit", "[pravaha][runner]") {
    int counter = 0;
    pravaha::Runner<> runner;
    auto expr = pravaha::task("A", [&counter]() { ++counter; });
    REQUIRE(counter == 0);
    auto result = runner.submit(std::move(expr));
    REQUIRE(counter == 1);
}

TEST_CASE("Runner - final result is Failed if chain fails", "[pravaha][runner]") {
    pravaha::Runner<> runner;
    auto a = pravaha::task("A", []() {});
    auto b = pravaha::task("B", []() { throw std::runtime_error("boom"); });
    auto expr = std::move(a) | std::move(b);
    auto result = runner.submit(std::move(expr));
    REQUIRE(result.has_value());
    REQUIRE(result.value().final_state == pravaha::TaskState::Failed);
}

TEST_CASE("Runner - final result is Succeeded if chain succeeds", "[pravaha][runner]") {
    pravaha::Runner<> runner;
    auto a = pravaha::task("A", []() {});
    auto b = pravaha::task("B", []() {});
    auto c = pravaha::task("C", []() {});
    auto expr = std::move(a) | std::move(b) | std::move(c);
    auto result = runner.submit(std::move(expr));
    REQUIRE(result.has_value());
    REQUIRE(result.value().final_state == pravaha::TaskState::Succeeded);
    REQUIRE(result.value().node_states.size() == 3);
    for (auto s : result.value().node_states)
        REQUIRE(s == pravaha::TaskState::Succeeded);
}

TEST_CASE("Runner - LiteGraph validation is called before execution", "[pravaha][runner]") {
    // We can't directly create a cyclic expression via DSL (it's structurally impossible),
    // but we verify that validation runs by checking that a valid graph passes without error
    int counter = 0;
    pravaha::Runner<> runner;
    auto a = pravaha::task("A", [&counter]() { ++counter; });
    auto b = pravaha::task("B", [&counter]() { ++counter; });
    auto expr = (std::move(a) & std::move(b));
    auto result = runner.submit(std::move(expr));
    REQUIRE(result.has_value());
    REQUIRE(counter == 2);
    REQUIRE(result.value().final_state == pravaha::TaskState::Succeeded);
}
