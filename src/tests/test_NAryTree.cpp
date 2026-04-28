#define CATCH_CONFIG_MAIN
#include <catch_amalgamated.hpp>
#include "containers/tree/NAryTree.hpp"

using IntTree = NAryTree<int>;

TEST_CASE("[NArayTree] Create empty tree") {
    IntTree tree;
    REQUIRE(tree.get_root() == nullptr);
    REQUIRE(tree.empty());
    REQUIRE(tree.height() == 0);
}

TEST_CASE("[NArayTree] Insert root node") {
    IntTree tree;
    auto *root = tree.insert(nullptr, 10);
    REQUIRE(root != nullptr);
    REQUIRE(tree.get_root() == root);
    REQUIRE(root->data == 10);
    REQUIRE(tree.size() == 1);
    REQUIRE(tree.height() == 1);
}

TEST_CASE("[NArayTree] Insert children") {
    IntTree tree;
    auto *root = tree.insert(nullptr, 1);
    auto *child1 = tree.insert(root, 2);
    auto *child2 = tree.insert(root, 3);
    REQUIRE(child1 != nullptr);
    REQUIRE(child2 != nullptr);
    REQUIRE(root->children.size() == 2);
    REQUIRE(tree.size() == 3);
    REQUIRE(tree.height() == 2);
}

TEST_CASE("[NArayTree] Bulk insert children") {
    IntTree tree;
    auto *root = tree.insert(nullptr, 1);
    std::vector<std::pair<int, EmptyMetadata> > items = {{2, {}}, {3, {}}};
    auto children = tree.bulk_insert(root, items);
    REQUIRE(children.size() == 2);
    REQUIRE(root->children.size() == 2);
    REQUIRE(tree.size() == 3);
}

TEST_CASE("[NArayTree] Remove node") {
    IntTree tree;
    auto *root = tree.insert(nullptr, 1);
    auto *child = tree.insert(root, 2);
    REQUIRE(tree.remove(child));
    REQUIRE(root->children.empty());
    REQUIRE(tree.size() == 1);
    REQUIRE_FALSE(tree.remove(child)); // Already removed
}

TEST_CASE("[NArayTree] Remove root node") {
    IntTree tree;
    auto *root = tree.insert(nullptr, 1);
    REQUIRE(tree.remove(root));
    REQUIRE(tree.get_root() == nullptr);
    REQUIRE(tree.empty());
}

TEST_CASE("[NArayTree] Find node by value") {
    IntTree tree;
    auto *root = tree.insert(nullptr, 1);
    auto *child = tree.insert(root, 2);
    auto *found = tree.find(2);
    REQUIRE(found == child);
    REQUIRE(tree.find(42) == nullptr);
}

TEST_CASE("[NArayTree] Find all nodes by value") {
    IntTree tree;
    auto *root = tree.insert(nullptr, 1);
    tree.insert(root, 2);
    tree.insert(root, 2);
    auto found = tree.find_all(2);
    REQUIRE(found.size() == 2);
}

TEST_CASE("[NArayTree] Pre-order traversal") {
    IntTree tree;
    auto *root = tree.insert(nullptr, 1);
    auto *c1 = tree.insert(root, 2);
    tree.insert(root, 3);
    tree.insert(c1, 4);
    std::vector<int> order;
    for (auto &node: tree) order.push_back(node.data);
    REQUIRE(order == std::vector<int>{1, 2, 4, 3});
}

TEST_CASE("[NArayTree] Post-order traversal") {
    IntTree tree;
    auto *root = tree.insert(nullptr, 1);
    auto *c1 = tree.insert(root, 2);
    tree.insert(root, 3);
    tree.insert(c1, 4);
    std::vector<int> order;
    for (auto it = tree.post_order().begin(); it != tree.post_order().end(); ++it)
        order.push_back(it->data);
    REQUIRE(order == std::vector<int>{4, 2, 3, 1});
}

TEST_CASE("[NArayTree] Copy constructor") {
    IntTree tree;
    auto *root = tree.insert(nullptr, 1);
    tree.insert(root, 2);
    IntTree copy = tree;
    REQUIRE(copy.size() == tree.size());
    REQUIRE(copy.get_root() != tree.get_root());
    REQUIRE(copy.get_root()->data == 1);
}

TEST_CASE("[NArayTree] Assignment operator") {
    IntTree tree;
    auto *root = tree.insert(nullptr, 1);
    tree.insert(root, 2);
    IntTree copy;
    copy = tree;
    REQUIRE(copy.size() == tree.size());
    REQUIRE(copy.get_root() != tree.get_root());
    REQUIRE(copy.get_root()->data == 1);
}

TEST_CASE("[NArayTree] Clear tree") {
    IntTree tree;
    auto *root = tree.insert(nullptr, 1);
    tree.insert(root, 2);
    tree.clear();
    REQUIRE(tree.get_root() == nullptr);
    REQUIRE(tree.empty());
}

TEST_CASE("[NArayTree] Remove non-existent node returns false") {
    IntTree tree;
    REQUIRE_FALSE(tree.remove(static_cast<IntTree::TreeNode*>(nullptr)));
}

TEST_CASE("[NArayTree] Remove by value") {
    IntTree tree;
    auto *root = tree.insert(nullptr, 1);
    tree.insert(root, 2);
    REQUIRE(tree.remove(2));
    REQUIRE(root->children.empty());
    REQUIRE(tree.size() == 1);
    REQUIRE_FALSE(tree.remove(2));
}

