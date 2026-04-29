# Lithe EDSL

## Introduction

Lithe is a header-only, embedded domain-specific language (EDSL) for composing and matching simple
grammars directly in C++. It lives entirely in `include/edsl/lithe.hpp` and has no external
dependencies beyond the C++20 standard library. The name "lithe" reflects the library's goal:
a lightweight, flexible grammar toolkit that bends easily to the shape of the problem at hand.

---

## Executive Summary

Building ad-hoc parsers or validators in C++ typically means writing repetitive, error-prone
character-scanning code or reaching for a heavy parser-generator framework. Lithe offers a
middle path: a small, composable API for describing token-level grammars as first-class C++
objects, then running a recursive-descent matcher against an input string.

Key characteristics:

| Property | Detail |
|---|---|
| Language standard | C++20 (uses `std::ranges`, `std::string_view::starts_with`, structured bindings) |
| Delivery | Single header — `include/edsl/lithe.hpp` |
| Namespace | `edsl` |
| Allocation model | `std::shared_ptr<Expr>` tree |
| Matching strategy | Recursive descent, first-match for choices |
| Captures | Named sub-matches returned in a `std::map<std::string, std::string>` |

---

## Usage with Examples and Public API

### Core Types

#### `edsl::TokenType` (enum class)

Enumerates the node kinds that can appear in an expression tree.

| Enumerator | Grammar analogue | Meaning |
|---|---|---|
| `LITERAL` | `"abc"` | Match an exact string |
| `SEQUENCE` | `a b` | Match `a` immediately followed by `b` |
| `CHOICE` | `a \| b` | Match `a` or, if that fails, `b` |
| `OPTIONAL` | `a?` | Match `a` zero or one times |
| `REPEAT` | `a+` | Match `a` one or more times |
| `KLEENE` | `a*` | Match `a` zero or more times |
| `GROUP` | `(a)` | Transparent grouping |
| `NAMED` | `name: a` | Match `a` and capture the result under `name` |
| `REF` | `<name>` | Substitute the named rule from the `Grammar` |

---

#### `edsl::Expr` / `edsl::ExprPtr`

`Expr` is the AST node. You normally never construct it directly; use `ExprBuilder` instead.
`ExprPtr` is a `std::shared_ptr<Expr>`.

Relevant fields (public):

```cpp
TokenType            type;
std::string          value;     // LITERAL text or REF target name
std::string          name;      // NAMED capture key
std::vector<ExprPtr> children;  // sub-expressions
```

Static factory helpers (used internally by `ExprBuilder`):

```cpp
Expr::literal(std::string val)
Expr::sequence(std::vector<ExprPtr> children)
Expr::choice(std::vector<ExprPtr> children)
Expr::optional(ExprPtr child)
Expr::repeat(ExprPtr child)    // one-or-more
Expr::kleene(ExprPtr child)    // zero-or-more
Expr::group(ExprPtr child)
Expr::named(std::string name, ExprPtr child)
Expr::ref(std::string name)
```

---

#### `edsl::ExprBuilder`

The fluent builder for constructing expression trees. All mutating methods are rvalue-qualified
(`&&`) so they consume the builder and return a new one, enabling safe method chaining without
accidental aliasing.

**Static constructors**

```cpp
ExprBuilder ExprBuilder::lit(std::string val);   // literal token
ExprBuilder ExprBuilder::ref(std::string name);  // reference to grammar rule
```

**Combinators**

```cpp
ExprBuilder then(ExprBuilder other) &&;          // sequence
ExprBuilder operator|(ExprBuilder other) &&;     // choice
ExprBuilder opt()   &&;                          // zero-or-one
ExprBuilder plus()  &&;                          // one-or-more
ExprBuilder star()  &&;                          // zero-or-more
ExprBuilder group() &&;                          // grouping
ExprBuilder as(std::string name) &&;             // named capture
```

**Terminal methods**

```cpp
ExprPtr          build() &&;   // consume builder, return the expression tree
const ExprPtr&   get()   const; // inspect without consuming
```

**Example — matching an HTTP method**

```cpp
#include <edsl/lithe.hpp>

auto method = (edsl::ExprBuilder::lit("GET")
              | edsl::ExprBuilder::lit("POST")
              | edsl::ExprBuilder::lit("PUT")
              | edsl::ExprBuilder::lit("DELETE"))
              .as("method");
```

---

#### `edsl::Grammar`

A named-rule registry. Rules are stored as `std::map<std::string, ExprPtr>`.

```cpp
// Add or overwrite a rule
Grammar& Grammar::rule(std::string name, ExprBuilder builder);

// Look up a rule (returns std::nullopt if absent)
std::optional<ExprPtr> Grammar::get(const std::string& name) const;

// Enumerate rule names (order is lexicographic)
std::vector<std::string> Grammar::rule_names() const;
```

**Example — simple identifier grammar**

```cpp
edsl::Grammar g;
g.rule("digit",  edsl::ExprBuilder::lit("0") | edsl::ExprBuilder::lit("1")
                 /* ... */ | edsl::ExprBuilder::lit("9"))
 .rule("letter", edsl::ExprBuilder::lit("a") | /* ... */ | edsl::ExprBuilder::lit("z"))
 .rule("ident",  edsl::ExprBuilder::ref("letter")
                   .then(edsl::ExprBuilder::ref("letter") | edsl::ExprBuilder::ref("digit")).star()
                   .as("ident"));
```

---

#### `edsl::MatchResult`

Returned by every `Matcher` call.

```cpp
struct MatchResult {
    bool        success;               // true if the expression matched
    std::string_view remaining;        // unconsumed portion of input
    std::string matched;               // text consumed by this match
    std::map<std::string, std::string> captures; // named sub-matches
};
```

