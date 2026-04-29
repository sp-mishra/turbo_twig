#ifndef LITEGRAPH_HPP
#define LITEGRAPH_HPP

#include <vector>
#include <optional>
#include <ranges>
#include <utility>
#include <type_traits>
#include <algorithm>
#include <iostream>
#include <functional>
#include <memory>
#include <span>
#include <execution>
#include <concepts>
#include <format>
#include <expected>
#include <mdspan>
#include <flat_map>
#include <limits>

namespace litegraph {
    // Error handling with std::expected
    enum class GraphError {
        InvalidNode,
        InvalidEdge,
        NodeNotFound,
        EdgeNotFound,
        InvalidOperation
    };

    // Marker types for directedness
    struct Directed {
        static constexpr bool is_directed = true;
    };

    struct Undirected {
        static constexpr bool is_directed = false;
    };

    // C++23 Concepts for type constraints
    template<typename T>
    concept Hashable = requires(T t)
    {
        std::hash<T>{}(t);
    } && std::equality_comparable<T>;

    template<typename T>
    concept Serializable = requires(T t)
    {
        { std::format("{}", t) } -> std::convertible_to<std::string>;
    };

    template<typename T>
    concept Numeric = std::integral<T> || std::floating_point<T>;

    template<typename T>
    concept DirectednessTag = std::same_as<T, Directed> || std::same_as<T, Undirected>;

    // Strong types for IDs with enhanced safety
    struct NodeId {
        std::size_t value;

        constexpr NodeId() noexcept : value(std::numeric_limits<std::size_t>::max()) {
        }

        explicit constexpr NodeId(std::size_t v) noexcept : value(v) {
        }

        explicit constexpr operator std::size_t() const noexcept { return value; }

        [[nodiscard]] constexpr bool is_valid() const noexcept {
            return value != std::numeric_limits<std::size_t>::max();
        }

        constexpr auto operator<=>(const NodeId &other) const noexcept = default;
    };

    struct EdgeId {
        std::size_t value;

        constexpr EdgeId() noexcept : value(std::numeric_limits<std::size_t>::max()) {
        }

        explicit constexpr EdgeId(std::size_t v) noexcept : value(v) {
        }

        explicit constexpr operator std::size_t() const noexcept { return value; }

        [[nodiscard]] constexpr bool is_valid() const noexcept {
            return value != std::numeric_limits<std::size_t>::max();
        }

        constexpr auto operator<=>(const EdgeId &other) const noexcept = default;
    };

    // Invalid ID constants
    inline constexpr NodeId INVALID_NODE_ID{};
    inline constexpr EdgeId INVALID_EDGE_ID{};

    // C++23 Graph Model Concept - Enhanced
    template<typename G>
    concept LiteGraphModel = requires(G g, const G cg, typename G::node_type node_data,
                                      typename G::edge_type edge_data, NodeId nid, EdgeId eid)
    {
        // Type requirements
        typename G::node_type;
        typename G::edge_type;
        typename G::directed_tag;

        // Basic operations with proper return types
        { g.add_node(node_data) } -> std::same_as<NodeId>;
        { g.add_edge(nid, nid, edge_data) } -> std::same_as<EdgeId>;
        { g.remove_node(nid) } -> std::same_as<void>;
        { g.remove_edge(eid) } -> std::same_as<void>;

        // Data access
        { g.node_data(nid) } -> std::same_as<typename G::node_type &>;
        { cg.node_data(nid) } -> std::same_as<const typename G::node_type &>;
        { g.edge_data(eid) } -> std::same_as<typename G::edge_type &>;
        { cg.edge_data(eid) } -> std::same_as<const typename G::edge_type &>;

        // Structure queries
        { cg.valid_node(nid) } -> std::same_as<bool>;
        { cg.valid_edge(eid) } -> std::same_as<bool>;
        { cg.node_count() } -> std::same_as<std::size_t>;
        { cg.edge_count() } -> std::same_as<std::size_t>;
        { cg.node_capacity() } -> std::same_as<std::size_t>;

        // Navigation
        { cg.neighbors(nid) };
        { cg.out_edges(nid) };
        { cg.degree(nid) } -> std::same_as<std::size_t>;

        // Iteration
        { cg.nodes() };
        { cg.edges() };
    } && DirectednessTag<typename G::directed_tag>;

