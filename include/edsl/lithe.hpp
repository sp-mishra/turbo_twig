#pragma once

#include <tuple>
#include <utility>
#include <type_traits>
#include <concepts>
#include <functional> // std::invoke
#include <variant>    // added
#include <string>     // added for emit::dump
#include <vector>     // added for compiler::context logs
#include <unordered_map> // added for memoize_pass cache
#include <unordered_set> // added for dead_subtree_elimination_rule
#include <optional>   // added for enhanced_algebraic_canonicalization_rule
#include <memory>     // for tree/graph integration
#include <algorithm>  // for graph algorithms
#include <ranges>     // for modern range operations
#include <any>        // for std::any

// Include modernized graph and tree containers for AST/CFG backend
#include "containers/tree/NAryTree.hpp"
#include "containers/graph/LiteGraph.hpp"
#include "containers/graph/LiteGraphAlgorithms.hpp"

// Forward declarations for hash specializations
namespace lithe::backend {
    struct BasicBlock;
    struct ControlEdge;
    struct SymbolicExpression;
    struct DependencyEdge;
}


// Hash specializations must come BEFORE first use
namespace std {
    // Hash specializations for backend types
    template<>
    struct hash<lithe::backend::BasicBlock> {
        std::size_t operator()(const lithe::backend::BasicBlock &block) const noexcept;
    };

    template<>
    struct hash<lithe::backend::ControlEdge> {
        std::size_t operator()(const lithe::backend::ControlEdge &edge) const noexcept;
    };

    template<>
    struct hash<lithe::backend::SymbolicExpression> {
        std::size_t operator()(const lithe::backend::SymbolicExpression &expr) const noexcept;
    };

    template<>
    struct hash<lithe::backend::DependencyEdge> {
        std::size_t operator()(const lithe::backend::DependencyEdge &edge) const noexcept;
    };
}

namespace lithe {
    // -----------------------------
    // 1) Tags (operations)
    // -----------------------------
    struct add_tag {
    };

    struct sub_tag {
    }; // Binary subtraction
    struct mul_tag {
    };

    struct div_tag {
    }; // Binary division
    struct mod_tag {
    }; // Binary modulo
    struct neg_tag {
    }; // Unary minus tag
    struct subscript_tag {
    }; // Multidimensional subscript tag

    // Comparison operators
    struct eq_tag {
    }; // Equality ==
    struct ne_tag {
    }; // Not equal !=
    struct lt_tag {
    }; // Less than <
    struct le_tag {
    }; // Less than or equal <=
    struct gt_tag {
    }; // Greater than >
    struct ge_tag {
    }; // Greater than or equal >=

    // Logical operators
    struct and_tag {
    }; // Logical AND &&
    struct or_tag {
    }; // Logical OR ||
    struct not_tag {
    }; // Logical NOT !

    // Bitwise operators
    struct bit_and_tag {
    }; // Bitwise AND &
    struct bit_or_tag {
    }; // Bitwise OR |
    struct bit_xor_tag {
    }; // Bitwise XOR ^
    struct bit_not_tag {
    }; // Bitwise NOT ~
    struct shl_tag {
    }; // Left shift <<
    struct shr_tag {
    }; // Right shift >>

    // Control flow constructs
    struct if_tag {
    }; // Conditional if
    struct while_tag {
    }; // While loop
    struct for_tag {
    }; // For loop
    struct let_tag {
    }; // Variable binding
    struct seq_tag {
    }; // Sequence of statements
    struct call_tag {
    }; // Function call

    // Type system tags
    struct cast_tag {
    }; // Type casting
    struct sizeof_tag {
    }; // Size operator

    // Memory access
    struct deref_tag {
    }; // Pointer dereference
    struct addr_tag {
    }; // Address-of operator

    // Lambda and function types
    struct lambda_tag {
    }; // Lambda expression
    struct return_tag {
    }; // Return statement

    // -----------------------------
    // 2) Forward declarations
    // -----------------------------
    template<class Tag, class... Children>
    struct node;

    template<class Tag, class... Args>
    constexpr auto make_node(Args &&... args);

    // -----------------------------
    // 3) Capture policy
    //    - lvalues => reference
    //    - rvalues => decay by value
    // -----------------------------
    template<class T>
    using capture_t = std::conditional_t<
        std::is_lvalue_reference_v<T>,
        T,
        std::decay_t<T>
    >;

    // -----------------------------
    // 4) Expression concept
    // -----------------------------
    template<class T>
    concept Expression = requires
    {
        // must expose the marker and tag_type (only real node<> types provide these)
        typename std::decay_t<T>::is_lithe_node;
        typename std::decay_t<T>::tag_type;
        // must have a 'children' member when accessed on an lvalue of the decayed type
        { std::declval<std::decay_t<T> &>().children };
    };

    // Variant expression concept: detects std::variant-like types
    // Robust detection of std::variant specializations:
    template<class T>
    struct is_std_variant : std::false_type {
    };

    template<class... Ts>
    struct is_std_variant<std::variant<Ts...> > : std::true_type {
    };

    template<class T>
    inline constexpr bool is_std_variant_v = is_std_variant<std::decay_t<T> >::value;

    template<class T>
    concept VariantExpr = is_std_variant_v<T>;

    // -----------------------------
    // 4a) Terminal trait: users may specialize this for custom terminal types.
    //      Default: arithmetic types are terminals.
    // -----------------------------
    template<class T>
    struct is_terminal : std::bool_constant<std::is_arithmetic_v<std::decay_t<T> > > {
    };

    template<class T>
    inline constexpr bool is_terminal_v = is_terminal<T>::value;

    // Replace Terminal concept to use is_terminal_v
    template<class T>
    concept Terminal = is_terminal_v<T>;

    // Operand remains expression or terminal
    template<class T>
    concept Operand = Expression<T> || Terminal<T>;

    // -----------------------------
    // 5) Syntactic interface (member operators)
    //    Inject operators via explicit object parameter (C++23)
    // -----------------------------
    template<class Derived>
    struct interface {
        // Arithmetic operators
        template<class Self, class R>
            requires Operand<R>
        constexpr auto operator+(this Self &&self, R &&rhs) {
            return make_node<add_tag>(std::forward<Self>(self), std::forward<R>(rhs));
        }

        template<class Self, class R>
            requires Operand<R>
        constexpr auto operator-(this Self &&self, R &&rhs) {
            return make_node<sub_tag>(std::forward<Self>(self), std::forward<R>(rhs));
        }

        template<class Self, class R>
            requires Operand<R>
        constexpr auto operator*(this Self &&self, R &&rhs) {
            return make_node<mul_tag>(std::forward<Self>(self), std::forward<R>(rhs));
        }

        template<class Self, class R>
            requires Operand<R>
        constexpr auto operator/(this Self &&self, R &&rhs) {
            return make_node<div_tag>(std::forward<Self>(self), std::forward<R>(rhs));
        }

        template<class Self, class R>
            requires Operand<R>
        constexpr auto operator%(this Self &&self, R &&rhs) {
            return make_node<mod_tag>(std::forward<Self>(self), std::forward<R>(rhs));
        }

        // Unary operators
        template<class Self>
        constexpr auto operator-(this Self &&self) {
            return make_node<neg_tag>(std::forward<Self>(self));
        }

        template<class Self>
        constexpr auto operator!(this Self &&self) {
            return make_node<not_tag>(std::forward<Self>(self));
        }

        template<class Self>
        constexpr auto operator~(this Self &&self) {
            return make_node<bit_not_tag>(std::forward<Self>(self));
        }

        // Comparison operators
        template<class Self, class R>
            requires Operand<R>
        constexpr auto operator==(this Self &&self, R &&rhs) {
            return make_node<eq_tag>(std::forward<Self>(self), std::forward<R>(rhs));
        }

        template<class Self, class R>
            requires Operand<R>
        constexpr auto operator!=(this Self &&self, R &&rhs) {
            return make_node<ne_tag>(std::forward<Self>(self), std::forward<R>(rhs));
        }

        template<class Self, class R>
            requires Operand<R>
        constexpr auto operator<(this Self &&self, R &&rhs) {
            return make_node<lt_tag>(std::forward<Self>(self), std::forward<R>(rhs));
        }

        template<class Self, class R>
            requires Operand<R>
        constexpr auto operator<=(this Self &&self, R &&rhs) {
            return make_node<le_tag>(std::forward<Self>(self), std::forward<R>(rhs));
        }

        template<class Self, class R>
            requires Operand<R>
        constexpr auto operator>(this Self &&self, R &&rhs) {
            return make_node<gt_tag>(std::forward<Self>(self), std::forward<R>(rhs));
        }

        template<class Self, class R>
            requires Operand<R>
        constexpr auto operator>=(this Self &&self, R &&rhs) {
            return make_node<ge_tag>(std::forward<Self>(self), std::forward<R>(rhs));
        }

        // Logical operators
        template<class Self, class R>
            requires Operand<R>
        constexpr auto operator&&(this Self &&self, R &&rhs) {
            return make_node<and_tag>(std::forward<Self>(self), std::forward<R>(rhs));
        }

        template<class Self, class R>
            requires Operand<R>
        constexpr auto operator||(this Self &&self, R &&rhs) {
            return make_node<or_tag>(std::forward<Self>(self), std::forward<R>(rhs));
        }

        // Bitwise operators
        template<class Self, class R>
            requires Operand<R>
        constexpr auto operator&(this Self &&self, R &&rhs) {
            return make_node<bit_and_tag>(std::forward<Self>(self), std::forward<R>(rhs));
        }

        template<class Self, class R>
            requires Operand<R>
        constexpr auto operator|(this Self &&self, R &&rhs) {
            return make_node<bit_or_tag>(std::forward<Self>(self), std::forward<R>(rhs));
        }

        template<class Self, class R>
            requires Operand<R>
        constexpr auto operator^(this Self &&self, R &&rhs) {
            return make_node<bit_xor_tag>(std::forward<Self>(self), std::forward<R>(rhs));
        }

        template<class Self, class R>
            requires Operand<R>
        constexpr auto operator<<(this Self &&self, R &&rhs) {
            return make_node<shl_tag>(std::forward<Self>(self), std::forward<R>(rhs));
        }

        template<class Self, class R>
            requires Operand<R>
        constexpr auto operator>>(this Self &&self, R &&rhs) {
            return make_node<shr_tag>(std::forward<Self>(self), std::forward<R>(rhs));
        }

        // Multidimensional subscript
        template<class Self, class... I>
            requires ((Operand<I>) && ...)
        constexpr auto operator[](this Self &&self, I &&... idx) {
            return make_node<subscript_tag>(std::forward<Self>(self), std::forward<I>(idx)...);
        }
    };

    // -----------------------------
    // 5c) Expression wrapper helpers (forward declarations)
    //      Forward-declare wrapper types and as_expr so they can be used
    //      by the free operator templates declared below.
    // -----------------------------
    template<class T>
    struct expr;
    template<class T>
    struct expr_ref;

    template<class T>
    constexpr auto as_expr(T &x) -> expr_ref<T>;

    template<class T>
    constexpr auto as_expr(T &&x) -> expr<std::decay_t<T> >;

    // -----------------------------
    // 5b) Free operators for plain terminals
    //     Provide non-member operator overloads but disable them if the
    //     left-hand type already supplies a member operator to avoid ambiguity.
    // -----------------------------
    template<class L, class R>
    concept has_member_plus = requires(L &&l, R &&r)
    {
        std::forward<L>(l).operator+(std::forward<R>(r));
    };

    template<class L, class R>
    concept has_member_mul = requires(L &&l, R &&r)
    {
        std::forward<L>(l).operator*(std::forward<R>(r));
    };

    // Free operator+ / operator* overloads.
    // - If L is an Expression, use it directly.
    template<class L, class R>
        requires Expression<std::remove_cvref_t<L> > && Operand<R> && (!has_member_plus<L, R>)
    constexpr auto operator+(L &&l, R &&r) {
        return make_node<add_tag>(std::forward<L>(l), std::forward<R>(r));
    }

    template<class L, class R>
        requires Expression<std::remove_cvref_t<L> > && Operand<R> && (!has_member_mul<L, R>)
    constexpr auto operator*(L &&l, R &&r) {
        return make_node<mul_tag>(std::forward<L>(l), std::forward<R>(r));
    }

    // - If L is a Terminal (but not an Expression), wrap it with as_expr(...) so
    //   terminals written directly (e.g. `x + 5`) get the same capture/operator
    //   semantics as lithe::as_expr(x) + 5.
    template<class L, class R>
        requires (!Expression<std::remove_cvref_t<L> >) && Terminal<std::remove_cvref_t<L> > && Operand<R> && (!
                     has_member_plus<L, R>)
    constexpr auto operator+(L &&l, R &&r) {
        return make_node<add_tag>(as_expr(std::forward<L>(l)), std::forward<R>(r));
    }

    template<class L, class R>
        requires (!Expression<std::remove_cvref_t<L> >) && Terminal<std::remove_cvref_t<L> > && Operand<R> && (!
                     has_member_mul<L, R>)
    constexpr auto operator*(L &&l, R &&r) {
        return make_node<mul_tag>(as_expr(std::forward<L>(l)), std::forward<R>(r));
    }

    // -----------------------------
    // 5c) Expression wrapper helpers
    //      Small wrappers that present a terminal as an operator-enabled object.
    // -----------------------------
    template<class T>
    struct expr : interface<expr<T> > {
        T value;

        constexpr explicit expr(T v) : value(std::move(v)) {
        }
    };

    template<class T>
    struct expr_ref : interface<expr_ref<T> > {
        T *p;

        constexpr explicit expr_ref(T *pp) : p(pp) {
        }
    };

    // mark wrappers as terminals so Terminal<expr<...>> and Terminal<expr_ref<...>> are true
    template<class T>
    struct is_terminal<expr<T> > : std::true_type {
    };

    template<class T>
    struct is_terminal<expr_ref<T> > : std::true_type {
    };

    // as_expr helpers: lvalues -> expr_ref, rvalues -> expr (by value)
    template<class T>
    constexpr auto as_expr(T &x) -> expr_ref<T> { return expr_ref<T>{&x}; }

    template<class T>
    constexpr auto as_expr(T &&x) -> expr<std::decay_t<T> > { return expr<std::decay_t<T> >{std::forward<T>(x)}; }

    // -----------------------------
    // 6) Node (flattened AST)
    // -----------------------------
    template<class Tag, class... Children>
    struct node : interface<node<Tag, Children...> > {
        using is_lithe_node = void; // marker to identify real AST node types
        using tag_type = Tag; // expose tag type

        // tuple holding child elements (capture policy applied elsewhere)
        std::tuple<Children...> children;

        // defaulted copy/move so node remains copyable/movable even though we have
        // a user-declared templated constructor below. This allows std::variant
        // and other contexts to copy/move nodes.
        constexpr node(const node &) = default;

        constexpr node(node &&) = default;

        node &operator=(const node &) = default;

        node &operator=(node &&) = default;

        // Only participate when the number of forwarded args equals the node arity.
        // This prevents the templated constructor from being selected for overloads
        // like node(node&) (which would try to forward a single node& into the
        // children tuple and fail).
        template<class... Args>
            requires (sizeof...(Args) == sizeof...(Children))
        constexpr explicit node(Args &&... args)
            : children(std::forward<Args>(args)...) {
        }
    };

    // Factory
    template<class Tag, class... Args>
    constexpr auto make_node(Args &&... args) {
        return node<Tag, Args...>(std::forward<Args>(args)...);
    }

    // Lightweight explicit AST builder (no state, zero-overhead wrapper).
    struct IRBuilder {
        template<class A, class B>
        constexpr auto CreateAdd(A &&a, B &&b) const {
            return make_node<add_tag>(std::forward<A>(a), std::forward<B>(b));
        }

        template<class A, class B>
        constexpr auto CreateSub(A &&a, B &&b) const {
            return make_node<sub_tag>(std::forward<A>(a), std::forward<B>(b));
        }

        template<class A, class B>
        constexpr auto CreateMul(A &&a, B &&b) const {
            return make_node<mul_tag>(std::forward<A>(a), std::forward<B>(b));
        }

        template<class A, class B>
        constexpr auto CreateDiv(A &&a, B &&b) const {
            return make_node<div_tag>(std::forward<A>(a), std::forward<B>(b));
        }

        template<class Base, class... I>
        constexpr auto CreateSubscript(Base &&base, I &&... idx) const {
            return make_node<subscript_tag>(std::forward<Base>(base), std::forward<I>(idx)...);
        }

        template<class Cond, class Then, class Else>
        constexpr auto CreateIf(Cond &&c, Then &&t, Else &&e) const {
            return make_node<if_tag>(std::forward<Cond>(c), std::forward<Then>(t), std::forward<Else>(e));
        }

        template<class... Args>
        constexpr auto CreateSeq(Args &&... args) const {
            return make_node<seq_tag>(std::forward<Args>(args)...);
        }

        template<class Fn, class... Args>
        constexpr auto CreateCall(Fn &&fn, Args &&... args) const {
            return make_node<call_tag>(std::forward<Fn>(fn), std::forward<Args>(args)...);
        }
    };

    // -----------------------------
    // 7) Basic evaluate (single-view)
    //    - terminal: t.on_terminal(x)
    //    - node:     t.on_node(tag{}, evaluated_children...)
    // -----------------------------
    template<class Expr, class Transform>
    constexpr decltype(auto) evaluate(Expr &&expr, Transform &&t) {
        if constexpr (VariantExpr<Expr>) {
            return std::visit(
                [&](auto &&alt) -> decltype(auto) {
                    return evaluate(std::forward<decltype(alt)>(alt), std::forward<Transform>(t));
                },
                std::forward<Expr>(expr)
            );
        } else if constexpr (Expression<Expr>) {
            using E = std::decay_t<Expr>;
            return std::apply(
                [&](auto &&... ch) -> decltype(auto) {
                    return t.on_node(
                        typename E::tag_type{},
                        evaluate(std::forward<decltype(ch)>(ch), t)...
                    );
                },
                std::forward<Expr>(expr).children
            );
        } else {
            return t.on_terminal(std::forward<Expr>(expr));
        }
    }

    // -----------------------------
    // 7b) visit: lightweight traversal/inspection (no dual-view)
    //    - terminal: call v.on_terminal(original)
    //    - node:     call v.on_node(tag{}, visit(child, v)...)
    // -----------------------------
    template<class Expr, class Visitor>
    constexpr decltype(auto) visit(Expr &&expr, Visitor &&v) {
        if constexpr (VariantExpr<Expr>) {
            return std::visit(
                [&](auto &&alt) -> decltype(auto) {
                    return visit(std::forward<decltype(alt)>(alt), std::forward<Visitor>(v));
                },
                std::forward<Expr>(expr)
            );
        } else if constexpr (Expression<Expr>) {
            using E = std::decay_t<Expr>;
            return std::apply(
                [&](auto &&... ch) -> decltype(auto) {
                    // forward the visitor so lvalue visitors keep state across recursive calls
                    return v.on_node(
                        typename E::tag_type{},
                        visit(std::forward<decltype(ch)>(ch), std::forward<Visitor>(v))...
                    );
                },
                std::forward<Expr>(expr).children
            );
        } else {
            return v.on_terminal(std::forward<Expr>(expr));
        }
    }

    // Add: transform: provide both original children and transformed children
    template<class Expr, class Transform>
    constexpr decltype(auto) transform(Expr &&expr, Transform &&t) {
        if constexpr (VariantExpr<Expr>) {
            return std::visit(
                [&](auto &&alt) -> decltype(auto) {
                    return transform(std::forward<decltype(alt)>(alt), std::forward<Transform>(t));
                },
                std::forward<Expr>(expr)
            );
        } else if constexpr (Expression<Expr>) {
            using E = std::decay_t<Expr>;
            return std::apply(
                [&](auto &&... ch) -> decltype(auto) {
                    return t.on_node(
                        typename E::tag_type{},
                        // original children
                        std::forward<decltype(ch)>(ch)...,
                        // transformed children (same order)
                        transform(std::forward<decltype(ch)>(ch), t)...
                    );
                },
                std::forward<Expr>(expr).children
            );
        } else {
            return t.on_terminal(std::forward<Expr>(expr));
        }
    }

