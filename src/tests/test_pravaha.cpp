// ============================================================================
// test_pravaha.cpp — Unit tests for pravaha.hpp
// ============================================================================

#include "catch_amalgamated.hpp"
#include "pravaha/pravaha.hpp"

#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <deque>
#include <future>
#include <mutex>
#include <chrono>
#include <thread>

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

TEST_CASE("EventKind values are distinct", "[pravaha][observer]") {
    STATIC_REQUIRE(pravaha::EventKind::TaskReady != pravaha::EventKind::TaskScheduled);
    STATIC_REQUIRE(pravaha::EventKind::TaskStarted != pravaha::EventKind::TaskCompleted);
    STATIC_REQUIRE(pravaha::EventKind::TaskFailed != pravaha::EventKind::TaskSkipped);
    STATIC_REQUIRE(pravaha::EventKind::TaskCanceled != pravaha::EventKind::JoinResolved);
    STATIC_REQUIRE(pravaha::EventKind::GraphLowered != pravaha::EventKind::GraphValidated);
}

TEST_CASE("Observer event payloads are constructible", "[pravaha][observer]") {
    const pravaha::TaskEvent task_event{
        pravaha::EventKind::TaskReady,
        pravaha::TaskId{7},
        "task_a",
        pravaha::TaskState::Ready,
        123
    };
    REQUIRE(task_event.kind == pravaha::EventKind::TaskReady);
    REQUIRE(task_event.task_id == pravaha::TaskId{7});
    REQUIRE(task_event.task_name == "task_a");
    REQUIRE(task_event.state == pravaha::TaskState::Ready);
    REQUIRE(task_event.frontend_hash == 123);

    const pravaha::JoinEvent join_event{
        pravaha::EventKind::JoinResolved,
        2,
        pravaha::JoinPolicy{pravaha::JoinPolicyKind::CollectAll, 0},
        true,
        4,
        3,
        1,
        0,
        0
    };
    REQUIRE(join_event.kind == pravaha::EventKind::JoinResolved);
    REQUIRE(join_event.group_id == 2);
    REQUIRE(join_event.policy.kind == pravaha::JoinPolicyKind::CollectAll);
    REQUIRE(join_event.success);
    REQUIRE(join_event.expected == 4);
    REQUIRE(join_event.succeeded == 3);
    REQUIRE(join_event.failed == 1);

    const pravaha::GraphEvent graph_event{
        pravaha::EventKind::GraphLowered,
        10,
        12,
        3
    };
    REQUIRE(graph_event.kind == pravaha::EventKind::GraphLowered);
    REQUIRE(graph_event.node_count == 10);
    REQUIRE(graph_event.edge_count == 12);
    REQUIRE(graph_event.join_group_count == 3);
}

TEST_CASE("NoObserver satisfies observer policy and no-op methods compile", "[pravaha][observer]") {
    STATIC_REQUIRE(pravaha::ObserverPolicy<pravaha::NoObserver>);

    const pravaha::TaskEvent task_event{};
    const pravaha::JoinEvent join_event{};
    const pravaha::GraphEvent graph_event{};

    REQUIRE_FALSE(pravaha::NoObserver::enabled);
    pravaha::NoObserver::on_task_event(task_event);
    pravaha::NoObserver::on_join_event(join_event);
    pravaha::NoObserver::on_graph_event(graph_event);
    SUCCEED();
}

// ============================================================================
// SECTION 5: JoinPolicyKind and ExecutionDomain Enums
// ============================================================================

TEST_CASE("JoinPolicyKind values", "[pravaha][policy]") {
    STATIC_REQUIRE(pravaha::JoinPolicyKind::AllOrNothing != pravaha::JoinPolicyKind::CollectAll);
    STATIC_REQUIRE(pravaha::JoinPolicyKind::AnySuccess != pravaha::JoinPolicyKind::Quorum);
}