    // Enhanced Graph class with C++23 features
    template<
        Hashable NodeT = std::monostate,
        Hashable EdgeT = std::monostate,
        DirectednessTag Directedness = Directed>
        requires (std::move_constructible<NodeT> && std::move_constructible<EdgeT>)
    class Graph {
    public:
        using node_type = NodeT;
        using edge_type = EdgeT;
        using directed_tag = Directedness;
        using IdMap = std::vector<std::optional<std::size_t> >;

        // Explicitly defaulted special member functions.
        // All six are defaulted so the compiler generates correct
        // copy/move semantics for the vector members.
        Graph()                            = default;
        ~Graph()                           = default;
        Graph(const Graph &)               = default;
        Graph(Graph &&) noexcept           = default;
        Graph &operator=(const Graph &)    = default;
        Graph &operator=(Graph &&) noexcept = default;

        // C++23 deducing this for CRTP-like behavior without inheritance
        template<typename Self>
        constexpr auto get_directedness(this Self &&) -> Directedness { return {}; }

        // Node/Edge internal structures
        struct Node {
            NodeT data;
            std::vector<EdgeId> out_edges; // indices to edges vector
            std::vector<EdgeId> in_edges; // only used for directed graphs
            bool active = true; // to support removals
        };

        struct Edge {
            NodeId from, to;
            EdgeT data;
            bool active = true;
        };

        // Add node
        NodeId add_node(const NodeT &data = NodeT{}) {
            nodes_.push_back({data, {}, {}, true});
            active_node_count_++;
            return NodeId{nodes_.size() - 1};
        }

        // Add edge
        EdgeId add_edge(NodeId from, NodeId to, const EdgeT &data = EdgeT{}) {
            if (!valid_node(from) || !valid_node(to))
                throw std::out_of_range("Cannot add edge to invalid node.");
            edges_.push_back({from, to, data, true});
            auto eid = EdgeId{edges_.size() - 1};
            nodes_[from.value].out_edges.push_back(eid);
            if constexpr (std::is_same_v<Directedness, Directed>)
                nodes_[to.value].in_edges.push_back(eid);
            if constexpr (std::is_same_v<Directedness, Undirected>)
                nodes_[to.value].out_edges.push_back(eid); // undirected: add to both
            active_edge_count_++;
            return eid;
        }

        // Add node (move overload)
        NodeId add_node(NodeT &&data) {
            nodes_.push_back({std::move(data), {}, {}, true});
            active_node_count_++;
            return NodeId{nodes_.size() - 1};
        }

        // Add edge (move overload)
        EdgeId add_edge(NodeId from, NodeId to, EdgeT &&data) {
            if (!valid_node(from) || !valid_node(to))
                throw std::out_of_range("Cannot add edge to invalid node.");
            edges_.push_back({from, to, std::move(data), true});
            auto eid = EdgeId{edges_.size() - 1};
            nodes_[from.value].out_edges.push_back(eid);
            if constexpr (std::is_same_v<Directedness, Directed>)
                nodes_[to.value].in_edges.push_back(eid);
            if constexpr (std::is_same_v<Directedness, Undirected>)
                nodes_[to.value].out_edges.push_back(eid); // undirected: add to both
            active_edge_count_++;
            return eid;
        }

        // Remove node (lazy)
        void remove_node(NodeId nid) {
            // 1. Use exception-based error handling instead of assert
            if (!valid_node(nid)) {
                throw std::out_of_range("Cannot remove invalid or inactive node.");
            }

            // 2. Deactivate the node and decrement the cached node count
            nodes_[nid.value].active = false;
            active_node_count_--;

            // 3. Deactivate all incident edges and decrement the cached edge count
            // Process outgoing edges
            for (EdgeId eid: nodes_[nid.value].out_edges) {
                // Check if the edge is active before modifying to avoid double counting
                if (edges_[eid.value].active) {
                    edges_[eid.value].active = false;
                    active_edge_count_--;
                }
            }

            // For directed graphs, also process incoming edges.
            // The check for 'active' status elegantly handles self-loops,
            // preventing the same edge from being counted twice.
            if constexpr (std::is_same_v<Directedness, Directed>) {
                for (EdgeId eid: nodes_[nid.value].in_edges) {
                    if (edges_[eid.value].active) {
                        edges_[eid.value].active = false;
                        active_edge_count_--;
                    }
                }
            }
        }