    // Helper: rebuild a node<Tag> from given children (useful for rewrite rules)
    template<class Tag, class... Children>
    constexpr auto rebuild(Children &&... children) {
        return make_node<Tag>(std::forward<Children>(children)...);
    }

    // Relay helper for rewrite_once: must be at namespace scope so its
    // member templates are valid (local classes cannot have template members).
    template<class Rule>
    struct rewrite_relay {
        // store pointer-to-const decayed rule type to avoid pointer-to-reference
        // situations when Rule is deduced as a reference type.
        const Rule *rule;

        constexpr explicit rewrite_relay(const Rule *r) : rule(r) {
        }

        template<class T>
        constexpr decltype(auto) on_terminal(T &&x) const {
            return std::forward<T>(x);
        }

        template<class Tag, class... Args>
        constexpr decltype(auto) on_node(Tag tag, Args &&... args) const {
            return rule->on_node(tag, std::forward<Args>(args)...);
        }
    };

    // rewrite_once: single-pass rewrite using a rule 'r'.
    // - terminals are returned unchanged
    // - nodes: call r.on_node(tag, orig..., transformed...) and return its result
    template<class Expr, class Rule>
    constexpr decltype(auto) rewrite_once(Expr &&expr, Rule &&r) {
        if constexpr (Expression<Expr>) {
            using DecayedRule = std::remove_reference_t<Rule>;
            return transform(std::forward<Expr>(expr), rewrite_relay<DecayedRule>{std::addressof(r)});
        } else {
            return std::forward<Expr>(expr);
        }
    }

    // -----------------------------------------------------------------------
    // Emit-phase helpers: structural equality and an emit::evaluate wrapper.
    // -----------------------------------------------------------------------
    namespace emit {
        template<class A, class B>
        constexpr bool structural_equal(const A &a, const B &b) {
            // Handle std::variant-like carriers first to avoid instantiating
            // comparisons across alternatives that may not be comparable.
            if constexpr (VariantExpr<A> && VariantExpr<B>) {
                return std::visit([](auto const &aa, auto const &bb) -> bool {
                    return structural_equal(aa, bb);
                }, a, b);
            } else if constexpr (VariantExpr<A>) {
                return std::visit([&](auto const &aa) -> bool { return structural_equal(aa, b); }, a);
            } else if constexpr (VariantExpr<B>) {
                return std::visit([&](auto const &bb) -> bool { return structural_equal(a, bb); }, b);
            }

            // Both are AST nodes: compare tag types and recurse on children.
            else if constexpr (Expression<A> && Expression<B>) {
                using DA = std::decay_t<A>;
                using DB = std::decay_t<B>;
                if constexpr (!std::is_same_v<typename DA::tag_type, typename DB::tag_type>) {
                    return false;
                } else {
                    using ca_t = decltype(std::declval<DA &>().children);
                    using cb_t = decltype(std::declval<DB &>().children);
                    constexpr std::size_t NA = std::tuple_size_v<std::decay_t<ca_t> >;
                    constexpr std::size_t NB = std::tuple_size_v<std::decay_t<cb_t> >;
                    if constexpr (NA != NB) {
                        return false;
                    } else {
                        const auto &ca = a.children;
                        const auto &cb = b.children;
                        return [&]<std::size_t... I>(std::index_sequence<I...>) -> bool {
                            return (... && structural_equal(std::get<I>(ca), std::get<I>(cb)));
                        }(std::make_index_sequence<NA>{});
                    }
                }
            }

            // Terminals and wrappers:
            else {
                using DA = std::decay_t<A>;
                using DB = std::decay_t<B>;

                // Both are expr_ref<T>: compare the pointed-to values structurally
                if constexpr (requires { a.p; } && requires { b.p; }) {
                    if constexpr (std::is_same_v<DA, DB>) {
                        if (a.p == b.p) return true;
                        if (a.p && b.p) return structural_equal(*a.p, *b.p);
                        return false;
                    } else {
                        // different wrapper types but both expose .p -> compare pointed values
                        if (a.p && b.p) return structural_equal(*a.p, *b.p);
                        return false;
                    }
                }

                // Both are expr<T> wrappers (by-value): compare held .value
                else if constexpr (requires { a.value; } && requires { b.value; }) {
                    return structural_equal(a.value, b.value);
                }

                // expr_ref vs expr<T> (or vice-versa): compare pointed value to held value
                else if constexpr (requires { a.p; } && requires { b.value; }) {
                    if (a.p) return structural_equal(*a.p, b.value);
                    return false;
                } else if constexpr (requires { a.value; } && requires { b.p; }) {
                    if (b.p) return structural_equal(a.value, *b.p);
                    return false;
                }

                // expr_ref vs plain arithmetic / terminal: compare pointed value to terminal
                else if constexpr (requires { a.p; } && std::is_arithmetic_v<DB>) {
                    if (a.p) return structural_equal(*a.p, b);
                    return false;
                } else if constexpr (std::is_arithmetic_v<DA> && requires { b.p; }) {
                    if (b.p) return structural_equal(a, *b.p);
                    return false;
                }

                // expr<T> (by-value) vs plain arithmetic / terminal: compare held value to terminal
                else if constexpr (requires { a.value; } && std::is_arithmetic_v<DB>) {
                    return structural_equal(a.value, b);
                } else if constexpr (std::is_arithmetic_v<DA> && requires { b.value; }) {
                    return structural_equal(a, b.value);
                }

                // General terminal equality when well-formed.
                else if constexpr (std::is_arithmetic_v<DA> && std::is_arithmetic_v<DB>) {
                    return a == b;
                } else {
                    return false;
                }
            }
        }

        // Thin forwarder so tests can call lithe::emit::evaluate(...)
        template<class Expr, class Transform>
        constexpr decltype(auto) evaluate(Expr &&expr, Transform &&t) {
            return lithe::evaluate(std::forward<Expr>(expr), std::forward<Transform>(t));
        }

        // Thin forwarder so passes can invoke visit via the emit phase.
        template<class Expr, class Visitor>
        constexpr decltype(auto) visit(Expr &&expr, Visitor &&v) {
            return lithe::visit(std::forward<Expr>(expr), std::forward<Visitor>(v));
        }

        // Comprehensive tag name mapping for debug dump
        template<class Tag>
        struct tag_name {
            static constexpr const char *value = "<tag>";
        };

        template<>
        struct tag_name<add_tag> {
            static constexpr const char *value = "+";
        };

        template<>
        struct tag_name<sub_tag> {
            static constexpr const char *value = "-";
        };

        template<>
        struct tag_name<mul_tag> {
            static constexpr const char *value = "*";
        };

        template<>
        struct tag_name<div_tag> {
            static constexpr const char *value = "/";
        };

        template<>
        struct tag_name<mod_tag> {
            static constexpr const char *value = "%";
        };

        template<>
        struct tag_name<neg_tag> {
            static constexpr const char *value = "neg";
        };

        template<>
        struct tag_name<eq_tag> {
            static constexpr const char *value = "==";
        };

        template<>
        struct tag_name<ne_tag> {
            static constexpr const char *value = "!=";
        };

        template<>
        struct tag_name<lt_tag> {
            static constexpr const char *value = "<";
        };

        template<>
        struct tag_name<le_tag> {
            static constexpr const char *value = "<=";
        };

        template<>
        struct tag_name<gt_tag> {
            static constexpr const char *value = ">";
        };

        template<>
        struct tag_name<ge_tag> {
            static constexpr const char *value = ">=";
        };

        template<>
        struct tag_name<and_tag> {
            static constexpr const char *value = "&&";
        };

        template<>
        struct tag_name<or_tag> {
            static constexpr const char *value = "||";
        };

        template<>
        struct tag_name<not_tag> {
            static constexpr const char *value = "!";
        };

        template<>
        struct tag_name<bit_and_tag> {
            static constexpr const char *value = "&";
        };

        template<>
        struct tag_name<bit_or_tag> {
            static constexpr const char *value = "|";
        };

        template<>
        struct tag_name<bit_xor_tag> {
            static constexpr const char *value = "^";
        };

        template<>
        struct tag_name<bit_not_tag> {
            static constexpr const char *value = "~";
        };

        template<>
        struct tag_name<shl_tag> {
            static constexpr const char *value = "<<";
        };

        template<>
        struct tag_name<shr_tag> {
            static constexpr const char *value = ">>";
        };

        template<>
        struct tag_name<subscript_tag> {
            static constexpr const char *value = "idx";
        };

        // ------------------------------------------------------------------
        // Hash utilities and structural hash for IR (variant/node/terminals)
        // ------------------------------------------------------------------
        // Classic hash combine (constexpr-friendly)
        constexpr std::size_t hash_combine(std::size_t h, std::size_t x) {
            return h ^ (x + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
        }

        // Small integer ids for tags to seed node hashes.
        template<class Tag>
        struct tag_id {
            static constexpr std::size_t value = 0x9u;
        };

        template<>
        struct tag_id<add_tag> {
            static constexpr std::size_t value = 1u;
        };

        template<>
        struct tag_id<mul_tag> {
            static constexpr std::size_t value = 2u;
        };

        template<>
        struct tag_id<neg_tag> {
            static constexpr std::size_t value = 3u;
        };

        template<>
        struct tag_id<subscript_tag> {
            static constexpr std::size_t value = 4u;
        };

        // structural_hash: produce a stable-ish size_t for an IR value.
        template<class E>
        constexpr std::size_t structural_hash(const E &e) {
            if constexpr (VariantExpr<E>) {
                return std::visit([](auto const &alt) -> std::size_t { return structural_hash(alt); }, e);
            } else if constexpr (Expression<E>) {
                using Dec = std::decay_t<E>;
                std::size_t h = tag_id<typename Dec::tag_type>::value;
                const auto &children = e.children;
                std::apply([&](auto const &... ch) {
                    ((h = hash_combine(h, structural_hash(ch))), ...);
                }, children);
                return h;
            } else {
                using D = std::decay_t<E>;
                // unwrap wrapper terminals if present
                if constexpr (requires { e.p; }) {
                    // expr_ref<T> -> hash pointed-to value
                    return structural_hash(*e.p);
                } else if constexpr (requires { e.value; }) {
                    // expr<T> -> hash held value
                    return structural_hash(e.value);
                } else if constexpr (std::is_arithmetic_v<D>) {
                    return std::hash<D>{}(e);
                } else if constexpr (requires { std::hash<D>{}(e); }) {
                    return std::hash<D>{}(e);
                } else {
                    // Unknown terminal: return fixed constant to keep compilable/stable
                    return 0x9e3779b97f4a7c15ULL;
                }
            }
        }

        // Debug dump: produce a simple string representation without requiring a custom printer.
        // - Nodes are emitted in prefix form: "(TAG child0 child1 ...)"
        // - Arithmetic terminals use std::to_string
        // - Other terminals print as "<term>"
        template<class Expr>
            requires (!requires(Expr e) { e.dag; e.dag.nodes; e.dag.root; })
        std::string dump(Expr &&expr) {
            if constexpr (VariantExpr<Expr>) {
                return std::visit([](auto &&alt) -> std::string { return dump(std::forward<decltype(alt)>(alt)); },
                                  std::forward<Expr>(expr));
            } else if constexpr (Expression<Expr>) {
                using E = std::decay_t<Expr>;
                const char *tag = tag_name<typename E::tag_type>::value;
                std::string out = std::string("(") + tag;
                std::apply([&](auto &&... ch) {
                    // append space + child dump for each child
                    ((out += " " + dump(std::forward<decltype(ch)>(ch))), ...);
                }, std::forward<Expr>(expr).children);
                out += ")";
                return out;
            } else {
                using D = std::decay_t<Expr>;
                if constexpr (std::is_arithmetic_v<D>) {
                    return std::to_string(expr);
                } else if constexpr (requires { expr.value; }) {
                    return dump(expr.value);
                } else if constexpr (requires { expr.p; }) {
                    return dump(*expr.p);
                } else {
                    return {"<term>"};
                }
            }
        }
    } // namespace emit

    // Small lang facade to preserve older call sites: lithe::lang::as_expr(...)
    namespace lang {
        template<class T>
        constexpr auto as_expr(T &x) -> expr_ref<T> { return lithe::as_expr(x); }

        template<class T>
        constexpr auto as_expr(T &&x) -> expr<std::decay_t<T> > { return lithe::as_expr(std::forward<T>(x)); }
    } // namespace lang

    // -----------------------------
    // Minimal Shared DAG Carrier
    // -----------------------------
    namespace graph {
        // Node identifier in the DAG
        using node_id = std::size_t;

        // DAG node: stores an expression with metadata using type erasure
        struct dag_node {
            node_id id{};
            std::size_t hash{};
            std::any expr; // Type-erased expression storage
            std::vector<node_id> children;
            std::size_t use_count = 0;
            std::string operation_name; // Store operation name for dump

            constexpr dag_node() = default;

            template<class Expr>
            constexpr dag_node(node_id nid, std::size_t h, Expr e, std::vector<node_id> ch = {}, std::size_t uses = 0,
                               std::string op_name = "")
                : id(nid), hash(h), expr(std::move(e)), children(std::move(ch)), use_count(uses),
                  operation_name(std::move(op_name)) {
            }
        };

        // DAG view: non-owning view into a DAG
        template<class Expr>
        struct dag_view {
            std::unordered_map<node_id, dag_node> nodes;
            node_id root{};

            constexpr dag_view() = default;

            constexpr dag_view(std::unordered_map<node_id, dag_node> n, node_id r)
                : nodes(std::move(n)), root(r) {
            }

            // Query interface
            [[nodiscard]] const dag_node *get_node(node_id id) const {
                auto it = nodes.find(id);
                return it != nodes.end() ? &it->second : nullptr;
            }

            [[nodiscard]] const dag_node *get_root_node() const {
                return get_node(root);
            }

            [[nodiscard]] std::size_t node_count() const {
                return nodes.size();
            }

            [[nodiscard]] std::size_t shared_node_count() const {
                std::size_t count = 0;
                for (const auto &[id, node]: nodes) {
                    if (node.use_count > 1) ++count;
                }
                return count;
            }
        };

        // Shared expression: complete DAG representation
        template<class Expr>
        struct shared_expr {
            dag_view<Expr> dag;

            constexpr shared_expr() = default;

            constexpr explicit shared_expr(dag_view<Expr> d)
                : dag(std::move(d)) {
            }

            // Convenience accessors
            [[nodiscard]] const dag_node *root_node() const {
                return dag.get_root_node();
            }

            [[nodiscard]] std::size_t size() const {
                return dag.node_count();
            }

            [[nodiscard]] std::size_t sharing_count() const {
                return dag.shared_node_count();
            }
        };

        // DAG builder with structural hash-based interning for CSE
        template<class Expr>
        struct dag_builder {
            dag_view<Expr> dag;
            std::unordered_map<std::size_t, std::vector<node_id> > buckets;
            node_id next_id = 1;

            // Intern an expression into the DAG, returning its node_id
            // Uses structural_hash as primary key and structural_equal to resolve collisions
            template<class E>
            node_id intern(const E &expr) {
                // Step 1: Compute structural hash
                std::size_t h = emit::structural_hash(expr);

                // Step 2: Look up hash bucket
                auto &bucket = buckets[h];

                // Step 3: Check for existing equivalent node
                for (node_id candidate_id: bucket) {
                    auto it = dag.nodes.find(candidate_id);
                    if (it != dag.nodes.end() && it->second.expr.has_value()) {
                        // Use structural_equal to check if expressions match
                        // Need to cast std::any back to E type to compare
                        try {
                            const auto &stored_expr = std::any_cast<const E &>(it->second.expr);
                            if (emit::structural_equal(stored_expr, expr)) {
                                // Found existing node - increment use count and return
                                ++dag.nodes[candidate_id].use_count;
                                return candidate_id;
                            }
                        } catch (const std::bad_any_cast &) {
                            // Type mismatch - not the same expression type, continue searching
                        }
                    }
                }

                // Step 4: Not found - create new node
                std::vector<node_id> child_ids;
                std::string op_name;

                // Recursively intern children if this is an expression node
                if constexpr (lithe::Expression<E>) {
                    // Get operation name from tag
                    using tag_t = std::decay_t<E>::tag_type;
                    op_name = std::string(emit::tag_name<tag_t>::value);

                    // Use std::apply to iterate over children tuple
                    std::apply([this, &child_ids]<typename... T0>(T0 const &... children) {
                        // Intern each child and collect their ids - template handles different child types
                        ((child_ids.push_back(this->template intern<std::decay_t<T0> >(children))), ...);
                    }, expr.children);
                } else {
                    // Terminal node - store its value representation
                    op_name = emit::dump(expr);
                }

                // Create the new dag_node with operation name
                node_id new_id = next_id++;
                dag_node new_node{new_id, h, expr, child_ids, 1, std::move(op_name)};

                // Insert into dag
                dag.nodes[new_id] = std::move(new_node);

                // Add to hash bucket
                bucket.push_back(new_id);

                // Step 5: Increment use_count for each child
                for (node_id child_id: child_ids) {
                    auto child_it = dag.nodes.find(child_id);
                    if (child_it != dag.nodes.end()) {
                        ++child_it->second.use_count;
                    }
                }

                return new_id;
            }
        };

        // Public graph entry point for CSE: build DAG from expression
        template<class Expr>
        constexpr auto build_dag(Expr const &expr) {
            using expr_t = std::decay_t<Expr>;

            // Create builder and intern the expression
            dag_builder<expr_t> builder;
            builder.dag.root = builder.intern(expr);

            // Return shared_expr with the completed DAG
            return shared_expr<expr_t>{std::move(builder.dag)};
        }

        // Topological order traversal: returns node IDs in dependency order (leaves to root)
        // Uses DFS-based postorder to guarantee all children appear before their parent
        template<class Expr>
        std::vector<node_id> topo_order(dag_view<Expr> const &dag) {
            std::vector<node_id> result;
            std::unordered_set<node_id> visited;

            // DFS helper: visit children first (postorder), then add current node
            auto dfs = [&](auto &dfs_ref, node_id id) -> void {
                // Skip if already visited
                if (visited.count(id)) return;

                visited.insert(id);

                // Get the node
                const dag_node *node = dag.get_node(id);
                if (!node) return;

                // Visit all children first (recursively)
                for (node_id child_id: node->children) {
                    dfs_ref(dfs_ref, child_id);
                }

                // Then add this node (postorder)
                result.push_back(id);
            };

            // Start DFS from root
            dfs(dfs, dag.root);

            return result;
        }
    } // namespace graph

    // Add dump overload for shared_expr after graph namespace is defined
    namespace emit {
        // Helper: dump a typed expression stored in dag_node
        template<class E>
        std::string dump_dag_node_expr(E const &expr) {
            // Check if it's a Lithe expression node or a terminal
            if constexpr (lithe::Expression<E>) {
                // It's an expression node - get the tag name
                using tag_t = std::decay_t<E>::tag_type;
                return std::string(tag_name<tag_t>::value);
            } else if constexpr (lithe::VariantExpr<E>) {
                // It's a variant - visit to get the active alternative
                return std::visit([](auto const &alt) -> std::string {
                    return dump_dag_node_expr(alt);
                }, expr);
            } else {
                // It's a terminal - use existing dump for terminals
                return dump(expr);
            }
        }