TEST_CASE("JoinPolicy data model", "[pravaha][policy]") {
    const pravaha::JoinPolicy any_success{pravaha::JoinPolicyKind::AnySuccess, 0};
    REQUIRE(any_success.kind == pravaha::JoinPolicyKind::AnySuccess);
    REQUIRE(any_success.quorum_required == 0);

    const pravaha::JoinPolicy quorum{pravaha::JoinPolicyKind::Quorum, 2};
    REQUIRE(quorum.kind == pravaha::JoinPolicyKind::Quorum);
    REQUIRE(quorum.quorum_required == 2);
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

TEST_CASE("TransferablePayload concept - memory-safe transferable types", "[pravaha][concepts]") {
    STATIC_REQUIRE(pravaha::TransferablePayload<int>);
    STATIC_REQUIRE(pravaha::TransferablePayload<double>);
    STATIC_REQUIRE(pravaha::TransferablePayload<TrivialPoint>);
}

TEST_CASE("TransferablePayload concept - non-trivial or heap-owning types excluded", "[pravaha][concepts]") {
    STATIC_REQUIRE(!pravaha::TransferablePayload<std::string>);
    STATIC_REQUIRE(!pravaha::TransferablePayload<std::vector<int>>);
    STATIC_REQUIRE(!pravaha::TransferablePayload<NonTrivial>);
}

TEST_CASE("TransferablePayload concept - move-only types excluded", "[pravaha][concepts]") {
    STATIC_REQUIRE(!pravaha::TransferablePayload<MoveOnly>);
}

TEST_CASE("CopyablePayload concept - local copyability", "[pravaha][concepts]") {
    STATIC_REQUIRE(pravaha::CopyablePayload<std::string>);
    STATIC_REQUIRE(pravaha::CopyablePayload<std::vector<int>>);
    STATIC_REQUIRE(!pravaha::CopyablePayload<MoveOnly>);
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

TEST_CASE("TaskCommand - Outcome<Unit> error is propagated", "[pravaha][taskcommand][regression]") {
    auto cmd = pravaha::TaskCommand::make([]() -> pravaha::Outcome<pravaha::Unit> {
        return std::unexpected(pravaha::PravahaError{pravaha::ErrorKind::TaskFailed, "propagated failure"});
    });
    auto result = cmd.run();
    REQUIRE(!result.has_value());
    REQUIRE(result.error().kind == pravaha::ErrorKind::TaskFailed);
    REQUIRE(result.error().message == "propagated failure");
}

TEST_CASE("TaskCommand - explicit void callable succeeds", "[pravaha][taskcommand][regression]") {
    int ran = 0;
    auto cmd = pravaha::TaskCommand::make([&ran]() -> void { ++ran; });
    auto result = cmd.run();
    REQUIRE(result.has_value());
    REQUIRE(ran == 1);
}

TEST_CASE("TaskCommand - Outcome<string> success is treated as success", "[pravaha][taskcommand][regression]") {
    auto cmd = pravaha::TaskCommand::make([]() -> pravaha::Outcome<std::string> {
        return std::string{"ok"};
    });
    auto result = cmd.run();
    REQUIRE(result.has_value());
}

TEST_CASE("TaskCommand - Outcome<string> error is propagated", "[pravaha][taskcommand][regression]") {
    auto cmd = pravaha::TaskCommand::make([]() -> pravaha::Outcome<std::string> {
        return std::unexpected(pravaha::PravahaError{pravaha::ErrorKind::TaskFailed, "string failure"});
    });
    auto result = cmd.run();
    REQUIRE(!result.has_value());
    REQUIRE(result.error().kind == pravaha::ErrorKind::TaskFailed);
    REQUIRE(result.error().message == "string failure");
}

TEST_CASE("TaskCommand - Outcome<int> error is propagated", "[pravaha][taskcommand][regression]") {
    auto cmd = pravaha::TaskCommand::make([]() -> pravaha::Outcome<int> {
        return std::unexpected(pravaha::PravahaError{pravaha::ErrorKind::InternalError, "int failure"});
    });
    auto result = cmd.run();
    REQUIRE(!result.has_value());
    REQUIRE(result.error().kind == pravaha::ErrorKind::InternalError);
    REQUIRE(result.error().message == "int failure");
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
    REQUIRE(par.policy.kind == pravaha::JoinPolicyKind::AllOrNothing);
    auto collected = pravaha::collect_all(std::move(par));
    REQUIRE(collected.policy.kind == pravaha::JoinPolicyKind::CollectAll);
}

TEST_CASE("any_success() - marks parallel expr with AnySuccess and updates frontend", "[pravaha][dsl][lithe]") {
    auto plain = pravaha::task("a", []{}) & pravaha::task("b", []{});
    auto any = pravaha::any_success(pravaha::task("a", []{}) & pravaha::task("b", []{}));
    auto collected = pravaha::collect_all(pravaha::task("a", []{}) & pravaha::task("b", []{}));

    REQUIRE(any.policy.kind == pravaha::JoinPolicyKind::AnySuccess);
    REQUIRE(any.policy.quorum_required == 0);
    REQUIRE(any.frontend.hash != 0);
    REQUIRE(any.frontend.hash != plain.frontend.hash);
    REQUIRE(any.frontend.hash != collected.frontend.hash);
}

TEST_CASE("quorum<N>() - marks parallel expr with Quorum and updates frontend", "[pravaha][dsl][lithe]") {
    auto plain = pravaha::task("a", []{}) & pravaha::task("b", []{});
    auto collected = pravaha::collect_all(pravaha::task("a", []{}) & pravaha::task("b", []{}));
    auto any = pravaha::any_success(pravaha::task("a", []{}) & pravaha::task("b", []{}));
    auto q1 = pravaha::quorum<1>(pravaha::task("a", []{}) & pravaha::task("b", []{}));
    auto q2 = pravaha::quorum<2>(pravaha::task("a", []{}) & pravaha::task("b", []{}));

    REQUIRE(q1.policy.kind == pravaha::JoinPolicyKind::Quorum);
    REQUIRE(q1.policy.quorum_required == 1);
    REQUIRE(q1.frontend.hash != 0);
    REQUIRE(q1.frontend.hash != plain.frontend.hash);
    REQUIRE(q1.frontend.hash != collected.frontend.hash);
    REQUIRE(q1.frontend.hash != any.frontend.hash);
    REQUIRE(q2.frontend.hash != q1.frontend.hash);
}

TEST_CASE("C++ DSL expressions carry lithe-derived frontend identity", "[pravaha][dsl][lithe]") {
    const auto e1 = pravaha::task("a", []{}) | pravaha::task("b", []{});
    REQUIRE(e1.frontend.hash != 0);
    REQUIRE_FALSE(e1.frontend.dump.empty());

    const auto e1_same = pravaha::task("a", []{}) | pravaha::task("b", []{});
    REQUIRE(e1.frontend.hash == e1_same.frontend.hash);

    const auto e1_reversed = pravaha::task("b", []{}) | pravaha::task("a", []{});
    REQUIRE(e1.frontend.hash != e1_reversed.frontend.hash);

    const auto par = pravaha::task("a", []{}) & pravaha::task("b", []{});
    REQUIRE(par.frontend.hash != e1.frontend.hash);

    const auto collected = pravaha::collect_all(pravaha::task("a", []{}) & pravaha::task("b", []{}));
    REQUIRE(collected.frontend.hash != par.frontend.hash);
}

TEST_CASE("Lithe is canonical symbolic identity across textual and C++ DSL", "[pravaha][lithe][regression][canonical]") {
    const auto text_seq_1 = pravaha::parse_pipeline("pipeline p { a then b }");
    const auto text_seq_2 = pravaha::parse_pipeline("pipeline p { a then b }");
    REQUIRE(text_seq_1.has_value());
    REQUIRE(text_seq_2.has_value());

    const auto text_seq_1_root = pravaha::symbolic::make_frontend_meta_for_symbolic_expr(text_seq_1->root);
    const auto text_seq_2_root = pravaha::symbolic::make_frontend_meta_for_symbolic_expr(text_seq_2->root);
    REQUIRE(text_seq_1_root.hash == text_seq_2_root.hash);
    REQUIRE(text_seq_1->frontend.hash == text_seq_2->frontend.hash);

    const auto text_seq_ba = pravaha::parse_pipeline("pipeline p { b then a }");
    REQUIRE(text_seq_ba.has_value());
    const auto text_seq_ba_root = pravaha::symbolic::make_frontend_meta_for_symbolic_expr(text_seq_ba->root);
    REQUIRE(text_seq_1_root.hash != text_seq_ba_root.hash);

    const auto text_par = pravaha::parse_pipeline("pipeline p { parallel { a, b } }");
    REQUIRE(text_par.has_value());
    const auto text_par_root = pravaha::symbolic::make_frontend_meta_for_symbolic_expr(text_par->root);
    REQUIRE(text_seq_1_root.hash != text_par_root.hash);

    const auto text_name_q = pravaha::parse_pipeline("pipeline q { a }");
    const auto text_name_p = pravaha::parse_pipeline("pipeline p { a }");
    REQUIRE(text_name_q.has_value());
    REQUIRE(text_name_p.has_value());
    REQUIRE(text_name_q->frontend.hash != text_name_p->frontend.hash);

    const auto cpp_seq_ab = pravaha::task("a", []{}) | pravaha::task("b", []{});
    const auto cpp_seq_ab_2 = pravaha::task("a", []{}) | pravaha::task("b", []{});
    const auto cpp_seq_ba = pravaha::task("b", []{}) | pravaha::task("a", []{});
    const auto cpp_par_ab = pravaha::task("a", []{}) & pravaha::task("b", []{});
    REQUIRE(cpp_seq_ab.frontend.hash != 0);
    REQUIRE(cpp_seq_ab.frontend.hash == cpp_seq_ab_2.frontend.hash);
    REQUIRE(cpp_seq_ab.frontend.hash != cpp_seq_ba.frontend.hash);
    REQUIRE(cpp_seq_ab.frontend.hash != cpp_par_ab.frontend.hash);

    REQUIRE(text_seq_1_root.hash == cpp_seq_ab.frontend.hash);

    const auto cpp_collect_all_ab = pravaha::collect_all(pravaha::task("a", []{}) & pravaha::task("b", []{}));
    REQUIRE(cpp_collect_all_ab.frontend.hash != cpp_par_ab.frontend.hash);
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

TEST_CASE("lower_to_ir - C++ DSL task propagates frontend diagnostics", "[pravaha][ir][frontend]") {
    auto expr = pravaha::task("diag_task", []() {});
    auto ir_result = pravaha::lower_to_ir(std::move(expr));
    REQUIRE(ir_result.has_value());
    REQUIRE(ir_result->nodes.size() == 1);
    REQUIRE(ir_result->nodes[0].frontend_hash != 0);
    REQUIRE_FALSE(ir_result->nodes[0].frontend_dump.empty());
}

TEST_CASE("lower_to_ir - Outcome<Unit> payload metadata unwraps to Unit", "[pravaha][ir][payload]") {
    auto a = pravaha::task("a", []() -> pravaha::Outcome<pravaha::Unit> { return pravaha::Unit{}; });
    auto result = pravaha::lower_to_ir(std::move(a));
    REQUIRE(result.has_value());
    REQUIRE(result->nodes.size() == 1);
    REQUIRE(result->nodes[0].payload_meta.output_type_name == std::string(meta::type_name<pravaha::Unit>()));
}

TEST_CASE("lower_to_ir - parallel join group policy defaults to AllOrNothing", "[pravaha][ir][policy]") {
    auto expr = pravaha::task("a", []() {}) & pravaha::task("b", []() {});
    auto result = pravaha::lower_to_ir(std::move(expr));
    REQUIRE(result.has_value());
    REQUIRE(result->join_groups.size() == 1);
    REQUIRE(result->join_groups[0].members.size() == 2);
    REQUIRE(result->join_groups[0].policy.kind == pravaha::JoinPolicyKind::AllOrNothing);
    REQUIRE(result->join_groups[0].policy.quorum_required == 0);
}

TEST_CASE("lower_to_ir - collect_all parallel join group policy is CollectAll", "[pravaha][ir][policy]") {
    auto expr = pravaha::collect_all(pravaha::task("a", []() {}) & pravaha::task("b", []() {}));
    auto result = pravaha::lower_to_ir(std::move(expr));
    REQUIRE(result.has_value());
    REQUIRE(result->join_groups.size() == 1);
    REQUIRE(result->join_groups[0].policy.kind == pravaha::JoinPolicyKind::CollectAll);
    REQUIRE(result->join_groups[0].policy.quorum_required == 0);
}

TEST_CASE("lower_to_ir - any_success parallel join group policy is AnySuccess", "[pravaha][ir][policy]") {
    auto expr = pravaha::any_success(pravaha::task("a", []() {}) & pravaha::task("b", []() {}));
    auto result = pravaha::lower_to_ir(std::move(expr));
    REQUIRE(result.has_value());
    REQUIRE(result->join_groups.size() == 1);
    REQUIRE(result->join_groups[0].policy.kind == pravaha::JoinPolicyKind::AnySuccess);
    REQUIRE(result->join_groups[0].policy.quorum_required == 0);
}

TEST_CASE("lower_to_ir - quorum parallel join group policy preserves quorum_required", "[pravaha][ir][policy]") {
    auto expr = pravaha::quorum<1>(pravaha::task("a", []() {}) & pravaha::task("b", []() {}));
    auto result = pravaha::lower_to_ir(std::move(expr));
    REQUIRE(result.has_value());
    REQUIRE(result->join_groups.size() == 1);
    REQUIRE(result->join_groups[0].policy.kind == pravaha::JoinPolicyKind::Quorum);
    REQUIRE(result->join_groups[0].policy.quorum_required == 1);
}

TEST_CASE("lower_to_ir - quorum larger than branch count fails validation", "[pravaha][ir][policy]") {
    auto expr = pravaha::quorum<3>(pravaha::task("a", []() {}) & pravaha::task("b", []() {}));
    auto result = pravaha::lower_to_ir(std::move(expr));
    REQUIRE(!result.has_value());
    REQUIRE(result.error().kind == pravaha::ErrorKind::ValidationError);
}

TEST_CASE("join runtime state - AllOrNothing success and failure", "[pravaha][runtime][join]") {
    pravaha::RuntimeState ok;
    ok.joins.push_back(pravaha::JoinRuntimeState{pravaha::JoinPolicy{pravaha::JoinPolicyKind::AllOrNothing, 0}, 2});
    ok.record_join_terminal(0, pravaha::TaskState::Succeeded);
    REQUIRE(ok.joins[0].resolved == false);
    REQUIRE(ok.joins[0].success == false);
    ok.record_join_terminal(0, pravaha::TaskState::Succeeded);
    REQUIRE(ok.joins[0].resolved == true);
    REQUIRE(ok.joins[0].success == true);

    pravaha::RuntimeState fail;
    fail.joins.push_back(pravaha::JoinRuntimeState{pravaha::JoinPolicy{pravaha::JoinPolicyKind::AllOrNothing, 0}, 2});
    fail.record_join_terminal(0, pravaha::TaskState::Failed);
    REQUIRE(fail.joins[0].resolved == true);
    REQUIRE(fail.joins[0].success == false);
}

TEST_CASE("join runtime state - CollectAll resolves only when all terminal", "[pravaha][runtime][join]") {
    pravaha::RuntimeState state;
    state.joins.push_back(pravaha::JoinRuntimeState{pravaha::JoinPolicy{pravaha::JoinPolicyKind::CollectAll, 0}, 2});
    state.record_join_terminal(0, pravaha::TaskState::Succeeded);
    REQUIRE(state.joins[0].resolved == false);
    state.record_join_terminal(0, pravaha::TaskState::Failed);
    REQUIRE(state.joins[0].resolved == true);
    REQUIRE(state.joins[0].success == false);
}

TEST_CASE("join runtime state - AnySuccess success and all-fail resolution", "[pravaha][runtime][join]") {
    pravaha::RuntimeState success_state;
    success_state.joins.push_back(pravaha::JoinRuntimeState{pravaha::JoinPolicy{pravaha::JoinPolicyKind::AnySuccess, 0}, 3});
    success_state.record_join_terminal(0, pravaha::TaskState::Succeeded);
    REQUIRE(success_state.joins[0].resolved == true);
    REQUIRE(success_state.joins[0].success == true);

    pravaha::RuntimeState fail_state;
    fail_state.joins.push_back(pravaha::JoinRuntimeState{pravaha::JoinPolicy{pravaha::JoinPolicyKind::AnySuccess, 0}, 2});
    fail_state.record_join_terminal(0, pravaha::TaskState::Failed);
    REQUIRE(fail_state.joins[0].resolved == false);
    fail_state.record_join_terminal(0, pravaha::TaskState::Skipped);
    REQUIRE(fail_state.joins[0].resolved == true);
    REQUIRE(fail_state.joins[0].success == false);
}

TEST_CASE("join runtime state - Quorum success and impossible failure", "[pravaha][runtime][join]") {
    pravaha::RuntimeState success_state;
    success_state.joins.push_back(pravaha::JoinRuntimeState{pravaha::JoinPolicy{pravaha::JoinPolicyKind::Quorum, 2}, 3});
    success_state.record_join_terminal(0, pravaha::TaskState::Succeeded);
    REQUIRE(success_state.joins[0].resolved == false);
    success_state.record_join_terminal(0, pravaha::TaskState::Failed);
    REQUIRE(success_state.joins[0].resolved == false);
    success_state.record_join_terminal(0, pravaha::TaskState::Succeeded);
    REQUIRE(success_state.joins[0].resolved == true);
    REQUIRE(success_state.joins[0].success == true);

    pravaha::RuntimeState fail_state;
    fail_state.joins.push_back(pravaha::JoinRuntimeState{pravaha::JoinPolicy{pravaha::JoinPolicyKind::Quorum, 2}, 3});
    fail_state.record_join_terminal(0, pravaha::TaskState::Failed);
    REQUIRE(fail_state.joins[0].resolved == false);
    fail_state.record_join_terminal(0, pravaha::TaskState::Skipped);
    REQUIRE(fail_state.joins[0].resolved == true);
    REQUIRE(fail_state.joins[0].success == false);
}

TEST_CASE("join runtime state - member accounting prevents double count", "[pravaha][runtime][join]") {
    pravaha::TaskIr ir;
    ir.add_node("a", pravaha::ExecutionDomain::CPU, pravaha::TaskCommand::make([]() {}));
    ir.add_node("b", pravaha::ExecutionDomain::CPU, pravaha::TaskCommand::make([]() {}));
    ir.add_join_group({pravaha::TaskId{0}, pravaha::TaskId{1}}, pravaha::JoinPolicy{pravaha::JoinPolicyKind::AllOrNothing, 0});

    auto state = pravaha::RuntimeState::build(ir);
    state.record_join_terminal_for_member(0, 0, pravaha::TaskState::Succeeded);
    state.record_join_terminal_for_member(0, 0, pravaha::TaskState::Succeeded);
    REQUIRE(state.joins[0].succeeded == 1);
}

TEST_CASE("join runtime state - AnySuccess ignores late terminal updates after success", "[pravaha][runtime][join]") {
    pravaha::RuntimeState state;
    state.joins.push_back(pravaha::JoinRuntimeState{pravaha::JoinPolicy{pravaha::JoinPolicyKind::AnySuccess, 0}, 2});

    state.record_join_terminal(0, pravaha::TaskState::Succeeded);
    REQUIRE(state.joins[0].resolved == true);
    REQUIRE(state.joins[0].success == true);
    REQUIRE(state.joins[0].succeeded == 1);
    REQUIRE(state.joins[0].failed == 0);

    state.record_join_terminal(0, pravaha::TaskState::Failed);
    REQUIRE(state.joins[0].resolved == true);
    REQUIRE(state.joins[0].success == true);
    REQUIRE(state.joins[0].succeeded == 1);
    REQUIRE(state.joins[0].failed == 0);
}

TEST_CASE("join runtime state - Quorum ignores late terminal updates after success", "[pravaha][runtime][join]") {
    pravaha::RuntimeState state;
    state.joins.push_back(pravaha::JoinRuntimeState{pravaha::JoinPolicy{pravaha::JoinPolicyKind::Quorum, 1}, 2});

    state.record_join_terminal(0, pravaha::TaskState::Succeeded);
    REQUIRE(state.joins[0].resolved == true);
    REQUIRE(state.joins[0].success == true);
    REQUIRE(state.joins[0].succeeded == 1);
    REQUIRE(state.joins[0].failed == 0);

    state.record_join_terminal(0, pravaha::TaskState::Failed);
    REQUIRE(state.joins[0].resolved == true);
    REQUIRE(state.joins[0].success == true);
    REQUIRE(state.joins[0].succeeded == 1);
    REQUIRE(state.joins[0].failed == 0);
}

TEST_CASE("join runtime state - CollectAll continues counting after partial success", "[pravaha][runtime][join]") {
    pravaha::RuntimeState state;
    state.joins.push_back(pravaha::JoinRuntimeState{pravaha::JoinPolicy{pravaha::JoinPolicyKind::CollectAll, 0}, 2});

    state.record_join_terminal(0, pravaha::TaskState::Succeeded);
    REQUIRE(state.joins[0].resolved == false);
    REQUIRE(state.joins[0].succeeded == 1);

    state.record_join_terminal(0, pravaha::TaskState::Failed);
    REQUIRE(state.joins[0].resolved == true);
    REQUIRE(state.joins[0].success == false);
    REQUIRE(state.joins[0].succeeded == 1);
    REQUIRE(state.joins[0].failed == 1);
}

TEST_CASE("join runtime state - AnySuccess releases downstream on first success", "[pravaha][runtime][join][early-release]") {
    pravaha::TaskIr ir;
    ir.add_node("fast_success", pravaha::ExecutionDomain::CPU, pravaha::TaskCommand::make([]() {}));
    ir.add_node("long_success", pravaha::ExecutionDomain::CPU, pravaha::TaskCommand::make([]() {}));
    ir.add_node("downstream", pravaha::ExecutionDomain::CPU, pravaha::TaskCommand::make([]() {}));
    ir.add_edge(pravaha::TaskId{0}, pravaha::TaskId{2}, pravaha::EdgeKind::Sequence);
    ir.add_edge(pravaha::TaskId{1}, pravaha::TaskId{2}, pravaha::EdgeKind::Sequence);
    ir.add_join_group({pravaha::TaskId{0}, pravaha::TaskId{1}}, pravaha::JoinPolicy{pravaha::JoinPolicyKind::AnySuccess, 0});

    auto state = pravaha::RuntimeState::build(ir);
    state.mark_succeeded(0);

    REQUIRE(state.node_states[2] == pravaha::TaskState::Ready);
}

TEST_CASE("join runtime state - Quorum<1> releases downstream on first success", "[pravaha][runtime][join][early-release]") {
    pravaha::TaskIr ir;
    ir.add_node("fast_success", pravaha::ExecutionDomain::CPU, pravaha::TaskCommand::make([]() {}));
    ir.add_node("long_success", pravaha::ExecutionDomain::CPU, pravaha::TaskCommand::make([]() {}));
    ir.add_node("downstream", pravaha::ExecutionDomain::CPU, pravaha::TaskCommand::make([]() {}));
    ir.add_edge(pravaha::TaskId{0}, pravaha::TaskId{2}, pravaha::EdgeKind::Sequence);
    ir.add_edge(pravaha::TaskId{1}, pravaha::TaskId{2}, pravaha::EdgeKind::Sequence);
    ir.add_join_group({pravaha::TaskId{0}, pravaha::TaskId{1}}, pravaha::JoinPolicy{pravaha::JoinPolicyKind::Quorum, 1});

    auto state = pravaha::RuntimeState::build(ir);
    state.mark_succeeded(0);

    REQUIRE(state.node_states[2] == pravaha::TaskState::Ready);
}

TEST_CASE("join runtime state - Quorum<2> waits for both successes", "[pravaha][runtime][join][early-release]") {
    pravaha::TaskIr ir;
    ir.add_node("first_success", pravaha::ExecutionDomain::CPU, pravaha::TaskCommand::make([]() {}));
    ir.add_node("second_success", pravaha::ExecutionDomain::CPU, pravaha::TaskCommand::make([]() {}));
    ir.add_node("downstream", pravaha::ExecutionDomain::CPU, pravaha::TaskCommand::make([]() {}));
    ir.add_edge(pravaha::TaskId{0}, pravaha::TaskId{2}, pravaha::EdgeKind::Sequence);
    ir.add_edge(pravaha::TaskId{1}, pravaha::TaskId{2}, pravaha::EdgeKind::Sequence);
    ir.add_join_group({pravaha::TaskId{0}, pravaha::TaskId{1}}, pravaha::JoinPolicy{pravaha::JoinPolicyKind::Quorum, 2});

    auto state = pravaha::RuntimeState::build(ir);
    state.mark_succeeded(0);
    REQUIRE(state.node_states[2] == pravaha::TaskState::Created);

    state.mark_succeeded(1);
    REQUIRE(state.node_states[2] == pravaha::TaskState::Ready);
}

TEST_CASE("join runtime state - CollectAll waits for both successes", "[pravaha][runtime][join][early-release]") {
    pravaha::TaskIr ir;
    ir.add_node("first_success", pravaha::ExecutionDomain::CPU, pravaha::TaskCommand::make([]() {}));
    ir.add_node("second_success", pravaha::ExecutionDomain::CPU, pravaha::TaskCommand::make([]() {}));
    ir.add_node("downstream", pravaha::ExecutionDomain::CPU, pravaha::TaskCommand::make([]() {}));
    ir.add_edge(pravaha::TaskId{0}, pravaha::TaskId{2}, pravaha::EdgeKind::Sequence);
    ir.add_edge(pravaha::TaskId{1}, pravaha::TaskId{2}, pravaha::EdgeKind::Sequence);
    ir.add_join_group({pravaha::TaskId{0}, pravaha::TaskId{1}}, pravaha::JoinPolicy{pravaha::JoinPolicyKind::CollectAll, 0});

    auto state = pravaha::RuntimeState::build(ir);
    state.mark_succeeded(0);

    REQUIRE(state.node_states[2] == pravaha::TaskState::Created);
}

TEST_CASE("join runtime state - AllOrNothing waits for both successes", "[pravaha][runtime][join][early-release]") {
    pravaha::TaskIr ir;
    ir.add_node("first_success", pravaha::ExecutionDomain::CPU, pravaha::TaskCommand::make([]() {}));
    ir.add_node("second_success", pravaha::ExecutionDomain::CPU, pravaha::TaskCommand::make([]() {}));
    ir.add_node("downstream", pravaha::ExecutionDomain::CPU, pravaha::TaskCommand::make([]() {}));
    ir.add_edge(pravaha::TaskId{0}, pravaha::TaskId{2}, pravaha::EdgeKind::Sequence);
    ir.add_edge(pravaha::TaskId{1}, pravaha::TaskId{2}, pravaha::EdgeKind::Sequence);
    ir.add_join_group({pravaha::TaskId{0}, pravaha::TaskId{1}}, pravaha::JoinPolicy{pravaha::JoinPolicyKind::AllOrNothing, 0});

    auto state = pravaha::RuntimeState::build(ir);
    state.mark_succeeded(0);

    REQUIRE(state.node_states[2] == pravaha::TaskState::Created);
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

TEST_CASE("topological_order - repeated calls are stable", "[pravaha][litegraph]") {
    auto a = pravaha::task("A", []() {});
    auto b = pravaha::task("B", []() {});
    auto c = pravaha::task("C", []() {});
    auto expr = std::move(a) | std::move(b) | std::move(c);
    auto ir_result = pravaha::lower_to_ir(std::move(expr));
    REQUIRE(ir_result.has_value());

    auto order1 = pravaha::topological_order(ir_result.value());
    auto order2 = pravaha::topological_order(ir_result.value());
    REQUIRE(order1.has_value());
    REQUIRE(order2.has_value());
    REQUIRE(order1.value() == order2.value());
}

TEST_CASE("validate_ir_with_litegraph - mutation invalidates cached dependency graph", "[pravaha][litegraph][regression]") {
    pravaha::TaskIr ir;
    ir.add_node("A", pravaha::ExecutionDomain::CPU, pravaha::TaskCommand::make([]() {}));
    ir.add_node("B", pravaha::ExecutionDomain::CPU, pravaha::TaskCommand::make([]() {}));
    ir.add_edge(pravaha::TaskId{0}, pravaha::TaskId{1}, pravaha::EdgeKind::Sequence);

    auto first_validation = pravaha::validate_ir_with_litegraph(ir);
    REQUIRE(first_validation.has_value());

    ir.add_edge(pravaha::TaskId{1}, pravaha::TaskId{0}, pravaha::EdgeKind::Sequence);
    auto second_validation = pravaha::validate_ir_with_litegraph(ir);
    REQUIRE(!second_validation.has_value());
    REQUIRE(second_validation.error().kind == pravaha::ErrorKind::CycleDetected);

    auto topo_result = pravaha::topological_order(ir);
    REQUIRE(!topo_result.has_value());
    REQUIRE(topo_result.error().kind == pravaha::ErrorKind::CycleDetected);
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

struct ControlledBackend {
    mutable std::mutex mutex;
    std::deque<pravaha::TaskCommand> queue;
    bool stop_requested{false};

    bool submit(pravaha::TaskCommand cmd) {
        std::lock_guard lock(mutex);
        if (stop_requested) return false;
        queue.push_back(std::move(cmd));
        return true;
    }

    void drain() noexcept {}

    void request_stop() noexcept {
        std::lock_guard lock(mutex);
        stop_requested = true;
    }

    [[nodiscard]] bool stopped() const noexcept {
        std::lock_guard lock(mutex);
        return stop_requested;
    }

    [[nodiscard]] std::size_t queue_size() const {
        std::lock_guard lock(mutex);
        return queue.size();
    }

    bool run_front() {
        pravaha::TaskCommand cmd;
        {
            std::lock_guard lock(mutex);
            if (queue.empty()) return false;
            cmd = std::move(queue.front());
            queue.pop_front();
        }
        return cmd.run().has_value();
    }

    bool run_back() {
        pravaha::TaskCommand cmd;
        {
            std::lock_guard lock(mutex);
            if (queue.empty()) return false;
            cmd = std::move(queue.back());
            queue.pop_back();
        }
        return cmd.run().has_value();
    }
};

TEST_CASE("Runner controlled backend - early release semantics", "[pravaha][runner][parallel][early-release][controlled]") {
    auto wait_for_queue_at_least = [](ControlledBackend& backend, std::size_t min_size) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        while (std::chrono::steady_clock::now() < deadline) {
            if (backend.queue_size() >= min_size) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return backend.queue_size() >= min_size;
    };

    auto finish_runner = [](ControlledBackend& backend, auto& fut) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (fut.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready
               && std::chrono::steady_clock::now() < deadline) {
            if (!backend.run_front()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        return fut.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
    };

    SECTION("AnySuccess releases downstream before slow branch") {
        ControlledBackend backend;
        pravaha::Runner<ControlledBackend> runner(backend);
        std::vector<std::string> events;

        auto expr = pravaha::any_success(
            pravaha::task("fast_success", [&] { events.push_back("fast"); })
            &
            pravaha::task("slow_success", [&] { events.push_back("slow"); })
        ) | pravaha::task("downstream", [&] { events.push_back("downstream"); });

        auto fut = std::async(std::launch::async, [&runner, expr = std::move(expr)]() mutable {
            return runner.submit(std::move(expr));
        });

        REQUIRE(wait_for_queue_at_least(backend, 2));
        REQUIRE(backend.run_front());
        REQUIRE(wait_for_queue_at_least(backend, 2));
        REQUIRE(backend.run_back());
        REQUIRE(events.size() >= 2);
        REQUIRE(events[0] == "fast");
        REQUIRE(events[1] == "downstream");

        REQUIRE(finish_runner(backend, fut));
        auto result = fut.get();
        REQUIRE(result.has_value());
        REQUIRE(result->succeeded());
    }

    SECTION("Quorum<1> releases downstream before slow branch") {
        ControlledBackend backend;
        pravaha::Runner<ControlledBackend> runner(backend);
        std::vector<std::string> events;

        auto expr = pravaha::quorum<1>(
            pravaha::task("fast_success", [&] { events.push_back("fast"); })
            &
            pravaha::task("slow_success", [&] { events.push_back("slow"); })
        ) | pravaha::task("downstream", [&] { events.push_back("downstream"); });

        auto fut = std::async(std::launch::async, [&runner, expr = std::move(expr)]() mutable {
            return runner.submit(std::move(expr));
        });

        REQUIRE(wait_for_queue_at_least(backend, 2));
        REQUIRE(backend.run_front());
        REQUIRE(wait_for_queue_at_least(backend, 2));
        REQUIRE(backend.run_back());
        REQUIRE(events.size() >= 2);
        REQUIRE(events[0] == "fast");
        REQUIRE(events[1] == "downstream");

        REQUIRE(finish_runner(backend, fut));
        auto result = fut.get();
        REQUIRE(result.has_value());
        REQUIRE(result->succeeded());
    }

    SECTION("Quorum<2> waits for both successes") {
        ControlledBackend backend;
        pravaha::Runner<ControlledBackend> runner(backend);
        std::vector<std::string> events;

        auto expr = pravaha::quorum<2>(
            pravaha::task("a", [&] { events.push_back("a"); })
            &
            pravaha::task("b", [&] { events.push_back("b"); })
        ) | pravaha::task("downstream", [&] { events.push_back("downstream"); });

        auto fut = std::async(std::launch::async, [&runner, expr = std::move(expr)]() mutable {
            return runner.submit(std::move(expr));
        });

        REQUIRE(wait_for_queue_at_least(backend, 2));
        REQUIRE(backend.run_front());
        REQUIRE(backend.queue_size() == 1);
        REQUIRE(events.size() == 1);
        REQUIRE(events[0] == "a");

        REQUIRE(backend.run_front());
        REQUIRE(wait_for_queue_at_least(backend, 1));

        REQUIRE(finish_runner(backend, fut));
        auto result = fut.get();
        REQUIRE(result.has_value());
        REQUIRE(result->succeeded());
    }

    SECTION("CollectAll waits for both successes") {
        ControlledBackend backend;
        pravaha::Runner<ControlledBackend> runner(backend);
        std::vector<std::string> events;

        auto expr = pravaha::collect_all(
            pravaha::task("a", [&] { events.push_back("a"); })
            &
            pravaha::task("b", [&] { events.push_back("b"); })
        ) | pravaha::task("downstream", [&] { events.push_back("downstream"); });

        auto fut = std::async(std::launch::async, [&runner, expr = std::move(expr)]() mutable {
            return runner.submit(std::move(expr));
        });

        REQUIRE(wait_for_queue_at_least(backend, 2));
        REQUIRE(backend.run_front());
        REQUIRE(backend.queue_size() == 1);
        REQUIRE(events.size() == 1);
        REQUIRE(events[0] == "a");

        REQUIRE(backend.run_front());
        REQUIRE(wait_for_queue_at_least(backend, 1));

        REQUIRE(finish_runner(backend, fut));
        auto result = fut.get();
        REQUIRE(result.has_value());
        REQUIRE(result->succeeded());
    }

    SECTION("AllOrNothing waits for both successes") {
        ControlledBackend backend;
        pravaha::Runner<ControlledBackend> runner(backend);
        std::vector<std::string> events;

        auto expr = (
            pravaha::task("a", [&] { events.push_back("a"); })
            &
            pravaha::task("b", [&] { events.push_back("b"); })
        ) | pravaha::task("downstream", [&] { events.push_back("downstream"); });

        auto fut = std::async(std::launch::async, [&runner, expr = std::move(expr)]() mutable {
            return runner.submit(std::move(expr));
        });

        REQUIRE(wait_for_queue_at_least(backend, 2));
        REQUIRE(backend.run_front());
        REQUIRE(backend.queue_size() == 1);
        REQUIRE(events.size() == 1);
        REQUIRE(events[0] == "a");

        REQUIRE(backend.run_front());
        REQUIRE(wait_for_queue_at_least(backend, 1));

        REQUIRE(finish_runner(backend, fut));
        auto result = fut.get();
        REQUIRE(result.has_value());
        REQUIRE(result->succeeded());
    }
}

// ============================================================================
// SECTION 17: Parallel Semantics & Dependency Counters
// ============================================================================

TEST_CASE("Runner parallel - A & B both run", "[pravaha][runner][parallel]") {
    int a_ran = 0, b_ran = 0;
    pravaha::Runner<> runner;
    auto a = pravaha::task("A", [&a_ran]() { ++a_ran; });
    auto b = pravaha::task("B", [&b_ran]() { ++b_ran; });
    auto expr = std::move(a) & std::move(b);
    auto result = runner.submit(std::move(expr));
    REQUIRE(result.has_value());
    REQUIRE(a_ran == 1);
    REQUIRE(b_ran == 1);
    REQUIRE(result.value().final_state == pravaha::TaskState::Succeeded);
    REQUIRE(result.value().node_states.size() == 2);
    REQUIRE(result.value().node_states[0] == pravaha::TaskState::Succeeded);
    REQUIRE(result.value().node_states[1] == pravaha::TaskState::Succeeded);
}

TEST_CASE("Runner parallel - (A & B) | C runs C after both", "[pravaha][runner][parallel]") {
    std::atomic<int> a_done{0}, b_done{0}, c_start{0};
    pravaha::Runner<> runner;
    auto a = pravaha::task("A", [&]() { a_done.store(1); });
    auto b = pravaha::task("B", [&]() { b_done.store(1); });
    auto c = pravaha::task("C", [&]() { c_start.store(a_done.load() + b_done.load()); });
    auto expr = (std::move(a) & std::move(b)) | std::move(c);
    auto result = runner.submit(std::move(expr));
    REQUIRE(result.has_value());
    REQUIRE(c_start.load() == 2);
    REQUIRE(result.value().final_state == pravaha::TaskState::Succeeded);
}

TEST_CASE("Runner parallel - failure skips downstream", "[pravaha][runner][parallel]") {
    std::atomic<int> c_ran{0};
    pravaha::Runner<> runner;
    auto a = pravaha::task("A", []() { throw std::runtime_error("fail"); });
    auto b = pravaha::task("B", [&c_ran]() { ++c_ran; });
    auto expr = std::move(a) | std::move(b);
    auto result = runner.submit(std::move(expr));
    REQUIRE(result.has_value());
    REQUIRE(c_ran.load() == 0);
    REQUIRE(result.value().final_state == pravaha::TaskState::Failed);
    REQUIRE(result.value().node_states[1] == pravaha::TaskState::Skipped);
}

TEST_CASE("Runner parallel - no task runs twice", "[pravaha][runner][parallel]") {
    std::vector<int> run_counts(3, 0);
    pravaha::Runner<> runner;
    auto a = pravaha::task("A", [&run_counts]() { ++run_counts[0]; });
    auto b = pravaha::task("B", [&run_counts]() { ++run_counts[1]; });
    auto c = pravaha::task("C", [&run_counts]() { ++run_counts[2]; });
    auto expr = (std::move(a) & std::move(b)) | std::move(c);
    auto result = runner.submit(std::move(expr));
    REQUIRE(result.has_value());
    REQUIRE(run_counts[0] == 1);
    REQUIRE(run_counts[1] == 1);
    REQUIRE(run_counts[2] == 1);
}

// ============================================================================
// SECTION 18: CollectAll Join Policy
// ============================================================================

TEST_CASE("CollectAll - runs both branches even if A fails", "[pravaha][runner][collectall]") {
    int a_ran = 0, b_ran = 0;
    pravaha::Runner<> runner;
    auto a = pravaha::task("A", [&a_ran]() { ++a_ran; throw std::runtime_error("A failed"); });
    auto b = pravaha::task("B", [&b_ran]() { ++b_ran; });
    auto par = pravaha::collect_all(std::move(a) & std::move(b));
    auto result = runner.submit(std::move(par));
    REQUIRE(result.has_value());
    REQUIRE(a_ran == 1);
    REQUIRE(b_ran == 1);
    REQUIRE(result.value().final_state == pravaha::TaskState::Failed);
}

TEST_CASE("CollectAll - successful branches allow downstream continuation", "[pravaha][runner][collectall]") {
    int c_ran = 0;
    pravaha::Runner<> runner;
    auto a = pravaha::task("A", []() {});
    auto b = pravaha::task("B", []() {});
    auto c = pravaha::task("C", [&c_ran]() { ++c_ran; });
    auto par = pravaha::collect_all(std::move(a) & std::move(b));
    auto expr = std::move(par) | std::move(c);
    auto result = runner.submit(std::move(expr));
    REQUIRE(result.has_value());
    REQUIRE(c_ran == 1);
    REQUIRE(result->final_state == pravaha::TaskState::Succeeded);
    REQUIRE(result->node_states[2] == pravaha::TaskState::Succeeded);
}

TEST_CASE("CollectAll - errors from failing branches are recorded", "[pravaha][runner][collectall]") {
    pravaha::Runner<> runner;
    auto a = pravaha::task("A", []() { throw std::runtime_error("err_A"); });
    auto b = pravaha::task("B", []() { throw std::runtime_error("err_B"); });
    auto par = pravaha::collect_all(std::move(a) & std::move(b));
    auto result = runner.submit(std::move(par));
    REQUIRE(result.has_value());
    REQUIRE(result.value().errors.size() >= 2);
    const bool has_collect_all = std::any_of(result->errors.begin(), result->errors.end(), [](const auto& e) {
        return e.message.find("CollectAll") != std::string::npos
            || e.message.find("collect_all") != std::string::npos;
    });
    REQUIRE(has_collect_all);
    REQUIRE(result.value().final_state == pravaha::TaskState::Failed);
}

TEST_CASE("CollectAll - downstream C is skipped if any branch failed", "[pravaha][runner][collectall]") {
    int a_ran = 0;
    int b_ran = 0;
    int c_ran = 0;
    pravaha::Runner<> runner;
    auto a = pravaha::task("A", [&a_ran]() { ++a_ran; throw std::runtime_error("fail"); });
    auto b = pravaha::task("B", [&b_ran]() { ++b_ran; });
    auto c = pravaha::task("C", [&c_ran]() { ++c_ran; });
    auto par = pravaha::collect_all(std::move(a) & std::move(b));
    auto expr = std::move(par) | std::move(c);
    auto result = runner.submit(std::move(expr));
    REQUIRE(result.has_value());
    REQUIRE(a_ran == 1);
    REQUIRE(b_ran == 1);
    REQUIRE(c_ran == 0);
    REQUIRE(result.value().node_states[2] == pravaha::TaskState::Skipped);
    REQUIRE(result.value().final_state == pravaha::TaskState::Failed);
}

TEST_CASE("CollectAll - both failing branches still run", "[pravaha][runner][collectall]") {
    int a_ran = 0;
    int b_ran = 0;
    pravaha::Runner<> runner;
    auto a = pravaha::task("A", [&a_ran]() { ++a_ran; throw std::runtime_error("err_A"); });
    auto b = pravaha::task("B", [&b_ran]() { ++b_ran; throw std::runtime_error("err_B"); });
    auto par = pravaha::collect_all(std::move(a) & std::move(b));
    auto result = runner.submit(std::move(par));
    REQUIRE(result.has_value());
    REQUIRE(a_ran == 1);
    REQUIRE(b_ran == 1);
    REQUIRE(result->final_state == pravaha::TaskState::Failed);
}

TEST_CASE("CollectAll - normal AllOrNothing behavior remains unchanged", "[pravaha][runner][collectall]") {
    int b_ran = 0;
    pravaha::Runner<> runner;
    // Default is AllOrNothing - A fails, B should still run (parallel sibling, no dep)
    // But downstream C should be skipped
    auto a = pravaha::task("A", []() { throw std::runtime_error("fail"); });
    auto b = pravaha::task("B", [&b_ran]() { ++b_ran; });
    auto c = pravaha::task("C", []() {});
    auto expr = (std::move(a) & std::move(b)) | std::move(c);
    auto result = runner.submit(std::move(expr));
    REQUIRE(result.has_value());
    // B still runs because it's a sibling with no dependency on A
    REQUIRE(b_ran == 1);
    // C is skipped because A (predecessor via edge) failed
    REQUIRE(result.value().node_states[2] == pravaha::TaskState::Skipped);
    REQUIRE(result.value().final_state == pravaha::TaskState::Failed);
}

// ============================================================================
// SECTION 19: JThreadBackend
// ============================================================================

TEST_CASE("JThreadBackend - submit and drain executes command", "[pravaha][jthread]") {
    std::atomic<int> counter{0};
    {
        pravaha::JThreadBackend backend(2);
        backend.submit(pravaha::TaskCommand::make([&counter]() { counter.fetch_add(1); }));
        backend.drain();
    }
    REQUIRE(counter.load() == 1);
}

TEST_CASE("JThreadBackend - multiple commands execute", "[pravaha][jthread]") {
    std::atomic<int> counter{0};
    {
        pravaha::JThreadBackend backend(2);
        for (int i = 0; i < 10; ++i) {
            backend.submit(pravaha::TaskCommand::make([&counter]() { counter.fetch_add(1); }));
        }
        backend.drain();
    }
    REQUIRE(counter.load() == 10);
}

TEST_CASE("JThreadBackend - request_stop is safe", "[pravaha][jthread]") {
    pravaha::JThreadBackend backend(2);
    backend.request_stop();
    REQUIRE(backend.stopped());
}

TEST_CASE("JThreadBackend - destructor does not deadlock", "[pravaha][jthread]") {
    std::atomic<int> counter{0};
    {
        pravaha::JThreadBackend backend(2);
        backend.submit(pravaha::TaskCommand::make([&counter]() { counter.fetch_add(1); }));
        // destructor should join cleanly without deadlock
    }
    // Command may or may not have executed before stop, but no deadlock occurred
    REQUIRE(true);
}

TEST_CASE("JThreadBackend - no graph logic in backend", "[pravaha][jthread]") {
    std::atomic<int> val{0};
    {
        pravaha::JThreadBackend backend(1);
        auto cmd = pravaha::TaskCommand::make([&val]() { val.store(42); });
        backend.submit(std::move(cmd));
        backend.drain();
    }
    REQUIRE(val.load() == 42);
}

TEST_CASE("JThreadBackend - bounded queue rejects when full", "[pravaha][jthread]") {
    std::mutex gate_mutex;
    std::condition_variable gate_cv;
    bool started = false;
    bool release = false;

    pravaha::JThreadBackend backend(1, 1);
    REQUIRE(backend.submit(pravaha::TaskCommand::make([&]() {
        std::unique_lock lock(gate_mutex);
        started = true;
        gate_cv.notify_all();
        gate_cv.wait(lock, [&]() { return release; });
    })));

    {
        std::unique_lock lock(gate_mutex);
        gate_cv.wait(lock, [&]() { return started; });
    }

    REQUIRE(backend.submit(pravaha::TaskCommand::make([]() {})));
    REQUIRE_FALSE(backend.submit(pravaha::TaskCommand::make([]() {})));

    {
        std::lock_guard lock(gate_mutex);
        release = true;
    }
    gate_cv.notify_all();
    backend.drain();
}

TEST_CASE("JThreadBackend - submit after stop is rejected and drain returns", "[pravaha][jthread][regression]") {
    std::atomic<int> counter{0};
    pravaha::JThreadBackend backend(2);
    backend.request_stop();
    backend.submit(pravaha::TaskCommand::make([&counter]() { counter.fetch_add(1); }));
    backend.drain();
    REQUIRE(counter.load() == 0);
}

TEST_CASE("Runner<JThreadBackend> - stopped backend submit is reported and does not deadlock", "[pravaha][jthread][runner][regression]") {
    pravaha::JThreadBackend backend(2);
    backend.request_stop();
    pravaha::Runner<pravaha::JThreadBackend> runner(backend);

    auto a = pravaha::task("A", []() {});
    auto result = runner.submit(std::move(a));

    REQUIRE(result.has_value());
    REQUIRE(result->final_state == pravaha::TaskState::Failed);
    REQUIRE_FALSE(result->errors.empty());
    const auto err_kind = result->errors.front().kind;
    const bool accepted_kind =
        err_kind == pravaha::ErrorKind::QueueRejected
        || err_kind == pravaha::ErrorKind::TaskFailed;
    REQUIRE(accepted_kind);
}

TEST_CASE("Runner<JThreadBackend> - bounded queue rejection does not deadlock", "[pravaha][jthread][runner][regression]") {
    std::mutex gate_mutex;
    std::condition_variable gate_cv;
    bool started = false;
    bool release = false;

    pravaha::JThreadBackend backend(1, 1);
    REQUIRE(backend.submit(pravaha::TaskCommand::make([&]() {
        std::unique_lock lock(gate_mutex);
        started = true;
        gate_cv.notify_all();
        gate_cv.wait(lock, [&]() { return release; });
    })));

    {
        std::unique_lock lock(gate_mutex);
        gate_cv.wait(lock, [&]() { return started; });
    }

    REQUIRE(backend.submit(pravaha::TaskCommand::make([]() {})));

    pravaha::Runner<pravaha::JThreadBackend> runner(backend);
    auto result = runner.submit(pravaha::task("A", []() {}));

    REQUIRE(result.has_value());
    REQUIRE(result->final_state == pravaha::TaskState::Failed);
    REQUIRE_FALSE(result->errors.empty());
    REQUIRE(result->errors.front().kind == pravaha::ErrorKind::QueueRejected);

    {
        std::lock_guard lock(gate_mutex);
        release = true;
    }
    gate_cv.notify_all();
    backend.drain();
}

// ============================================================================
// SECTION 20: Runner with JThreadBackend
// ============================================================================

TEST_CASE("Runner<JThreadBackend> - sequence works", "[pravaha][jthread][runner]") {
    std::atomic<int> order_a{0}, order_b{0};
    std::atomic<int> seq{0};
    pravaha::JThreadBackend backend(2);
    pravaha::Runner<pravaha::JThreadBackend> runner(backend);
    auto a = pravaha::task("A", [&]() { order_a.store(seq.fetch_add(1)); });
    auto b = pravaha::task("B", [&]() { order_b.store(seq.fetch_add(1)); });
    auto expr = std::move(a) | std::move(b);
    auto result = runner.submit(std::move(expr));
    REQUIRE(result.has_value());
    REQUIRE(result.value().final_state == pravaha::TaskState::Succeeded);
    REQUIRE(order_a.load() < order_b.load());
}

TEST_CASE("Runner<JThreadBackend> - parallel independent tasks run", "[pravaha][jthread][runner]") {
    std::atomic<int> counter{0};
    pravaha::JThreadBackend backend(4);
    pravaha::Runner<pravaha::JThreadBackend> runner(backend);
    auto a = pravaha::task("A", [&counter]() { counter.fetch_add(1); });
    auto b = pravaha::task("B", [&counter]() { counter.fetch_add(1); });
    auto expr = std::move(a) & std::move(b);
    auto result = runner.submit(std::move(expr));
    REQUIRE(result.has_value());
    REQUIRE(counter.load() == 2);
    REQUIRE(result.value().final_state == pravaha::TaskState::Succeeded);
}

TEST_CASE("Runner<JThreadBackend> - (A & B) | C waits for both", "[pravaha][jthread][runner]") {
    std::atomic<int> a_done{0}, b_done{0}, c_start{0};
    pravaha::JThreadBackend backend(4);
    pravaha::Runner<pravaha::JThreadBackend> runner(backend);
    auto a = pravaha::task("A", [&]() { a_done.store(1); });
    auto b = pravaha::task("B", [&]() { b_done.store(1); });
    auto c = pravaha::task("C", [&]() { c_start.store(a_done.load() + b_done.load()); });
    auto expr = (std::move(a) & std::move(b)) | std::move(c);
    auto result = runner.submit(std::move(expr));
    REQUIRE(result.has_value());
    REQUIRE(c_start.load() == 2);
    REQUIRE(result.value().final_state == pravaha::TaskState::Succeeded);
}

TEST_CASE("Runner<JThreadBackend> - failure skips downstream", "[pravaha][jthread][runner]") {
    std::atomic<int> c_ran{0};
    pravaha::JThreadBackend backend(2);
    pravaha::Runner<pravaha::JThreadBackend> runner(backend);
    auto a = pravaha::task("A", []() { throw std::runtime_error("fail"); });
    auto b = pravaha::task("B", [&c_ran]() { ++c_ran; });
    auto expr = std::move(a) | std::move(b);
    auto result = runner.submit(std::move(expr));
    REQUIRE(result.has_value());
    REQUIRE(c_ran.load() == 0);
    REQUIRE(result.value().final_state == pravaha::TaskState::Failed);
    REQUIRE(result.value().node_states[1] == pravaha::TaskState::Skipped);
}

TEST_CASE("Runner<JThreadBackend> - no deadlocks on repeated runs", "[pravaha][jthread][runner]") {
    pravaha::JThreadBackend backend(2);
    pravaha::Runner<pravaha::JThreadBackend> runner(backend);
    for (int run = 0; run < 5; ++run) {
        std::atomic<int> counter{0};
        auto a = pravaha::task("A", [&counter]() { counter.fetch_add(1); });
        auto b = pravaha::task("B", [&counter]() { counter.fetch_add(1); });
        auto expr = std::move(a) & std::move(b);
        auto result = runner.submit(std::move(expr));
        REQUIRE(result.has_value());
        REQUIRE(counter.load() == 2);
        REQUIRE(result.value().final_state == pravaha::TaskState::Succeeded);
    }
}

TEST_CASE("Runner<JThreadBackend> - repeated runs do not share stale state", "[pravaha][jthread][runner]") {
    pravaha::JThreadBackend backend(2);
    pravaha::Runner<pravaha::JThreadBackend> runner(backend);

    // First run: succeed
    {
        auto a = pravaha::task("A", []() {});
        auto result = runner.submit(std::move(a));
        REQUIRE(result.has_value());
        REQUIRE(result.value().final_state == pravaha::TaskState::Succeeded);
    }
    // Second run: fail
    {
        auto a = pravaha::task("A", []() { throw std::runtime_error("fail"); });
        auto result = runner.submit(std::move(a));
        REQUIRE(result.has_value());
        REQUIRE(result.value().final_state == pravaha::TaskState::Failed);
    }
    // Third run: succeed again
    {
        auto a = pravaha::task("A", []() {});
        auto result = runner.submit(std::move(a));
        REQUIRE(result.has_value());
        REQUIRE(result.value().final_state == pravaha::TaskState::Succeeded);
    }
}

// ============================================================================
// SECTION 20.5: Runner Policy Slots
// ============================================================================

struct CountingGraphPolicy {
    static inline int validate_calls = 0;

    static pravaha::Outcome<pravaha::Unit> validate(const pravaha::TaskIr& ir) {
        ++validate_calls;
        return pravaha::DefaultGraphAlgorithmPolicy::validate(ir);
    }

    static pravaha::Outcome<std::vector<pravaha::TaskId>> topological_order(const pravaha::TaskIr& ir) {
        return pravaha::DefaultGraphAlgorithmPolicy::topological_order(ir);
    }
};

struct CountingNoProgressPolicy {
    static inline int checks = 0;

    template <class SharedSchedulerStateLike>
    static bool handle_no_progress(SharedSchedulerStateLike& sstate) {
        ++checks;
        return pravaha::DefaultNoProgressPolicy::handle_no_progress(sstate);
    }
};

struct CountingReadyPolicy {
    static inline int ready_checks = 0;

    template <class RuntimeStateLike>
    static bool is_ready(const RuntimeStateLike& state, std::size_t index) {
        ++ready_checks;
        return pravaha::DefaultReadyPolicy::is_ready(state, index);
    }
};

struct CompileOnlyObserver {
    static constexpr bool enabled = true;
    static inline int task_calls = 0;
    static inline int join_calls = 0;
    static inline int graph_calls = 0;

    static void on_task_event(const pravaha::TaskEvent&) noexcept { ++task_calls; }
    static void on_join_event(const pravaha::JoinEvent&) noexcept { ++join_calls; }
    static void on_graph_event(const pravaha::GraphEvent&) noexcept { ++graph_calls; }
};

struct DisabledObserver {
    static constexpr bool enabled = false;
    static inline int task_calls = 0;
    static inline int join_calls = 0;
    static inline int graph_calls = 0;

    static void reset() {
        task_calls = 0;
        join_calls = 0;
        graph_calls = 0;
    }

    static void on_task_event(const pravaha::TaskEvent&) noexcept { ++task_calls; }
    static void on_join_event(const pravaha::JoinEvent&) noexcept { ++join_calls; }
    static void on_graph_event(const pravaha::GraphEvent&) noexcept { ++graph_calls; }
};

struct EnabledObserver {
    static constexpr bool enabled = true;
    static inline int task_calls = 0;
    static inline int join_calls = 0;
    static inline int graph_calls = 0;

    static void reset() {
        task_calls = 0;
        join_calls = 0;
        graph_calls = 0;
    }

    static void on_task_event(const pravaha::TaskEvent&) noexcept { ++task_calls; }
    static void on_join_event(const pravaha::JoinEvent&) noexcept { ++join_calls; }
    static void on_graph_event(const pravaha::GraphEvent&) noexcept { ++graph_calls; }
};

struct QueueWaitObserver {
    static constexpr bool enabled = true;
    static inline std::mutex mutex{};
    static inline std::vector<pravaha::TaskEvent> task_events{};

    static void reset() {
        std::lock_guard lock(mutex);
        task_events.clear();
    }

    static void on_task_event(const pravaha::TaskEvent& e) noexcept {
        std::lock_guard lock(mutex);
        task_events.push_back(e);
    }

    static void on_join_event(const pravaha::JoinEvent&) noexcept {}
    static void on_graph_event(const pravaha::GraphEvent&) noexcept {}
};

struct TestObserver {
    static constexpr bool enabled = true;
    static inline int lowered = 0;
    static inline int validated = 0;
    static inline std::vector<pravaha::EventKind> task_events{};
    static inline std::vector<pravaha::TaskEvent> task_event_records{};
    static inline std::vector<pravaha::JoinEvent> join_events{};
    static inline std::vector<pravaha::GraphEvent> graph_events{};

    static void reset() {
        lowered = 0;
        validated = 0;
        task_events.clear();
        task_event_records.clear();
        join_events.clear();
        graph_events.clear();
    }

    static void on_task_event(const pravaha::TaskEvent& e) noexcept {
        task_events.push_back(e.kind);
        task_event_records.push_back(e);
    }
    static void on_join_event(const pravaha::JoinEvent& e) noexcept { join_events.push_back(e); }
    static void on_graph_event(const pravaha::GraphEvent& e) noexcept {
        graph_events.push_back(e);
        if (e.kind == pravaha::EventKind::GraphLowered) ++lowered;
        if (e.kind == pravaha::EventKind::GraphValidated) ++validated;
    }
};

struct TraceObserver {
    static constexpr bool enabled = true;
    static inline std::vector<std::string> trace{};

    static void reset() {
        trace.clear();
    }

    static void on_task_event(const pravaha::TaskEvent& e) noexcept {
        std::string kind;
        switch (e.kind) {
        case pravaha::EventKind::TaskReady:
            kind = "ready";
            break;
        case pravaha::EventKind::TaskScheduled:
            kind = "scheduled";
            break;
        case pravaha::EventKind::TaskStarted:
            kind = "started";
            break;
        case pravaha::EventKind::TaskCompleted:
            kind = "completed";
            break;
        case pravaha::EventKind::TaskFailed:
            kind = "failed";
            break;
        case pravaha::EventKind::TaskSkipped:
            kind = "skipped";
            break;
        case pravaha::EventKind::TaskCanceled:
            kind = "canceled";
            break;
        default:
            return;
        }
        trace.push_back("task:" + kind + ":" + std::string(e.task_name));
    }

    static void on_join_event(const pravaha::JoinEvent& e) noexcept {
        if (e.kind == pravaha::EventKind::JoinResolved) {
            trace.push_back(std::string{"join:resolved:"} + (e.success ? "success" : "failure"));
        }
    }

    static void on_graph_event(const pravaha::GraphEvent& e) noexcept {
        if (e.kind == pravaha::EventKind::GraphLowered) {
            trace.push_back("graph:lowered");
        } else if (e.kind == pravaha::EventKind::GraphValidated) {
            trace.push_back("graph:validated");
        }
    }
};

struct BadObserver {
    static constexpr bool enabled = true;
    static void on_task_event(const pravaha::TaskEvent&) noexcept {}
    static void on_join_event(const pravaha::JoinEvent&) noexcept {}
};

static_assert(pravaha::ObserverPolicy<pravaha::NoObserver>);
static_assert(pravaha::ObserverPolicy<TestObserver>);
static_assert(!pravaha::ObserverPolicy<BadObserver>);
static_assert(pravaha::ObserverPolicy<DisabledObserver>);
static_assert(pravaha::ObserverPolicy<EnabledObserver>);

TEST_CASE("Runner policy slot - custom GraphAlgorithmPolicy is used", "[pravaha][runner][policy]") {
    CountingGraphPolicy::validate_calls = 0;
    pravaha::Runner<pravaha::InlineBackend, CountingGraphPolicy> runner;
    auto a = pravaha::task("A", []() {});
    auto b = pravaha::task("B", []() {});
    auto expr = std::move(a) | std::move(b);
    auto result = runner.submit(std::move(expr));
    REQUIRE(result.has_value());
    REQUIRE(CountingGraphPolicy::validate_calls > 0);
}

TEST_CASE("Runner policy slot - custom NoProgressPolicy is used", "[pravaha][runner][policy]") {
    CountingNoProgressPolicy::checks = 0;
    pravaha::Runner<pravaha::InlineBackend,
                    pravaha::DefaultGraphAlgorithmPolicy,
                    pravaha::DefaultReadyPolicy,
                    CountingNoProgressPolicy> runner;
    auto a = pravaha::task("A", []() {});
    auto result = runner.submit(std::move(a));
    REQUIRE(result.has_value());
    REQUIRE(CountingNoProgressPolicy::checks > 0);
}

TEST_CASE("Runner policy slot - custom ReadyPolicy is used", "[pravaha][runner][policy]") {
    CountingReadyPolicy::ready_checks = 0;
    pravaha::Runner<pravaha::InlineBackend,
                    pravaha::DefaultGraphAlgorithmPolicy,
                    CountingReadyPolicy> runner;
    auto a = pravaha::task("A", []() {});
    auto b = pravaha::task("B", []() {});
    auto expr = std::move(a) | std::move(b);
    auto result = runner.submit(std::move(expr));
    REQUIRE(result.has_value());
    REQUIRE(CountingReadyPolicy::ready_checks > 0);
}

TEST_CASE("Runner policy slot - default Runner compiles and runs", "[pravaha][runner][policy]") {
    STATIC_REQUIRE(std::is_same_v<typename pravaha::Runner<>::observer_type, pravaha::NoObserver>);
    pravaha::Runner<> runner;
    auto a = pravaha::task("A", []() {});
    auto b = pravaha::task("B", []() {});
    auto expr = std::move(a) | std::move(b);
    auto result = runner.submit(std::move(expr));
    REQUIRE(result.has_value());
    REQUIRE(result->final_state == pravaha::TaskState::Succeeded);
}

TEST_CASE("Runner policy slot - explicit NoObserver slot compiles", "[pravaha][runner][policy]") {
    using ExplicitNoObserverRunner = pravaha::Runner<
        pravaha::InlineBackend,
        pravaha::DefaultGraphAlgorithmPolicy,
        pravaha::DefaultReadyPolicy,
        pravaha::DefaultNoProgressPolicy,
        pravaha::NoObserver>;

    STATIC_REQUIRE(std::is_same_v<typename ExplicitNoObserverRunner::observer_type, pravaha::NoObserver>);
    ExplicitNoObserverRunner runner;
    auto result = runner.submit(pravaha::task("A", []() {}));
    REQUIRE(result.has_value());
}

TEST_CASE("Runner policy slot - custom Observer slot compiles", "[pravaha][runner][policy]") {
    using CustomObserverRunner = pravaha::Runner<
        pravaha::InlineBackend,
        pravaha::DefaultGraphAlgorithmPolicy,
        pravaha::DefaultReadyPolicy,
        pravaha::DefaultNoProgressPolicy,
        CompileOnlyObserver>;

    CompileOnlyObserver::task_calls = 0;
    CompileOnlyObserver::join_calls = 0;
    CompileOnlyObserver::graph_calls = 0;

    STATIC_REQUIRE(std::is_same_v<typename CustomObserverRunner::observer_type, CompileOnlyObserver>);
    CustomObserverRunner runner;
    auto result = runner.submit(pravaha::task("A", []() {}));
    REQUIRE(result.has_value());
    REQUIRE(CompileOnlyObserver::task_calls == 4);
    REQUIRE(CompileOnlyObserver::join_calls == 0);
    REQUIRE(CompileOnlyObserver::graph_calls == 2);
}

TEST_CASE("Runner policy slot - disabled observer emits no events", "[pravaha][runner][policy]") {
    using DisabledRunner = pravaha::Runner<
        pravaha::InlineBackend,
        pravaha::DefaultGraphAlgorithmPolicy,
        pravaha::DefaultReadyPolicy,
        pravaha::DefaultNoProgressPolicy,
        DisabledObserver>;

    DisabledObserver::reset();

    DisabledRunner runner;
    auto result = runner.submit(pravaha::task("A", []() {}) | pravaha::task("B", []() {}));

    REQUIRE(result.has_value());
    REQUIRE(DisabledObserver::task_calls == 0);
    REQUIRE(DisabledObserver::join_calls == 0);
    REQUIRE(DisabledObserver::graph_calls == 0);
}

TEST_CASE("Runner policy slot - enabled observer emits events", "[pravaha][runner][policy]") {
    using EnabledRunner = pravaha::Runner<
        pravaha::InlineBackend,
        pravaha::DefaultGraphAlgorithmPolicy,
        pravaha::DefaultReadyPolicy,
        pravaha::DefaultNoProgressPolicy,
        EnabledObserver>;

    EnabledObserver::reset();

    EnabledRunner runner;
    auto result = runner.submit(pravaha::task("A", []() {}) | pravaha::task("B", []() {}));

    REQUIRE(result.has_value());
    REQUIRE(EnabledObserver::task_calls > 0);
    REQUIRE(EnabledObserver::graph_calls > 0);
}

TEST_CASE("Runner emits GraphLowered and GraphValidated events", "[pravaha][runner][observer]") {
    using ObservedRunner = pravaha::Runner<
        pravaha::InlineBackend,
        pravaha::DefaultGraphAlgorithmPolicy,
        pravaha::DefaultReadyPolicy,
        pravaha::DefaultNoProgressPolicy,
        TestObserver>;

    TestObserver::reset();

    ObservedRunner runner;
    auto result = runner.submit(pravaha::task("a", []() {}));

    REQUIRE(result.has_value());
    REQUIRE(TestObserver::lowered == 1);
    REQUIRE(TestObserver::validated == 1);
}

TEST_CASE("Runner emits task lifecycle events for successful task", "[pravaha][runner][observer]") {
    using ObservedRunner = pravaha::Runner<
        pravaha::InlineBackend,
        pravaha::DefaultGraphAlgorithmPolicy,
        pravaha::DefaultReadyPolicy,
        pravaha::DefaultNoProgressPolicy,
        TestObserver>;

    TestObserver::reset();

    ObservedRunner runner;
    auto result = runner.submit(pravaha::task("a", []() {}));

    REQUIRE(result.has_value());
    REQUIRE(TestObserver::task_events == std::vector<pravaha::EventKind>{
        pravaha::EventKind::TaskReady,
        pravaha::EventKind::TaskScheduled,
        pravaha::EventKind::TaskStarted,
        pravaha::EventKind::TaskCompleted
    });
}

TEST_CASE("Runner emits TaskStarted and TaskFailed for failing task", "[pravaha][runner][observer]") {
    using ObservedRunner = pravaha::Runner<
        pravaha::InlineBackend,
        pravaha::DefaultGraphAlgorithmPolicy,
        pravaha::DefaultReadyPolicy,
        pravaha::DefaultNoProgressPolicy,
        TestObserver>;

    TestObserver::reset();

    ObservedRunner runner;
    auto result = runner.submit(pravaha::task("bad", []() -> pravaha::Outcome<pravaha::Unit> {
        return std::unexpected(pravaha::PravahaError{pravaha::ErrorKind::TaskFailed, "bad"});
    }));

    REQUIRE(result.has_value());
    const auto started_it = std::find(TestObserver::task_events.begin(), TestObserver::task_events.end(), pravaha::EventKind::TaskStarted);
    const auto failed_it = std::find(TestObserver::task_events.begin(), TestObserver::task_events.end(), pravaha::EventKind::TaskFailed);
    REQUIRE(started_it != TestObserver::task_events.end());
    REQUIRE(failed_it != TestObserver::task_events.end());
    REQUIRE(started_it < failed_it);
}

TEST_CASE("Runner emits TaskSkipped for downstream task after failure", "[pravaha][runner][observer]") {
    using ObservedRunner = pravaha::Runner<
        pravaha::InlineBackend,
        pravaha::DefaultGraphAlgorithmPolicy,
        pravaha::DefaultReadyPolicy,
        pravaha::DefaultNoProgressPolicy,
        TestObserver>;

    TestObserver::reset();

    int after_ran = 0;
    ObservedRunner runner;
    auto result = runner.submit(
        pravaha::task("bad", []() -> pravaha::Outcome<pravaha::Unit> {
            return std::unexpected(pravaha::PravahaError{pravaha::ErrorKind::TaskFailed, "bad"});
        })
        |
        pravaha::task("after", [&after_ran]() { ++after_ran; })
    );

    REQUIRE(result.has_value());
    REQUIRE(after_ran == 0);
    REQUIRE(std::count(TestObserver::task_events.begin(), TestObserver::task_events.end(), pravaha::EventKind::TaskFailed) == 1);
    REQUIRE(std::count(TestObserver::task_events.begin(), TestObserver::task_events.end(), pravaha::EventKind::TaskSkipped) == 1);
    const auto failed_it = std::find(TestObserver::task_events.begin(), TestObserver::task_events.end(), pravaha::EventKind::TaskFailed);
    const auto skipped_it = std::find(TestObserver::task_events.begin(), TestObserver::task_events.end(), pravaha::EventKind::TaskSkipped);
    REQUIRE(failed_it != TestObserver::task_events.end());
    REQUIRE(skipped_it != TestObserver::task_events.end());
    REQUIRE(failed_it < skipped_it);
}

TEST_CASE("Runner emits JoinResolved for AllOrNothing success", "[pravaha][runner][observer]") {
    using ObservedRunner = pravaha::Runner<
        pravaha::InlineBackend,
        pravaha::DefaultGraphAlgorithmPolicy,
        pravaha::DefaultReadyPolicy,
        pravaha::DefaultNoProgressPolicy,
        TestObserver>;

    TestObserver::reset();
    ObservedRunner runner;
    auto result = runner.submit(pravaha::task("a", []() {}) & pravaha::task("b", []() {}));

    REQUIRE(result.has_value());
    REQUIRE(TestObserver::join_events.size() == 1);
    const auto& e = TestObserver::join_events.front();
    REQUIRE(e.kind == pravaha::EventKind::JoinResolved);
    REQUIRE(e.policy.kind == pravaha::JoinPolicyKind::AllOrNothing);
    REQUIRE(e.success);
    REQUIRE(e.expected == 2);
    REQUIRE(e.succeeded == 2);
    REQUIRE(e.failed == 0);
    REQUIRE(e.canceled == 0);
    REQUIRE(e.skipped == 0);
}

TEST_CASE("Runner emits JoinResolved for CollectAll failure", "[pravaha][runner][observer]") {
    using ObservedRunner = pravaha::Runner<
        pravaha::InlineBackend,
        pravaha::DefaultGraphAlgorithmPolicy,
        pravaha::DefaultReadyPolicy,
        pravaha::DefaultNoProgressPolicy,
        TestObserver>;

    TestObserver::reset();
    ObservedRunner runner;
    auto result = runner.submit(pravaha::collect_all(
        pravaha::task("ok", []() {})
        &
        pravaha::task("fail", []() -> pravaha::Outcome<pravaha::Unit> {
            return std::unexpected(pravaha::PravahaError{pravaha::ErrorKind::TaskFailed, "fail"});
        })
    ));

    REQUIRE(result.has_value());
    REQUIRE(TestObserver::join_events.size() == 1);
    const auto& e = TestObserver::join_events.front();
    REQUIRE(e.kind == pravaha::EventKind::JoinResolved);
    REQUIRE(e.policy.kind == pravaha::JoinPolicyKind::CollectAll);
    REQUIRE_FALSE(e.success);
    REQUIRE(e.expected == 2);
    REQUIRE(e.succeeded == 1);
    REQUIRE(e.failed == 1);
    REQUIRE(e.canceled == 0);
    REQUIRE(e.skipped == 0);
}

TEST_CASE("Runner emits one JoinResolved for AnySuccess", "[pravaha][runner][observer]") {
    using ObservedRunner = pravaha::Runner<
        pravaha::InlineBackend,
        pravaha::DefaultGraphAlgorithmPolicy,
        pravaha::DefaultReadyPolicy,
        pravaha::DefaultNoProgressPolicy,
        TestObserver>;

    TestObserver::reset();
    ObservedRunner runner;
    auto result = runner.submit(pravaha::any_success(
        pravaha::task("ok", []() {})
        &
        pravaha::task("fail", []() -> pravaha::Outcome<pravaha::Unit> {
            return std::unexpected(pravaha::PravahaError{pravaha::ErrorKind::TaskFailed, "fail"});
        })
    ));

    REQUIRE(result.has_value());
    REQUIRE(TestObserver::join_events.size() == 1);
    const auto& e = TestObserver::join_events.front();
    REQUIRE(e.kind == pravaha::EventKind::JoinResolved);
    REQUIRE(e.policy.kind == pravaha::JoinPolicyKind::AnySuccess);
    REQUIRE(e.success);
}

TEST_CASE("Runner emits one JoinResolved for Quorum<1>", "[pravaha][runner][observer]") {
    using ObservedRunner = pravaha::Runner<
        pravaha::InlineBackend,
        pravaha::DefaultGraphAlgorithmPolicy,
        pravaha::DefaultReadyPolicy,
        pravaha::DefaultNoProgressPolicy,
        TestObserver>;

    TestObserver::reset();
    ObservedRunner runner;
    auto result = runner.submit(pravaha::quorum<1>(
        pravaha::task("ok", []() {})
        &
        pravaha::task("fail", []() -> pravaha::Outcome<pravaha::Unit> {
            return std::unexpected(pravaha::PravahaError{pravaha::ErrorKind::TaskFailed, "fail"});
        })
    ));

    REQUIRE(result.has_value());
    REQUIRE(TestObserver::join_events.size() == 1);
    const auto& e = TestObserver::join_events.front();
    REQUIRE(e.kind == pravaha::EventKind::JoinResolved);
    REQUIRE(e.policy.kind == pravaha::JoinPolicyKind::Quorum);
    REQUIRE(e.success);
}

TEST_CASE("Runner emits non-zero timestamps for task/join/graph events", "[pravaha][runner][observer]") {
    using ObservedRunner = pravaha::Runner<
        pravaha::InlineBackend,
        pravaha::DefaultGraphAlgorithmPolicy,
        pravaha::DefaultReadyPolicy,
        pravaha::DefaultNoProgressPolicy,
        TestObserver>;

    TestObserver::reset();
    ObservedRunner runner;
    auto result = runner.submit(pravaha::any_success(
        pravaha::task("ok", []() {})
        &
        pravaha::task("fail", []() -> pravaha::Outcome<pravaha::Unit> {
            return std::unexpected(pravaha::PravahaError{pravaha::ErrorKind::TaskFailed, "fail"});
        })
    ));

    REQUIRE(result.has_value());
    REQUIRE_FALSE(TestObserver::task_event_records.empty());
    REQUIRE_FALSE(TestObserver::join_events.empty());
    REQUIRE_FALSE(TestObserver::graph_events.empty());
    REQUIRE(std::all_of(TestObserver::task_event_records.begin(), TestObserver::task_event_records.end(),
        [](const pravaha::TaskEvent& e) { return e.timestamp_ns > 0; }));
    REQUIRE(std::all_of(TestObserver::join_events.begin(), TestObserver::join_events.end(),
        [](const pravaha::JoinEvent& e) { return e.timestamp_ns > 0; }));
    REQUIRE(std::all_of(TestObserver::graph_events.begin(), TestObserver::graph_events.end(),
        [](const pravaha::GraphEvent& e) { return e.timestamp_ns > 0; }));
}

TEST_CASE("TraceObserver captures compact external trace", "[pravaha][runner][observer]") {
    using TraceRunner = pravaha::Runner<
        pravaha::InlineBackend,
        pravaha::DefaultGraphAlgorithmPolicy,
        pravaha::DefaultReadyPolicy,
        pravaha::DefaultNoProgressPolicy,
        TraceObserver>;

    TraceObserver::reset();

    TraceRunner runner;
    auto result = runner.submit(
        (pravaha::task("a", []() {}) & pravaha::task("b", []() {}))
        | pravaha::task("c", []() {})
    );

    REQUIRE(result.has_value());
    REQUIRE(std::find(TraceObserver::trace.begin(), TraceObserver::trace.end(), "graph:lowered") != TraceObserver::trace.end());
    REQUIRE(std::find(TraceObserver::trace.begin(), TraceObserver::trace.end(), "graph:validated") != TraceObserver::trace.end());
    REQUIRE(std::find(TraceObserver::trace.begin(), TraceObserver::trace.end(), "task:scheduled:a") != TraceObserver::trace.end());
    REQUIRE(std::find(TraceObserver::trace.begin(), TraceObserver::trace.end(), "task:scheduled:b") != TraceObserver::trace.end());
    REQUIRE(std::find(TraceObserver::trace.begin(), TraceObserver::trace.end(), "task:scheduled:c") != TraceObserver::trace.end());
    REQUIRE(std::find(TraceObserver::trace.begin(), TraceObserver::trace.end(), "join:resolved:success") != TraceObserver::trace.end());
}

TEST_CASE("Runner<JThreadBackend> exposes queue wait timing via task timestamps", "[pravaha][jthread][runner][observer]") {
    using ObservedRunner = pravaha::Runner<
        pravaha::JThreadBackend,
        pravaha::DefaultGraphAlgorithmPolicy,
        pravaha::DefaultReadyPolicy,
        pravaha::DefaultNoProgressPolicy,
        QueueWaitObserver>;

    QueueWaitObserver::reset();

    pravaha::JThreadBackend backend(2);
    ObservedRunner runner(backend);
    auto result = runner.submit(pravaha::task("a", []() {}) | pravaha::task("b", []() {}));

    REQUIRE(result.has_value());

    std::vector<std::pair<pravaha::TaskId, std::uint64_t>> scheduled;
    std::vector<std::pair<pravaha::TaskId, std::uint64_t>> started;
    {
        std::lock_guard lock(QueueWaitObserver::mutex);
        for (const auto& e : QueueWaitObserver::task_events) {
            if (e.kind == pravaha::EventKind::TaskScheduled) {
                REQUIRE(e.timestamp_ns > 0);
                scheduled.emplace_back(e.task_id, e.timestamp_ns);
            }
            if (e.kind == pravaha::EventKind::TaskStarted) {
                REQUIRE(e.timestamp_ns > 0);
                started.emplace_back(e.task_id, e.timestamp_ns);
            }
        }
    }

    REQUIRE(scheduled.size() >= 2);
    REQUIRE(started.size() >= 2);

    for (const auto& [task_id, started_ns] : started) {
        const auto it = std::find_if(scheduled.begin(), scheduled.end(), [task_id](const auto& p) {
            return p.first == task_id;
        });
        REQUIRE(it != scheduled.end());
        REQUIRE(started_ns >= it->second);
    }
}

