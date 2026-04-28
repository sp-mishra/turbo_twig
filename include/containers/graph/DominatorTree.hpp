#ifndef LITEGRAPH_DOMINATOR_TREE_HPP
#define LITEGRAPH_DOMINATOR_TREE_HPP

#include "LiteGraph.hpp"
#include "LiteGraphAlgorithms.hpp"
#include "../tree/NAryTree.hpp"
#include <map>
#include <vector>
#include <ranges>
#include <span>
#include <concepts>
#include <optional>
#include <functional>
#include <algorithm>

namespace litegraph {
    // C++23 Concepts for Dominator Tree
    template<typename T>
    concept DominatorTreeNode = requires(T t)
    {
        typename T::value_type;
        { t.value } -> std::convertible_to<size_t>;
    };

    template<typename GraphT>
    concept DirectedGraphForDominators = LiteGraphModel<GraphT> &&
                                         std::is_same_v<typename GraphT::directed_tag, Directed>;

    // Error types for dominator tree computation
    enum class DominatorError {
        InvalidStartNode,
        GraphNotDirected,
        UnreachableNodes,
        ComputationFailed
    };

    namespace detail {
        // Modern C++23 DSU implementation for dominator computation
        class DominatorDSU {
        private:
            std::vector<size_t> parent_;
            std::vector<size_t> label_;
            std::vector<size_t> size_;

        public:
            explicit constexpr DominatorDSU(size_t n)
                : parent_(n), label_(n), size_(n, 1) {
                for (size_t i = 0; i < n; ++i) {
                    parent_[i] = i;
                    label_[i] = i;
                }
            }

            constexpr void unite(size_t i, size_t j) noexcept {
                size_t root_i = find(i);
                size_t root_j = find(j);
                if (root_i != root_j) {
                    if (size_[root_i] < size_[root_j]) [[likely]] {
                        std::swap(root_i, root_j);
                    }
                    parent_[root_j] = root_i;
                    size_[root_i] += size_[root_j];
                }
            }

            constexpr size_t find(size_t i) noexcept {
                if (parent_[i] == i) [[likely]] return i;

                size_t root = find(parent_[i]);
                // Path compression with label optimization
                if (label_[parent_[i]] < label_[i]) [[unlikely]] {
                    label_[i] = label_[parent_[i]];
                }
                parent_[i] = root;
                return root;
            }

            constexpr size_t eval(size_t i) noexcept {
                find(i);
                return label_[i];
            }

            [[nodiscard]] constexpr std::span<const size_t> parents() const noexcept {
                return parent_;
            }

            [[nodiscard]] constexpr std::span<const size_t> labels() const noexcept {
                return label_;
            }
        };

        // C++23 constexpr DFS implementation for dominator tree
        template<DirectedGraphForDominators GraphT>
        constexpr auto perform_dominator_dfs(const GraphT &g, NodeId start_node) {
            struct DFSResult {
                std::vector<NodeId> dfs_nodes;
                std::vector<std::optional<NodeId> > dfs_parent;
                std::vector<int> dfs_num;
            };

            const auto node_cap = g.node_capacity();
            DFSResult result{
                .dfs_nodes = {},
                .dfs_parent = std::vector<std::optional<NodeId> >(node_cap, std::nullopt),
                .dfs_num = std::vector<int>(node_cap, -1)
            };

            result.dfs_nodes.reserve(g.node_count());

            int counter = 0;
            std::function<void(NodeId)> dfs_impl = [&](NodeId u) {
                result.dfs_num[u.value] = counter;
                result.dfs_nodes.push_back(u);
                ++counter;

                for (auto v: g.neighbors(u)) {
                    if (result.dfs_num[v.value] == -1) {
                        result.dfs_parent[v.value] = u;
                        dfs_impl(v);
                    }
                }
            };

            dfs_impl(start_node);
            return result;
        }