        // Dump shared_expr DAG in readable text format
        template<class Expr>
        std::string dump(graph::shared_expr<Expr> const &g) {
            std::string result;

            auto terminal_text = [](graph::dag_node const &node) -> std::string {
                // Prefer precomputed terminal text when available.
                if (!node.operation_name.empty() && node.operation_name != "<term>") {
                    return node.operation_name;
                }
                // Deterministic fallback for opaque terminals.
                return std::string("term(") + std::to_string(node.hash) + ")";
            };

            auto op_text = [](graph::dag_node const &node) -> std::string {
                if (!node.operation_name.empty()) {
                    return node.operation_name;
                }
                return "node";
            };

            // Get all node IDs and sort them for deterministic output
            std::vector<graph::node_id> node_ids;
            node_ids.reserve(g.dag.nodes.size());
            for (const auto &[id, node]: g.dag.nodes) {
                node_ids.push_back(id);
            }
            std::sort(node_ids.begin(), node_ids.end());

            // Print each node in order
            for (auto id: node_ids) {
                auto it = g.dag.nodes.find(id);
                if (it == g.dag.nodes.end()) continue;

                const auto &node = it->second;

                // Start with node ID
                result += "%" + std::to_string(id) + " = ";

                // Render node body in a compact, deterministic form.
                if (!node.expr.has_value()) {
                    result += "<empty>";
                } else {
                    if (node.children.empty()) {
                        result += terminal_text(node);
                    } else {
                        result += op_text(node) + "(";
                        for (std::size_t i = 0; i < node.children.size(); ++i) {
                            if (i > 0) result += ", ";
                            result += "%" + std::to_string(node.children[i]);
                        }
                        result += ")";
                    }
                }

                // Add use count - this is the key information for CSE verification
                result += " [uses=" + std::to_string(node.use_count) + "]\n";
            }

            // Print root reference
            result += "root = %" + std::to_string(g.dag.root) + "\n";

            return result;
        }

        // Evaluate shared_expr by processing DAG nodes in topological order
        // This enables CSE by evaluating each node once and reusing results
        template<class Expr, class Transform>
        auto evaluate(graph::shared_expr<Expr> const &g, Transform &&t) {
            // Get topological order (leaves to root)
            auto order = graph::topo_order(g.dag);

            // Map from node_id to evaluated result
            std::unordered_map<graph::node_id, std::any> memo;

            // Process nodes in topological order
            for (auto id: order) {
                const auto *node = g.dag.get_node(id);
                if (!node) continue;

                // Get the typed expression from std::any
                if (!node->expr.has_value()) continue;

                try {
                    // For terminals (no children)
                    if (node->children.empty()) {
                        // Cast to terminal type and evaluate
                        const auto &expr = std::any_cast<const Expr &>(node->expr);
                        auto result = t.on_terminal(expr);
                        memo[id] = std::move(result);
                    } else {
                        // For expression nodes - reconstruct from memoized children
                        // Get child results
                        std::vector<std::any> child_results;
                        child_results.reserve(node->children.size());
                        for (auto child_id: node->children) {
                            auto it = memo.find(child_id);
                            if (it != memo.end()) {
                                child_results.push_back(it->second);
                            }
                        }

                        // Cast expression and evaluate with children
                        const auto &expr = std::any_cast<const Expr &>(node->expr);

                        // This is a simplified version - full implementation would need
                        // to properly unwrap child results and call t.on_node with correct types
                        // For now, just evaluate the expression directly
                        auto result = lithe::evaluate(expr, std::forward<Transform>(t));
                        memo[id] = std::move(result);
                    }
                } catch (const std::bad_any_cast &) {
                    // Type mismatch - skip this node
                    continue;
                }
            }

            // Return result for root node
            auto root_it = memo.find(g.dag.root);
            if (root_it != memo.end()) {
                // Extract result from std::any - need to know return type
                // For now, return the root expression evaluated normally
                const auto *root_node = g.dag.get_root_node();
                if (root_node && root_node->expr.has_value()) {
                    try {
                        const auto &root_expr = std::any_cast<const Expr &>(root_node->expr);
                        return lithe::evaluate(root_expr, std::forward<Transform>(t));
                    } catch (const std::bad_any_cast &) {
                        // Fallback - this shouldn't happen but handle gracefully
                    }
                }
            }

            // Fallback: evaluate root expression normally
            const auto *root_node = g.dag.get_root_node();
            if (root_node && root_node->expr.has_value()) {
                const auto &root_expr = std::any_cast<const Expr &>(root_node->expr);
                return lithe::evaluate(root_expr, std::forward<Transform>(t));
            }

            // Last resort - return default constructed result
            // This requires Transform to have a default-constructible return type
            return decltype(lithe::evaluate(std::declval<Expr>(), std::forward<Transform>(t))){};
        }
    } // namespace emit (extension for graph types)

    // -----------------------------
    // Compiler helpers: pipeline runner with optimization levels
    // -----------------------------
    namespace compiler {
        struct identity_pass {
            template<class E>
            constexpr decltype(auto) operator()(E &&e) const {
                return std::forward<E>(e);
            }
        };

        // A pass must be invocable with the IR type. Use std::invocable to avoid
        // instantiating the pass body at constraint-check time (prevents deep
        // template instantiation when passes manipulate variant-like carriers).
        template<class P, class E>
        concept Pass = std::invocable<P, E>;

        // Helper: recursively apply a sequence of passes left-to-right.
        // This is a template recursion so each intermediate pass result may have
        // a different type (no single 'out' variable with fixed type is required).
        template<class E>
        constexpr E apply_passes(E &&e) {
            return std::forward<E>(e);
        }

        template<class E, class P, class... Rest>
        constexpr auto apply_passes(E &&e, P &&p, Rest &&... rest) {
            auto mid = std::forward<P>(p)(std::forward<E>(e));
            return apply_passes(std::move(mid), std::forward<Rest>(rest)...);
        }

        template<class E, class... Passes>
        constexpr auto compile(E &&e, Passes &&... ps) {
            if constexpr (sizeof...(ps) == 0) {
                // No passes: return the input unchanged (preserve value category)
                return std::forward<E>(e);
            } else {
                return apply_passes(std::forward<E>(e), std::forward<Passes>(ps)...);
            }
        }

        // optimize: apply a single pass repeatedly until structural equality
        // or max_iters iterations. The pass must be invocable with E and return
        // a result convertible to E so we can continue iterating in-place.
        template<class E, class PassType>
            requires std::invocable<PassType, E>
                     && std::convertible_to<std::invoke_result_t<PassType, E>, E>
        constexpr auto optimize(E e, PassType p, int max_iters = 8) {
            if (max_iters <= 0) max_iters = 1;
            for (int i = 0; i < max_iters; ++i) {
                auto next = p(std::move(e));
                // use structural equality from the emit phase to detect stability
                if (emit::structural_equal(next, e)) {
                    return next;
                }
                e = std::move(next);
            }
            return e;
        }

        // optimize_any: bounded fixpoint runner that supports passes which return
        // a possibly different carrier type (e.g. std::variant). The pass must be
        // invocable with the initial E and with the evolving Cur type produced by
        // the pass. The function applies the pass at least once and repeats up to
        // max_iters, returning early when structural_equal reports stability.
        template<class E, class Pass>
            requires std::invocable<Pass, E>
                     && std::invocable<Pass, std::invoke_result_t<Pass, E> >
        constexpr auto optimize_any(E e, Pass p, int max_iters = 8) {
            if (max_iters <= 0) max_iters = 1;
            // first application produces the carrier type Cur
            auto cur = p(std::forward<E>(e));
            for (int i = 1; i < max_iters; ++i) {
                auto next = p(cur); // requires Pass to accept the current carrier type
                if (emit::structural_equal(next, cur)) {
                    return next;
                }
                cur = std::move(next);
            }
            return cur;
        }

        // Language phase -> IR container
        // Construct the user's chosen IR holder (e.g. lithe::ir::any_expr<...>)
        // from a front-end expression/value by forwarding into IR's constructor.
        template<class IR, class E>
        constexpr IR to_ir(E &&e) {
            return IR{std::forward<E>(e)};
        }

        // Optional compiler-side tracing context: users can enable `trace` to
        // collect before/after dumps and count passes executed. Default is off.
        struct context {
            bool trace = false;
            int passes_run = 0;
            std::vector<std::string> logs; // collected debug strings when trace==true
        };
    } // namespace compiler

    // -----------------------------
    // Passes: conservative, self-documenting pass objects
    // -----------------------------
    namespace passes {
        // fixpoint_pass: wraps a single-pass rule and applies it repeatedly
        // (fixpoint iteration) up to max_iters, using optimize_any for structural
        // equality detection. This allows optimization passes to plug naturally
        // into the compile() pipeline.
        template<class Pass>
        struct fixpoint_pass {
            Pass p;
            int max_iters;

            template<class E>
            constexpr auto operator()(E &&e) const {
                return compiler::optimize_any(std::forward<E>(e), p, max_iters);
            }
        };

        // Helper: construct a fixpoint_pass with the given pass and iteration limit.
        template<class Pass>
        constexpr auto fixpoint(Pass p, int max_iters = 8) {
            return fixpoint_pass<Pass>{std::move(p), max_iters};
        }

        // Rule implementing conservative x+0 / 0+x simplification.
        // Implemented at namespace scope so member templates are allowed.
        struct simplify_add_zero_rule {
            // terminals unchanged
            template<class T>
            constexpr decltype(auto) on_terminal(T &&t) const {
                return std::forward<T>(t);
            }

            // add node: consider original children (origA, origB) and transformed
            // children (ta, tb). We conservatively handle arithmetic zeros.
            template<class A, class B, class TA, class TB>
            constexpr auto on_node(add_tag, A &&origA, B &&origB, TA ta, TB tb) const {
                // node result type when rebuilding from originals
                using node_t = decltype(lithe::rebuild<add_tag>(std::forward<A>(origA), std::forward<B>(origB)));
                using altA_t = std::decay_t<TA>;
                using altB_t = std::decay_t<TB>;
                using var_t = std::variant<node_t, altA_t, altB_t>;

                if constexpr (std::is_arithmetic_v<altB_t>) {
                    if (tb == 0) {
                        // return the transformed left child (as the simplified result)
                        return var_t{std::in_place_index<1>, ta};
                    }
                }
                if constexpr (std::is_arithmetic_v<altA_t>) {
                    if (ta == 0) {
                        // return the transformed right child
                        return var_t{std::in_place_index<2>, tb};
                    }
                }

                // otherwise return the rebuilt add node (no simplification)
                return var_t{
                    std::in_place_index<0>, lithe::rebuild<add_tag>(std::forward<A>(origA), std::forward<B>(origB))
                };
            }

            // fallback: rebuild other nodes from original children (first half of args)
            template<class Tag, class... Args>
            constexpr decltype(auto) on_node(Tag, Args &&... args) const {
                constexpr std::size_t N = sizeof...(Args);
                static_assert(N % 2 == 0);
                auto tup = std::forward_as_tuple(std::forward<Args>(args)...);
                return [&]<std::size_t... I>(std::index_sequence<I...>) {
                    return lithe::rebuild<Tag>(std::get<I>(tup)...);
                }(std::make_index_sequence<N / 2>{});
            }
        };

        // Pass that applies the simplify_add_zero_rule once (single-pass rewrite).
        struct simplify_add_zero_pass {
            template<class E>
            constexpr auto operator()(E &&e) const {
                return rewrite_once(std::forward<E>(e), simplify_add_zero_rule{});
            }
        };

        // Rule implementing multiplication identity/annihilator:
        // x * 1  -> x
        // 1 * x  -> x
        // x * 0  -> 0
        // 0 * x  -> 0
        struct simplify_mul_identity_rule {
            // terminals unchanged
            template<class T>
            constexpr decltype(auto) on_terminal(T &&t) const {
                return std::forward<T>(t);
            }

            // mul node: consider original children (origA, origB) and transformed
            // children (ta, tb). We conservatively handle arithmetic identities/zeros.
            template<class A, class B, class TA, class TB>
            constexpr auto on_node(mul_tag, A &&origA, B &&origB, TA ta, TB tb) const {
                using node_t = decltype(lithe::rebuild<mul_tag>(std::forward<A>(origA), std::forward<B>(origB)));
                using altA_t = std::decay_t<TA>;
                using altB_t = std::decay_t<TB>;
                using var_t = std::variant<node_t, altA_t, altB_t>;

                // helpers to detect numeric 0/1 even when wrapped in expr<T> or expr_ref<T>
                auto is_zero = [](auto const &v) constexpr -> bool {
                    using V = std::decay_t<decltype(v)>;
                    if constexpr (std::is_arithmetic_v<V>) {
                        return v == 0;
                    } else if constexpr (requires { v.value; }) {
                        using ValT = std::decay_t<decltype(v.value)>;
                        if constexpr (std::is_arithmetic_v<ValT>) {
                            return v.value == 0;
                        } else {
                            return false;
                        }
                    } else if constexpr (requires { v.p; }) {
                        using Pointee = std::decay_t<decltype(*v.p)>;
                        if constexpr (std::is_arithmetic_v<Pointee>) {
                            return (*v.p) == 0;
                        } else {
                            return false;
                        }
                    } else {
                        return false;
                    }
                };

                auto is_one = [](auto const &v) constexpr -> bool {
                    using V = std::decay_t<decltype(v)>;
                    if constexpr (std::is_arithmetic_v<V>) {
                        return v == 1;
                    } else if constexpr (requires { v.value; }) {
                        using ValT = std::decay_t<decltype(v.value)>;
                        if constexpr (std::is_arithmetic_v<ValT>) {
                            return v.value == 1;
                        } else {
                            return false;
                        }
                    } else if constexpr (requires { v.p; }) {
                        using Pointee = std::decay_t<decltype(*v.p)>;
                        if constexpr (std::is_arithmetic_v<Pointee>) {
                            return (*v.p) == 1;
                        } else {
                            return false;
                        }
                    } else {
                        return false;
                    }
                };

                // Right-side: if tb is numeric 0 -> return tb (zero)
                if (is_zero(tb)) {
                    return var_t{std::in_place_index<2>, tb};
                }
                // Right-side identity: tb == 1 -> return ta (left)
                if (is_one(tb)) {
                    return var_t{std::in_place_index<1>, ta};
                }

                // Left-side: if ta is numeric 0 -> return ta (zero)
                if (is_zero(ta)) {
                    return var_t{std::in_place_index<1>, ta};
                }
                // Left-side identity: ta == 1 -> return tb (right)
                if (is_one(ta)) {
                    return var_t{std::in_place_index<2>, tb};
                }

                // otherwise return the rebuilt mul node (no simplification)
                return var_t{
                    std::in_place_index<0>, lithe::rebuild<mul_tag>(std::forward<A>(origA), std::forward<B>(origB))
                };
            }

            // fallback: rebuild other nodes from original children (first half of args)
            template<class Tag, class... Args>
            constexpr decltype(auto) on_node(Tag, Args &&... args) const {
                constexpr std::size_t N = sizeof...(Args);
                static_assert(N % 2 == 0);
                auto tup = std::forward_as_tuple(std::forward<Args>(args)...);
                return [&]<std::size_t... I>(std::index_sequence<I...>) {
                    return lithe::rebuild<Tag>(std::get<I>(tup)...);
                }(std::make_index_sequence<N / 2>{});
            }
        };

        // Pass that applies the simplify_mul_identity_rule once (single-pass rewrite).
        struct simplify_mul_identity_pass {
            template<class E>
            constexpr auto operator()(E &&e) const {
                return rewrite_once(std::forward<E>(e), simplify_mul_identity_rule{});
            }
        };

        // ------------------------------------------------------------
        // constant_fold_arith_rule: if transformed children are arithmetic
        // compute the result (constant-fold) and return it; otherwise
        // return a rebuilt node. Results are returned in a std::variant
        // to allow the pass to change carrier type like other rules.
        // ------------------------------------------------------------
        struct constant_fold_arith_rule {
            // terminals unchanged
            template<class T>
            constexpr decltype(auto) on_terminal(T &&t) const {
                return std::forward<T>(t);
            }

            // add: if both transformed children are arithmetic -> fold to a+b
            template<class A, class B, class TA, class TB>
            constexpr auto on_node(add_tag, A &&origA, B &&origB, TA a, TB b) const {
                using node_t = decltype(lithe::rebuild<add_tag>(std::forward<A>(origA), std::forward<B>(origB)));
                using altA_t = std::decay_t<TA>;
                using altB_t = std::decay_t<TB>;
                using sum_t = std::decay_t<decltype(a + b)>;
                using var_t = std::variant<node_t, sum_t, altA_t, altB_t>;

                if constexpr (std::is_arithmetic_v<altA_t> && std::is_arithmetic_v<altB_t>) {
                    return var_t{std::in_place_index<1>, static_cast<sum_t>(a + b)};
                }
                return var_t{
                    std::in_place_index<0>, lithe::rebuild<add_tag>(std::forward<A>(origA), std::forward<B>(origB))
                };
            }

            // mul: if both transformed children are arithmetic -> fold to a*b
            template<class A, class B, class TA, class TB>
            constexpr auto on_node(mul_tag, A &&origA, B &&origB, TA a, TB b) const {
                using node_t = decltype(lithe::rebuild<mul_tag>(std::forward<A>(origA), std::forward<B>(origB)));
                using altA_t = std::decay_t<TA>;
                using altB_t = std::decay_t<TB>;

                if constexpr (std::is_arithmetic_v<altA_t> && std::is_arithmetic_v<altB_t>) {
                    using prod_t = std::decay_t<decltype(a * b)>;
                    using var_t = std::variant<node_t, prod_t, altA_t, altB_t>;
                    return var_t{std::in_place_index<1>, static_cast<prod_t>(a * b)};
                } else {
                    using var_t = std::variant<node_t, altA_t, altB_t>;
                    return var_t{
                        std::in_place_index<0>, lithe::rebuild<mul_tag>(std::forward<A>(origA), std::forward<B>(origB))
                    };
                }
            }

            // neg: unary negation, fold if transformed child is arithmetic
            template<class A, class TA>
            constexpr auto on_node(neg_tag, A &&origA, TA a) const {
                using node_t = decltype(lithe::rebuild<neg_tag>(std::forward<A>(origA)));
                using altA_t = std::decay_t<TA>;
                using neg_t = std::decay_t<decltype(-a)>;
                using var_t = std::variant<node_t, neg_t, altA_t>;

                if constexpr (std::is_arithmetic_v<altA_t>) {
                    return var_t{std::in_place_index<1>, static_cast<neg_t>(-a)};
                }
                return var_t{std::in_place_index<0>, lithe::rebuild<neg_tag>(std::forward<A>(origA))};
            }

            // fallback: rebuild other nodes from original children (first half of args)
            template<class Tag, class... Args>
            constexpr decltype(auto) on_node(Tag, Args &&... args) const {
                constexpr std::size_t N = sizeof...(Args);
                static_assert(N % 2 == 0);
                auto tup = std::forward_as_tuple(std::forward<Args>(args)...);
                return [&]<std::size_t... I>(std::index_sequence<I...>) {
                    return lithe::rebuild<Tag>(std::get<I>(tup)...);
                }(std::make_index_sequence<N / 2>{});
            }
        };

        // Pass that applies the constant folding rule once.
        struct constant_fold_arith_pass {
            template<class E>
            constexpr auto operator()(E &&e) const {
                return rewrite_once(std::forward<E>(e), constant_fold_arith_rule{});
            }
        };

        // ------------------------------------------------------------
        // canonicalize_commutative_rule: normalize operand order for commutative
        // operations (add, mul) in a small, safe set of cases so structurally
        // equivalent expressions (e.g. x+5 and 5+x) become identical.
        // - Conservative: only reorders top-level binary commutative nodes when
        //   it's safe to do so based on transformed-child terminal arithmetic info.
        // - Rebuild uses the original children (origA/origB) in the chosen order so
        //   wrapper/terminal types remain stable for structural comparisons.
        // ------------------------------------------------------------
        struct canonicalize_commutative_rule {
            // terminals unchanged
            template<class T>
            constexpr decltype(auto) on_terminal(T &&t) const {
                return std::forward<T>(t);
            }

