#pragma once

#ifndef SRC_EXAMPLES_EXAMPLE_MLX_HPP
#define SRC_EXAMPLES_EXAMPLE_MLX_HPP

// Helpful MLX usage examples. These mirror the API usage in
// src/tests/test_mlx.cpp so you can copy/paste snippets into a
// small program or documentation. The examples are guarded so
// they only compile when <mlx/mlx.h> is available.

#if __has_include(<mlx/mlx.h>)
#include <mlx/mlx.h>
#include <iostream>
#include <vector>
#include <array>
#include <string_view>
#include <span>
#include <cmath>
#include "test/example_registry.hpp" // bring testfw::Result and helpers into scope

namespace examples {
namespace mlx_examples {

// Simple: create arrays and check shapes / dtype
inline void example_creation_and_shape() {
	auto arr = mlx::core::ones({3, 2}, mlx::core::float32);
	arr.eval();
	const float *data = arr.data<float>();
	std::cout << "ones(3,2) first element = " << data[0] << '\n';
}

// Elementwise add / multiply
inline void example_elementwise() {
	auto a = mlx::core::ones({4}, mlx::core::float32);
	auto b = mlx::core::full({4}, 2.0f, mlx::core::float32);
	auto c = mlx::core::add(a, b);
	c.eval();
	const float *c_data = c.data<float>();
	std::cout << "c[0] = " << c_data[0] << " (expect 3)\n";
}

// Matmul example
inline void example_matmul() {
	auto a = mlx::core::ones({2, 3}, mlx::core::float32);
	auto b = mlx::core::full({3, 4}, 2.0f, mlx::core::float32);
	auto c = mlx::core::matmul(a, b);
	c.eval();
	std::cout << "matmul shape: ";
	for (auto d : c.shape()) std::cout << d << " ";
	std::cout << '\n';
}

// Broadcasting example
inline void example_broadcasting() {
	auto a = mlx::core::ones({2, 1}, mlx::core::float32);
	auto b = mlx::core::full({1, 3}, 5.0f, mlx::core::float32);
	auto c = mlx::core::add(a, b);
	c.eval();
	const float *c_data = c.data<float>();
	std::cout << "broadcasted[0] = " << c_data[0] << " (expect 6)\n";
}

// Creation utilities: zeros, arange, reshape, astype
inline void example_creation_reshape_cast() {
	auto zeros = mlx::core::zeros({5, 2}, mlx::core::float32);
	zeros.eval();
	auto ar = mlx::core::arange(0, 6, 1, mlx::core::int32);
	ar.eval();
	auto reshaped = mlx::core::reshape(ar, {2, 3});
	reshaped.eval();
	auto casted = mlx::core::astype(reshaped, mlx::core::float32);
	casted.eval();
	std::cout << "reshaped.dtype = " << casted.dtype() << '\n';
}

// Slicing and indexing
inline void example_slice_index() {
	auto arr = mlx::core::arange(0, 10, 1, mlx::core::int32);
	arr.eval();
	auto sliced = mlx::core::slice(arr, {2}, {7}, {1});
	sliced.eval();
	const int *s = sliced.data<int>();
	std::cout << "sliced[0] = " << s[0] << " (expect 2)\n";
}

// Where / logical / comparison example
inline void example_where_logical() {
	auto a = mlx::core::arange(0, 5, 1, mlx::core::float32);
	auto b = mlx::core::full({5}, 2.0f, mlx::core::float32);
	auto mask = mlx::core::greater(a, b);
	mask.eval();
	auto res = mlx::core::where(mask, a, b);
	res.eval();
	const float *r = res.data<float>();
	std::cout << "where result[3] = " << r[3] << "\n";
}

// Random / monte carlo pi example (small N for demonstration)
inline float example_monte_carlo_pi(int N = 10000) {
	auto x = mlx::core::random::uniform({N}, mlx::core::float32);
	auto y = mlx::core::random::uniform({N}, mlx::core::float32);
	x.eval(); y.eval();
	auto x2 = mlx::core::multiply(x, x);
	auto y2 = mlx::core::multiply(y, y);
	auto dist2 = mlx::core::add(x2, y2);
	auto ones = mlx::core::full({N}, 1.0f, mlx::core::float32);
	auto mask = mlx::core::less_equal(dist2, ones);
	mask.eval();
	auto mask_f = mlx::core::astype(mask, mlx::core::float32);
	mask_f.eval();
	auto inside = mlx::core::sum(mask_f);
	inside.eval();
	float count = inside.item<float>();
	return (N > 0) ? (4.0f * count / N) : 0.0f;
}

// Softmax example (numerically stable; uses exp and sum)
inline mlx::core::array example_softmax(const mlx::core::array &v) {
	// subtract max for numerical stability
	auto vmax = mlx::core::max(v);
	auto shifted = mlx::core::subtract(v, vmax);
	auto ex = mlx::core::exp(shifted);
	auto s = mlx::core::sum(ex);
	// broadcasting division
	auto sm = mlx::core::divide(ex, s);
	return sm;
}

// Additional comprehensive MLX examples (deterministic, self-checking)

// Reduction: compute mean of a constant array and verify value
inline bool example_reduction_mean() {
	constexpr int N = 100;
	constexpr float value = 3.5f;
	auto v = mlx::core::full({N}, value, mlx::core::float32);
	v.eval();
	auto s = mlx::core::sum(v);
	s.eval();
	float total = s.item<float>();
	float mean = total / static_cast<float>(N);
	std::cout << "reduction mean = " << mean << " (expect " << value << ")\n";
	return std::fabs(mean - value) < 1e-6f;
}

// Dot product implemented via matmul on shaped vectors
inline bool example_dot_with_matmul() {
	constexpr int N = 16;
	// a = ones(1, N), b = full(N, 2.0) shaped as (N,1) -> result is 1x1 with value N*2
	auto a = mlx::core::ones({1, N}, mlx::core::float32);
	auto b = mlx::core::full({N, 1}, 2.0f, mlx::core::float32);
	auto c = mlx::core::matmul(a, b);
	c.eval();
	// c should be shape {1,1}
	float val = c.item<float>();
	float expect = static_cast<float>(N) * 2.0f;
	std::cout << "dot(matmul) = " << val << " (expect " << expect << ")\n";
	return std::fabs(val - expect) < 1e-6f;
}

// Masking with where: sum elements greater than threshold
inline bool example_where_masking() {
	constexpr int N = 10;
	constexpr int threshold = 5;
	// create 0..N-1
	auto ar = mlx::core::arange(0, N, 1, mlx::core::int32);
	ar.eval();
	auto thresh = mlx::core::full({N}, static_cast<float>(threshold), mlx::core::float32);
	// convert ar to float to compare
	auto ar_f = mlx::core::astype(ar, mlx::core::float32);
	ar_f.eval();
	auto mask = mlx::core::greater(ar_f, thresh);
	mask.eval();
	auto selected = mlx::core::where(mask, ar_f, mlx::core::full({N}, 0.0f, mlx::core::float32));
	selected.eval();
	auto s = mlx::core::sum(selected);
	s.eval();
	float total = s.item<float>();
	// expected sum of integers > threshold: sum_{i=threshold+1..N-1} i
	int first = threshold + 1;
	int last = N - 1;
	int expected_int = 0;
	for (int i = first; i <= last; ++i) expected_int += i;
	std::cout << "where sum = " << total << " (expect " << expected_int << ")\n";
	return std::fabs(total - static_cast<float>(expected_int)) < 1e-6f;
}


} // namespace mlx_examples
} // namespace examples

