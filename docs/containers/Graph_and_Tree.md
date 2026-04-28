# 🧩 Modern C++ Graph & Tree Libraries — Comprehensive User & Design Guide

## 🚀 Overview

This suite of lightweight, header-only C++ libraries provides essential building blocks for graph, tree, and hierarchical data modeling, analysis, and algorithmics. Optimized for **C++23**, **high performance**, **zero-cost abstractions**, and **modern design patterns**, they cover:

- **DominatorTree.hpp** — Ultra-fast dominator tree construction with Lengauer-Tarjan algorithm
- **NAryTree.hpp** — Memory-safe N-ary tree with smart pointers and modern traversal patterns
- **LiteGraph.hpp** — High-performance graph with CRTP patterns and constexpr operations
- **LiteGraphAlgorithms.hpp** — Comprehensive algorithm suite with STL execution policies

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

## 📚 Library Summaries

### DominatorTree.hpp
**Purpose**: Compute dominators/immediate dominators for directed graphs using the optimized Lengauer-Tarjan algorithm  
**Design**: Template-based with strong typing, constexpr operations, and O(α(n)) amortized complexity  
**Modern Features**: CRTP pattern, concepts-based constraints, zero-cost abstractions  

### NAryTree.hpp  
**Purpose**: Memory-safe N-ary tree with smart pointer management and flexible traversal strategies  
**Design**: RAII-compliant with automatic memory management, iterator support, and serialization  
**Modern Features**: Structured bindings support, range-based algorithms, concept-constrained templates  

### LiteGraph.hpp
**Purpose**: High-performance directed/undirected graph with adjacency list representation  
**Design**: Cache-friendly data layout, strong ID types, constexpr graph operations  
**Modern Features**: CRTP optimizations, concepts for type safety, parallel-ready iterators  

### LiteGraphAlgorithms.hpp
**Purpose**: Comprehensive suite of graph algorithms with STL execution policy support  
**Design**: Generic templates working with any graph representation, stateless and composable  
**Modern Features**: Parallel algorithm variants, concept constraints, deterministic behavior  

---

## 🔬 Design Deep Dives & Modern C++ Features

### DominatorTree: Advanced Control Flow Analysis

```cpp
#include "DominatorTree.hpp"

// Modern usage with strong types and constexpr operations  
constexpr auto build_dominator_analysis() {
    DominatorTree<NodeId> dom_tree;
    
    // Add edges with compile-time validation
    dom_tree.add_edge(NodeId{0}, NodeId{1});
    dom_tree.add_edge(NodeId{1}, NodeId{2});
    dom_tree.add_edge(NodeId{0}, NodeId{3});
    dom_tree.add_edge(NodeId{3}, NodeId{2});
    
    // Build with root - uses Lengauer-Tarjan algorithm
    dom_tree.build(NodeId{0});
    
    return dom_tree;
}

// Query dominance relationships
auto dom_tree = build_dominator_analysis();
auto idom_2 = dom_tree.immediate_dominator(NodeId{2}); // O(1) lookup
bool dominates = dom_tree.dominates(NodeId{0}, NodeId{2}); // O(1) check
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
NAryTree<ExpressionNode> ast;
auto root = ast.create_root(ExpressionNode{BinaryOp::Add});

// Structured binding support
for (auto [node_id, node_data] : ast.traverse_preorder()) {
    process_node(node_data);
}

// Range-based traversal with concepts
template<std::invocable<const ExpressionNode&> Visitor>
void visit_leaves(const NAryTree<ExpressionNode>& tree, Visitor visitor) {
    for (const auto& node : tree.leaf_nodes()) {
        std::invoke(visitor, node.data);
    }
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

// Modern graph with strong typing
template<bool IsDirected = true>
class Graph : public LiteGraph<NodeId, IsDirected> {
public:
    // CRTP pattern for zero-cost customization
    template<typename Visitor>
    constexpr void for_each_edge(Visitor&& visitor) const {
        for (const auto& [from, adjacents] : this->adjacency_list()) {
            for (const auto& to : adjacents) {
                std::invoke(visitor, from, to);
            }
        }
    }
    
    // Constexpr graph properties
    constexpr size_t vertex_count() const noexcept {
        return this->node_count();
    }
    
    constexpr size_t edge_count() const noexcept {
        size_t count = 0;
        for_each_edge([&count](auto, auto) { ++count; });
        return count;
    }
};

// Usage with modern C++ features
Graph<true> directed_graph;
directed_graph.add_edge(NodeId{1}, NodeId{2});

// Concepts-based validation
template<GraphConcept G>
void analyze_connectivity(const G& graph) {
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
template<typename Graph>
auto parallel_shortest_paths(const Graph& g, NodeId source) {
    return dijkstra(std::execution::par_unseq, g, source);
}

// Deterministic topological sorting
std::vector<NodeId> nodes = {NodeId{0}, NodeId{1}, NodeId{2}, NodeId{3}};
std::vector<std::pair<NodeId, NodeId>> edges = {
    {NodeId{0}, NodeId{2}}, {NodeId{1}, NodeId{2}}, {NodeId{2}, NodeId{3}}
};

std::vector<NodeId> topo_order;
std::vector<NodeId> cycle_nodes;

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

## 🍳 Advanced Integration Patterns

### 1. Complete Control Flow Analysis Pipeline

```cpp
#include "LiteGraph.hpp"
#include "DominatorTree.hpp"
#include "LiteGraphAlgorithms.hpp"