            // Detect arithmetic or wrappers around arithmetic (expr<T>, expr_ref<T>)
            template<class U>
            static constexpr bool is_arith_val([[maybe_unused]] const U &v) {
                using V = std::decay_t<U>;
                if constexpr (std::is_arithmetic_v<V>) {
                    return true;
                } else if constexpr (requires { v.value; }) {
                    using ValT = std::decay_t<decltype(v.value)>;
                    return std::is_arithmetic_v<ValT>;
                } else if constexpr (requires { v.p; }) {
                    using Pointee = std::decay_t<decltype(*v.p)>;
                    return std::is_arithmetic_v<Pointee>;
                } else {
                    return false;
                }
            }

            // Convert a transformed child into the common_type for comparison.
            template<class CT, class U>
            static constexpr CT to_common(const U &v) {
                using V = std::decay_t<U>;
                if constexpr (std::is_arithmetic_v<V>) {
                    return static_cast<CT>(v);
                } else if constexpr (requires { v.value; }) {
                    return static_cast<CT>(v.value);
                } else if constexpr (requires { v.p; }) {
                    return static_cast<CT>(*v.p);
                } else {
                    return CT{};
                }
            }

            // Generic handler for binary commutative tags:
            // - If both transformed children are arithmetic (including wrappers),
            //   compare via common_type and order so smaller comes first.
            // - If exactly one is arithmetic, place the non-arithmetic child first.
            // - Otherwise keep original order.
            template<class Tag, class A, class B, class TA, class TB>
            constexpr auto handle_comm(Tag, A &&origA, B &&origB, TA ta, TB tb) const {
                constexpr bool a_num = is_arith_val(ta);
                constexpr bool b_num = is_arith_val(tb);

                if constexpr (a_num && b_num) {
                    using CTA = std::decay_t<TA>;
                    using CTB = std::decay_t<TB>;
                    using CT = std::common_type_t<CTA, CTB>;
                    auto aa = to_common<CT>(ta);
                    auto bb = to_common<CT>(tb);
                    if (aa <= bb) {
                        return lithe::rebuild<Tag>(std::forward<A>(origA), std::forward<B>(origB));
                    } else {
                        return lithe::rebuild<Tag>(std::forward<B>(origB), std::forward<A>(origA));
                    }
                } else if constexpr (a_num && !b_num) {
                    // numeric left, non-numeric right -> place non-numeric first
                    return lithe::rebuild<Tag>(std::forward<B>(origB), std::forward<A>(origA));
                } else if constexpr (!a_num && b_num) {
                    // non-numeric left, numeric right -> keep non-numeric first
                    return lithe::rebuild<Tag>(std::forward<A>(origA), std::forward<B>(origB));
                } else {
                    // both non-numeric: preserve original order
                    return lithe::rebuild<Tag>(std::forward<A>(origA), std::forward<B>(origB));
                }
            }

            // add: canonicalize ordering for add
            template<class A, class B, class TA, class TB>
            constexpr auto on_node(add_tag, A &&origA, B &&origB, TA ta, TB tb) const {
                return handle_comm(add_tag{}, std::forward<A>(origA), std::forward<B>(origB), ta, tb);
            }

            // mul: canonicalize ordering for mul
            template<class A, class B, class TA, class TB>
            constexpr auto on_node(mul_tag, A &&origA, B &&origB, TA ta, TB tb) const {
                return handle_comm(mul_tag{}, std::forward<A>(origA), std::forward<B>(origB), ta, tb);
            }

            // fallback: rebuild other nodes from original children (first half of args)
            template<class Tag, class... Args>
            constexpr decltype(auto) on_node(Tag, Args &&... args) const {
                constexpr std::size_t N = sizeof...(Args);
                static_assert(N % 2 == 0);
                auto tup = std::forward_as_tuple(std::forward<Args>(args)...);
                return [&]<std::size_t... I>(std::index_sequence<I...>) {
                    return lithe::rebuild<Tag>(std::get<I>(tup)...);
                }(std::make_index_sequence<N / 2>{});
            }
        };

        // Pass that applies the canonicalize_commutative_rule once.
        struct canonicalize_commutative_pass {
            template<class E>
            constexpr auto operator()(E &&e) const {
                return rewrite_once(std::forward<E>(e), canonicalize_commutative_rule{});
            }
        };

        // ------------------------------------------------------------------
        // rewrite_fixpoint: bounded fixpoint runner for a pass
        //
        // Pragmatic bounded runner: ensure at least one application and then
        // repeat up to `max_iters`. The pass is applied to the result of the
        // previous application; the implementation allows the pass to return
        // a (consistent) result type across iterations.
        // ------------------------------------------------------------------
        template<class E, class Pass>
        constexpr auto rewrite_fixpoint(E e, Pass p, int max_iters = 8) {
            if (max_iters <= 0) max_iters = 1;
            auto cur = p(std::forward<E>(e));
            for (int i = 1; i < max_iters; ++i) {
                cur = p(std::move(cur));
            }
            return cur;
        }

        // pipeline_pass: bundle multiple passes into a single pass object.
        template<class... P>
        struct pipeline_pass {
            std::tuple<P...> ps;

            template<class E>
            constexpr auto operator()(E &&e) const {
                return std::apply([&](auto const &... p) {
                    return compiler::compile(std::forward<E>(e), p...);
                }, ps);
            }
        };

        // Helper to construct a pipeline_pass (moves provided passes into the tuple).
        template<class... P>
        constexpr auto pipeline(P... p) {
            return pipeline_pass<P...>{std::tuple<P...>{std::move(p)...}};
        }

        // Adapter: turn any dual-view transformer (has on_node/on_terminal that
        // receive original children plus transformed children) into a Pass object.
        template<class T>
        struct transform_pass {
            T t;

            template<class E>
            constexpr auto operator()(E &&e) const {
                return lithe::transform(std::forward<E>(e), t);
            }
        };

        // Helper to build a transform_pass by moving the transformer in.
        template<class T>
        constexpr auto from_transformer(T t) {
            return transform_pass<T>{std::move(t)};
        }

        // Adapter: wrap a rule-based rewriter into a Pass (applies a single rewrite).
        template<class R>
        struct rewrite_pass {
            R r;

            template<class E>
            constexpr auto operator()(E &&e) const {
                return lithe::rewrite_once(std::forward<E>(e), r);
            }
        };

        // Helper to build a rewrite_pass by moving the rule in.
        template<class R>
        constexpr auto from_rewriter(R r) {
            return rewrite_pass<R>{std::move(r)};
        }

        // ------------------------------------------------------------
        // NEW: visitor-as-pass adapter: run a visitor for inspection and
        // return the original IR unchanged so it composes in pipelines.
        // ------------------------------------------------------------
        template<class V>
        struct visit_pass {
            V *v;

            template<class E>
            constexpr auto operator()(E &&e) const {
                // use emit::visit (forwarder) so visitor behavior mirrors emit-phase
                emit::visit(std::forward<E>(e), *v);
                return std::forward<E>(e);
            }
        };

        template<class V>
        constexpr auto from_visitor(V &v) {
            return visit_pass<V>{std::addressof(v)};
        }

        // ------------------------------------------------------------
        // trace_pass adapter: optionally capture debug dumps before/after a pass
        // and increment the context pass counter. When ctx.trace==false the
        // adapter only forwards to the wrapped pass and increments passes_run.
        // ------------------------------------------------------------
        template<class P>
        struct trace_pass {
            P p;
            compiler::context *ctx;

            template<class E>
            constexpr auto operator()(E &&e) const {
                if (ctx && ctx->trace) {
                    // collect before-dump when possible
                    ctx->logs.push_back(emit::dump(std::forward<E>(e)));
                    auto res = p(std::forward<E>(e));
                    // count this pass
                    ++ctx->passes_run;
                    // collect after-dump if emit::dump is well-formed for the result
                    if constexpr (requires { emit::dump(res); }) {
                        ctx->logs.push_back(emit::dump(res));
                    }
                    return res;
                } else {
                    if (ctx) ++ctx->passes_run;
                    return p(std::forward<E>(e));
                }
            }
        };

        // Helper to build a trace_pass by referencing a context
        template<class P>
        constexpr auto with_trace(P p, compiler::context &ctx) {
            return trace_pass<P>{std::move(p), std::addressof(ctx)};
        }

        // ------------------------------------------------------------------
        // memoize_pass: optional adapter to cache pass outputs by structural_hash.
        // - Opt-in only: callers must provide the cache map and use memoize(...)
        // - IR is the stored result type (e.g. same as pass output type)
        // ------------------------------------------------------------------
        template<class Pass, class IR>
        struct memoize_pass {
            Pass p;
            std::unordered_map<std::size_t, IR> *cache;

            template<class E>
            IR operator()(E &&e) const {
                auto h = emit::structural_hash(e);
                if (auto it = cache->find(h); it != cache->end()) return it->second;
                IR out = p(std::forward<E>(e));
                // Avoid operator[] which default-constructs the mapped_type (may be ill-formed);
                // insert_or_assign constructs/assigns the mapped value without requiring a default ctor.
                cache->insert_or_assign(h, std::move(out));
                // Return the cached value (moved into map), find it to return a copy/move
                return cache->find(h)->second;
            }
        };

        // helper to construct a memoize_pass by referencing an existing cache map
        template<class Pass, class IR>
        constexpr auto memoize(Pass p, std::unordered_map<std::size_t, IR> &cache) {
            return memoize_pass<Pass, IR>{std::move(p), std::addressof(cache)};
        }
    } // namespace passes

    // Small, flexible IR holder: users can pick a variant of types to serve
    // as a stable "program" carrier across compiler passes.
    namespace ir {
        // any_expr is a thin alias over std::variant so callers can enumerate the
        // minimal set of alternative carriers they need (e.g. node types, printers,
        // transformer results, etc.). Example: any_expr<int,double,node_t>.
        template<class... Ts>
        using any_expr = std::variant<Ts...>;

        // Helper to place an expression/value into the chosen any_expr variant.
        // Note: E must be one of the Ts... (or convertible) for this to be well-formed.
        template<class... Ts, class E>
        constexpr auto to_ir(E &&e) -> any_expr<Ts...> {
            return any_expr<Ts...>{std::forward<E>(e)};
        }
    } // namespace ir

    // -----------------------------
    // Tree Utility Layer
    // -----------------------------
    namespace tree {
        namespace detail {
            template<class T>
            using direct_children_tuple_t = std::conditional_t<
                Expression<T>,
                decltype(std::declval<std::remove_reference_t<T> &>().children),
                std::tuple<>
            >;

            template<class V>
            struct children_variant;

            template<class... Ts>
            struct children_variant<std::variant<Ts...> > {
                using type = std::variant<std::decay_t<direct_children_tuple_t<Ts> >...>;
            };

            template<class V>
            using children_variant_t = children_variant<std::decay_t<V> >::type;
        } // namespace detail

        // Rebuild a node using the same tag but new direct children.
        template<class E, class... NewChildren>
        constexpr auto rebuild_with(E &&e, NewChildren &&... children) {
            using ET = std::remove_reference_t<E>;

            if constexpr (Expression<ET>) {
                using tag_t = ET::tag_type;
                constexpr std::size_t N = std::tuple_size_v<decltype(std::declval<ET &>().children)>;
                static_assert(sizeof...(NewChildren) == N,
                              "lithe::tree::rebuild_with: child count must match node arity");
                return make_node<tag_t>(std::forward<NewChildren>(children)...);
            } else {
                static_assert(sizeof...(NewChildren) == 0,
                              "lithe::tree::rebuild_with: terminals do not accept replacement children");
                return std::forward<E>(e);
            }
        }

        // Rebuild a node with one direct child replaced.
        template<std::size_t I, class E, class NewChild>
        constexpr auto replace_child(E &&e, NewChild &&child) {
            using ET = std::remove_reference_t<E>;

            if constexpr (Expression<ET>) {
                using tag_t = ET::tag_type;
                constexpr std::size_t N = std::tuple_size_v<decltype(std::declval<ET &>().children)>;
                static_assert(I < N, "lithe::tree::replace_child: index out of range");

                auto &&tup = std::forward<E>(e).children;
                return [&]<std::size_t... J>(std::index_sequence<J...>) {
                    auto pick = [&]<std::size_t Jx>() -> decltype(auto) {
                        if constexpr (Jx == I) {
                            return std::forward<NewChild>(child);
                        } else {
                            return std::get<Jx>(std::forward<decltype(tup)>(tup));
                        }
                    };
                    return make_node<tag_t>(pick.template operator()<J>()...);
                }(std::make_index_sequence<N>{});
            } else {
                return std::forward<E>(e);
            }
        }

        // Map each direct child and rebuild with mapped children.
        template<class E, class Fn>
        constexpr auto map_children(E &&e, Fn &&fn) {
            using ET = std::remove_reference_t<E>;

            if constexpr (VariantExpr<ET>) {
                return std::visit(
                    [&](auto &&alt) constexpr {
                        return map_children(std::forward<decltype(alt)>(alt), std::forward<Fn>(fn));
                    },
                    std::forward<E>(e)
                );
            } else if constexpr (Expression<ET>) {
                return std::apply(
                    [&](auto &&... ch) constexpr {
                        return rebuild_with(
                            std::forward<E>(e),
                            fn(std::forward<decltype(ch)>(ch))...
                        );
                    },
                    std::forward<E>(e).children
                );
            } else {
                return std::forward<E>(e);
            }
        }

        // Get the arity (number of direct children)
        template<class E>
        constexpr std::size_t arity(E const &e) {
            if constexpr (VariantExpr<E>) {
                return std::visit([](auto const &alt) constexpr { return arity(alt); }, e);
            } else if constexpr (Expression<E>) {
                using children_t = decltype(std::declval<std::remove_reference_t<E> &>().children);
                return std::tuple_size_v<std::remove_reference_t<children_t> >;
            } else {
                return 0;
            }
        }

        // Count total nodes including descendants and terminals
        template<class E>
        constexpr std::size_t size(E const &e) {
            if constexpr (VariantExpr<E>) {
                return std::visit([](auto const &alt) constexpr { return size(alt); }, e);
            } else if constexpr (Expression<E>) {
                std::size_t count = 1;
                std::apply([&](auto const &... ch) constexpr {
                    ((count += size(ch)), ...);
                }, e.children);
                return count;
            } else {
                return 1;
            }
        }

        // Compute maximum subtree depth
        template<class E>
        constexpr std::size_t depth(E const &e) {
            if constexpr (VariantExpr<E>) {
                return std::visit([](auto const &alt) constexpr { return depth(alt); }, e);
            } else if constexpr (Expression<E>) {
                std::size_t max_child_depth = 0;
                std::apply([&](auto const &... ch) constexpr {
                    ((max_child_depth = std::max(max_child_depth, depth(ch))), ...);
                }, e.children);
                return 1 + max_child_depth;
            } else {
                return 1;
            }
        }

        // Visit direct children only
        template<class E, class Fn>
        constexpr void for_each_child(E &&e, Fn &&fn) {
            if constexpr (VariantExpr<std::remove_reference_t<E> >) {
                std::visit([&](auto &&alt) constexpr {
                    for_each_child(std::forward<decltype(alt)>(alt), std::forward<Fn>(fn));
                }, std::forward<E>(e));
            } else if constexpr (Expression<std::remove_reference_t<E> >) {
                std::apply([&](auto &&... ch) constexpr {
                    ((fn(std::forward<decltype(ch)>(ch))), ...);
                }, std::forward<E>(e).children);
            }
        }

        // Return the direct children tuple (or empty tuple for terminals).
        // For variants, return a variant of possible child-tuples.
        template<class E>
        constexpr decltype(auto) children_tuple(E &&e) {
            if constexpr (VariantExpr<std::remove_reference_t<E> >) {
                return std::visit([](auto &&alt) constexpr -> detail::children_variant_t<E> {
                    return detail::children_variant_t<E>{children_tuple(std::forward<decltype(alt)>(alt))};
                }, std::forward<E>(e));
            } else if constexpr (Expression<std::remove_reference_t<E> >) {
                return (std::forward<E>(e).children);
            } else {
                return std::tuple<>{};
            }
        }

        // Helper: check if an expression is a leaf (terminal)
        template<class Expr>
        constexpr bool is_leaf(const Expr &expr) {
            return arity(expr) == 0;
        }

        // Helper: count only internal nodes (non-terminals)
        template<class Expr>
        constexpr std::size_t internal_nodes(const Expr &expr) {
            if constexpr (Expression<Expr>) {
                std::size_t count = 1; // Count this internal node
                std::apply([&count](auto &&... ch) {
                    ((count += internal_nodes(ch)), ...);
                }, expr.children);
                return count;
            } else {
                return 0; // Terminals are not internal nodes
            }
        }

        // Helper: count only leaf nodes (terminals)
        template<class Expr>
        constexpr std::size_t leaf_nodes(const Expr &expr) {
            if constexpr (Expression<Expr>) {
                std::size_t count = 0;
                std::apply([&count](auto &&... ch) {
                    ((count += leaf_nodes(ch)), ...);
                }, expr.children);
                return count;
            } else {
                return 1; // Terminals are leaf nodes
            }
        }
    } // namespace tree

    // -----------------------------
    // DSL Extension Infrastructure
    // -----------------------------
    namespace dsl_extension {
        // Custom operation tag registration system
        namespace tag_registry {
            // Base class for custom tag metadata
            struct tag_metadata {
                const char *name;
                int precedence;
                bool is_associative;
                bool is_commutative;
                std::size_t arity;

                constexpr explicit tag_metadata(const char *n, int prec = 0, bool assoc = false, bool comm = false,
                                                std::size_t ar = 2)
                    : name(n), precedence(prec), is_associative(assoc), is_commutative(comm), arity(ar) {
                }
            };

            // Registry for custom tags - DSL implementers can extend this
            template<class Tag>
            struct custom_tag_info {
                static constexpr tag_metadata metadata{"custom", 0, false, false, 2};
            };

            // Macro helper for registering custom tags
#define LITHE_REGISTER_TAG(TagType, TagName, Precedence, Associative, Commutative, Arity) \
        template <> \
        struct ::lithe::dsl_extension::tag_registry::custom_tag_info<TagType> { \
          static constexpr ::lithe::dsl_extension::tag_registry::tag_metadata metadata{ \
            TagName, Precedence, Associative, Commutative, Arity \
          }; \
        };

            // Query interface for tag properties
            template<class Tag>
            constexpr const char *get_tag_name() {
                if constexpr (requires { custom_tag_info<Tag>::metadata.name; }) {
                    return custom_tag_info<Tag>::metadata.name;
                } else {
                    return emit::tag_name<Tag>::value;
                }
            }

            template<class Tag>
            constexpr int get_precedence() {
                if constexpr (requires { custom_tag_info<Tag>::metadata.precedence; }) {
                    return custom_tag_info<Tag>::metadata.precedence;
                } else {
                    return 0; // Default precedence
                }
            }

            template<class Tag>
            constexpr bool is_associative() {
                if constexpr (requires { custom_tag_info<Tag>::metadata.is_associative; }) {
                    return custom_tag_info<Tag>::metadata.is_associative;
                } else {
                    return false; // Conservative default
                }
            }

            template<class Tag>
            constexpr bool is_commutative() {
                if constexpr (requires { custom_tag_info<Tag>::metadata.is_commutative; }) {
                    return custom_tag_info<Tag>::metadata.is_commutative;
                } else if constexpr (std::is_same_v<Tag, add_tag> || std::is_same_v<Tag, mul_tag>) {
                    return true; // Built-in commutative ops
                } else {
                    return false;
                }
            }
        } // namespace tag_registry

        // Typed symbolic variables for algebraic DSLs
        namespace symbolic {
            // Base symbolic variable with type information
            template<class T>
            struct symbolic_var {
                std::string name;
                std::size_t id;

