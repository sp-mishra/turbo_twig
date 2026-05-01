// ============================================================================
// test_pravaha.cpp — Unit tests for pravaha.hpp foundational skeleton
// ============================================================================
// Tests for: ErrorKind, PravahaError, Outcome, TaskState, JoinPolicyKind,
//            ExecutionDomain, Payload concepts, SerializablePayload concept
// ============================================================================

#include "catch_amalgamated.hpp"
#include "pravaha/pravaha.hpp"

#include <string>
#include <vector>
#include <memory>

// ============================================================================
// Test types for concept validation
// ============================================================================

// A trivially copyable, standard-layout aggregate (should satisfy SerializablePayload)
struct TrivialPoint {
    double x;
    double y;
};

// A non-trivially-copyable type (should NOT satisfy SerializablePayload)
struct NonTrivial {
    std::string name;
    int value;
};

// A move-only type (should satisfy Payload and LocalPayload but NOT TransferablePayload)
struct MoveOnly {
    std::unique_ptr<int> data;

    MoveOnly() = default;
    MoveOnly(MoveOnly&&) = default;
    MoveOnly& operator=(MoveOnly&&) = default;
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;
};

// A type with a pointer field (should NOT satisfy SerializablePayload via meta)
struct WithPointer {
    int* ptr;
    int  val;
};

// A plain int-like wrapper (should satisfy SerializablePayload)
struct IntWrapper {
    int value;
};

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
    // Ensure all enum values are usable
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
    auto err = pravaha::PravahaError{
        pravaha::ErrorKind::TaskFailed,
        "something went wrong",
        "task_42"
    };

    REQUIRE(err.kind == pravaha::ErrorKind::TaskFailed);
    REQUIRE(err.message == "something went wrong");
    REQUIRE(err.task_identity == "task_42");
    REQUIRE(err.location.line() > 0);
}

TEST_CASE("PravahaError::make factory", "[pravaha][error]") {
    auto err = pravaha::PravahaError::make(
        pravaha::ErrorKind::CycleDetected,
        "cycle found in DAG"
    );

    REQUIRE(err.kind == pravaha::ErrorKind::CycleDetected);
    REQUIRE(err.message == "cycle found in DAG");
    REQUIRE(err.task_identity.empty());
}

TEST_CASE("PravahaError::make_for_task factory", "[pravaha][error]") {
    auto err = pravaha::PravahaError::make_for_task(
        pravaha::ErrorKind::Timeout,
        "deadline exceeded",
        "expensive_task"
    );

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
    pravaha::Outcome<int> result = std::unexpected(
        pravaha::PravahaError{pravaha::ErrorKind::TaskFailed, "oops"}
    );

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
    auto step1 = []() -> pravaha::Outcome<int> {
        return std::unexpected(
            pravaha::PravahaError{pravaha::ErrorKind::InternalError, "fail"}
        );
    };
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
        pravaha::TaskState::Created,
        pravaha::TaskState::Ready,
        pravaha::TaskState::Scheduled,
        pravaha::TaskState::Running,
        pravaha::TaskState::Succeeded,
        pravaha::TaskState::Failed,
        pravaha::TaskState::Canceled,
        pravaha::TaskState::Skipped
    };

    REQUIRE(states.size() == 8);
    // All values are distinct
    for (std::size_t i = 0; i < states.size(); ++i) {
        for (std::size_t j = i + 1; j < states.size(); ++j) {
            REQUIRE(states[i] != states[j]);
        }
    }
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
        pravaha::ExecutionDomain::Inline,
        pravaha::ExecutionDomain::CPU,
        pravaha::ExecutionDomain::IO,
        pravaha::ExecutionDomain::Fiber,
        pravaha::ExecutionDomain::Coroutine,
        pravaha::ExecutionDomain::External
    };

    REQUIRE(domains.size() == 6);
    for (std::size_t i = 0; i < domains.size(); ++i) {
        for (std::size_t j = i + 1; j < domains.size(); ++j) {
            REQUIRE(domains[i] != domains[j]);
        }
    }
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
    // WithPointer has a pointer field; meta::is_zero_copy_serializable rejects it
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
    auto no_arg_task = []() -> pravaha::Outcome<pravaha::Unit> {
        return pravaha::Unit{};
    };
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