        // Remove edge (lazy)
        void remove_edge(EdgeId eid) {
            if (!valid_edge(eid))
                throw std::out_of_range("Cannot remove invalid edge.");
            if (edges_[eid.value].active) {
                edges_[eid.value].active = false;
                active_edge_count_--;
            }
        }

        // Physically remove inactive nodes/edges and remap all IDs.
        // IMPORTANT: After this, all old NodeId/EdgeId handles are invalid!
        // Returns a pair of maps: {old_node_id -> new_node_id, old_edge_id -> new_edge_id}
        // to allow the caller to update their stored IDs.
        std::pair<IdMap, IdMap> compact() {
            // Mapping from old id to new id
            IdMap node_id_map(nodes_.size(), std::nullopt);
            IdMap edge_id_map(edges_.size(), std::nullopt);

            // Compact nodes
            std::vector<Node> new_nodes;
            for (std::size_t i = 0; i < nodes_.size(); ++i) {
                if (nodes_[i].active) {
                    node_id_map[i] = new_nodes.size();
                    new_nodes.push_back(std::move(nodes_[i]));
                }
            }
            nodes_ = std::move(new_nodes);

            // Compact edges
            std::vector<Edge> new_edges;
            for (std::size_t i = 0; i < edges_.size(); ++i) {
                if (edges_[i].active &&
                    node_id_map[edges_[i].from.value] && node_id_map[edges_[i].to.value]) {
                    // Remap endpoints
                    edges_[i].from.value = *node_id_map[edges_[i].from.value];
                    edges_[i].to.value = *node_id_map[edges_[i].to.value];
                    edge_id_map[i] = new_edges.size();
                    new_edges.push_back(std::move(edges_[i]));
                }
            }
            edges_ = std::move(new_edges);

            // Update adjacency lists
            for (auto &node: nodes_) {
                auto remap = [&](std::vector<EdgeId> &vec) {
                    std::vector<EdgeId> new_vec;
                    for (const auto [value]: vec)
                        if (value < edge_id_map.size() && edge_id_map[value])
                            new_vec.push_back(EdgeId{*edge_id_map[value]});
                    vec = std::move(new_vec);
                };
                remap(node.out_edges);
                if constexpr (std::is_same_v<Directedness, Directed>)
                    remap(node.in_edges);
            }

            // Return the maps to the caller.
            return {node_id_map, edge_id_map};
        }

        // Node/edge iteration (skip removed)
        auto nodes() const {
            return std::views::iota(std::size_t{0}, nodes_.size())
                   | std::views::filter([this](std::size_t i) { return nodes_[i].active; })
                   | std::views::transform([this](std::size_t i) {
                       // Return a pair of the index (ID) and a const reference to the node
                       return std::pair<std::size_t, const Node &>(i, nodes_[i]);
                   });
        }

        auto edges() const {
            return std::views::iota(std::size_t{0}, edges_.size())
                   | std::views::filter([this](std::size_t i) { return edges_[i].active; })
                   | std::views::transform([this](std::size_t i) {
                       // Return a pair of the index (ID) and a const reference to the edge
                       return std::pair<std::size_t, const Edge &>(i, edges_[i]);
                   });
        }

        // Accessors
        NodeT &node_data(NodeId nid) {
            if (!valid_node(nid))
                throw std::out_of_range("Cannot get invalid node.");
            return nodes_[nid.value].data;
        }

        const NodeT &node_data(NodeId nid) const {
            if (!valid_node(nid))
                throw std::out_of_range("Cannot get invalid node.");
            return nodes_[nid.value].data;
        }

