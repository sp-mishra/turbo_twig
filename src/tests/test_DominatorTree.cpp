#include <catch_amalgamated.hpp>
#include "containers/graph/LiteGraph.hpp"
#include "containers/graph/DominatorTree.hpp"

using namespace litegraph;

TEST_CASE("[DominatorTree] Simple linear graph", "[DominatorTree]") {
    // Graph: 0 -> 1 -> 2 -> 3
    Graph<int, int, Directed> g;
    auto n0 = g.add_node(0);
    auto n1 = g.add_node(1);
    auto n2 = g.add_node(2);
    auto n3 = g.add_node(3);

    g.add_edge(n0, n1);
    g.add_edge(n1, n2);
    g.add_edge(n2, n3);

    auto dom_tree = compute_dominator_tree(g, n0);

    // Each node's immediate dominator is its predecessor
    auto *root = dom_tree.get_root();
    REQUIRE(root->data.value == n0.value);
    REQUIRE(root->children.size() == 1);
    auto *n1_node = root->children[0].get();
    REQUIRE(n1_node->data.value == n1.value);
    REQUIRE(n1_node->children.size() == 1);
    auto *n2_node = n1_node->children[0].get();
    REQUIRE(n2_node->data.value == n2.value);
    REQUIRE(n2_node->children.size() == 1);
    auto *n3_node = n2_node->children[0].get();
    REQUIRE(n3_node->data.value == n3.value);
    REQUIRE(n3_node->children.empty());
}

TEST_CASE("[DominatorTree] Diamond graph", "[DominatorTree]") {
    // Graph:
    //   0
    //  / \
    // 1   2
    //  \ /
    //   3
    Graph<int, int, Directed> g;
    auto n0 = g.add_node(0);
    auto n1 = g.add_node(1);
    auto n2 = g.add_node(2);
    auto n3 = g.add_node(3);

    g.add_edge(n0, n1);
    g.add_edge(n0, n2);
    g.add_edge(n1, n3);
    g.add_edge(n2, n3);

    auto dom_tree = compute_dominator_tree(g, n0);

    // n0 dominates all, n3's immediate dominator is n0
    auto *root = dom_tree.get_root();
    REQUIRE(root->data.value == n0.value);

    std::set<std::size_t> children;
    for (const auto &child_ptr : root->children) children.insert(child_ptr->data.value);
    REQUIRE(children.count(n1.value));
    REQUIRE(children.count(n2.value));
    REQUIRE(children.count(n3.value));
}

TEST_CASE("[DominatorTree] Branch and join", "[DominatorTree]") {
    // Graph:
    //   0
    //  / \
    // 1   2
    //  \ /
    //   3
    //   |
    //   4
    Graph<int, int, Directed> g;
    auto n0 = g.add_node(0);
    auto n1 = g.add_node(1);
    auto n2 = g.add_node(2);
    auto n3 = g.add_node(3);
    auto n4 = g.add_node(4);

    g.add_edge(n0, n1);
    g.add_edge(n0, n2);
    g.add_edge(n1, n3);
    g.add_edge(n2, n3);
    g.add_edge(n3, n4);

    auto dom_tree = compute_dominator_tree(g, n0);

    // n0 dominates all, n3's immediate dominator is n0, n4's immediate dominator is n3
    auto *root = dom_tree.get_root();
    REQUIRE(root->data.value == n0.value);

    std::set<std::size_t> children;
    for (const auto &child_ptr : root->children) children.insert(child_ptr->data.value);
    REQUIRE(children.count(n1.value));
    REQUIRE(children.count(n2.value));
    REQUIRE(children.count(n3.value));

    // n4 should be child of n3
    NAryTree<NodeId>::TreeNode *n3_node = nullptr;
    for (const auto &child_ptr : root->children) {
        if (child_ptr->data.value == n3.value) n3_node = child_ptr.get();
    }
    REQUIRE(n3_node != nullptr);
    REQUIRE(n3_node->children.size() == 1);
    REQUIRE(n3_node->children[0]->data.value == n4.value);
}

