#ifndef LITEGRAPH_HIGHWAY_HPP
#define LITEGRAPH_HIGHWAY_HPP

#include "LiteGraphAlgorithms.hpp"
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <stdexcept>
#include <vector>

#ifdef LITEGRAPH_ENABLE_HIGHWAY
#include <hwy/highway.h>
#endif

namespace litegraph::highway {
    namespace detail {
        inline void fill_vector(std::vector<double> &v, const double value) {
#ifdef LITEGRAPH_ENABLE_HIGHWAY
            const HWY_FULL(double) d;
            const std::size_t lanes = hwy::Lanes(d);
            const auto vv = hwy::Set(d, value);

            std::size_t i = 0;
            for (; i + lanes <= v.size(); i += lanes) {
                hwy::StoreU(vv, d, v.data() + i);
            }
            for (; i < v.size(); ++i) {
                v[i] = value;
            }
#else
            std::fill(v.begin(), v.end(), value);
#endif
        }

        inline void scale_vector(std::vector<double> &v, const double factor) {
#ifdef LITEGRAPH_ENABLE_HIGHWAY
            const HWY_FULL(double) d;
            const std::size_t lanes = hwy::Lanes(d);
            const auto vf = hwy::Set(d, factor);

            std::size_t i = 0;
            for (; i + lanes <= v.size(); i += lanes) {
                const auto x = hwy::LoadU(d, v.data() + i);
                hwy::StoreU(hwy::Mul(x, vf), d, v.data() + i);
            }
            for (; i < v.size(); ++i) {
                v[i] *= factor;
            }
#else
            for (double &x : v) x *= factor;
#endif
        }

        inline void add_scaled(std::vector<double> &dst,
                               const std::vector<double> &src,
                               const double scale) {
#ifdef LITEGRAPH_ENABLE_HIGHWAY
            const HWY_FULL(double) d;
            const std::size_t lanes = hwy::Lanes(d);
            const auto vs = hwy::Set(d, scale);

            std::size_t i = 0;
            for (; i + lanes <= dst.size(); i += lanes) {
                const auto vd = hwy::LoadU(d, dst.data() + i);
                const auto vx = hwy::LoadU(d, src.data() + i);
                hwy::StoreU(hwy::Add(vd, hwy::Mul(vx, vs)), d, dst.data() + i);
            }
            for (; i < dst.size(); ++i) {
                dst[i] += src[i] * scale;
            }
#else
            for (std::size_t i = 0; i < dst.size(); ++i) {
                dst[i] += src[i] * scale;
            }
#endif
        }

        [[nodiscard]] inline double l1_delta(const std::vector<double> &a,
                                             const std::vector<double> &b) {
#ifdef LITEGRAPH_ENABLE_HIGHWAY
            const HWY_FULL(double) d;
            const std::size_t lanes = hwy::Lanes(d);
            auto vacc = hwy::Zero(d);

            std::size_t i = 0;
            for (; i + lanes <= a.size(); i += lanes) {
                const auto va = hwy::LoadU(d, a.data() + i);
                const auto vb = hwy::LoadU(d, b.data() + i);
                vacc = hwy::Add(vacc, hwy::Abs(hwy::Sub(va, vb)));
            }

            double sum = hwy::GetLane(hwy::SumOfLanes(d, vacc));
            for (; i < a.size(); ++i) {
                sum += std::abs(a[i] - b[i]);
            }
            return sum;
#else
            double sum = 0.0;
            for (std::size_t i = 0; i < a.size(); ++i) {
                sum += std::abs(a[i] - b[i]);
            }
            return sum;
#endif
        }
    } // namespace detail

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
        if (options.damping_factor < 0.0 || options.damping_factor > 1.0) {
            throw std::invalid_argument("PageRank damping_factor must be in [0, 1].");
        }

        const std::size_t n = g.node_count();
        if (n == 0) {
            return {};
        }

        const auto &offsets = g.offsets();
        const auto &targets = g.targets();

        std::vector<double> rank(n, 0.0);
        std::vector<double> next_rank(n, 0.0);
        std::vector<double> ones(n, 0.0);

        detail::fill_vector(rank, 1.0);
        detail::scale_vector(rank, 1.0 / static_cast<double>(n));
        detail::fill_vector(ones, 1.0);

        CsrPageRankResult result;
        result.ranks = rank;

        const double d = options.damping_factor;
        for (std::size_t iter = 0; iter < options.max_iterations; ++iter) {
            double dangling_mass = 0.0;
            for (std::size_t u = 0; u < n; ++u) {
                if (offsets[u] == offsets[u + 1]) {
                    dangling_mass += rank[u];
                }
            }

            const double teleport = (1.0 - d) / static_cast<double>(n);
            detail::fill_vector(next_rank, teleport);

            if (dangling_mass != 0.0) {
                detail::add_scaled(next_rank, ones, d * dangling_mass / static_cast<double>(n));
            }

            for (std::size_t u = 0; u < n; ++u) {
                const std::size_t out_degree = offsets[u + 1] - offsets[u];
                if (out_degree == 0) continue;

                const double contrib = d * rank[u] / static_cast<double>(out_degree);
                for (std::size_t i = offsets[u]; i < offsets[u + 1]; ++i) {
                    next_rank[targets[i].value] += contrib;
                }
            }

            const double delta = detail::l1_delta(next_rank, rank);
            rank.swap(next_rank);
            result.iterations = iter + 1;

            if (delta <= options.tolerance) {
                result.converged = true;
                break;
            }
        }

        result.ranks = std::move(rank);
        return result;
#endif
        return litegraph::pagerank(g, options);
    }
} // namespace litegraph::highway

#endif // LITEGRAPH_HIGHWAY_HPP