        EdgeT &edge_data(EdgeId eid) {
            if (!valid_edge(eid))
                throw std::out_of_range("Cannot get invalid edge.");
            return edges_[eid.value].data;
        }

        const EdgeT &edge_data(EdgeId eid) const {
            if (!valid_edge(eid))
                throw std::out_of_range("Cannot get invalid edge.");
            return edges_[eid.value].data;
        }

        // Returns a lazy, non-owning filtered range over the active outgoing EdgeIds
        // of the given node.
        //
        // INVALIDATION HAZARD: The returned range holds a reference into the graph's
        // internal adjacency storage.  Any structural modification to the graph
        // (add_node, remove_node, add_edge, remove_edge, compact, clear) while the
        // range is in use is undefined behaviour.  If you need to mutate the graph
        // during iteration, call out_edge_ids() instead to materialise a copy first.
        auto out_edges(NodeId nid) const {
            const auto &vec = nodes_[nid.value].out_edges;
            return vec | std::views::filter([this](EdgeId eid) { return edges_[eid.value].active; });
        }

        // Returns a lazy, non-owning filtered range over the active incoming EdgeIds
        // of the given node (directed graphs only).
        //
        // INVALIDATION HAZARD: Same as out_edges() — any structural modification to the
        // graph while this range is live is undefined behaviour.  Use in_edge_ids() to
        // obtain a safe copy before mutating.
        auto in_edges(NodeId nid) const {
            static_assert(std::is_same_v<Directedness, Directed>, "in_edges() only for directed graphs");
            const auto &vec = nodes_[nid.value].in_edges;
            return vec | std::views::filter([this](EdgeId eid) { return edges_[eid.value].active; });
        }

        // Returns a lazy, non-owning range of NodeIds reachable from nid via active
        // outgoing edges.
        //
        // INVALIDATION HAZARD: This range is derived from out_edges() and shares the
        // same invalidation rules — any structural modification to the graph while this
        // range is live is undefined behaviour.
        auto neighbors(NodeId nid) const {
            return out_edges(nid)
                   | std::views::transform([this, nid](EdgeId eid) {
                       const auto &edge = edges_[eid.value];
                       // If the current node is 'from', the neighbor is 'to'.
                       // Otherwise, the neighbor is 'from'.
                       return edge.from.value == nid.value ? edge.to : edge.from;
                   });
        }

        // Safe materialising helpers — these copy the current active edge IDs into an
        // owned vector so the caller can freely mutate the graph afterwards.

        // Returns a vector of active outgoing EdgeIds for nid.
        // Safe to use across graph mutations; the returned vector is independent of
        // graph storage.
        [[nodiscard]] std::vector<EdgeId> out_edge_ids(NodeId nid) const {
            std::vector<EdgeId> result;
            for (EdgeId eid : nodes_[nid.value].out_edges) {
                if (edges_[eid.value].active) result.push_back(eid);
            }
            return result;
        }

        // Returns a vector of active incoming EdgeIds for nid (directed graphs only).
        // Safe to use across graph mutations; the returned vector is independent of
        // graph storage.
        template<typename D = Directedness>
        [[nodiscard]] std::enable_if_t<std::is_same_v<D, Directed>, std::vector<EdgeId>>
        in_edge_ids(NodeId nid) const {
            std::vector<EdgeId> result;
            for (EdgeId eid : nodes_[nid.value].in_edges) {
                if (edges_[eid.value].active) result.push_back(eid);
            }
            return result;
        }

        // Basic validity check
        [[nodiscard]] bool valid_node(NodeId nid) const noexcept {
            return nid.value < nodes_.size() && nodes_[nid.value].active;
        }

        [[nodiscard]] bool valid_edge(EdgeId eid) const noexcept {
            return eid.value < edges_.size() && edges_[eid.value].active;
        }