        // Predecessor computation (with potential for future parallel optimization)
        template<DirectedGraphForDominators GraphT>
        auto compute_predecessors_parallel(const GraphT &g, std::span<const int> dfs_num) {
            const auto node_cap = g.node_capacity();
            std::vector<std::vector<NodeId> > pred(node_cap);

            // Sequential implementation (can be parallelized in future)
            for (const auto &[eid_val, edge]: g.edges()) {
                if (dfs_num[edge.to.value] != -1) {
                    pred[edge.to.value].push_back(edge.from);
                }
            }

            return pred;
        }
    } // namespace detail

    /**
     * @brief Modern C++23 implementation of Lengauer-Tarjan Dominator Tree algorithm.
     *
     * This function computes dominator relationships efficiently using C++23 features,
     * concepts for type safety, and improved algorithms while maintaining API compatibility.
     *
     * @tparam GraphT The graph type conforming to LiteGraphModel concept
     * @param g The directed graph to analyze
     * @param start_node The entry point (root) of the dominator tree
     * @return NAryTree representing the dominator relationships
     */
    template<LiteGraphModel GraphT>
    auto compute_dominator_tree(const GraphT &g, NodeId start_node) -> NAryTree<NodeId> {
        static_assert(std::is_same_v<typename GraphT::directed_tag, Directed>,
                      "Dominator Tree is only defined for directed graphs.");

        const auto node_cap = g.node_capacity();

        // Step 1: Modern DFS traversal using C++23 features
        std::vector<NodeId> dfs_nodes;
        dfs_nodes.reserve(g.node_count());
        std::vector<std::optional<NodeId> > dfs_parent(node_cap, std::nullopt);
        std::vector<int> dfs_num(node_cap, -1);
        int counter = 0;

        // Custom DFS with modern C++ features
        std::function<void(NodeId)> perform_dfs = [&](NodeId u) {
            dfs_num[u.value] = counter;
            dfs_nodes.push_back(u);
            counter++;
            for (auto v: g.neighbors(u)) {
                if (dfs_num[v.value] == -1) {
                    dfs_parent[v.value] = u;
                    perform_dfs(v);
                }
            }
        };

        perform_dfs(start_node);

        // Step 2: Build predecessor lists efficiently
        std::vector<std::vector<NodeId> > pred(node_cap);
        for (const auto &[eid_val, edge]: g.edges()) {
            if (dfs_num[edge.to.value] != -1) {
                pred[edge.to.value].push_back(edge.from);
            }
        }

        // Step 3: Modern algorithm data structures
        std::vector<int> sdom(node_cap, -1);
        std::vector<std::vector<NodeId> > bucket(node_cap);
        detail::DominatorDSU dsu(node_cap);
        std::vector<NodeId> idom(node_cap, NodeId{node_cap});

        // Step 4: Lengauer-Tarjan algorithm with modern C++ ranges
        for (auto w_node: dfs_nodes | std::views::reverse) {
            if (w_node.value == start_node.value) continue;

            // Part A: Compute semidominator efficiently
            int min_sdom = dfs_num[w_node.value];
            for (auto p_node: pred[w_node.value]) {
                if (dfs_num[p_node.value] != -1) {
                    min_sdom = std::min(min_sdom, dfs_num[dsu.eval(p_node.value)]);
                }
            }
            sdom[w_node.value] = min_sdom;

            // Link DSU
            if (auto p = dfs_parent[w_node.value]) {
                dsu.unite(p->value, w_node.value);
            }

            // Part B: Process immediate dominators
            bucket[dfs_nodes[sdom[w_node.value]].value].push_back(w_node);

            if (auto p = dfs_parent[w_node.value]) {
                for (auto v_node: bucket[p->value]) {
                    auto u_node_val = dsu.eval(v_node.value);
                    if (sdom[u_node_val] < sdom[v_node.value]) {
                        idom[v_node.value] = NodeId{u_node_val};
                    } else {
                        idom[v_node.value] = *dfs_parent[v_node.value];
                    }
                }
                bucket[p->value].clear();
            }
        }

        // Step 5: Resolve immediate dominators
        for (auto w_node: dfs_nodes) {
            if (w_node.value != start_node.value) {
                if (idom[w_node.value].value != dfs_nodes[sdom[w_node.value]].value) {
                    idom[w_node.value] = idom[idom[w_node.value].value];
                }
            }
        }

        // Step 6: Build the NAryTree efficiently
        NAryTree<NodeId> dom_tree(start_node);
        std::map<size_t, NAryTree<NodeId>::TreeNode *> node_map;
        node_map[start_node.value] = dom_tree.get_root();

        for (auto u_node: dfs_nodes) {
            if (u_node.value == start_node.value) continue;

            NodeId p_node = idom[u_node.value];
            if (auto it = node_map.find(p_node.value); it != node_map.end()) {
                auto *parent_in_tree = it->second;
                auto *new_node = dom_tree.insert(parent_in_tree, u_node);
                node_map[u_node.value] = new_node;
            }
        }

        return dom_tree;
    }