                constexpr explicit symbolic_var(std::string n, std::size_t var_id = 0)
                    : name(std::move(n)), id(var_id) {
                }

                // Type information
                using value_type = T;

                // Comparison for use in containers
                constexpr bool operator==(const symbolic_var &other) const {
                    return id == other.id && name == other.name;
                }

                constexpr bool operator<(const symbolic_var &other) const {
                    return id < other.id || (id == other.id && name < other.name);
                }
            };

            // Mark symbolic variables as terminals (moved to global namespace below)

            // Symbolic variable factory with automatic ID assignment
            class symbolic_factory {
                mutable std::size_t next_id = 1;

            public:
                template<class T>
                constexpr auto create(std::string name) const {
                    return symbolic_var<T>{std::move(name), next_id++};
                }

                template<class T>
                constexpr auto operator()(std::string name) const {
                    return create<T>(std::move(name));
                }
            };

            // Global symbolic variable factory
            inline constexpr symbolic_factory symbol{};

            // Convenience aliases for common types
            using int_var = symbolic_var<int>;
            using double_var = symbolic_var<double>;
            using bool_var = symbolic_var<bool>;

            // Hash support for symbolic variables (moved to std namespace below)
        } // namespace symbolic

        // Domain-specific operator injection hooks
        namespace operator_hooks {
            // Trait to detect if a type has custom operator semantics
            template<class T>
            struct has_custom_operators : std::false_type {
            };

            // Base class for custom operator semantics
            template<class Derived>
            struct custom_operator_mixin {
                // Mark as having custom operators
                using has_custom_ops = std::true_type;

                // DSL implementers can override these in their derived class
                template<class R>
                constexpr auto custom_add(R &&rhs) const {
                    return static_cast<const Derived *>(this)->template operator_impl<add_tag>(std::forward<R>(rhs));
                }

                template<class R>
                constexpr auto custom_mul(R &&rhs) const {
                    return static_cast<const Derived *>(this)->template operator_impl<mul_tag>(std::forward<R>(rhs));
                }

                template<class R>
                constexpr auto custom_sub(R &&rhs) const {
                    return static_cast<const Derived *>(this)->template operator_impl<sub_tag>(std::forward<R>(rhs));
                }

                // Fallback implementation - creates standard nodes
                template<class Tag, class R>
                constexpr auto operator_impl(R &&rhs) const {
                    return make_node<Tag>(static_cast<const Derived &>(*this), std::forward<R>(rhs));
                }
            };

            // Specialization detector (fixed redundant definition)

            // Specialization for types with has_custom_ops member type
            template<class T>
                requires requires { typename T::has_custom_ops; }
            struct has_custom_operators<T> : std::true_type {
            };

            template<class T>
            inline constexpr bool has_custom_operators_v = has_custom_operators<T>::value;

            // Hook dispatcher - checks if LHS has custom operators and uses them
            template<class L, class R, class Tag>
            constexpr auto dispatch_binary_op(L &&lhs, R &&rhs, Tag tag) {
                if constexpr (has_custom_operators_v<std::decay_t<L> >) {
                    if constexpr (std::is_same_v<Tag, add_tag>) {
                        return std::forward<L>(lhs).custom_add(std::forward<R>(rhs));
                    } else if constexpr (std::is_same_v<Tag, mul_tag>) {
                        return std::forward<L>(lhs).custom_mul(std::forward<R>(rhs));
                    } else if constexpr (std::is_same_v<Tag, sub_tag>) {
                        return std::forward<L>(lhs).custom_sub(std::forward<R>(rhs));
                    } else {
                        // Fallback to standard node creation
                        return make_node<Tag>(std::forward<L>(lhs), std::forward<R>(rhs));
                    }
                } else {
                    // Standard node creation
                    return make_node<Tag>(std::forward<L>(lhs), std::forward<R>(rhs));
                }
            }
        } // namespace operator_hooks

        // Enhanced lambda/closure support with proper IR representation
        namespace functional {
            // Parameter list representation
            template<class... ParamTypes>
            struct parameter_list {
                std::tuple<ParamTypes...> params;

                template<class... Args>
                constexpr explicit parameter_list(Args &&... args)
                    : params(std::forward<Args>(args)...) {
                }

                static constexpr std::size_t arity = sizeof...(ParamTypes);
            };

            // Closure capture representation
            template<class... CapturedTypes>
            struct capture_list {
                std::tuple<CapturedTypes...> captures;

                template<class... Args>
                constexpr explicit capture_list(Args &&... args)
                    : captures(std::forward<Args>(args)...) {
                }

                static constexpr std::size_t capture_count = sizeof...(CapturedTypes);
            };

            // Enhanced lambda node with proper type information
            template<class ParamList, class CaptureList, class Body>
            struct lambda_node : interface<lambda_node<ParamList, CaptureList, Body> > {
                using is_lithe_node = void;
                using tag_type = lambda_tag;

                ParamList parameters;
                CaptureList captures;
                Body body;

                std::tuple<ParamList, CaptureList, Body> children;

                constexpr lambda_node(ParamList params, CaptureList caps, Body b)
                    : parameters(std::move(params)), captures(std::move(caps)), body(std::move(b))
                      , children(parameters, captures, body) {
                }

                // Type information
                static constexpr std::size_t arity = ParamList::arity;
                static constexpr std::size_t capture_count = CaptureList::capture_count;
            };

            // Lambda builder helpers
            template<class... Params>
            constexpr auto params(Params &&... p) {
                return parameter_list<std::decay_t<Params>...>{std::forward<Params>(p)...};
            }

            template<class... Captures>
            constexpr auto captures(Captures &&... c) {
                return capture_list<std::decay_t<Captures>...>{std::forward<Captures>(c)...};
            }

            // Lambda construction
            template<class ParamList, class CaptureList, class Body>
            constexpr auto make_lambda(ParamList params, CaptureList caps, Body body) {
                return lambda_node<ParamList, CaptureList, Body>{
                    std::move(params), std::move(caps), std::move(body)
                };
            }

            // Convenience overloads
            template<class ParamList, class Body>
            constexpr auto make_lambda(ParamList params, Body body) {
                return make_lambda(std::move(params), capture_list<>{}, std::move(body));
            }

            // Function application node
            template<class Function, class... Args>
            struct application_node : interface<application_node<Function, Args...> > {
                using is_lithe_node = void;
                using tag_type = call_tag;

                Function function;
                std::tuple<Args...> arguments;
                std::tuple<Function, Args...> children;

                template<class F, class... A>
                constexpr explicit application_node(F &&f, A &&... args)
                    : function(std::forward<F>(f))
                      , arguments(std::forward<A>(args)...)
                      , children(function, std::forward<A>(args)...) {
                }

                static constexpr std::size_t arg_count = sizeof...(Args);
            };

            // Application builder
            template<class Function, class... Args>
            constexpr auto apply(Function &&func, Args &&... args) {
                return application_node<std::decay_t<Function>, std::decay_t<Args>...>{
                    std::forward<Function>(func), std::forward<Args>(args)...
                };
            }
        } // namespace functional
    } // namespace dsl_extension

    // -----------------------------
    // Enhanced IRBuilder with Control Flow and DSL Support
    // -----------------------------
    namespace builder {
        // Enhanced IRBuilder class with comprehensive DSL building capabilities
        class IRBuilder {
        public:
            // Arithmetic operations
            template<class L, class R>
            static constexpr auto add(L &&left, R &&right) {
                return dsl_extension::operator_hooks::dispatch_binary_op(
                    std::forward<L>(left), std::forward<R>(right), add_tag{}
                );
            }

            template<class L, class R>
            static constexpr auto sub(L &&left, R &&right) {
                return dsl_extension::operator_hooks::dispatch_binary_op(
                    std::forward<L>(left), std::forward<R>(right), sub_tag{}
                );
            }

            template<class L, class R>
            static constexpr auto mul(L &&left, R &&right) {
                return dsl_extension::operator_hooks::dispatch_binary_op(
                    std::forward<L>(left), std::forward<R>(right), mul_tag{}
                );
            }

            template<class L, class R>
            static constexpr auto div(L &&left, R &&right) {
                return make_node<div_tag>(std::forward<L>(left), std::forward<R>(right));
            }

            template<class L, class R>
            static constexpr auto mod(L &&left, R &&right) {
                return make_node<mod_tag>(std::forward<L>(left), std::forward<R>(right));
            }

            // Unary operations
            template<class Operand>
            static constexpr auto neg(Operand &&operand) {
                return make_node<neg_tag>(std::forward<Operand>(operand));
            }

            template<class Operand>
            static constexpr auto logical_not(Operand &&operand) {
                return make_node<not_tag>(std::forward<Operand>(operand));
            }

            template<class Operand>
            static constexpr auto bitwise_not(Operand &&operand) {
                return make_node<bit_not_tag>(std::forward<Operand>(operand));
            }

            // Comparison operations
            template<class L, class R>
            static constexpr auto eq(L &&left, R &&right) {
                return make_node<eq_tag>(std::forward<L>(left), std::forward<R>(right));
            }

            template<class L, class R>
            static constexpr auto ne(L &&left, R &&right) {
                return make_node<ne_tag>(std::forward<L>(left), std::forward<R>(right));
            }

            template<class L, class R>
            static constexpr auto lt(L &&left, R &&right) {
                return make_node<lt_tag>(std::forward<L>(left), std::forward<R>(right));
            }

            template<class L, class R>
            static constexpr auto le(L &&left, R &&right) {
                return make_node<le_tag>(std::forward<L>(left), std::forward<R>(right));
            }

            template<class L, class R>
            static constexpr auto gt(L &&left, R &&right) {
                return make_node<gt_tag>(std::forward<L>(left), std::forward<R>(right));
            }

            template<class L, class R>
            static constexpr auto ge(L &&left, R &&right) {
                return make_node<ge_tag>(std::forward<L>(left), std::forward<R>(right));
            }

            // Logical operations
            template<class L, class R>
            static constexpr auto logical_and(L &&left, R &&right) {
                return make_node<and_tag>(std::forward<L>(left), std::forward<R>(right));
            }

            template<class L, class R>
            static constexpr auto logical_or(L &&left, R &&right) {
                return make_node<or_tag>(std::forward<L>(left), std::forward<R>(right));
            }

            // Bitwise operations
            template<class L, class R>
            static constexpr auto bitwise_and(L &&left, R &&right) {
                return make_node<bit_and_tag>(std::forward<L>(left), std::forward<R>(right));
            }

            template<class L, class R>
            static constexpr auto bitwise_or(L &&left, R &&right) {
                return make_node<bit_or_tag>(std::forward<L>(left), std::forward<R>(right));
            }

            template<class L, class R>
            static constexpr auto bitwise_xor(L &&left, R &&right) {
                return make_node<bit_xor_tag>(std::forward<L>(left), std::forward<R>(right));
            }

            template<class L, class R>
            static constexpr auto shift_left(L &&left, R &&right) {
                return make_node<shl_tag>(std::forward<L>(left), std::forward<R>(right));
            }

            template<class L, class R>
            static constexpr auto shift_right(L &&left, R &&right) {
                return make_node<shr_tag>(std::forward<L>(left), std::forward<R>(right));
            }

            // Array/subscript operations
            template<class Base, class... Indices>
            static constexpr auto subscript(Base &&base, Indices &&... indices) {
                return make_node<subscript_tag>(std::forward<Base>(base), std::forward<Indices>(indices)...);
            }

            // Enhanced control flow constructs
            template<class Condition, class ThenBranch, class ElseBranch>
            static constexpr auto if_then_else(Condition &&cond, ThenBranch &&then_stmt, ElseBranch &&else_stmt) {
                return make_node<if_tag>(std::forward<Condition>(cond),
                                         std::forward<ThenBranch>(then_stmt),
                                         std::forward<ElseBranch>(else_stmt));
            }

            template<class Condition, class ThenBranch>
            static constexpr auto if_then(Condition &&cond, ThenBranch &&then_stmt) {
                return make_node<if_tag>(std::forward<Condition>(cond),
                                         std::forward<ThenBranch>(then_stmt));
            }

            template<class Condition, class Body>
            static constexpr auto while_loop(Condition &&cond, Body &&body) {
                return make_node<while_tag>(std::forward<Condition>(cond), std::forward<Body>(body));
            }

            template<class Init, class Condition, class Update, class Body>
            static constexpr auto for_loop(Init &&init, Condition &&cond, Update &&update, Body &&body) {
                return make_node<for_tag>(std::forward<Init>(init),
                                          std::forward<Condition>(cond),
                                          std::forward<Update>(update),
                                          std::forward<Body>(body));
            }

            // Enhanced function calls with type safety
            template<class Function, class... Args>
            static constexpr auto call(Function &&func, Args &&... args) {
                return dsl_extension::functional::apply(std::forward<Function>(func), std::forward<Args>(args)...);
            }

            // Lambda construction helpers
            template<class... Params, class Body>
            static constexpr auto lambda(Body &&body, Params &&... params) {
                auto param_list = dsl_extension::functional::params(std::forward<Params>(params)...);
                return dsl_extension::functional::make_lambda(param_list, std::forward<Body>(body));
            }

            template<class... Params, class... Captures, class Body>
            static constexpr auto lambda_with_captures(Body &&body,
                                                       dsl_extension::functional::parameter_list<Params...> params,
                                                       dsl_extension::functional::capture_list<Captures...> caps) {
                return dsl_extension::functional::make_lambda(std::move(params), std::move(caps),
                                                              std::forward<Body>(body));
            }

            // Sequence construction
            template<class... Statements>
            static constexpr auto sequence(Statements &&... stmts) {
                return make_node<seq_tag>(std::forward<Statements>(stmts)...);
            }

            // Variable binding
            template<class Name, class Value, class Body>
            static constexpr auto let(Name &&name, Value &&value, Body &&body) {
                return make_node<let_tag>(std::forward<Name>(name),
                                          std::forward<Value>(value),
                                          std::forward<Body>(body));
            }

            // Constants and variables
            template<class T>
            static constexpr auto constant(T &&value) {
                return as_expr(std::forward<T>(value));
            }

            template<class T>
            static constexpr auto variable(T &&var) {
                return as_expr(std::forward<T>(var));
            }

            // Symbolic variable creation
            template<class T>
            static constexpr auto symbol(std::string name) {
                return dsl_extension::symbolic::symbol.create<T>(std::move(name));
            }

            // Named value creation with symbol table support
            template<class T>
            static constexpr auto named_value(const char *name, T &&value) {
                return as_expr(std::forward<T>(value));
            }

            // Custom tag node creation
            template<class CustomTag, class... Args>
            static constexpr auto custom_node(CustomTag tag, Args &&... args) {
                return make_node<CustomTag>(std::forward<Args>(args)...);
            }

            // Type casting with enhanced semantics
            template<class TargetType, class Expression>
            static constexpr auto cast_to(Expression &&expr) {
                return make_node<cast_tag>(std::forward<Expression>(expr));
            }

            // Memory operations
            template<class Expression>
            static constexpr auto deref(Expression &&expr) {
                return make_node<deref_tag>(std::forward<Expression>(expr));
            }

            template<class Expression>
            static constexpr auto address_of(Expression &&expr) {
                return make_node<addr_tag>(std::forward<Expression>(expr));
            }

            // Size operations
            template<class Expression>
            static constexpr auto size_of(Expression &&expr) {
                return make_node<sizeof_tag>(std::forward<Expression>(expr));
            }

            // Return statement
            template<class Expression>
            static constexpr auto return_stmt(Expression &&expr) {
                return make_node<return_tag>(std::forward<Expression>(expr));
            }

            // Block/scope construction
            template<class... Statements>
            static constexpr auto block(Statements &&... stmts) {
                return sequence(std::forward<Statements>(stmts)...);
            }

            // Pattern matching placeholder
            template<class Pattern, class Value, class... Cases>
            static constexpr auto match(Pattern &&pattern, Value &&value, Cases &&... cases) {
                // Simplified match - could be expanded with proper pattern matching IR
                return sequence(std::forward<Cases>(cases)...);
            }
        };

        // Global IRBuilder instance for convenience
        inline constexpr IRBuilder IR{};

        // DSL builder with domain-specific extensions
        template<class Domain>
        class domain_builder : public IRBuilder {
        public:
            // Domain-specific builder extensions can be added here
            // Derived classes can extend this for specific domains

            // Allow derived builders to add custom methods
            template<class... Args>
            constexpr auto domain_op(Args &&... args) {
                return static_cast<Domain *>(this)->custom_domain_op(std::forward<Args>(args)...);
            }
        };
    } // namespace builder

    // -----------------------------
    // Advanced EDSL Constructs
    // -----------------------------

    // Control flow helpers
    template<class Condition, class ThenBranch, class ElseBranch>
    constexpr auto if_else(Condition &&cond, ThenBranch &&then_branch, ElseBranch &&else_branch) {
        return make_node<if_tag>(std::forward<Condition>(cond),
                                 std::forward<ThenBranch>(then_branch),
                                 std::forward<ElseBranch>(else_branch));
    }

    template<class Condition, class Body>
    constexpr auto while_loop(Condition &&cond, Body &&body) {
        return make_node<while_tag>(std::forward<Condition>(cond), std::forward<Body>(body));
    }

    template<class Init, class Condition, class Update, class Body>
    constexpr auto for_loop(Init &&init, Condition &&cond, Update &&update, Body &&body) {
        return make_node<for_tag>(std::forward<Init>(init),
                                  std::forward<Condition>(cond),
                                  std::forward<Update>(update),
                                  std::forward<Body>(body));
    }

    // Variable binding
    template<class Name, class Value, class Body>
    constexpr auto let_binding(Name &&name, Value &&value, Body &&body) {
        return make_node<let_tag>(std::forward<Name>(name),
                                  std::forward<Value>(value),
                                  std::forward<Body>(body));
    }

    // Sequence operator
    template<class... Statements>
    constexpr auto sequence(Statements &&... stmts) {
        return make_node<seq_tag>(std::forward<Statements>(stmts)...);
    }

    // Function call
    template<class Function, class... Args>
    constexpr auto call(Function &&func, Args &&... args) {
        return make_node<call_tag>(std::forward<Function>(func), std::forward<Args>(args)...);
    }

    // Type casting
    template<class TargetType, class Expression>
    constexpr auto cast_to(Expression &&expr) {
        return make_node<cast_tag>(std::forward<Expression>(expr));
    }

    // Size operator
    template<class Expression>
    constexpr auto size_of(Expression &&expr) {
        return make_node<sizeof_tag>(std::forward<Expression>(expr));
    }

    // Memory access operators
    template<class Expression>
    constexpr auto dereference(Expression &&expr) {
        return make_node<deref_tag>(std::forward<Expression>(expr));
    }

    template<class Expression>
    constexpr auto address_of(Expression &&expr) {
        return make_node<addr_tag>(std::forward<Expression>(expr));
    }

    // Lambda expression
    template<class... Params, class Body>
    constexpr auto lambda(Body &&body, Params &&... params) {
        return make_node<lambda_tag>(std::forward<Body>(body), std::forward<Params>(params)...);
    }

    // Return statement
    template<class Expression>
    constexpr auto return_stmt(Expression &&expr) {
        return make_node<return_tag>(std::forward<Expression>(expr));
    }

    // -----------------------------
    // Advanced Pattern Matching
    // -----------------------------
    namespace patterns {
        // Pattern matching utilities
        template<class Pattern, class Value>
        struct pattern_match {
            Pattern pattern;
            Value value;

            template<class Visitor>
            constexpr auto apply(Visitor &&visitor) const {
                return visitor(pattern, value);
            }
        };

        // Create a pattern match
        template<class Pattern, class Value>
        constexpr auto match(Pattern &&pattern, Value &&value) {
            return pattern_match<std::decay_t<Pattern>, std::decay_t<Value> >{
                std::forward<Pattern>(pattern),
                std::forward<Value>(value)
            };
        }