class ControlFlowAnalyzer {
private:
    LiteGraph<NodeId> cfg_;
    DominatorTree<NodeId> dom_tree_;
    
public:
    void build_cfg(const std::vector<BasicBlock>& blocks) {
        // Build control flow graph from basic blocks
        for (const auto& block : blocks) {
            cfg_.add_node(block.id);
            for (const auto& successor : block.successors) {
                cfg_.add_edge(block.id, successor);
            }
        }
    }
    
    void compute_dominance(NodeId entry) {
        // Transfer edges to dominator tree
        for (const auto& [from, adjacents] : cfg_.adjacency_list()) {
            for (const auto& to : adjacents) {
                dom_tree_.add_edge(from, to);
            }
        }
        
        dom_tree_.build(entry);
    }
    
    std::vector<NodeId> find_optimization_candidates() {
        std::vector<NodeId> candidates;
        
        // Find nodes that dominate multiple successors
        for (const auto& [node, _] : cfg_.adjacency_list()) {
            size_t dominated_successors = 0;
            for (const auto& succ : cfg_.neighbors(node)) {
                if (dom_tree_.dominates(node, succ)) {
                    ++dominated_successors;
                }
            }
            if (dominated_successors > 1) {
                candidates.push_back(node);
            }
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
    std::unordered_map<std::string, NodeId> symbol_table_;
    
public:
    NodeId create_expression(const ExprType& expr) {
        return ast_.create_node(expr);
    }
    
    void add_child_expression(NodeId parent, NodeId child) {
        ast_.add_child(parent, child);
    }
    
    template<typename Visitor>
    void visit_postorder(Visitor&& visitor) {
        ast_.traverse_postorder(std::forward<Visitor>(visitor));
    }
};

// Phase 2: Control Flow Analysis using LiteGraph  
class LitheControlFlow {
    LiteGraph<NodeId> flow_graph_;
    DominatorTree<NodeId> dominators_;
    
public:
    void add_control_edge(NodeId from, NodeId to) {
        flow_graph_.add_edge(from, to);
        dominators_.add_edge(from, to);
    }
    
    void analyze_flow(NodeId entry_point) {
        dominators_.build(entry_point);
    }
    
    bool can_optimize_branch(NodeId branch_node) {
        // Use dominator information for optimization decisions
        const auto& successors = flow_graph_.neighbors(branch_node);
        return std::all_of(successors.begin(), successors.end(),
            [&](NodeId succ) { return dominators_.dominates(branch_node, succ); });
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

## 🧪 Comprehensive Algorithm Reference

### Core Traversal Algorithms

| Algorithm | Complexity | Parallel Support | Use Cases |
|-----------|------------|------------------|-----------|
| DFS | O(V + E) | ✅ | Cycle detection, topological sort |
| BFS | O(V + E) | ✅ | Shortest path (unweighted), level-order |
| Bidirectional BFS | O(V + E) | ✅ | Shortest path optimization |

### Shortest Path Algorithms

| Algorithm | Complexity | Weights | Use Cases |
|-----------|------------|---------|-----------|
| Dijkstra | O((V + E) log V) | Non-negative | Single-source shortest path |
| Bellman-Ford | O(VE) | Any | Negative weight detection |
| Floyd-Warshall | O(V³) | Any | All-pairs shortest path |
| A* | O(b^d) | Heuristic | Pathfinding with admissible heuristic |

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
LiteGraph<NodeId> graph;
graph.reserve_nodes(10000);
graph.reserve_edges(50000);

// Use parallel algorithms for large datasets
auto result = dijkstra(std::execution::par_unseq, graph, source);

// Prefer batch operations
std::vector<std::pair<NodeId, NodeId>> edges = get_all_edges();
graph.add_edges(edges); // More efficient than individual add_edge calls
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
for (const auto& [node_id, adjacents] : graph.adjacency_list()) {
    for (const auto& neighbor : adjacents) {
        process_edge(node_id, neighbor);
    }
}

// Tree traversal with structured bindings
for (const auto& [depth, node] : tree.traverse_with_depth()) {
    std::cout << std::string(depth * 2, ' ') << node.data << '\n';
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

## 📋 Quick Reference & Examples

### Basic Graph Operations
```cpp
LiteGraph<int> g;
g.add_edge(1, 2);
g.add_edge(2, 3);

// Check connectivity
bool connected = has_path(g, 1, 3);

// Find shortest path
auto distances = bfs_shortest_path(g.adjacency_list(), 1);
```

### Tree Operations
```cpp
NAryTree<std::string> tree;
auto root = tree.create_root("program");
auto func_node = tree.add_child(root, "function");
tree.add_child(func_node, "parameter");

// Serialize for persistence  
std::ofstream file("ast.txt");
tree.serialize(file);
```

### Dominator Analysis
```cpp
DominatorTree<NodeId> dom;
// Build CFG edges
dom.add_edge(entry, block1);
dom.add_edge(block1, block2);
dom.build(entry);

// Query dominance
if (dom.dominates(entry, block2)) {
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