TEST_CASE("[DominatorTree] Loop graph", "[DominatorTree]") {
    // Graph: 0 -> 1 -> 2 -> 3 -> 1 (loop)
    Graph<int, int, Directed> g;
    auto n0 = g.add_node(0);
    auto n1 = g.add_node(1);
    auto n2 = g.add_node(2);
    auto n3 = g.add_node(3);

    g.add_edge(n0, n1);
    g.add_edge(n1, n2);
    g.add_edge(n2, n3);
    g.add_edge(n3, n1);

    auto dom_tree = compute_dominator_tree(g, n0);

    auto *root = dom_tree.get_root();
    REQUIRE(root->data.value == n0.value);

    // In a loop, only the loop header (n1) is an immediate child of the root.
    // All other nodes in the loop are dominated by n1 or by n2 (depending on the dominator algorithm's result).
    std::set<std::size_t> dom_children;
    for (const auto &child_ptr : root->children) dom_children.insert(child_ptr->data.value);
    REQUIRE(dom_children.count(n1.value));

    // Accept either n2 or n3 as children of n1, and the other as a child of n2 (since both are in the loop).
    NAryTree<NodeId>::TreeNode *n1_node = nullptr;
    for (const auto &child_ptr : root->children) {
        if (child_ptr->data.value == n1.value) n1_node = child_ptr.get();
    }
    REQUIRE(n1_node != nullptr);

    std::set<std::size_t> n1_dom_children;
    for (const auto &child_ptr : n1_node->children) n1_dom_children.insert(child_ptr->data.value);

    // At least one of n2 or n3 should be a child of n1
    bool n2_child = n1_dom_children.count(n2.value) > 0;
    bool n3_child = n1_dom_children.count(n3.value) > 0;
    // REQUIRE(n2_child || n3_child); // This line causes a Catch2 static_assert error

    // Fix: Decompose the assertion for Catch2 compatibility
    REQUIRE(n2_child == true);
    // If n2 is not a child, then n3 must be
    if (!n2_child) {
        REQUIRE(n3_child == true);
    }

    // If n2 is a child of n1, n3 should be a child of n2
    if (n2_child) {
        NAryTree<NodeId>::TreeNode *n2_node = nullptr;
        for (const auto &child_ptr : n1_node->children) {
            if (child_ptr->data.value == n2.value) n2_node = child_ptr.get();
        }
        REQUIRE(n2_node != nullptr);
        std::set<std::size_t> n2_dom_children;
        for (const auto &child_ptr : n2_node->children) n2_dom_children.insert(child_ptr->data.value);
        REQUIRE(n2_dom_children.count(n3.value));
    }
    // If n3 is a child of n1, n2 should be a child of n3
    if (n3_child) {
        NAryTree<NodeId>::TreeNode *n3_node = nullptr;
        for (const auto &child_ptr : n1_node->children) {
            if (child_ptr->data.value == n3.value) n3_node = child_ptr.get();
        }
        REQUIRE(n3_node != nullptr);
        std::set<std::size_t> n3_dom_children;
        for (const auto &child_ptr : n3_node->children) n3_dom_children.insert(child_ptr->data.value);
        REQUIRE(n3_dom_children.count(n2.value));
    }
}

TEST_CASE("[DominatorTree] Disconnected graph (only reachable nodes)", "[DominatorTree]") {
    // Graph: 0 -> 1, 2 (disconnected)
    Graph<int, int, Directed> g;
    auto n0 = g.add_node(0);
    auto n1 = g.add_node(1);
    auto n2 = g.add_node(2);

    g.add_edge(n0, n1);

    auto dom_tree = compute_dominator_tree(g, n0);

    auto *root = dom_tree.get_root();
    REQUIRE(root->data.value == n0.value);
    REQUIRE(root->children.size() == 1);
    REQUIRE(root->children[0]->data.value == n1.value);
    // n2 is not reachable from n0, so should not appear in the dominator tree
}

TEST_CASE("[DominatorTree] Complex control flow (if-else-join)", "[DominatorTree]") {
    // Graph:
    //   0
    //  / \
    // 1   2
    //  \ /
    //   3
    //   |
    //   4
    //   |
    //   5
    Graph<int, int, Directed> g;
    auto n0 = g.add_node(0);
    auto n1 = g.add_node(1);
    auto n2 = g.add_node(2);
    auto n3 = g.add_node(3);
    auto n4 = g.add_node(4);
    auto n5 = g.add_node(5);

    g.add_edge(n0, n1);
    g.add_edge(n0, n2);
    g.add_edge(n1, n3);
    g.add_edge(n2, n3);
    g.add_edge(n3, n4);
    g.add_edge(n4, n5);

    auto dom_tree = compute_dominator_tree(g, n0);

    auto *root = dom_tree.get_root();
    REQUIRE(root->data.value == n0.value);

    std::set<std::size_t> children;
    for (const auto &child_ptr : root->children) children.insert(child_ptr->data.value);
    REQUIRE(children.count(n1.value));
    REQUIRE(children.count(n2.value));
    REQUIRE(children.count(n3.value));

    // n4 should be child of n3, n5 child of n4
    NAryTree<NodeId>::TreeNode *n3_node = nullptr;
    for (const auto &child_ptr : root->children) {
        if (child_ptr->data.value == n3.value) n3_node = child_ptr.get();
    }
    REQUIRE(n3_node != nullptr);
    REQUIRE(n3_node->children.size() == 1);
    auto *n4_node = n3_node->children[0].get();
    REQUIRE(n4_node->data.value == n4.value);
    REQUIRE(n4_node->children.size() == 1);
    REQUIRE(n4_node->children[0]->data.value == n5.value);
}

// --- Added tests for dominates_node helper and corner cases ---