        // Size / capacity
        [[nodiscard]] std::size_t node_count()    const noexcept { return active_node_count_; }
        [[nodiscard]] std::size_t edge_count()    const noexcept { return active_edge_count_; }
        [[nodiscard]] std::size_t node_capacity() const noexcept { return nodes_.size(); }
        [[nodiscard]] std::size_t edge_capacity() const noexcept { return edges_.size(); }
        [[nodiscard]] bool        empty()         const noexcept { return active_node_count_ == 0; }

        // Access edge object by EdgeId (const and non-const)
        Edge &get_edge(EdgeId eid) {
            if (!valid_edge(eid))
                throw std::out_of_range("Cannot get invalid edge.");
            return edges_[eid.value];
        }

        const Edge &get_edge(EdgeId eid) const {
            if (!valid_edge(eid))
                throw std::out_of_range("Cannot get invalid edge.");
            return edges_[eid.value];
        }

        // Degree
        [[nodiscard]] std::size_t out_degree(const NodeId nid) const {
            if (!valid_node(nid))
                throw std::out_of_range("Cannot get degree of invalid node.");
            return std::ranges::distance(out_edges(nid));
        }

        template<typename T = Directedness>
        std::enable_if_t<std::is_same_v<T, Directed>, std::size_t>
        in_degree(const NodeId nid) const {
            if (!valid_node(nid))
                throw std::out_of_range("Cannot get degree of invalid node.");
            return std::ranges::distance(in_edges(nid));
        }

        [[nodiscard]] std::size_t degree(const NodeId nid) const {
            if (!valid_node(nid))
                throw std::out_of_range("Cannot get degree of invalid node.");
            if constexpr (std::is_same_v<Directedness, Directed>) {
                return in_degree(nid) + out_degree(nid);
            } else {
                return out_degree(nid);
            }
        }

        void reserve_nodes(std::size_t capacity) {
            nodes_.reserve(capacity);
        }

        void reserve_edges(std::size_t capacity) {
            edges_.reserve(capacity);
        }

        // Empties the graph of all nodes and edges.
        void clear() {
            nodes_.clear();
            edges_.clear();
            active_node_count_ = 0;
            active_edge_count_ = 0;
        }


        // Parallel node processing
        template<typename Exec, typename UnaryPred>
        auto parallel_count_nodes_if(Exec &&policy, UnaryPred pred) const
            requires std::is_execution_policy_v<std::remove_cvref_t<Exec> > {
            return std::count_if(policy, nodes_.begin(), nodes_.end(),
                                 [&](const auto &node) { return node.active && pred(node); });
        }

        // Error handling with std::expected
        [[nodiscard]] std::expected<std::reference_wrapper<NodeT>, GraphError> try_node_data(NodeId nid) noexcept {
            if (!valid_node(nid)) {
                return std::unexpected(GraphError::InvalidNode);
            }
            return std::ref(nodes_[nid.value].data);
        }

        [[nodiscard]] std::expected<std::reference_wrapper<const NodeT>, GraphError> try_node_data(NodeId nid) const noexcept {
            if (!valid_node(nid)) {
                return std::unexpected(GraphError::InvalidNode);
            }
            return std::cref(nodes_[nid.value].data);
        }

        [[nodiscard]] std::expected<std::reference_wrapper<EdgeT>, GraphError> try_edge_data(EdgeId eid) noexcept {
            if (!valid_edge(eid)) {
                return std::unexpected(GraphError::InvalidEdge);
            }
            return std::ref(edges_[eid.value].data);
        }

        // Alternative to generators: lazy iteration using ranges
        auto node_range() const {
            return std::views::iota(std::size_t{0}, nodes_.size())
                   | std::views::filter([this](std::size_t i) { return nodes_[i].active; })
                   | std::views::transform([this](std::size_t i) {
                       return std::pair<NodeId, const NodeT &>{NodeId{i}, nodes_[i].data};
                   });
        }

        auto edge_range() const {
            return std::views::iota(std::size_t{0}, edges_.size())
                   | std::views::filter([this](std::size_t i) { return edges_[i].active; })
                   | std::views::transform([this](std::size_t i) {
                       return std::pair<EdgeId, const Edge &>{EdgeId{i}, edges_[i]};
                   });
        }