#if __has_include(<mlx/mlx.h>)
// Provide a small ExampleType that can be registered with the example registry.
// This keeps the existing helper functions above for interactive examples,
// and exposes a single `MlxExample` that the examples runner can call.
struct MlxExample {
	static constexpr std::string_view name() { return "mlx_example"; }
	static constexpr std::string_view description() { return "MLX API example: basic array ops and device info"; }

	static constexpr std::array<std::string_view, 2> tag_data{"mlx", "external"};
	static constexpr std::span<const std::string_view> tags() { return tag_data; }

	static testfw::Result run() {
		try {
			// Query device counts (may be zero)
			int cpu_count = mlx::core::device_count(mlx::core::Device::cpu);
			int gpu_count = mlx::core::device_count(mlx::core::Device::gpu);
			std::cout << "MLX devices: cpu=" << cpu_count << " gpu=" << gpu_count << '\n';

			// Print some device info if available
			const auto &dev = mlx::core::default_device();
			const auto info = mlx::core::device_info(dev);
			if (auto it = info.find("device_name"); it != info.end()) {
				if (auto p = std::get_if<std::string>(&it->second)) {
					std::cout << "device_name: " << *p << '\n';
				}
			}
			if (auto it = info.find("architecture"); it != info.end()) {
				if (auto p = std::get_if<std::string>(&it->second)) {
					std::cout << "architecture: " << *p << '\n';
				}
			}

			// Small arithmetic smoke test using arrays
			auto a = mlx::core::array(1.0f);
			auto b = mlx::core::array(2.0f);
			auto c = a + b;
			c.eval();
			float val = c.item<float>();
			if (val != 3.0f) {
				return testfw::fail("MLX arithmetic produced unexpected result");
			}
			std::cout << "MLX arithmetic succeeded: 1 + 2 = " << val << '\n';

			// Run additional deterministic examples and fail the example if any check fails
			if (!examples::mlx_examples::example_reduction_mean()) {
				return testfw::fail("example_reduction_mean failed");
			}
			if (!examples::mlx_examples::example_dot_with_matmul()) {
				return testfw::fail("example_dot_with_matmul failed");
			}
			if (!examples::mlx_examples::example_where_masking()) {
				return testfw::fail("example_where_masking failed");
			}

			return {};
		} catch (const std::exception &e) {
			return testfw::fail(std::string_view{e.what()});
		}
	}
};
#define EXAMPLE_MLX_AVAILABLE 1
#endif

#else
// <mlx/mlx.h> not available: provide compilation-friendly placeholders
#include <iostream>
namespace examples {
namespace mlx_examples {
inline void example_creation_and_shape() { std::cerr << "MLX not available." << std::endl; }
inline void example_elementwise() { std::cerr << "MLX not available." << std::endl; }
inline void example_matmul() { std::cerr << "MLX not available." << std::endl; }
inline void example_broadcasting() { std::cerr << "MLX not available." << std::endl; }
inline void example_creation_reshape_cast() { std::cerr << "MLX not available." << std::endl; }
inline void example_slice_index() { std::cerr << "MLX not available." << std::endl; }
inline void example_where_logical() { std::cerr << "MLX not available." << std::endl; }
inline float example_monte_carlo_pi(int) { std::cerr << "MLX not available." << std::endl; return 0.0f; }
// softmax not provided when MLX missing
} // namespace mlx_examples
} // namespace examples
#endif

#endif // SRC_EXAMPLES_EXAMPLE_MLX_HPP