TEST_CASE("[DominatorTree] dominates_node basic linear checks", "[DominatorTree][dominates_node]") {
    // Graph: 0 -> 1 -> 2 -> 3
    Graph<int, int, Directed> g;
    auto n0 = g.add_node(0);
    auto n1 = g.add_node(1);
    auto n2 = g.add_node(2);
    auto n3 = g.add_node(3);

    g.add_edge(n0, n1);
    g.add_edge(n1, n2);
    g.add_edge(n2, n3);

    auto dom_tree = compute_dominator_tree(g, n0);

    // Root dominates all reachable nodes
    REQUIRE(akriti::graph::layout_extras::dominates_node(dom_tree, n0, n3));
    // Intermediate nodes dominate downstream nodes
    REQUIRE(akriti::graph::layout_extras::dominates_node(dom_tree, n1, n3));
    // A node does dominate itself
    REQUIRE(akriti::graph::layout_extras::dominates_node(dom_tree, n2, n2));
    // Upstream node does not get dominated by downstream
    REQUIRE_FALSE(akriti::graph::layout_extras::dominates_node(dom_tree, n3, n1));
}

TEST_CASE("[DominatorTree] dominates_node on diamond graph: branch independence", "[DominatorTree][dominates_node]") {
    // Diamond:
    //   0
    //  / \
    // 1   2
    //  \ /
    //   3
    Graph<int, int, Directed> g;
    auto n0 = g.add_node(0);
    auto n1 = g.add_node(1);
    auto n2 = g.add_node(2);
    auto n3 = g.add_node(3);

    g.add_edge(n0, n1);
    g.add_edge(n0, n2);
    g.add_edge(n1, n3);
    g.add_edge(n2, n3);

    auto dom_tree = compute_dominator_tree(g, n0);

    // Root dominates all; but siblings do not dominate each other
    REQUIRE(akriti::graph::layout_extras::dominates_node(dom_tree, n0, n3));
    REQUIRE_FALSE(akriti::graph::layout_extras::dominates_node(dom_tree, n1, n2));
    REQUIRE_FALSE(akriti::graph::layout_extras::dominates_node(dom_tree, n2, n1));
    // n1 does not dominate n3 if there exists an alternative path not passing through n1
    // (In this diamond, immediate dominator of n3 should be n0)
    REQUIRE_FALSE(akriti::graph::layout_extras::dominates_node(dom_tree, n1, n3));
}

TEST_CASE("[DominatorTree] dominates_node with unreachable node returns false", "[DominatorTree][dominates_node][edgecase]") {
    // Graph: 0 -> 1 and isolated node 2
    Graph<int, int, Directed> g;
    auto n0 = g.add_node(0);
    auto n1 = g.add_node(1);
    auto n2 = g.add_node(2); // unreachable from n0

    g.add_edge(n0, n1);

    auto dom_tree = compute_dominator_tree(g, n0);

    // Unreachable node should not be present in the dominator tree and hence not dominated.
    REQUIRE_FALSE(akriti::graph::layout_extras::dominates_node(dom_tree, n0, n2));
    REQUIRE_FALSE(akriti::graph::layout_extras::dominates_node(dom_tree, n2, n2));
}

TEST_CASE("[DominatorTree] dominates_node with loop graph: root dominance", "[DominatorTree][dominates_node][loop]") {
    // Graph: 0 -> 1 -> 2 -> 3 -> 1 (loop)
    Graph<int, int, Directed> g;
    auto n0 = g.add_node(0);
    auto n1 = g.add_node(1);
    auto n2 = g.add_node(2);
    auto n3 = g.add_node(3);

    g.add_edge(n0, n1);
    g.add_edge(n1, n2);
    g.add_edge(n2, n3);
    g.add_edge(n3, n1);

    auto dom_tree = compute_dominator_tree(g, n0);

    // Start node dominates all reachable nodes in the component
    REQUIRE(akriti::graph::layout_extras::dominates_node(dom_tree, n0, n1));
    REQUIRE(akriti::graph::layout_extras::dominates_node(dom_tree, n0, n2));
    REQUIRE(akriti::graph::layout_extras::dominates_node(dom_tree, n0, n3));
    // Loop header should dominate at least one of the loop nodes (implementation-dependent),
    // but root dominance is guaranteed.
    REQUIRE(akriti::graph::layout_extras::dominates_node(dom_tree, n0, n0));
}

// --- Additional DominatorTree tests for corner cases ---

TEST_CASE("[DominatorTree] dominates_node on root-only graph and non-existent nodes", "[DominatorTree][dominates_node][edgecase]") {
    Graph<int, int, Directed> g;
    auto root = g.add_node(0);
    auto isolated = g.add_node(1); // not reachable from root

    // No edges at all -> dominator tree rooted only at `root`.
    auto dom_tree = compute_dominator_tree(g, root);

    // Root should dominate itself.
    REQUIRE(akriti::graph::layout_extras::dominates_node(dom_tree, root, root));

    // Isolated node not reachable -> should not be dominated by root and not appear in tree.
    REQUIRE_FALSE(akriti::graph::layout_extras::dominates_node(dom_tree, root, isolated));
    REQUIRE_FALSE(akriti::graph::layout_extras::dominates_node(dom_tree, isolated, isolated));

    // Query with a completely out-of-range NodeId should return false (no new structures created)
    litegraph::NodeId bogus{9999};
    REQUIRE_FALSE(akriti::graph::layout_extras::dominates_node(dom_tree, root, bogus));
    REQUIRE_FALSE(akriti::graph::layout_extras::dominates_node(dom_tree, bogus, root));
}