        // Wildcard pattern
        struct wildcard_pattern {
        };

        constexpr inline wildcard_pattern _{};

        // Variable capture pattern
        template<class T>
        struct capture_pattern {
            T *target;

            constexpr explicit capture_pattern(T *t) : target(t) {
            }
        };

        template<class T>
        constexpr auto capture(T &var) {
            return capture_pattern<T>{&var};
        }
    } // namespace patterns

    // -----------------------------
    // Type-level computations and metaprogramming helpers
    // -----------------------------
    namespace meta {
        // Type list for compile-time type manipulation
        template<class... Ts>
        struct type_list {
            static constexpr std::size_t size = sizeof...(Ts);
        };

        // Get the nth type from a type list
        template<std::size_t N, class... Ts>
        using nth_type = std::tuple_element_t<N, std::tuple<Ts...> >;

        // Concatenate type lists
        template<class List1, class List2>
        struct concat;

        template<class... T1s, class... T2s>
        struct concat<type_list<T1s...>, type_list<T2s...> > {
            using type = type_list<T1s..., T2s...>;
        };

        template<class List1, class List2>
        using concat_t = concat<List1, List2>::type;

        // Filter types based on a predicate
        template<template <class> class Predicate, class... Ts>
        struct filter;

        template<template <class> class Predicate>
        struct filter<Predicate> {
            using type = type_list<>;
        };

        template<template <class> class Predicate, class T, class... Rest>
        struct filter<Predicate, T, Rest...> {
            using rest_type = filter<Predicate, Rest...>::type;
            using type = std::conditional_t<
                Predicate<T>::value,
                concat_t<type_list<T>, rest_type>,
                rest_type
            >;
        };

        template<template <class> class Predicate, class... Ts>
        using filter_t = filter<Predicate, Ts...>::type;
    } // namespace meta

    // -----------------------------
    // DSL Extensions and Combinators
    // -----------------------------
    namespace dsl {
        // Combinator for building complex expressions
        template<class Left, class Right>
        struct compose_expr {
            Left left;
            Right right;

            template<class Input>
            constexpr auto operator()(Input &&input) const {
                return right(left(std::forward<Input>(input)));
            }
        };

        template<class Left, class Right>
        constexpr auto compose(Left &&left, Right &&right) {
            return compose_expr<std::decay_t<Left>, std::decay_t<Right> >{
                std::forward<Left>(left),
                std::forward<Right>(right)
            };
        }

        // Partial application helper
        template<class Func, class... BoundArgs>
        struct partial_application {
            Func func;
            std::tuple<BoundArgs...> bound_args;

            template<class... RemainingArgs>
            constexpr auto operator()(RemainingArgs &&... remaining) const {
                return std::apply([&](auto &&... bound) {
                    return func(std::forward<decltype(bound)>(bound)...,
                                std::forward<RemainingArgs>(remaining)...);
                }, bound_args);
            }
        };

        template<class Func, class... BoundArgs>
        constexpr auto partial(Func &&func, BoundArgs &&... bound_args) {
            return partial_application<std::decay_t<Func>, std::decay_t<BoundArgs>...>{
                std::forward<Func>(func),
                std::tuple<std::decay_t<BoundArgs>...>{std::forward<BoundArgs>(bound_args)...}
            };
        }

        // Currying support
        template<class Func>
        struct curry_helper {
            Func func;

            template<class... Args>
            constexpr auto operator()(Args &&... args) const {
                if constexpr (std::is_invocable_v<Func, Args...>) {
                    return func(std::forward<Args>(args)...);
                } else {
                    return partial(func, std::forward<Args>(args)...);
                }
            }
        };

        template<class Func>
        constexpr auto curry(Func &&func) {
            return curry_helper<std::decay_t<Func> >{std::forward<Func>(func)};
        }
    } // namespace dsl

    // -----------------------------
    // Foundational Analysis Passes for Compiler Infrastructure
    // -----------------------------
    namespace analysis {
        // Cost/complexity analysis for guiding optimization decisions
        struct complexity_analyzer {
            mutable std::size_t node_count = 0;
            mutable std::size_t depth = 0;
            mutable std::size_t max_depth = 0;
            mutable std::unordered_map<std::size_t, std::size_t> operation_costs;

            template<class T>
            std::size_t on_terminal(T &&) {
                ++node_count;
                return 1; // Base cost for terminals
            }

            template<class Tag, class... Children>
            std::size_t on_node(Tag, Children &&... children) {
                ++depth;
                max_depth = std::max(max_depth, depth);
                ++node_count;

                // Calculate operation-specific costs
                std::size_t op_cost = get_operation_cost<Tag>();
                operation_costs[typeid(Tag).hash_code()] = op_cost;

                // Accumulate child costs
                std::size_t total_cost = op_cost;
                ((total_cost += children), ...);

                --depth;
                return total_cost;
            }

            template<class Tag>
            constexpr std::size_t get_operation_cost() const {
                if constexpr (std::is_same_v<Tag, add_tag> || std::is_same_v<Tag, sub_tag>)
                    return 1; // Low cost arithmetic
                else if constexpr (std::is_same_v<Tag, mul_tag>) {
                    return 2; // Medium cost
                } else if constexpr (std::is_same_v<Tag, div_tag> || std::is_same_v<Tag, mod_tag>) {
                    return 4; // High cost arithmetic
                } else if constexpr (std::is_same_v<Tag, shl_tag> || std::is_same_v<Tag, shr_tag>) {
                    return 1; // Cheap bitwise ops
                } else {
                    return 3; // Default medium cost
                }
            }

            // Analysis results
            std::size_t get_node_count() const { return node_count; }
            std::size_t get_max_depth() const { return max_depth; }

            std::size_t get_total_cost() const {
                std::size_t total = 0;
                for (const auto &[op, cost]: operation_costs) {
                    total += cost;
                }
                return total;
            }
        };

        // Usage frequency analysis for identifying optimization targets
        struct frequency_analyzer {
            mutable std::unordered_map<std::size_t, std::size_t> subexpr_frequencies;
            mutable std::unordered_map<std::size_t, std::size_t> operation_frequencies;

            template<class T>
            void on_terminal(T &&t) {
                auto hash = emit::structural_hash(t);
                ++subexpr_frequencies[hash];
            }

            template<class Tag, class... Children>
            void on_node(Tag, Children &&... children) {
                // Count operation frequency
                auto op_hash = typeid(Tag).hash_code();
                ++operation_frequencies[op_hash];

                // Visit children to build complete frequency map
                (on_terminal(children), ...);
            }

            // Query interface
            std::size_t get_frequency(std::size_t hash) const {
                auto it = subexpr_frequencies.find(hash);
                return it != subexpr_frequencies.end() ? it->second : 0;
            }

            std::size_t get_operation_frequency(std::size_t op_hash) const {
                auto it = operation_frequencies.find(op_hash);
                return it != operation_frequencies.end() ? it->second : 0;
            }

            // Find most common subexpressions for CSE targeting
            std::vector<std::pair<std::size_t, std::size_t> > get_common_subexprs(std::size_t min_frequency = 2) const {
                std::vector<std::pair<std::size_t, std::size_t> > result;
                for (const auto &[hash, freq]: subexpr_frequencies) {
                    if (freq >= min_frequency) {
                        result.emplace_back(hash, freq);
                    }
                }
                std::sort(result.begin(), result.end(),
                          [](const auto &a, const auto &b) { return a.second > b.second; });
                return result;
            }
        };

        // Side-effect detection for safe operation reordering
        struct side_effect_analyzer {
            enum class effect_type {
                pure, // No side effects - safe to reorder/eliminate
                read_only, // Only reads state - safe to reorder with other reads
                writes_local, // Writes to local state - careful reordering needed
                writes_global, // Writes to global state - very restricted reordering
                io_operation, // Performs I/O - no reordering across I/O boundary
                throws, // May throw exceptions - affects control flow
                terminates // May not terminate - affects liveness
            };

            mutable std::unordered_map<std::size_t, effect_type> expression_effects;

            template<class T>
            effect_type on_terminal(T &&t) {
                auto hash = emit::structural_hash(t);
                effect_type effect = classify_terminal_effects<std::decay_t<T> >();
                expression_effects[hash] = effect;
                return effect;
            }

            template<class Tag, class... Children>
            effect_type on_node(Tag, Children &&... children) {
                // Analyze operation's intrinsic effects
                effect_type op_effect = classify_operation_effects<Tag>();

                // Combine with children effects (take maximum severity)
                effect_type combined_effect = op_effect;
                ((combined_effect = combine_effects(combined_effect, children)), ...);

                return combined_effect;
            }

            template<class T>
            constexpr effect_type classify_terminal_effects() const {
                if constexpr (std::is_arithmetic_v<T>) {
                    return effect_type::pure; // Literals are pure
                } else {
                    return effect_type::read_only; // Variables read state
                }
            }

            template<class Tag>
            constexpr effect_type classify_operation_effects() const {
                if constexpr (std::is_same_v<Tag, add_tag> || std::is_same_v<Tag, sub_tag> ||
                              std::is_same_v<Tag, mul_tag> || std::is_same_v<Tag, neg_tag> ||
                              std::is_same_v<Tag, shl_tag> || std::is_same_v<Tag, shr_tag>) {
                    return effect_type::pure; // Pure arithmetic
                } else if constexpr (std::is_same_v<Tag, div_tag> || std::is_same_v<Tag, mod_tag>) {
                    return effect_type::throws; // Division can throw on zero
                } else if constexpr (std::is_same_v<Tag, call_tag>) {
                    return effect_type::writes_global; // Function calls assumed impure
                } else {
                    return effect_type::read_only; // Conservative default
                }
            }

            static constexpr effect_type combine_effects(effect_type a, effect_type b) {
                return static_cast<effect_type>(
                    std::max(static_cast<int>(a), static_cast<int>(b))
                );
            }

            // Query interface
            bool is_pure(std::size_t hash) const {
                auto it = expression_effects.find(hash);
                return it != expression_effects.end() && it->second == effect_type::pure;
            }

            bool can_reorder_with(std::size_t hash1, std::size_t hash2) const {
                auto it1 = expression_effects.find(hash1);
                auto it2 = expression_effects.find(hash2);

                if (it1 == expression_effects.end() || it2 == expression_effects.end()) {
                    return false; // Conservative: unknown effects
                }

                // Simple reordering rules
                effect_type e1 = it1->second, e2 = it2->second;

                if (e1 == effect_type::pure && e2 == effect_type::pure) return true;
                if (e1 == effect_type::read_only && e2 == effect_type::read_only) return true;
                if ((e1 == effect_type::pure && e2 == effect_type::read_only) ||
                    (e1 == effect_type::read_only && e2 == effect_type::pure))
                    return true;

                return false; // Conservative: don't reorder other combinations
            }
        };

        // Dependency analysis for transformation scheduling
        struct dependency_analyzer {
            struct dependency_info {
                std::unordered_set<std::size_t> depends_on; // What this expression depends on
                std::unordered_set<std::size_t> depended_by; // What depends on this expression
                bool has_data_dependency = false;
                bool has_control_dependency = false;
                bool has_anti_dependency = false; // Write-after-read
                bool has_output_dependency = false; // Write-after-write
            };

            mutable std::unordered_map<std::size_t, dependency_info> dependency_graph;
            mutable std::vector<std::size_t> topological_order;

            template<class T>
            void on_terminal(T &&t) {
                auto hash = emit::structural_hash(t);
                dependency_graph[hash]; // Ensure entry exists
            }

            template<class Tag, class... Children>
            void on_node(Tag, Children &&... children) const {
                auto expr_hash = calculate_node_hash<Tag>(children...);
                auto &deps = dependency_graph[expr_hash];

                // Add dependencies on children
                ((add_dependency(expr_hash, emit::structural_hash(children))), ...);

                // Analyze dependency types
                classify_dependencies<Tag>(expr_hash, children...);
            }

            template<class Tag, class... Children>
            std::size_t calculate_node_hash(Children &&... children) const {
                // Simplified hash calculation
                std::size_t h = typeid(Tag).hash_code();
                ((h = emit::hash_combine(h, emit::structural_hash(children))), ...);
                return h;
            }

            void add_dependency(std::size_t dependent, std::size_t dependency) const {
                dependency_graph[dependent].depends_on.insert(dependency);
                dependency_graph[dependency].depended_by.insert(dependent);
                dependency_graph[dependent].has_data_dependency = true;
            }

            template<class Tag, class... Children>
            void classify_dependencies(std::size_t expr_hash, Children &&... children) const {
                auto &info = dependency_graph[expr_hash];

                if constexpr (std::is_same_v<Tag, if_tag>) {
                    info.has_control_dependency = true;
                } else if constexpr (std::is_same_v<Tag, call_tag>) {
                    info.has_output_dependency = true; // Function calls may write
                }
                // Add more sophisticated classification as needed
            }

            // Build topological ordering for safe transformation scheduling
            std::vector<std::size_t> get_safe_execution_order() const {
                topological_order.clear();
                std::unordered_set<std::size_t> visited;
                std::unordered_set<std::size_t> temp_visited;

                for (const auto &[hash, info]: dependency_graph) {
                    if (visited.find(hash) == visited.end()) {
                        if (!topological_visit(hash, visited, temp_visited)) {
                            // Cycle detected - return empty order
                            return {};
                        }
                    }
                }

                std::reverse(topological_order.begin(), topological_order.end());
                return topological_order;
            }

            bool topological_visit(std::size_t node,
                                   std::unordered_set<std::size_t> &visited,
                                   std::unordered_set<std::size_t> &temp_visited) const {
                if (temp_visited.find(node) != temp_visited.end()) {
                    return false; // Cycle detected
                }
                if (visited.find(node) != visited.end()) {
                    return true; // Already processed
                }

                temp_visited.insert(node);

                auto it = dependency_graph.find(node);
                if (it != dependency_graph.end()) {
                    for (auto dep: it->second.depends_on) {
                        if (!topological_visit(dep, visited, temp_visited)) {
                            return false;
                        }
                    }
                }

                temp_visited.erase(node);
                visited.insert(node);
                topological_order.push_back(node);
                return true;
            }

            // Query interface
            bool has_cycle() const {
                return get_safe_execution_order().empty() && !dependency_graph.empty();
            }

            bool can_execute_concurrently(std::size_t hash1, std::size_t hash2) const {
                auto it1 = dependency_graph.find(hash1);
                auto it2 = dependency_graph.find(hash2);

                if (it1 == dependency_graph.end() || it2 == dependency_graph.end()) {
                    return false;
                }

                // Check if either depends on the other
                return it1->second.depends_on.find(hash2) == it1->second.depends_on.end() &&
                       it2->second.depends_on.find(hash1) == it2->second.depends_on.end();
            }
        };

        // Structural DAG construction from ASTs
        struct dag_constructor {
            struct dag_node {
                std::size_t id;
                std::string operation;
                std::vector<std::size_t> operands;
                std::size_t use_count = 0;
                bool is_terminal = false;
            };

            mutable std::unordered_map<std::size_t, dag_node> dag_nodes;
            mutable std::unordered_map<std::size_t, std::size_t> hash_to_dag_id;
            mutable std::size_t next_id = 1;

            template<class T>
            std::size_t on_terminal(T &&t) {
                auto hash = emit::structural_hash(t);

                if (auto it = hash_to_dag_id.find(hash); it != hash_to_dag_id.end()) {
                    ++dag_nodes[it->second].use_count;
                    return it->second;
                }

                auto id = next_id++;
                dag_node node{id, emit::dump(t), {}, 1, true};
                dag_nodes[id] = std::move(node);
                hash_to_dag_id[hash] = id;
                return id;
            }

            template<class Tag, class... Children>
            std::size_t on_node(Tag, Children &&... children) {
                // Get operand IDs
                std::vector<std::size_t> operand_ids = {children...};

                // Create a canonical representation
                auto expr_hash = calculate_expression_hash<Tag>(operand_ids);

                if (auto it = hash_to_dag_id.find(expr_hash); it != hash_to_dag_id.end()) {
                    ++dag_nodes[it->second].use_count;
                    return it->second;
                }

                auto id = next_id++;
                dag_node node{id, get_operation_name<Tag>(), operand_ids, 1, false};
                dag_nodes[id] = std::move(node);
                hash_to_dag_id[expr_hash] = id;
                return id;
            }

            template<class Tag>
            std::string get_operation_name() const {
                if constexpr (std::is_same_v<Tag, add_tag>) return "+";
                else if constexpr (std::is_same_v<Tag, sub_tag>) return "-";
                else if constexpr (std::is_same_v<Tag, mul_tag>) return "*";
                else if constexpr (std::is_same_v<Tag, div_tag>) return "/";
                else if constexpr (std::is_same_v<Tag, neg_tag>) return "neg";
                else return "op";
            }

            template<class Tag>
            std::size_t calculate_expression_hash(const std::vector<std::size_t> &operands) const {
                std::size_t h = typeid(Tag).hash_code();
                for (auto op_id: operands) {
                    h = emit::hash_combine(h, op_id);
                }
                return h;
            }

            // Generate DOT representation for visualization
            std::string generate_dot() const {
                std::string dot = "digraph DAG {\n";

                for (const auto &[id, node]: dag_nodes) {
                    if (node.is_terminal) {
                        dot += "  " + std::to_string(id) + " [label=\"" + node.operation +
                                " (×" + std::to_string(node.use_count) + ")\", shape=box];\n";
                    } else {
                        dot += "  " + std::to_string(id) + " [label=\"" + node.operation +
                                " (×" + std::to_string(node.use_count) + ")\"];\n";

                        for (auto operand: node.operands) {
                            dot += "  " + std::to_string(operand) + " -> " + std::to_string(id) + ";\n";
                        }
                    }
                }

                dot += "}\n";
                return dot;
            }

            // Query interface
            std::size_t get_node_count() const { return dag_nodes.size(); }

            std::size_t get_shared_nodes() const {
                std::size_t count = 0;
                for (const auto &[id, node]: dag_nodes) {
                    if (node.use_count > 1) ++count;
                }
                return count;
            }

            double get_sharing_ratio() const {
                return get_node_count() > 0 ? static_cast<double>(get_shared_nodes()) / get_node_count() : 0.0;
            }
        };

        // Combined analysis runner
        template<class Expr>
        struct analysis_results {
            std::size_t node_count{};
            std::size_t max_depth{};
            std::size_t total_cost{};
            std::vector<std::pair<std::size_t, std::size_t> > common_subexprs;
            bool has_dependency_cycles{};
            double dag_sharing_ratio{};

            // Convenience accessors
            [[nodiscard]] bool is_optimization_worthwhile() const {
                return total_cost > 10 ||
                       !common_subexprs.empty() ||
                       dag_sharing_ratio > 0.3;
            }

            [[nodiscard]] std::vector<std::size_t> get_optimization_targets() const {
                std::vector<std::size_t> targets;
                for (const auto &hash: common_subexprs | std::views::keys) {
                    targets.push_back(hash);
                }
                return targets;
            }
        };

        template<class Expr>
        analysis_results<Expr> analyze(const Expr &expr) {
            complexity_analyzer complexity;
            frequency_analyzer frequency;
            side_effect_analyzer effects;
            dependency_analyzer dependencies;
            dag_constructor dag;

            // Run all analyses
            lithe::visit(expr, complexity);
            lithe::visit(expr, frequency);
            lithe::visit(expr, effects);
            lithe::visit(expr, dependencies);
            lithe::visit(expr, dag);

            return analysis_results<Expr>{
                complexity.get_node_count(),
                complexity.get_max_depth(),
                complexity.get_total_cost(),
                frequency.get_common_subexprs(2),
                dependencies.has_cycle(),
                dag.get_sharing_ratio()
            };
        }
    } // namespace analysis

