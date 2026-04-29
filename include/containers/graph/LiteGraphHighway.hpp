#ifndef LITEGRAPH_HIGHWAY_HPP
#define LITEGRAPH_HIGHWAY_HPP

#include "LiteGraphAlgorithms.hpp"
#include <cstdint>

#ifdef LITEGRAPH_ENABLE_HIGHWAY
#include <hwy/highway.h>
#endif

namespace litegraph::highway {
    [[nodiscard]] constexpr bool enabled() noexcept {
#ifdef LITEGRAPH_ENABLE_HIGHWAY
        return true;
#else
        return false;
#endif
    }

    [[nodiscard]] inline std::int64_t supported_targets_mask() noexcept {
#ifdef LITEGRAPH_ENABLE_HIGHWAY
        return hwy::SupportedTargets();
#else
        return 0;
#endif
    }

    // Optional boundary: callers can include this header and opt-in to Highway
    // without changing the core serial algorithm API.
    template<typename EdgeT, DirectednessTag Directedness>
    CsrPageRankResult pagerank(const CsrGraph<EdgeT, Directedness> &g,
                               const CsrPageRankOptions &options = {}) {
#ifdef LITEGRAPH_ENABLE_HIGHWAY
        (void)hwy::SupportedTargets();
#endif
        return litegraph::pagerank(g, options);
    }
} // namespace litegraph::highway

#endif // LITEGRAPH_HIGHWAY_HPP