        // Memory-efficient batch operations
        template<std::ranges::input_range Range>
        void batch_add_nodes(Range &&node_data_range)
            requires std::convertible_to<std::ranges::range_value_t<Range>, NodeT> {
            const auto size_hint = std::ranges::size(node_data_range);
            nodes_.reserve(nodes_.size() + size_hint);

            for (auto &&data: node_data_range) {
                nodes_.push_back({std::forward<decltype(data)>(data), {}, {}, true});
                active_node_count_++;
            }
        }

        // as_node_matrix has been removed.
        //
        // The former implementation performed a reinterpret_cast from the internal
        // Node* storage pointer (which contains adjacency vectors and metadata) to
        // a raw T* and returned an mdspan into that memory.  This is undefined
        // behaviour: Node is not layout-compatible with T, the cast violates strict
        // aliasing rules, and the resulting mdspan pointed into non-contiguous,
        // non-T memory.
        //
        // If a contiguous matrix view over node data is needed in the future, the
        // correct approach is to maintain a separate SoA (Structure-of-Arrays)
        // buffer for the node payload and expose an mdspan into that buffer.
        // That change is outside the scope of this fix.

        // Performance monitoring
        struct GraphStats {
            std::size_t total_nodes;
            std::size_t active_nodes;
            std::size_t total_edges;
            std::size_t active_edges;
            double load_factor;
            std::size_t memory_usage;
        };

        [[nodiscard]] GraphStats get_stats() const noexcept {
            // Accumulate heap bytes held by each node's adjacency vectors.
            // out_edges is used by both directed and undirected graphs.
            // in_edges is only populated for directed graphs (the vector exists
            // in all instantiations but remains empty for undirected ones, so
            // counting its capacity() is always safe and accurate).
            std::size_t adj_bytes = 0;
            for (const auto &n : nodes_) {
                adj_bytes += n.out_edges.capacity() * sizeof(EdgeId);
                adj_bytes += n.in_edges.capacity()  * sizeof(EdgeId);
            }

            return {
                .total_nodes = nodes_.size(),
                .active_nodes = active_node_count_,
                .total_edges = edges_.size(),
                .active_edges = active_edge_count_,
                .load_factor = nodes_.empty() ? 0.0 : static_cast<double>(active_node_count_) / nodes_.size(),
                .memory_usage = sizeof(*this) +
                                nodes_.capacity() * sizeof(Node) +
                                edges_.capacity() * sizeof(Edge) +
                                adj_bytes
            };
        }

    private:
        std::vector<Node> nodes_;
        std::vector<Edge> edges_;
        std::size_t active_node_count_ = 0;
        std::size_t active_edge_count_ = 0;
    };

    // Type aliases for common graph instantiations
    using SimpleGraph = Graph<>;
    using WeightedGraph = Graph<std::monostate, double>;
    using LabeledGraph = Graph<std::string, std::string>;
    using UndirectedGraph = Graph<std::monostate, std::monostate, Undirected>;
    using WeightedUndirectedGraph = Graph<std::monostate, double, Undirected>;

    // Graph factory functions
    template<typename NodeT = std::monostate, typename EdgeT = std::monostate>
    auto make_directed_graph() -> Graph<NodeT, EdgeT, Directed> {
        return Graph<NodeT, EdgeT, Directed>{};
    }

    template<typename NodeT = std::monostate, typename EdgeT = std::monostate>
    auto make_undirected_graph() -> Graph<NodeT, EdgeT, Undirected> {
        return Graph<NodeT, EdgeT, Undirected>{};
    }

} // namespace litegraph

namespace std {
    // std::hash specializations for NodeId and EdgeId so they can be used in unordered containers.
    template<>
    struct hash<litegraph::NodeId> {
        std::size_t operator()(const litegraph::NodeId &nid) const noexcept {
            return std::hash<std::size_t>{}(nid.value);
        }
    };

    template<>
    struct hash<litegraph::EdgeId> {
        std::size_t operator()(const litegraph::EdgeId &eid) const noexcept {
            return std::hash<std::size_t>{}(eid.value);
        }
    };
} // namespace std

#endif // LITEGRAPH_HPP