TEST_CASE("[NArayTree] Insert root when already exists returns nullptr") {
    IntTree tree;
    auto *root1 = tree.insert(nullptr, 1);
    auto *root2 = tree.insert(nullptr, 2);
    REQUIRE(root1 != nullptr);
    REQUIRE(root2 == nullptr);
    REQUIRE(tree.get_root()->data == 1);
}

TEST_CASE("[NArayTree] Emplace root and children") {
    NAryTree<int> tree;
    auto *root = tree.emplace(nullptr, EmptyMetadata{}, 42);
    REQUIRE(root != nullptr);
    auto *child = tree.emplace(root, EmptyMetadata{}, 99);
    REQUIRE(child != nullptr);
    REQUIRE(root->children.size() == 1);
    REQUIRE(root->children.front()->data == 99);
}

TEST_CASE("[NArayTree] Bulk insert returns correct pointers") {
    IntTree tree;
    auto *root = tree.insert(nullptr, 1);
    std::vector<std::pair<int, EmptyMetadata> > items = {{2, {}}, {3, {}}, {4, {}}};
    auto children = tree.bulk_insert(root, items);
    REQUIRE(children.size() == 3);
    for (size_t i = 0; i < children.size(); ++i) {
        REQUIRE(children[i] == root->children[i].get());
    }
}

TEST_CASE("[NArayTree] Find_if and find_all_if") {
    IntTree tree;
    auto *root = tree.insert(nullptr, 1);
    tree.insert(root, 2);
    tree.insert(root, 3);
    tree.insert(root, 2);
    auto found = tree.find_if([](const IntTree::TreeNode &n) { return n.data == 3; });
    REQUIRE(found != nullptr);
    auto all_twos = tree.find_all_if([](const IntTree::TreeNode &n) { return n.data == 2; });
    REQUIRE(all_twos.size() == 2);
}

TEST_CASE("[NArayTree] Traverse depth first visits all nodes") {
    IntTree tree;
    auto *root = tree.insert(nullptr, 1);
    tree.insert(root, 2);
    tree.insert(root, 3);
    size_t count = 0;
    tree.traverse_depth_first([&count](const IntTree::TreeNode *) { ++count; });
    REQUIRE(count == 3);
}

TEST_CASE("[NArayTree] Traverse breadth first visits all nodes") {
    IntTree tree;
    auto *root = tree.insert(nullptr, 1);
    tree.insert(root, 2);
    tree.insert(root, 3);
    size_t count = 0;
    tree.traverse_breadth_first([&count](const IntTree::TreeNode *) { ++count; });
    REQUIRE(count == 3);
}

TEST_CASE("[NArayTree] Height of empty tree is zero") {
    IntTree tree;
    REQUIRE(tree.height() == 0);
}

TEST_CASE("[NArayTree] Height of single node tree is one") {
    IntTree tree;
    tree.insert(nullptr, 1);
    REQUIRE(tree.height() == 1);
}

TEST_CASE("[NArayTree] Height with deeper children") {
    IntTree tree;
    auto *root = tree.insert(nullptr, 1);
    auto *c1 = tree.insert(root, 2);
    tree.insert(c1, 3);
    REQUIRE(tree.height() == 3);
}

TEST_CASE("[NArayTree] is_leaf returns true for leaf nodes") {
    IntTree tree;
    auto *root = tree.insert(nullptr, 1);
    auto *child = tree.insert(root, 2);
    REQUIRE(child->is_leaf());
    REQUIRE_FALSE(root->is_leaf());
}

// --- Additional NAryTree corner-case tests ---

TEST_CASE("[NArayTree] Serialize and deserialize roundtrip", "[NAryTree][Serialize]") {
    IntTree tree;
    auto *root = tree.insert(nullptr, 1);
    auto *c1 = tree.insert(root, 2);
    tree.insert(c1, 3);

    // Serialize
    std::ostringstream oss;
    tree.serialize(oss);

    // Deserialize into a fresh tree
    IntTree tree2;
    std::istringstream iss(oss.str());
    tree2.deserialize(iss);

    REQUIRE(tree2.size() == tree.size());

    // Compare preorder orders
    std::vector<int> order1, order2;
    for (auto &n: tree) order1.push_back(n.data);
    for (auto &n: tree2) order2.push_back(n.data);
    REQUIRE(order1 == order2);
}

TEST_CASE("[NArayTree] Subtree constructor clones subtree correctly", "[NAryTree][Subtree]") {
    IntTree tree;
    auto *root = tree.insert(nullptr, 10);
    auto *c1 = tree.insert(root, 20);
    tree.insert(c1, 30);
    tree.insert(root, 40);

    // Construct a new NAryTree from the subtree rooted at c1 (no copying of unique_ptrs by us)
    IntTree subtree(*c1);
    REQUIRE(subtree.get_root() != nullptr);
    REQUIRE(subtree.get_root()->data == 20);
    REQUIRE(subtree.size() == 2); // c1 and its child (30)
    // Ensure preorder order matches expected
    std::vector<int> data_order;
    for (auto &n: subtree) data_order.push_back(n.data);
    REQUIRE(data_order == std::vector<int>{20, 30});
}
