# 🧩 Modern C++ Graph & Tree Libraries — Comprehensive User & Design Guide

<a name="overview"></a>
## 🚀 Overview

This suite of lightweight, header-only C++ libraries provides essential building blocks for graph, tree, and hierarchical data modeling, analysis, and algorithmics. Optimized for **C++23**, **high performance**, **zero-cost abstractions**, and **modern design patterns**, they cover:

- **DominatorTree.hpp** — Functions to compute dominator trees (litegraph::compute_dominator_tree) and helper queries (litegraph::dominator_analysis) using a modern Lengauer–Tarjan implementation
- **NAryTree.hpp** — Memory-safe N-ary tree with smart pointers and modern traversal patterns
- **LiteGraph.hpp** — High-performance graph with CRTP patterns and constexpr operations
- **LiteGraphAlgorithms.hpp** — Comprehensive algorithm suite with STL execution policies

<a name="table-of-contents"></a>
## 📑 Table of contents

- [Overview](#overview)
- [Library Summaries](#library-summaries)
  - [DominatorTree.hpp](#dominatoretreehpp)
  - [NAryTree.hpp](#narytreehpp)
  - [LiteGraph.hpp](#litegraphhpp)
  - [LiteGraphAlgorithms.hpp](#litegraphalgorithmshpp)
- [Design Deep Dives & Modern C++ Features](#design-deep-dives)
- [Advanced Integration Patterns](#advanced-integration-patterns)
- [Graph Algorithms Quick Index](#graph-algorithms-quick-index)
- [Comprehensive Algorithm Reference](#comprehensive-algorithm-reference)
- [Best Practices & Performance Guidelines](#best-practices-performance-guidelines)
- [Quick Reference & Examples](#quick-reference-examples)


### Key Design Principles

✅ **C++23 Modern**: No virtual functions, no macros, extensive use of concepts and constexpr  
✅ **High Performance**: CRTP patterns, zero-cost abstractions, cache-friendly data structures  
✅ **Single Header Modular**: Each component is self-contained but seamlessly interoperable  
✅ **Parallelism Ready**: STL execution policies integrated throughout  
✅ **Rich Algorithm Suite**: 15+ essential graph algorithms with modern implementations  
✅ **Solid Architecture**: Strong type safety with compile-time guarantees  

### Ideal Use Cases

- **Compiler/Language Development**: Control-flow analysis, AST processing, optimization passes
- **Dependency Resolution**: Build systems, package managers, workflow orchestration
- **Game Development**: AI graphs, scene hierarchies, pathfinding systems
- **Scientific Computing**: Network analysis, simulation graphs, data flow modeling
- **Educational Projects**: Algorithm visualization, graph theory learning

---

<a name="library-summaries"></a>
## 📚 Library Summaries

<a name="dominatoretreehpp"></a>
### DominatorTree.hpp
**Purpose**: Compute dominator relationships for directed graphs using a modern Lengauer–Tarjan implementation.  

**Design**: Provides a free function `litegraph::compute_dominator_tree(const Graph&, NodeId)` which returns an `NAryTree<NodeId>` encoding the dominator tree, plus helpers in `litegraph::dominator_analysis` for dominance queries and lowest-common-dominator style helpers.  

**Modern Features**: Concepts-constrained APIs, constexpr-friendly helpers, and an efficient DSU-backed semidominator computation.

<a name="narytreehpp"></a>
### NAryTree.hpp  
**Purpose**: Memory-safe N-ary tree with smart pointer management and flexible traversal strategies

**Design**: RAII-compliant with automatic memory management, iterator support, and serialization. The `NAryTree<T>` type exposes insertion methods (`insert`, `emplace`), range-friendly iterators (pre/post order), and serialization helpers.

**Modern Features**: Range-based iteration and C++23-friendly APIs. Note: the tree iterators yield `TreeNode` references (not pairs), so structured bindings like `[id, data]` are not produced by the iterator directly — access `node.data` / `node.node_id` instead.

<a name="litegraphhpp"></a>
### LiteGraph.hpp
**Purpose**: High-performance directed/undirected graph with adjacency list representation  
**Design**: Cache-friendly data layout, strong ID types, constexpr graph operations  
**Modern Features**: CRTP optimizations, concepts for type safety, parallel-ready iterators  

<a name="litegraphalgorithmshpp"></a>
### LiteGraphAlgorithms.hpp
**Purpose**: Comprehensive suite of graph algorithms with STL execution policy support  
**Design**: Generic templates working with any graph representation, stateless and composable  
**Modern Features**: Parallel algorithm variants, concept constraints, deterministic behavior  

---

<a name="design-deep-dives"></a>
## 🔬 Design Deep Dives & Modern C++ Features

### DominatorTree: Advanced Control Flow Analysis

```cpp
#include "LiteGraph.hpp"
#include "DominatorTree.hpp"

// Build a control-flow graph using litegraph::Graph and compute the dominator tree.
using namespace litegraph;

litegraph::SimpleGraph build_cfg() {
    litegraph::SimpleGraph cfg;
    auto n0 = cfg.add_node();
    auto n1 = cfg.add_node();
    auto n2 = cfg.add_node();
    auto n3 = cfg.add_node();

    cfg.add_edge(n0, n1);
    cfg.add_edge(n1, n2);
    cfg.add_edge(n0, n3);
    cfg.add_edge(n3, n2);

    return cfg;
}

int main() {
    auto cfg = build_cfg();
    // Compute dominator tree with entry node n0
    auto dom_tree = litegraph::compute_dominator_tree(cfg, litegraph::NodeId{0});

    // Query dominance using helpers
    bool dominates = litegraph::dominator_analysis::dominates(dom_tree, litegraph::NodeId{0}, litegraph::NodeId{2});
}
```

**Key Improvements**:
- **Strong ID Types**: `NodeId` and `EdgeId` with hash support and ordering
- **Constexpr Support**: Many operations can be evaluated at compile time
- **Zero-Cost Queries**: All domination queries are O(1) after construction
- **Memory Efficient**: Minimal memory overhead with cache-friendly layout

### NAryTree: Modern Hierarchical Data Structures

```cpp
#include "NAryTree.hpp"
// Modern tree with automatic memory management
NAryTree<ExpressionNode> ast(ExpressionNode{BinaryOp::Add}); // construct with root
auto root = ast.get_root();

// Pre-order iteration: iterator yields TreeNode references
for (const auto &node : ast) {
    process_node(node.data);
}

// Range-based traversal for leaves
for (const auto &leaf : ast.leaves()) {
    std::invoke([](const auto &n) { process_node(n.data); }, leaf);
}

// Serialization support
std::ostringstream oss;
ast.serialize(oss);
NAryTree<ExpressionNode> loaded_ast;
std::istringstream iss(oss.str());
loaded_ast.deserialize(iss);
```

**Key Improvements**:
- **Smart Pointer Management**: Automatic memory cleanup with RAII
- **Subtree Cloning**: Deep copy constructor for subtree operations
- **Serialization Support**: Text-based format with custom payload support
- **Iterator Support**: Standard iterator patterns with range-based loops

### LiteGraph: High-Performance Graph Operations

```cpp
#include "LiteGraph.hpp"
using namespace litegraph;

// Create a simple directed graph (SimpleGraph is an alias for Graph<>)
SimpleGraph g;
auto n1 = g.add_node();
auto n2 = g.add_node();
g.add_edge(n1, n2);

// Query counts
constexpr size_t vcount = g.node_count();
size_t ecount = g.edge_count();

// Use concepts in algorithm signatures
template<LiteGraphModel G>
void analyze_connectivity(const G &graph) {
    // Algorithm implementation with concept constraints
}
```

**Key Improvements**:
- **CRTP Optimization**: Zero-cost customization through inheritance
- **Strong Type Safety**: NodeId and EdgeId prevent mixing different graph types
- **Constexpr Operations**: Many graph operations can be evaluated at compile time
- **Cache-Friendly Layout**: Adjacency list optimized for memory locality

### LiteGraphAlgorithms: Modern Algorithm Suite

```cpp
#include "LiteGraphAlgorithms.hpp"
#include <execution>

// Parallel-ready algorithms with execution policies
// Parallel-ready algorithms with execution policies
template<typename Graph>
auto parallel_shortest_paths(const Graph &g, litegraph::NodeId source) {
    return litegraph::parallel::parallel_dijkstra(std::execution::par_unseq, g, source);
}

// Deterministic topological sorting
std::vector<litegraph::NodeId> nodes = {litegraph::NodeId{0}, litegraph::NodeId{1}, litegraph::NodeId{2}, litegraph::NodeId{3}};
std::vector<std::pair<litegraph::NodeId, litegraph::NodeId>> edges = {
    {litegraph::NodeId{0}, litegraph::NodeId{2}}, {litegraph::NodeId{1}, litegraph::NodeId{2}}, {litegraph::NodeId{2}, litegraph::NodeId{3}}
};

std::vector<litegraph::NodeId> topo_order;
std::vector<litegraph::NodeId> cycle_nodes;

bool is_acyclic = akriti::graph::layout_extras::stable_toposort_kahn_by_index(
    nodes, edges, topo_order, &cycle_nodes
);

// Deterministic tie-breaking: smallest NodeId chosen when multiple zero-indegree nodes exist
```

**Algorithm Coverage**:
- **Traversal**: DFS, BFS with parallel variants
- **Shortest Path**: Dijkstra, Floyd-Warshall, BFS for unweighted graphs  
- **Connectivity**: Strongly Connected Components, weakly connected components
- **Topological**: Kahn's algorithm with deterministic tie-breaking
- **Cycle Detection**: Both directed and undirected cycle detection
- **Matching**: Maximum matching algorithms
- **Flow**: Max flow implementations

---

<a name="advanced-integration-patterns"></a>
## 🍳 Advanced Integration Patterns

### 1. Complete Control Flow Analysis Pipeline

```cpp
#include "LiteGraph.hpp"
#include "DominatorTree.hpp"
#include "LiteGraphAlgorithms.hpp"

class ControlFlowAnalyzer {
private:
    litegraph::SimpleGraph cfg_;

public:
    void build_cfg(const std::vector<BasicBlock> &blocks) {
        // Build control flow graph from basic blocks
        for (const auto &block: blocks) {
            cfg_.add_node(); // node data can store block payload if desired
            for (const auto &successor: block.successors) {
                cfg_.add_edge(litegraph::NodeId{block.id}, litegraph::NodeId{successor});
            }
        }
    }

    // Compute dominator tree on-demand and return it (NAryTree<NodeId>)
    auto compute_dominance(litegraph::NodeId entry) {
        return litegraph::compute_dominator_tree(cfg_, entry);
    }

    std::vector<litegraph::NodeId> find_optimization_candidates(litegraph::NodeId entry) {
        auto dom_tree = compute_dominance(entry);
        std::vector<litegraph::NodeId> candidates;

        // Find nodes that dominate multiple successors
        for (const auto &[nid_val, node_obj]: cfg_.nodes()) {
            litegraph::NodeId node{nid_val};
            size_t dominated_successors = 0;
            for (const auto &succ: cfg_.neighbors(node)) {
                if (litegraph::dominator_analysis::dominates(dom_tree, node, succ)) {
                    ++dominated_successors;
                }
            }
            if (dominated_successors > 1) candidates.push_back(node);
        }

        return candidates;
    }
};
```

### 2. Lithe EDSL Integration with Modern Graph Backend

The Lithe EDSL framework now leverages all modernized graph components:

```cpp
// Phase 1: AST Backend using NAryTree
template<typename ExprType>
class LitheASTBackend {
    NAryTree<ExprType> ast_;
    std::unordered_map<std::string, litegraph::NodeId> symbol_table_;

public:
    // Create a node containing `expr` and return its NodeId (node_id is assigned internally)
    litegraph::NodeId create_expression(const ExprType &expr) {
        auto *node = ast_.insert(nullptr, expr);
        return litegraph::NodeId{node->node_id};
    }

    // Add a child expression under a parent NodeId; returns the new child's NodeId
    litegraph::NodeId add_child_expression(litegraph::NodeId parent_id, const ExprType &expr) {
        auto *parent = ast_.find_if([&](const auto &n) { return n.node_id == parent_id.value; });
        auto *child = ast_.insert(parent, expr);
        return litegraph::NodeId{child->node_id};
    }

    template<typename Visitor>
    void visit_postorder(Visitor &&visitor) {
        for (const auto &node: ast_.post_order()) {
            visitor(node);
        }
    }
};

// Phase 2: Control Flow Analysis using LiteGraph  
class LitheControlFlow {
    litegraph::SimpleGraph flow_graph_;

public:
    void add_control_edge(litegraph::NodeId from, litegraph::NodeId to) {
        flow_graph_.add_edge(from, to);
    }

    // Analyze flow by computing dominator tree for the given entry point
    auto analyze_flow(litegraph::NodeId entry_point) {
        return litegraph::compute_dominator_tree(flow_graph_, entry_point);
    }

    bool can_optimize_branch(litegraph::NodeId branch_node, const NAryTree<litegraph::NodeId> &dom_tree) {
        // Use dominator information for optimization decisions
        const auto &successors = flow_graph_.neighbors(branch_node);
        return std::all_of(successors.begin(), successors.end(), [&](litegraph::NodeId succ) {
            return litegraph::dominator_analysis::dominates(dom_tree, branch_node, succ);
        });
    }
};

// Phase 3: Comprehensive DSL Integration
template<typename SymbolType, typename ExprType>
class LitheDSLFramework {
private:
    LitheASTBackend<ExprType> ast_backend_;
    LitheControlFlow control_flow_;
    std::unordered_map<NodeId, SymbolType> symbol_map_;
    
public:
    // High-level DSL operations backed by modern graph structures
    NodeId define_symbol(const std::string& name, const SymbolType& symbol) {
        auto node = ast_backend_.create_expression(ExprType::create_symbol(name));
        symbol_map_[node] = symbol;
        return node;
    }
    
    NodeId create_binary_op(NodeId left, NodeId right, BinaryOperation op) {
        auto op_node = ast_backend_.create_expression(ExprType::create_binary(op));
        ast_backend_.add_child_expression(op_node, left);
        ast_backend_.add_child_expression(op_node, right);
        
        // Add control flow dependencies
        control_flow_.add_control_edge(left, op_node);
        control_flow_.add_control_edge(right, op_node);
        
        return op_node;
    }
    
    // Advanced optimization using dominator analysis
    void optimize_expression_tree(NodeId root) {
        control_flow_.analyze_flow(root);
        
        // Use dominator tree for dead code elimination
        ast_backend_.visit_postorder([&](NodeId node, const ExprType& expr) {
            if (can_eliminate_node(node)) {
                // Safe to remove - no other expressions depend on this
                remove_node_optimization(node);
            }
        });
    }
};
```

### 3. Parallel Graph Processing

```cpp
#include <execution>
#include "LiteGraphAlgorithms.hpp"

template<typename Graph>
class ParallelGraphProcessor {
public:
    // Parallel shortest path computation
    auto compute_all_pairs_shortest_paths(const Graph& g) {
        const auto nodes = g.get_all_nodes();
        std::vector<std::unordered_map<NodeId, int>> all_distances(nodes.size());
        
        std::transform(std::execution::par_unseq, 
                      nodes.begin(), nodes.end(), all_distances.begin(),
                      [&](NodeId source) {
                          return dijkstra(g.adjacency_list(), source);
                      });
        
        return all_distances;
    }
    
    // Parallel connected component analysis
    auto find_connected_components_parallel(const Graph& g) {
        return strongly_connected_components(std::execution::par, g.adjacency_list());
    }
    
    // Parallel topological processing
    template<typename ProcessFunc>
    void parallel_topological_process(const Graph& g, ProcessFunc processor) {
        auto topo_order = topological_sort(g.adjacency_list());
        
        // Process in topologically sorted batches for parallelism
        for (size_t level = 0; level < topo_order.size(); ++level) {
            std::vector<NodeId> current_level;
            // Collect nodes at current topological level
            
            std::for_each(std::execution::par_unseq,
                         current_level.begin(), current_level.end(),
                         processor);
        }
    }
};
```

---

<a name="comprehensive-algorithm-reference"></a>
## 🧪 Comprehensive Algorithm Reference

<a name="graph-algorithms-quick-index"></a>
## 📌 Graph Algorithms Quick Index

This quick index maps high-level graph algorithms to the primary functions provided in the headers. Use this as a rapid lookup when implementing or porting algorithms.

| Algorithm | Primary function / location | Notes |
|---|---:|---|
| Breadth-first search (BFS) | `litegraph::bfs(const GraphT&, NodeId, Fn)` | Visitor-based traversal |
| Depth-first search (DFS) | `litegraph::dfs(const GraphT&, NodeId, Fn)` | Iterative stack-based traversal |
| Dijkstra (single-source) | `litegraph::dijkstra(const GraphT&, NodeId, weight_fn)` | Returns (dist, pred) |
| A* search | `litegraph::a_star_search(const GraphT&, NodeId, NodeId, weight_fn, heuristic_fn)` | Heuristic-guided search |
| Bellman-Ford | `litegraph::bellman_ford(const GraphT&, NodeId, weight_fn)` | Negative-weight detection |
| Floyd–Warshall (all-pairs) | `litegraph::floyd_warshall(const GraphT&, weight_fn)` | Returns dist & next matrices |
| Topological sort (DFS) | `litegraph::topological_sort(const GraphT&)` | DFS-based ordering for DAGs |
| Stable Kahn topological sort | `akriti::graph::layout_extras::stable_toposort_kahn_by_index(nodes, edges, out_order, cycle_nodes)` | Deterministic Kahn variant with tie-breaking |
| Cycle detection | `litegraph::has_cycle(const GraphT&)` | Directed/undirected aware |
| Strongly connected components | `litegraph::strongly_connected_components(const GraphT&)` | Tarjan implementation |
| Edmonds–Karp (max flow) | `litegraph::edmonds_karp_max_flow(const GraphT&, NodeId, NodeId, capacity_fn)` | Residual BFS-based flow |
| Kruskal / Prim (MST) | `litegraph::kruskal_mst`, `litegraph::prim_mst` | Undirected weighted MSTs |
| Subgraph isomorphism (VF2) | `litegraph::vf2_subgraph_isomorphism(pattern, target, node_comp, edge_comp)` | Returns vector of mappings |
| Graph edit distance (GED) | `litegraph::graph_edit_distance(g1, g2, node_subst_cost, node_ins_cost, ...)` | A*-based GED implementation |
| Bipartite matching (augmenting-path) | `litegraph::max_bipartite_matching(const GraphT&)` | Returns matching edges |
| Greedy coloring (serial & parallel) | `litegraph::greedy_graph_coloring`, `litegraph::parallel::parallel_greedy_coloring` | Deterministic ordering for stability |
| Centrality measures (degree/closeness/betweenness) | `litegraph::degree_centrality`, `litegraph::closeness_centrality`, `litegraph::betweenness_centrality` | Analysis utilities |
| Parallel Dijkstra | `litegraph::parallel::parallel_dijkstra(policy, graph, source)` | Execution-policy first parameter |

Use the fuller "Comprehensive Algorithm Reference" below for complexities, variants, and code examples.

<a name="core-traversal-algorithms"></a>
### Core Traversal Algorithms

| Algorithm | Complexity | Parallel Support | Use Cases |
|-----------|------------|------------------|-----------|
| DFS | O(V + E) | ✅ | Cycle detection, topological sort |
| BFS | O(V + E) | ✅ | Shortest path (unweighted), level-order |
| Bidirectional BFS | O(V + E) | ✅ | Shortest path optimization |

<a name="shortest-path-algorithms"></a>
### Shortest Path Algorithms

| Algorithm | Complexity | Weights | Use Cases |
|-----------|------------|---------|-----------|
| Dijkstra | O((V + E) log V) | Non-negative | Single-source shortest path |
| Bellman-Ford | O(VE) | Any | Negative weight detection |
| Floyd-Warshall | O(V³) | Any | All-pairs shortest path |
| A* | O(b^d) | Heuristic | Pathfinding with admissible heuristic |

<a name="connectivity-algorithms"></a>
### Connectivity Algorithms

| Algorithm | Complexity | Graph Type | Output |
|-----------|------------|------------|--------|
| Tarjan SCC | O(V + E) | Directed | Strongly connected components |
| Kosaraju SCC | O(V + E) | Directed | Strongly connected components |
| Union-Find | O(α(V)) | Undirected | Connected components |

### Advanced Algorithms

```cpp
// Deterministic Topological Sort with Kahn's Algorithm
template<typename Index, std::ranges::input_range NodeRange, std::ranges::input_range EdgeRange>
bool stable_toposort_kahn_by_index(const NodeRange &nodes,
                                   const EdgeRange &edges,
                                   std::vector<Index> &out_order,
                                   std::vector<Index> *cycle_nodes = nullptr);

// Features:
// - Deterministic tie-breaking (smallest index for totally-ordered types)
// - Insertion-order preservation for non-comparable types  
// - Cycle detection with diagnostic output
// - Fast paths for hashable/orderable types, fallback for exotic types

// Dominator Tree Helper
bool dominates_node(const NAryTree<NodeId> &dom_tree, NodeId a, NodeId b);
// - O(log V) parent traversal in dominator tree
// - No additional caching or complex logic
// - Pure convenience wrapper for existing NAryTree API
```

---

<a name="best-practices-performance-guidelines"></a>
## 💡 Best Practices & Performance Guidelines

### Memory Management
- **Smart Pointers**: NAryTree uses `std::unique_ptr` for automatic cleanup
- **Cache Locality**: Adjacency lists stored contiguously for better cache performance
- **Strong Types**: NodeId/EdgeId prevent accidental mixing of different graph instances

### Parallelism Guidelines  
- Use `std::execution::par_unseq` for CPU-intensive graph algorithms
- Prefer batch processing for better load balancing
- Consider graph partitioning for very large graphs

### Type Safety
```cpp
// Strong ID types prevent errors
NodeId node1{1}, node2{2};
EdgeId edge{1};
// node1 == edge; // Compile error - different types

// Concepts provide compile-time validation
template<GraphConcept G>
void algorithm(const G& graph) {
    static_assert(requires { graph.neighbors(NodeId{}); });
}
```

### Performance Optimizations
```cpp
// Reserve capacity for known graph sizes
litegraph::SimpleGraph graph;
graph.reserve_nodes(10000);
graph.reserve_edges(50000);

// Use parallel algorithms for large datasets
auto result = litegraph::dijkstra(graph, source);

// Prefer batch-style construction to minimize overhead (no dedicated add_edges helper in the public API)
std::vector<std::pair<litegraph::NodeId, litegraph::NodeId>> edges = get_all_edges();
for (auto [u, v] : edges) graph.add_edge(u, v);
```

---

## 🔧 Integration with Modern C++ Features

### Concepts and Constraints
```cpp
template<typename T>
concept GraphNode = requires(T node) {
    { node.id() } -> std::convertible_to<size_t>;
    { node == node } -> std::convertible_to<bool>;
};

template<GraphNode Node>
class TypeSafeGraph {
    // Implementation guaranteed to work with valid node types
};
```

### Structured Bindings
```cpp
// Modern iteration patterns
for (const auto &[nid_val, node_obj] : graph.nodes()) {
    litegraph::NodeId nid{nid_val};
    for (auto neighbor : graph.neighbors(nid)) {
        process_edge(nid, neighbor);
    }
}

// Tree traversal (iterators yield TreeNode references)
for (const auto &node : tree) {
    std::cout << node.node_id << ": " << node.data << '\n';
}
```

### Ranges and Views
```cpp
#include <ranges>

// Modern functional-style processing
auto leaf_values = tree.leaf_nodes() 
    | std::views::transform([](const auto& node) { return node.data; })
    | std::views::filter([](const auto& value) { return value > 0; });
```

---

<a name="quick-reference-examples"></a>
## 📋 Quick Reference & Examples

### Basic Graph Operations
```cpp
using namespace litegraph;

Graph<int> g;
auto n1 = g.add_node(1);
auto n2 = g.add_node(2);
auto n3 = g.add_node(3);
g.add_edge(n1, n2);
g.add_edge(n2, n3);

// BFS traversal
std::vector<int> seen;
litegraph::bfs(g, n1, [&](litegraph::NodeId nid, const int &data) { seen.push_back(data); });

// Shortest path (unweighted) via Dijkstra with unit weights
auto [dist, pred] = litegraph::dijkstra(g, n1, [](auto &&) { return 1.0; });
auto path = litegraph::reconstruct_path(n3, pred);
```

### Tree Operations
```cpp
NAryTree<std::string> tree;
// Create root
auto root = tree.insert(nullptr, std::string("program"));
auto func_node = tree.insert(root, std::string("function"));
tree.insert(func_node, std::string("parameter"));

// Serialize for persistence
std::ofstream file("ast.txt");
tree.serialize(file);
```

### Dominator Analysis
```cpp
using namespace litegraph;
SimpleGraph cfg;
auto e = cfg.add_node();
auto b1 = cfg.add_node();
auto b2 = cfg.add_node();
cfg.add_edge(e, b1);
cfg.add_edge(b1, b2);

auto dom = litegraph::compute_dominator_tree(cfg, e);
if (litegraph::dominator_analysis::dominates(dom, e, b2)) {
    // Safe to optimize
}
```

---

## 🚀 Recent Enhancements Summary

### Algorithm Improvements
- **Deterministic Topological Sort**: Stable tie-breaking with configurable ordering
- **Parallel Algorithm Support**: STL execution policies throughout  
- **Enhanced Dominator Queries**: Helper functions for common domination checks
- **Memory-Efficient Serialization**: Text-based format for tree persistence

### Type System Enhancements
- **Strong ID Types**: NodeId and EdgeId with proper ordering and hashing
- **Concept-Based Validation**: Compile-time checks for graph operations
- **CRTP Optimizations**: Zero-cost customization patterns

### Modern C++ Integration  
- **Constexpr Support**: Compile-time graph property computation
- **Structured Bindings**: Modern iteration patterns throughout
- **Range-Based Algorithms**: Integration with C++20 ranges
- **Parallel Execution**: Support for parallel STL algorithms

---

## 🏁 Conclusion

This comprehensive graph and tree framework represents a modern approach to C++ data structure design, emphasizing:

- **Zero-Cost Abstractions**: Performance equivalent to hand-optimized code
- **Type Safety**: Strong typing prevents common graph algorithm errors  
- **Composability**: Libraries work together seamlessly for complex analysis
- **Extensibility**: Template-based design allows easy customization
- **Standards Compliance**: Leverages modern C++ features appropriately

The framework is production-ready for compiler development, game engines, scientific computing, and any domain requiring high-performance graph processing with modern C++ design patterns.

Whether you're building a new language compiler, optimizing game AI pathfinding, or analyzing complex networks, these libraries provide the foundational tools with the performance and safety guarantees expected in modern C++ development.