    // -----------------------------
    // Advanced Optimization Passes for Serious Transformations
    // -----------------------------
    namespace passes {
        // -----------------------------
        // Constant Propagation Pass
        // -----------------------------
        struct constant_propagation_rule {
            // Track known constant values through expressions
            mutable std::unordered_map<std::size_t, double> known_values;

            template<class T>
            constexpr decltype(auto) on_terminal(T &&t) const {
                return std::forward<T>(t);
            }

            // Propagate constants through binary operations
            template<class A, class B, class TA, class TB>
            constexpr auto on_node(add_tag, A &&origA, B &&origB, TA ta, TB tb) const {
                using node_t = decltype(lithe::rebuild<add_tag>(std::forward<A>(origA), std::forward<B>(origB)));
                using altA_t = std::decay_t<TA>;
                using altB_t = std::decay_t<TB>;

                // If both operands are known constants, fold them
                if constexpr (std::is_arithmetic_v<altA_t> && std::is_arithmetic_v<altB_t>) {
                    using result_t = std::decay_t<decltype(ta + tb)>;
                    using var_t = std::variant<node_t, result_t>;
                    return var_t{std::in_place_index<1>, static_cast<result_t>(ta + tb)};
                }

                // If one operand is zero, propagate the other
                if constexpr (std::is_arithmetic_v<altB_t>) {
                    if (tb == 0) {
                        using var_t = std::variant<node_t, altA_t>;
                        return var_t{std::in_place_index<1>, ta};
                    }
                }
                if constexpr (std::is_arithmetic_v<altA_t>) {
                    if (ta == 0) {
                        using var_t = std::variant<node_t, altB_t>;
                        return var_t{std::in_place_index<1>, tb};
                    }
                }

                using var_t = std::variant<node_t>;
                return var_t{
                    std::in_place_index<0>, lithe::rebuild<add_tag>(std::forward<A>(origA), std::forward<B>(origB))
                };
            }

            template<class A, class B, class TA, class TB>
            constexpr auto on_node(mul_tag, A &&origA, B &&origB, TA ta, TB tb) const {
                using node_t = decltype(lithe::rebuild<mul_tag>(std::forward<A>(origA), std::forward<B>(origB)));
                using altA_t = std::decay_t<TA>;
                using altB_t = std::decay_t<TB>;

                // If both operands are known constants, fold them
                if constexpr (std::is_arithmetic_v<altA_t> && std::is_arithmetic_v<altB_t>) {
                    using result_t = std::decay_t<decltype(ta * tb)>;
                    using var_t = std::variant<node_t, result_t>;
                    return var_t{std::in_place_index<1>, static_cast<result_t>(ta * tb)};
                }

                // If one operand is zero, result is zero
                if constexpr (std::is_arithmetic_v<altB_t>) {
                    if (tb == 0) {
                        using var_t = std::variant<node_t, altB_t>;
                        return var_t{std::in_place_index<1>, tb};
                    }
                    if (tb == 1) {
                        using var_t = std::variant<node_t, altA_t>;
                        return var_t{std::in_place_index<1>, ta};
                    }
                }
                if constexpr (std::is_arithmetic_v<altA_t>) {
                    if (ta == 0) {
                        using var_t = std::variant<node_t, altA_t>;
                        return var_t{std::in_place_index<1>, ta};
                    }
                    if (ta == 1) {
                        using var_t = std::variant<node_t, altB_t>;
                        return var_t{std::in_place_index<1>, tb};
                    }
                }

                using var_t = std::variant<node_t>;
                return var_t{
                    std::in_place_index<0>, lithe::rebuild<mul_tag>(std::forward<A>(origA), std::forward<B>(origB))
                };
            }

            // Fallback: rebuild other nodes
            template<class Tag, class... Args>
            constexpr decltype(auto) on_node(Tag, Args &&... args) const {
                constexpr std::size_t N = sizeof...(Args);
                static_assert(N % 2 == 0);
                auto tup = std::forward_as_tuple(std::forward<Args>(args)...);
                return [&]<std::size_t... I>(std::index_sequence<I...>) {
                    return lithe::rebuild<Tag>(std::get<I>(tup)...);
                }(std::make_index_sequence<N / 2>{});
            }
        };

        struct constant_propagation_pass {
            template<class E>
            constexpr auto operator()(E &&e) const {
                return rewrite_once(std::forward<E>(e), constant_propagation_rule{});
            }
        };

        // -----------------------------
        // Comprehensive Strength Reduction Rules
        // -----------------------------
        struct strength_reduction_rule {
            template<class T>
            constexpr decltype(auto) on_terminal(T &&t) const {
                return std::forward<T>(t);
            }

            // Helper to check if a value is a power of 2
            static constexpr bool is_power_of_two(auto value) {
                if constexpr (std::is_integral_v<decltype(value)>) {
                    return value > 0 && (value & (value - 1)) == 0;
                }
                return false;
            }

            // Helper to get log2 of a power of 2
            static constexpr int get_power_of_two_exponent(auto value) {
                if constexpr (std::is_integral_v<decltype(value)>) {
                    int result = 0;
                    auto v = value;
                    while (v > 1) {
                        v >>= 1;
                        ++result;
                    }
                    return result;
                }
                return 0;
            }

            // x * 2^n -> x << n (for integers)
            template<class A, class B, class TA, class TB>
            constexpr auto on_node(mul_tag, A &&origA, B &&origB, TA ta, TB tb) const {
                using node_t = decltype(lithe::rebuild<mul_tag>(std::forward<A>(origA), std::forward<B>(origB)));
                using altA_t = std::decay_t<TA>;
                using altB_t = std::decay_t<TB>;

                // Check if multiplying by power of 2
                if constexpr (std::is_integral_v<altB_t>) {
                    if (is_power_of_two(tb)) {
                        int shift_amount = get_power_of_two_exponent(tb);
                        using shift_node_t = decltype(lithe::rebuild<shl_tag>(std::forward<A>(origA), shift_amount));
                        using var_t = std::variant<node_t, shift_node_t>;
                        return var_t{
                            std::in_place_index<1>, lithe::rebuild<shl_tag>(std::forward<A>(origA), shift_amount)
                        };
                    }
                }
                if constexpr (std::is_integral_v<altA_t>) {
                    if (is_power_of_two(ta)) {
                        int shift_amount = get_power_of_two_exponent(ta);
                        using shift_node_t = decltype(lithe::rebuild<shl_tag>(std::forward<B>(origB), shift_amount));
                        using var_t = std::variant<node_t, shift_node_t>;
                        return var_t{
                            std::in_place_index<1>, lithe::rebuild<shl_tag>(std::forward<B>(origB), shift_amount)
                        };
                    }
                }

                // x * x -> x^2 pattern (could be extended to pow function)
                // For now, detect simple squaring patterns
                if (emit::structural_equal(ta, tb)) {
                    // Could replace with pow(x, 2) or keep as x*x for simplicity
                    using var_t = std::variant<node_t>;
                    return var_t{
                        std::in_place_index<0>, lithe::rebuild<mul_tag>(std::forward<A>(origA), std::forward<B>(origB))
                    };
                }

                using var_t = std::variant<node_t>;
                return var_t{
                    std::in_place_index<0>, lithe::rebuild<mul_tag>(std::forward<A>(origA), std::forward<B>(origB))
                };
            }

            // x / 2^n -> x >> n (for integers)
            template<class A, class B, class TA, class TB>
            constexpr auto on_node(div_tag, A &&origA, B &&origB, TA ta, TB tb) const {
                using node_t = decltype(lithe::rebuild<div_tag>(std::forward<A>(origA), std::forward<B>(origB)));
                using altB_t = std::decay_t<TB>;

                // Check if dividing by power of 2
                if constexpr (std::is_integral_v<altB_t>) {
                    if (is_power_of_two(tb)) {
                        int shift_amount = get_power_of_two_exponent(tb);
                        using shift_node_t = decltype(lithe::rebuild<shr_tag>(std::forward<A>(origA), shift_amount));
                        using var_t = std::variant<node_t, shift_node_t>;
                        return var_t{
                            std::in_place_index<1>, lithe::rebuild<shr_tag>(std::forward<A>(origA), shift_amount)
                        };
                    }
                }

                using var_t = std::variant<node_t>;
                return var_t{
                    std::in_place_index<0>, lithe::rebuild<div_tag>(std::forward<A>(origA), std::forward<B>(origB))
                };
            }

            // Fallback: rebuild other nodes
            template<class Tag, class... Args>
            constexpr decltype(auto) on_node(Tag, Args &&... args) const {
                constexpr std::size_t N = sizeof...(Args);
                static_assert(N % 2 == 0);
                auto tup = std::forward_as_tuple(std::forward<Args>(args)...);
                return [&]<std::size_t... I>(std::index_sequence<I...>) {
                    return lithe::rebuild<Tag>(std::get<I>(tup)...);
                }(std::make_index_sequence<N / 2>{});
            }
        };

        struct strength_reduction_pass {
            template<class E>
            constexpr auto operator()(E &&e) const {
                return rewrite_once(std::forward<E>(e), strength_reduction_rule{});
            }
        };

        // -----------------------------
        // Enhanced Algebraic Canonicalization
        // -----------------------------
        struct enhanced_algebraic_canonicalization_rule {
            template<class T>
            constexpr decltype(auto) on_terminal(T &&t) const {
                return std::forward<T>(t);
            }

            // Helper to extract numeric value for comparison
            template<class U>
            static constexpr auto extract_numeric_value(const U &v) -> std::optional<double> {
                using V = std::decay_t<U>;
                if constexpr (std::is_arithmetic_v<V>) {
                    return static_cast<double>(v);
                } else if constexpr (requires { v.value; }) {
                    using ValT = std::decay_t<decltype(v.value)>;
                    if constexpr (std::is_arithmetic_v<ValT>) {
                        return static_cast<double>(v.value);
                    }
                } else if constexpr (requires { v.p; }) {
                    using Pointee = std::decay_t<decltype(*v.p)>;
                    if constexpr (std::is_arithmetic_v<Pointee>) {
                        return static_cast<double>(*v.p);
                    }
                }
                return std::nullopt;
            }

            // Canonicalize addition: sort operands by complexity/value
            template<class A, class B, class TA, class TB>
            constexpr auto on_node(add_tag, A &&origA, B &&origB, TA ta, TB tb) const {
                auto val_a = extract_numeric_value(ta);
                auto val_b = extract_numeric_value(tb);

                // If both are numeric, sort by value
                if (val_a && val_b) {
                    if (*val_a <= *val_b) {
                        return lithe::rebuild<add_tag>(std::forward<A>(origA), std::forward<B>(origB));
                    } else {
                        return lithe::rebuild<add_tag>(std::forward<B>(origB), std::forward<A>(origA));
                    }
                }

                // If only one is numeric, put non-numeric first (variables before constants)
                if (val_a && !val_b) {
                    return lithe::rebuild<add_tag>(std::forward<B>(origB), std::forward<A>(origA));
                }
                if (!val_a && val_b) {
                    return lithe::rebuild<add_tag>(std::forward<A>(origA), std::forward<B>(origB));
                }

                // Both non-numeric: could use structural hash for consistent ordering
                auto hash_a = emit::structural_hash(ta);
                auto hash_b = emit::structural_hash(tb);
                if (hash_a <= hash_b) {
                    return lithe::rebuild<add_tag>(std::forward<A>(origA), std::forward<B>(origB));
                } else {
                    return lithe::rebuild<add_tag>(std::forward<B>(origB), std::forward<A>(origA));
                }
            }

            // Canonicalize multiplication similarly
            template<class A, class B, class TA, class TB>
            constexpr auto on_node(mul_tag, A &&origA, B &&origB, TA ta, TB tb) const {
                auto val_a = extract_numeric_value(ta);
                auto val_b = extract_numeric_value(tb);

                // If both are numeric, sort by value
                if (val_a && val_b) {
                    if (*val_a <= *val_b) {
                        return lithe::rebuild<mul_tag>(std::forward<A>(origA), std::forward<B>(origB));
                    } else {
                        return lithe::rebuild<mul_tag>(std::forward<B>(origB), std::forward<A>(origA));
                    }
                }

                // If only one is numeric, put non-numeric first
                if (val_a && !val_b) {
                    return lithe::rebuild<mul_tag>(std::forward<B>(origB), std::forward<A>(origA));
                }
                if (!val_a && val_b) {
                    return lithe::rebuild<mul_tag>(std::forward<A>(origA), std::forward<B>(origB));
                }

                // Both non-numeric: use structural hash for ordering
                auto hash_a = emit::structural_hash(ta);
                auto hash_b = emit::structural_hash(tb);
                if (hash_a <= hash_b) {
                    return lithe::rebuild<mul_tag>(std::forward<A>(origA), std::forward<B>(origB));
                } else {
                    return lithe::rebuild<mul_tag>(std::forward<B>(origB), std::forward<A>(origA));
                }
            }

            // Fallback: rebuild other nodes
            template<class Tag, class... Args>
            constexpr decltype(auto) on_node(Tag, Args &&... args) const {
                constexpr std::size_t N = sizeof...(Args);
                static_assert(N % 2 == 0);
                auto tup = std::forward_as_tuple(std::forward<Args>(args)...);
                return [&]<std::size_t... I>(std::index_sequence<I...>) {
                    return lithe::rebuild<Tag>(std::get<I>(tup)...);
                }(std::make_index_sequence<N / 2>{});
            }
        };

        struct enhanced_algebraic_canonicalization_pass {
            template<class E>
            constexpr auto operator()(E &&e) const {
                return rewrite_once(std::forward<E>(e), enhanced_algebraic_canonicalization_rule{});
            }
        };

        // -----------------------------
        // Dead Subtree Elimination
        // -----------------------------
        struct dead_subtree_elimination_rule {
            // Track live/dead subtrees
            mutable std::unordered_set<std::size_t> live_subtrees;

            template<class T>
            constexpr decltype(auto) on_terminal(T &&t) const {
                return std::forward<T>(t);
            }

            // Mark subtrees as live if they have side effects or are used
            template<class Tag, class... Args>
            constexpr auto on_node(Tag, Args &&... args) const {
                constexpr std::size_t N = sizeof...(Args);
                static_assert(N % 2 == 0);
                auto tup = std::forward_as_tuple(std::forward<Args>(args)...);

                // Rebuild the node first
                auto rebuilt = [&]<std::size_t... I>(std::index_sequence<I...>) {
                    return lithe::rebuild<Tag>(std::get<I>(tup)...);
                }(std::make_index_sequence<N / 2>{});

                // Mark this subtree as live (could be more sophisticated)
                auto hash = emit::structural_hash(rebuilt);
                live_subtrees.insert(hash);

                return rebuilt;
            }

            // Remove dead code in sequences
            template<class... Args>
            constexpr auto on_node(seq_tag, Args &&... args) const {
                constexpr std::size_t N = sizeof...(Args);
                static_assert(N % 2 == 0);
                auto tup = std::forward_as_tuple(std::forward<Args>(args)...);

                // For now, just rebuild - could filter out dead statements
                return [&]<std::size_t... I>(std::index_sequence<I...>) {
                    return lithe::rebuild<seq_tag>(std::get<I>(tup)...);
                }(std::make_index_sequence<N / 2>{});
            }
        };

        struct dead_subtree_elimination_pass {
            template<class E>
            constexpr auto operator()(E &&e) const {
                return rewrite_once(std::forward<E>(e), dead_subtree_elimination_rule{});
            }
        };

        // Legacy aliases for compatibility
        using dead_code_elimination_rule = dead_subtree_elimination_rule;
        using dead_code_elimination_pass = dead_subtree_elimination_pass;

        // -----------------------------
        // True Common Subexpression Elimination
        //
        // Conceptually, this pass converts tree IR into a shared DAG so structurally
        // equal subtrees map to the same node id and can be reused (CSE).
        // The current pass keeps tree carrier compatibility and returns the input
        // unchanged; callers can materialize shared form explicitly via graph::build_dag.
        // -----------------------------
        struct true_cse_pass {
            template<class E>
            constexpr auto operator()(E &&e) const {
                // For now, return the expression unchanged to maintain compatibility
                // with existing code that expects to evaluate the result
                return std::forward<E>(e);
            }
        };

        // Legacy alias for compatibility
        using cse_pass = true_cse_pass;

        // -----------------------------
        // Enhanced Memoized Pass Execution
        // -----------------------------
        template<class Pass, class IR>
        struct enhanced_memoize_pass {
            Pass p;
            std::unordered_map<std::size_t, IR> *cache;
            mutable std::size_t hits = 0;
            mutable std::size_t misses = 0;

            template<class E>
            IR operator()(E &&e) const {
                auto h = emit::structural_hash(e);

                // Check cache first
                if (auto it = cache->find(h); it != cache->end()) {
                    ++hits;
                    return it->second;
                }

                // Cache miss - compute result
                ++misses;
                IR out = p(std::forward<E>(e));

                // Store in cache
                cache->insert_or_assign(h, out);
                return out;
            }

            // Statistics
            double hit_rate() const {
                auto total = hits + misses;
                return total > 0 ? static_cast<double>(hits) / total : 0.0;
            }
        };

        // Enhanced memoization with statistics
        template<class Pass, class IR>
        constexpr auto enhanced_memoize(Pass p, std::unordered_map<std::size_t, IR> &cache) {
            return enhanced_memoize_pass<Pass, IR>{std::move(p), std::addressof(cache), 0, 0};
        }
    } // namespace passes

    // Add preset optimization levels after passes namespace is complete
    namespace compiler {
        // -----------------------------
        // Preset Optimization Levels (O0, O1, O2)
        // -----------------------------
        enum class opt_level {
            O0, // No optimization - identity
            O1, // Basic optimizations
            OG1, // Graph-based optimizations with CSE
            O2 // Aggressive optimizations
        };

        // Apply optimization preset to expression
        template<class E>
        constexpr auto optimize_preset(E &&e, opt_level level) {
            switch (level) {
                case opt_level::O0:
                    // No optimization - return identity
                    return std::forward<E>(e);

                case opt_level::O1:
                    // Basic optimizations: algebraic simplifications
                    return compile(
                        std::forward<E>(e),
                        passes::fixpoint(passes::simplify_add_zero_pass{}, 4),
                        passes::fixpoint(passes::simplify_mul_identity_pass{}, 4),
                        passes::fixpoint(passes::constant_fold_arith_pass{}, 4)
                    );

                case opt_level::OG1:
                    // Graph-based optimizations with CSE
                    return compile(
                        std::forward<E>(e),
                        passes::fixpoint(passes::simplify_add_zero_pass{}, 6),
                        passes::fixpoint(passes::simplify_mul_identity_pass{}, 6),
                        passes::canonicalize_commutative_pass{},
                        passes::fixpoint(passes::constant_fold_arith_pass{}, 6),
                        passes::fixpoint(passes::true_cse_pass{}, 4)
                    );

                case opt_level::O2:
                    // Aggressive optimizations: include CSE and canonicalization
                    return compile(
                        std::forward<E>(e),
                        passes::fixpoint(passes::simplify_add_zero_pass{}, 8),
                        passes::fixpoint(passes::simplify_mul_identity_pass{}, 8),
                        passes::fixpoint(passes::constant_fold_arith_pass{}, 8),
                        passes::canonicalize_commutative_pass{},
                        passes::fixpoint(passes::true_cse_pass{}, 6),
                        passes::dead_code_elimination_pass{}
                    );

                default:
                    return std::forward<E>(e);
            }
        }
    } // namespace compiler

    // -----------------------------
    // Top-level ergonomic pass objects and compile forwarding
    // -----------------------------

    // Forwarding helper so users can write lithe::compile(expr, pass...)
    // instead of lithe::compiler::compile(expr, pass...).
    template<class E, class... Passes>
    constexpr auto compile(E &&e, Passes &&... ps) {
        return compiler::compile(std::forward<E>(e), std::forward<Passes>(ps)...);
    }