    // Utility functions for dominator tree analysis
    namespace dominator_analysis {
        /**
         * @brief Check if node A dominates node B in the dominator tree
         */
        template<DominatorTreeNode NodeT>
        [[nodiscard]] constexpr bool dominates(const NAryTree<NodeT> &dom_tree,
                                               const NodeT &a, const NodeT &b) noexcept {
            auto *b_node = dom_tree.find_if([&](const auto &node) {
                return node.data.value == b.value;
            });

            if (!b_node) [[unlikely]] return false;

            // Walk up the dominator tree
            for (const auto *current = b_node; current != nullptr; current = current->parent) {
                if (current->data.value == a.value) [[likely]] return true;
            }
            return false;
        }

        /**
         * @brief Find the lowest common dominator of two nodes
         */
        template<DominatorTreeNode NodeT>
        [[nodiscard]] constexpr std::optional<NodeT> lowest_common_dominator(
            const NAryTree<NodeT> &dom_tree, const NodeT &a, const NodeT &b) noexcept {
            auto *a_node = dom_tree.find_if([&](const auto &node) { return node.data.value == a.value; });
            auto *b_node = dom_tree.find_if([&](const auto &node) { return node.data.value == b.value; });

            if (!a_node || !b_node) [[unlikely]] return std::nullopt;

            // Collect ancestors of both nodes
            std::vector<const typename NAryTree<NodeT>::TreeNode *> a_ancestors, b_ancestors;

            for (auto *current = a_node; current; current = current->parent) {
                a_ancestors.push_back(current);
            }
            for (auto *current = b_node; current; current = current->parent) {
                b_ancestors.push_back(current);
            }

            // Find common ancestors from root down
            std::ranges::reverse(a_ancestors);
            std::ranges::reverse(b_ancestors);

            const typename NAryTree<NodeT>::TreeNode *lca = nullptr;
            auto min_size = std::min(a_ancestors.size(), b_ancestors.size());

            for (size_t i = 0; i < min_size && a_ancestors[i] == b_ancestors[i]; ++i) {
                lca = a_ancestors[i];
            }

            return lca ? std::optional<NodeT>{lca->data} : std::nullopt;
        }

        /**
         * @brief Get all nodes dominated by a given node
         */
        template<DominatorTreeNode NodeT>
        [[nodiscard]] constexpr std::vector<NodeT> get_dominated_nodes(
            const NAryTree<NodeT> &dom_tree, const NodeT &dominator) {
            std::vector<NodeT> result;
            auto *dom_node = dom_tree.find_if([&](const auto &node) {
                return node.data.value == dominator.value;
            });

            if (!dom_node) [[unlikely]] return result;

            // Collect all descendants
            std::function<void(const typename NAryTree<NodeT>::TreeNode *)> collect_descendants =
                    [&](const auto *node) {
                if (node != dom_node) {
                    // Don't include the dominator itself
                    result.push_back(node->data);
                }
                for (const auto *child: node->children) {
                    collect_descendants(child);
                }
            };

            collect_descendants(dom_node);
            return result;
        }
    } // namespace dominator_analysis
} // namespace litegraph

#endif // LITEGRAPH_DOMINATOR_TREE_HPP