---

#### `edsl::Matcher`

Executes a recursive-descent match against a `std::string_view`.

```cpp
explicit Matcher(const Grammar& grammar);

// Match an expression tree against input
MatchResult match(const ExprPtr& expr, std::string_view input) const;

// Match a named rule from the grammar against input
MatchResult match_rule(const std::string& rule_name, std::string_view input) const;
```

**Example — full round-trip**

```cpp
#include <edsl/lithe.hpp>
#include <cassert>

int main() {
    using namespace edsl;

    // Build a small grammar for "hello" or "world"
    Grammar g;
    g.rule("greeting",
        (ExprBuilder::lit("hello") | ExprBuilder::lit("world")).as("word"));

    Matcher m(g);

    auto r = m.match_rule("greeting", "hello there");
    assert(r.success);
    assert(r.matched   == "hello");
    assert(r.remaining == " there");
    assert(r.captures.at("word") == "hello");

    auto r2 = m.match_rule("greeting", "goodbye");
    assert(!r2.success);
}
```

**Example — optional and repeat**

```cpp
// Match one or more digits
auto digits = ExprBuilder::lit("0").plus()
            | ExprBuilder::lit("1").plus();   // simplified

// Match an optional sign followed by digits
auto number = ExprBuilder::lit("-").opt()
                .then(ExprBuilder::lit("42"))  // simplified literal
                .as("number");
```

**Example — named captures**

```cpp
Grammar g;
g.rule("pair",
    ExprBuilder::lit("key").as("k")
        .then(ExprBuilder::lit("="))
        .then(ExprBuilder::lit("value").as("v")));

Matcher m(g);
auto r = m.match_rule("pair", "key=value");
// r.captures["k"] == "key"
// r.captures["v"] == "value"
```

---

## Implementation Details

### AST Representation

The expression tree is a heap-allocated, reference-counted structure built from `Expr` nodes
linked by `std::shared_ptr`. Each node carries:

* its `TokenType` tag,
* an optional string `value` (literal text or rule reference name),
* an optional string `name` (capture label),
* a `std::vector<ExprPtr>` of child nodes.

This means any sub-tree can be shared or reused across rules without copying.

### `ExprBuilder` Ownership Semantics

All combinator methods are declared `&& ` (rvalue reference qualified). The builder takes
ownership of the inner `ExprPtr`, applies a transformation, and returns a fresh builder.
This prevents silent double-use of a partially-built expression and makes the intent of the
fluent chain unambiguous at the call-site.

`then()` and `operator|` contain a small optimisation: if the left-hand builder already holds
a `SEQUENCE` (or `CHOICE`) node they append the new child directly rather than wrapping in
another layer, keeping the tree flat and improving matching performance on long chains.

### Recursive Descent Matching

`Matcher::match()` dispatches on `TokenType` to a private handler for each node kind:

| Handler | Behaviour |
|---|---|
| `match_literal` | `starts_with` prefix check; O(len) |
| `match_sequence` | iterates children left-to-right, threading `remaining` through; rolls back on first failure |
| `match_choice` | tries each child in order; returns first success (ordered / PEG semantics) |
| `match_optional` | attempts child; always returns `success = true` |
| `match_repeat` | requires at least one child match, then greedily consumes more |
| `match_kleene` | greedily consumes zero or more child matches |
| `match_group` | transparent — delegates to its single child |
| `match_named` | delegates to child; on success inserts `name → matched` into `captures` |
| `match_ref` | looks up the rule name in the `Grammar` and recurses via `match_rule` |

Captures from child matches are merged into the result via `std::map::merge`, so nested named
expressions accumulate into a single flat map.

### Limitations

* **No left-recursion guard.** A grammar whose rules mutually recurse in a left-recursive
  pattern will cause infinite recursion.
* **Greedy only.** Quantifiers (`+`, `*`) are greedy with no backtracking into already-consumed
  input.
* **No error messages.** On failure, `MatchResult::success == false` is the only signal; the
  caller cannot distinguish "wrong token" from "missing rule".
* **Literal-only leaves.** The EDSL currently has no character-class predicates (e.g., "any
  digit", "any uppercase letter"); every terminal must be spelt out as an explicit string.

---

## Future Work

1. **Character-class predicates** — Add `ExprBuilder::pred(std::function<bool(char)>)` or
   a dedicated `CharClass` node type so that patterns like `[a-z]` or `\d` can be expressed
   without enumerating every alternative.

2. **Left-recursion detection** — Maintain a visited-rule set during matching to detect and
   report (or break) left-recursive cycles instead of overflowing the call stack.

3. **Error recovery and diagnostics** — Extend `MatchResult` with a failure position and an
   expected-token description to produce human-readable parse-error messages.

4. **Backtracking / possessive quantifiers** — Provide opt-in backtracking semantics so that
   greedy quantifiers can give back consumed input when a later part of a sequence fails.

5. **Tokeniser / lexer layer** — Add a pre-processing stage that splits input into a token
   stream (handling whitespace, comments, etc.) before the grammar rules run.

6. **Serialisation and pretty-printing** — Implement a `to_string(ExprPtr)` utility that
   reconstructs a human-readable grammar notation from the AST, useful for debugging and
   documentation generation.

7. **`std::string_view`-native captures** — Return captures as `std::string_view` slices into
   the original input rather than heap-allocated copies, reducing allocation pressure in
   high-throughput scenarios.

8. **Compile-time grammar construction** — Investigate `constexpr`-friendly node types so that

   simple grammars can be verified and partially evaluated at compile time.