    struct O0 {
        template<class E>
        constexpr auto operator()(E &&e) const {
            return std::forward<E>(e);
        }
    };

    struct O1 {
        int max_iters = 8;

        template<class E>
        constexpr auto operator()(E &&e) const {
            auto pipeline = passes::pipeline(
                passes::simplify_add_zero_pass{},
                passes::simplify_mul_identity_pass{},
                passes::simplify_add_zero_pass{},
                passes::canonicalize_commutative_pass{}
            );
            return pipeline(std::forward<E>(e));
        }
    };

    struct O2 {
        int max_iters = 8;

        template<class E>
        constexpr auto operator()(E &&e) const {
            auto pipeline = passes::pipeline(
                O1{max_iters},
                passes::fixpoint(passes::strength_reduction_pass{}, max_iters),
                passes::fixpoint(passes::true_cse_pass{}, max_iters)
            );
            return pipeline(std::forward<E>(e));
        }
    };

    // -----------------------------
    // AST Backend Integration using Modernized Graph/Tree Framework
    // -----------------------------
    namespace backend {
        // AST node representation using NAryTree
        struct ASTNodeData {
            std::string operation;
            std::variant<double, int, std::string> value;
            std::size_t node_id;
            std::string op_;

            constexpr explicit ASTNodeData(std::string op = "", std::size_t id = 0)
                : operation(std::move(op)), node_id(id), op_(std::move(op)) {
            }

            constexpr bool operator==(const ASTNodeData &other) const {
                return operation == other.operation && value == other.value;
            }
        };

        using ASTTree = NAryTree<ASTNodeData, EmptyMetadata>;
        using ASTNode = ASTTree::TreeNode;

        // Convert lithe expression to AST tree representation
        struct ast_builder {
            mutable ASTTree tree;
            mutable std::size_t next_id = 1;

            template<class T>
            ASTNode *on_terminal(T &&t) const {
                ASTNodeData data{"terminal", next_id++};

                if constexpr (std::is_arithmetic_v<std::decay_t<T> >) {
                    if constexpr (std::is_integral_v<std::decay_t<T> >) {
                        data.value = static_cast<int>(t);
                    } else {
                        data.value = static_cast<double>(t);
                    }
                } else {
                    data.value = std::string("var");
                }

                if (!tree.get_root()) {
                    return tree.insert(nullptr, std::move(data));
                } else {
                    // This is a child - would need parent context in real implementation
                    return nullptr;
                }
            }

            template<class Tag, class... Children>
            ASTNode *on_node(Tag, Children &&... children) const {
                ASTNodeData data{get_operation_name<Tag>(), next_id++};

                ASTNode *node;
                if (!tree.get_root()) {
                    node = tree.insert(nullptr, std::move(data));
                } else {
                    // Would need proper parent tracking
                    node = nullptr;
                }

                // Process children recursively
                ((void) children, ...); // Placeholder for child processing

                return node;
            }

            template<class Tag>
            std::string get_operation_name() const {
                if constexpr (std::is_same_v<Tag, add_tag>) return "add";
                else if constexpr (std::is_same_v<Tag, sub_tag>) return "sub";
                else if constexpr (std::is_same_v<Tag, mul_tag>) return "mul";
                else if constexpr (std::is_same_v<Tag, div_tag>) return "div";
                else if constexpr (std::is_same_v<Tag, neg_tag>) return "neg";
                else return "unknown";
            }
        };

        // Control Flow Graph representation using LiteGraph
        struct BasicBlock {
            std::size_t block_id;
            std::vector<std::string> instructions;

            constexpr bool operator==(const BasicBlock &other) const {
                return block_id == other.block_id;
            }

            constexpr auto operator<=>(const BasicBlock &other) const = default;
        };

        struct ControlEdge {
            enum class Type { Fallthrough, Branch, Jump, Return };

            Type edge_type = Type::Fallthrough;
            std::optional<std::string> condition;

            constexpr bool operator==(const ControlEdge &other) const {
                return edge_type == other.edge_type && condition == other.condition;
            }

            constexpr auto operator<=>(const ControlEdge &other) const = default;
        };

        // Forward declare CFG
        using CFG = litegraph::Graph<BasicBlock, ControlEdge>;

        // Simplified CFG builder to avoid API conflicts
        struct cfg_builder {
            mutable std::vector<BasicBlock> blocks;
            mutable std::size_t next_block_id = 1;

            template<class T>
            std::size_t on_terminal(T &&) const {
                blocks.emplace_back(BasicBlock{next_block_id++, {"terminal"}});
                return blocks.size() - 1;
            }

            template<class Tag, class... Args>
            std::size_t on_node(Tag, Args &&... args) const {
                blocks.emplace_back(BasicBlock{next_block_id++, {get_operation_name<Tag>()}});
                return blocks.size() - 1;
            }

            template<class Tag>
            std::string get_operation_name() const {
                if constexpr (std::is_same_v<Tag, add_tag>) return "add";
                else if constexpr (std::is_same_v<Tag, sub_tag>) return "sub";
                else if constexpr (std::is_same_v<Tag, mul_tag>) return "mul";
                else if constexpr (std::is_same_v<Tag, seq_tag>) return "sequence";
                else return "operation";
            }
        };

        // Symbolic computation backend using DAG
        struct SymbolicExpression {
            enum class Type { Variable, Constant, Operation };

            Type type;
            std::string name;
            std::variant<double, int, std::string> value;
            std::size_t expr_id{};

            constexpr bool operator==(const SymbolicExpression &other) const {
                return expr_id == other.expr_id && type == other.type && name == other.name;
            }

            constexpr auto operator<=>(const SymbolicExpression &other) const = default;
        };

        struct DependencyEdge {
            enum class Type { DataFlow, ControlFlow, AntiDep, OutputDep };

            Type dep_type = Type::DataFlow;

            constexpr bool operator==(const DependencyEdge &other) const {
                return dep_type == other.dep_type;
            }

            constexpr auto operator<=>(const DependencyEdge &other) const = default;
        };

        using SymbolicDAG = litegraph::Graph<SymbolicExpression, DependencyEdge>;

        // DAG builder for symbolic computation
        struct symbolic_dag_builder {
            mutable SymbolicDAG dag;
            mutable std::size_t next_expr_id = 1;
            mutable std::unordered_map<std::size_t, litegraph::NodeId> hash_to_vertex;

            template<class T>
            litegraph::NodeId on_terminal(T &&t) const {
                auto hash = emit::structural_hash(t);

                // Check if we've seen this terminal before (CSE)
                if (auto it = hash_to_vertex.find(hash); it != hash_to_vertex.end()) {
                    return it->second;
                }

                SymbolicExpression expr;
                expr.expr_id = next_expr_id++;

                if constexpr (std::is_arithmetic_v<std::decay_t<T> >) {
                    expr.type = SymbolicExpression::Type::Constant;
                    expr.name = "const";
                    if constexpr (std::is_integral_v<std::decay_t<T> >) {
                        expr.value = static_cast<int>(t);
                    } else {
                        expr.value = static_cast<double>(t);
                    }
                } else {
                    expr.type = SymbolicExpression::Type::Variable;
                    expr.name = "var";
                    expr.value = std::string("unknown");
                }

                auto vertex_id = dag.add_node(std::move(expr));
                hash_to_vertex[hash] = vertex_id;
                return vertex_id;
            }

            template<class Tag, class... Children>
            litegraph::NodeId on_node(Tag, Children &&... children) const {
                // Calculate hash for this operation + children
                std::size_t op_hash = typeid(Tag).hash_code();
                ((op_hash = emit::hash_combine(op_hash, emit::structural_hash(children))), ...);

                // Check for existing equivalent expression (true CSE)
                if (auto it = hash_to_vertex.find(op_hash); it != hash_to_vertex.end()) {
                    return it->second;
                }

                SymbolicExpression expr;
                expr.type = SymbolicExpression::Type::Operation;
                expr.name = get_operation_name<Tag>();
                expr.expr_id = next_expr_id++;

                auto vertex_id = dag.add_node(std::move(expr));
                hash_to_vertex[op_hash] = vertex_id;

                // Add dependencies to child expressions
                ((add_dependency(vertex_id, children)), ...);

                return vertex_id;
            }

            template<class Child>
            void add_dependency(litegraph::NodeId parent, Child &&child) const {
                auto child_id = on_terminal(child); // This will recurse properly in real implementation
                dag.add_edge(child_id, parent, DependencyEdge{DependencyEdge::Type::DataFlow});
            }

            template<class Tag>
            std::string get_operation_name() const {
                if constexpr (std::is_same_v<Tag, add_tag>) return "add";
                else if constexpr (std::is_same_v<Tag, sub_tag>) return "sub";
                else if constexpr (std::is_same_v<Tag, mul_tag>) return "mul";
                else if constexpr (std::is_same_v<Tag, div_tag>) return "div";
                else return "op";
            }
        };

        // Unified backend interface
        struct unified_backend {
            ast_builder ast;
            cfg_builder cfg;
            symbolic_dag_builder symbolic;

            template<class Expr>
            auto analyze_expression(const Expr &expr) const {
                struct analysis_result {
                    ASTTree ast_tree;
                    CFG control_flow;
                    SymbolicDAG symbolic_dag;

                    // Analysis summary
                    [[nodiscard]] std::size_t complexity_score() const {
                        return ast_tree.size() + control_flow.node_count() + symbolic_dag.node_count();
                    }

                    [[nodiscard]] bool has_control_flow() const {
                        return control_flow.node_count() > 1;
                    }

                    [[nodiscard]] double cse_benefit() const {
                        auto total_nodes = symbolic_dag.node_count();
                        // In real implementation, count shared nodes
                        return total_nodes > 0 ? 0.3 : 0.0; // Placeholder
                    }
                };

                // Run all backend analyses
                visit(expr, ast);
                visit(expr, cfg);
                visit(expr, symbolic);

                return analysis_result{
                    std::move(ast.tree),
                    CFG{}, // Create empty CFG for now
                    std::move(symbolic.dag)
                };
            }
        };
    } // namespace backend

    // -----------------------------
    // Advanced Graph-Based Optimization Pipeline
    // -----------------------------
    namespace optimization {
        // Graph-based pass scheduler using topological ordering
        struct dependency_aware_scheduler {
            struct PassInfo {
                std::string name;
                std::size_t pass_id;
                std::unordered_set<std::size_t> prerequisites;
                bool is_analysis = false;

                constexpr bool operator==(const PassInfo &other) const {
                    return pass_id == other.pass_id;
                }

                constexpr bool operator<(const PassInfo &other) const {
                    return pass_id < other.pass_id;
                }
            };

            struct DependencyEdge {
                enum class Type { Required, Optional, Invalidates };

                Type dep_type = Type::Required;

                constexpr bool operator==(const DependencyEdge &other) const {
                    return dep_type == other.dep_type;
                }
            };

            mutable std::vector<PassInfo> passes;
            mutable std::size_t next_pass_id = 1;

            // Register a pass with its dependencies
            template<class PassType>
            std::size_t register_pass(const std::string &name,
                                      const std::vector<std::string> &dependencies = {},
                                      bool is_analysis = false) const {
                PassInfo info{name, next_pass_id++, {}, is_analysis};
                passes.push_back(std::move(info));
                return passes.size() - 1;
            }

            // Get optimal pass execution order
            std::vector<std::string> get_optimal_ordering() const {
                std::vector<std::string> ordering;

                // Simple ordering for now - topological sort would be implemented here
                for (const auto &pass: passes) {
                    ordering.push_back(pass.name);
                }

                return ordering;
            }

            // Check for circular dependencies
            static bool has_circular_dependencies() {
                // Simple implementation - would use graph algorithms in full version
                return false;
            }
        };

        // Pattern matching using tree/graph isomorphism
        struct pattern_matcher {
            backend::ASTTree pattern_tree;

            template<class Pattern>
            explicit pattern_matcher(const Pattern &pattern) {
                // Convert pattern to AST tree
                backend::ast_builder builder;
                visit(pattern, builder);
                pattern_tree = std::move(builder.tree);
            }

            // Check if expression matches pattern using tree isomorphism
            template<class Expr>
            bool matches(const Expr &expr) const {
                backend::ast_builder builder;
                visit(expr, builder);

                // Use structural equality from tree framework
                return pattern_tree.structural_equal(builder.tree);
            }

            // Find all subexpressions matching pattern
            template<class Expr>
            std::vector<std::size_t> find_matches(const Expr &expr) const {
                std::vector<std::size_t> matches;

                // In full implementation, would use subgraph isomorphism
                // from graph algorithms to find all matching substructures

                return matches;
            }
        };

        // Enhanced rewrite rules using graph-based pattern matching
        struct graph_rewrite_rule {
            pattern_matcher pattern;
            std::function<std::any(const std::any &)> rewriter;

            template<class Pattern, class Rewriter>
            graph_rewrite_rule(Pattern &&pat, Rewriter &&rw)
                : pattern(std::forward<Pattern>(pat))
                  , rewriter([rw = std::forward<Rewriter>(rw)](const std::any &expr) -> std::any {
                      // Would need proper type erasure handling here
                      return std::any{};
                  }) {
            }

            template<class Expr>
            bool can_apply(const Expr &expr) const {
                return pattern.matches(expr);
            }

            template<class Expr>
            auto apply(const Expr &expr) const {
                if (can_apply(expr)) {
                    // Apply rewrite rule
                    return rewriter(std::any{expr});
                }
                return std::any{expr};
            }
        };
    } // namespace optimization

    // -----------------------------
    // Integration Interface - Easy-to-use API
    // -----------------------------
    namespace integration {
        // Main integration point - enhanced compilation with graph analysis
        template<class Expr>
        struct enhanced_compilation_result {
            using original_type = std::decay_t<Expr>;

            Expr optimized_expr;
            // Placeholder for backend analysis - would be properly typed in full implementation
            std::string backend_analysis;
            analysis::analysis_results<Expr> frontend_analysis;

            // Query interface
            [[nodiscard]] bool should_use_cse() const {
                return frontend_analysis.dag_sharing_ratio > 0.2;
            }

            [[nodiscard]] bool has_optimization_opportunities() const {
                return frontend_analysis.is_optimization_worthwhile();
            }

            [[nodiscard]] std::vector<std::string> get_optimization_recommendations() const {
                std::vector<std::string> recommendations;

                if (should_use_cse()) {
                    recommendations.emplace_back("Enable common subexpression elimination");
                }
                if (!frontend_analysis.common_subexprs.empty()) {
                    recommendations.emplace_back("Consider control flow optimizations");
                }
                if (frontend_analysis.total_cost > 20) {
                    recommendations.emplace_back("Apply strength reduction");
                }

                return recommendations;
            }
        };

        // Enhanced compile function with full graph analysis
        template<class Expr, class... Passes>
        auto compile_with_analysis(Expr &&expr, Passes &&... passes) {
            // Run frontend analysis
            auto frontend_analysis = analysis::analyze(expr);

            // Run backend analysis
            backend::unified_backend backend;
            auto backend_analysis = backend.analyze_expression(expr);

            // Apply optimization passes
            auto optimized = compiler::compile(std::forward<Expr>(expr), std::forward<Passes>(passes)...);

            return enhanced_compilation_result<std::decay_t<Expr> >{
                std::move(optimized),
                std::move(backend_analysis),
                std::move(frontend_analysis)
            };
        }

        // Smart optimization - automatically choose best passes based on analysis
        template<class Expr>
        auto smart_optimize(Expr &&expr) {
            auto analysis_result = analysis::analyze(expr);

            if (analysis_result.total_cost < 5) {
                // Simple expression - minimal optimization
                return compiler::optimize_preset(std::forward<Expr>(expr), compiler::opt_level::O1);
            } else if (analysis_result.is_optimization_worthwhile()) {
                // Complex expression - aggressive optimization
                return compiler::optimize_preset(std::forward<Expr>(expr), compiler::opt_level::O2);
            } else {
                // Medium complexity - balanced optimization
                return compiler::compile(
                    std::forward<Expr>(expr),
                    passes::fixpoint(passes::constant_fold_arith_pass{}, 6),
                    passes::canonicalize_commutative_pass{},
                    passes::fixpoint(passes::simplify_add_zero_pass{}, 4),
                    passes::fixpoint(passes::simplify_mul_identity_pass{}, 4)
                );
            }
        }

        // Pattern-based optimization
        template<class Expr, class Pattern, class Replacement>
        auto optimize_with_pattern(Expr &&expr, Pattern &&pattern, Replacement &&replacement) {
            optimization::pattern_matcher matcher(std::forward<Pattern>(pattern));

            if (matcher.matches(expr)) {
                // Apply replacement
                return std::forward<Replacement>(replacement);
            } else {
                // No match - return original
                return std::forward<Expr>(expr);
            }
        }
    } // namespace integration
} // namespace lithe

// Template specializations must be in the correct namespaces

// Specialize is_terminal for symbolic variables
namespace lithe {
    template<class T>
    struct is_terminal<dsl_extension::symbolic::symbolic_var<T> > : std::true_type {
    };
}

// Hash specializations implementation
namespace std {
    template<class T>
    struct hash<lithe::dsl_extension::symbolic::symbolic_var<T> > {
        std::size_t operator()(const lithe::dsl_extension::symbolic::symbolic_var<T> &var) const {
            return std::hash<std::string>{}(var.name) ^ std::hash<std::size_t>{}(var.id);
        }
    };

    // Implement hash specializations for backend types
    inline std::size_t hash<lithe::backend::BasicBlock>::operator()(
        const lithe::backend::BasicBlock &block) const noexcept {
        return std::hash<std::size_t>{}(block.block_id);
    }

    inline std::size_t hash<lithe::backend::ControlEdge>::operator()(
        const lithe::backend::ControlEdge &edge) const noexcept {
        std::size_t h1 = std::hash<int>{}(static_cast<int>(edge.edge_type));
        std::size_t h2 = edge.condition ? std::hash<std::string>{}(*edge.condition) : 0;
        return h1 ^ (h2 << 1);
    }

    inline std::size_t hash<lithe::backend::SymbolicExpression>::operator()(
        const lithe::backend::SymbolicExpression &expr) const noexcept {
        std::size_t h1 = std::hash<std::size_t>{}(expr.expr_id);
        std::size_t h2 = std::hash<std::string>{}(expr.name);
        std::size_t h3 = std::hash<int>{}(static_cast<int>(expr.type));
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }

    inline std::size_t hash<lithe::backend::DependencyEdge>::operator()(
        const lithe::backend::DependencyEdge &edge) const noexcept {
        return std::hash<int>{}(static_cast<int>(edge.dep_type));
    }
}

// Allow plain terminals (user types in global namespace) to use `x + 5`
// by providing narrowly-constrained global forwarding operators that call
// into lithe::operator+ / lithe::operator*.
// Keep constraints strict: RHS must be an Operand, LHS must be a lithe terminal
// that is NOT an arithmetic type and not already an Expression (avoid hijacking).
template<class L, class R>
    requires (!lithe::Expression<std::remove_cvref_t<L> >)
             && lithe::is_terminal<std::remove_cvref_t<L> >::value
             && (!std::is_arithmetic_v<std::remove_cvref_t<L> >)
             && lithe::Operand<R>
             && (!lithe::has_member_plus<L, R>)
constexpr auto operator+(L &&l, R &&r) {
    return lithe::operator+(std::forward<L>(l), std::forward<R>(r));
}

template<class L, class R>
    requires (!lithe::Expression<std::remove_cvref_t<L> >)
             && lithe::is_terminal<std::remove_cvref_t<L> >::value
             && (!std::is_arithmetic_v<std::remove_cvref_t<L> >)
             && lithe::Operand<R>
             && (!lithe::has_member_mul<L, R>)
constexpr auto operator*(L &&l, R &&r) {
    return lithe::operator*(std::forward<L>(l), std::forward<R>(r));
}
