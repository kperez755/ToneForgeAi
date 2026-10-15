#pragma once

// Minimal, dependency-free unit test harness. The sandbox this project was
// bootstrapped in has no outbound access to package/CDN hosts (verified:
// raw.githubusercontent.com and GitHub release assets are both blocked by
// the environment's network allowlist), so pulling in Catch2/GoogleTest via
// FetchContent was not possible here. This harness is intentionally tiny -
// registration + REQUIRE + a runner - and should be swapped for Catch2 (via
// FetchContent, or vendored) as soon as this project is built somewhere
// with normal internet access. Swapping is low-risk: it only touches
// core/tests/*, never core/include or core/src.

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace tfcore::test {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

struct AssertionFailure {
    std::string message;
};

inline int& failureCount() {
    static int count = 0;
    return count;
}

inline void reportFailure(const char* expr, const char* file, int line) {
    std::fprintf(stderr, "  FAILED: %s (%s:%d)\n", expr, file, line);
    ++failureCount();
}

inline int runAll() {
    int total = 0;
    for (auto& test : registry()) {
        ++total;
        int before = failureCount();
        std::printf("[RUN ] %s\n", test.name.c_str());
        test.fn();
        if (failureCount() == before) {
            std::printf("[ OK ] %s\n", test.name.c_str());
        } else {
            std::printf("[FAIL] %s\n", test.name.c_str());
        }
    }
    std::printf("\n%d test case(s) run, %d assertion failure(s)\n", total, failureCount());
    return failureCount() == 0 ? 0 : 1;
}

} // namespace tfcore::test

#define TF_CONCAT_INNER(a, b) a##b
#define TF_CONCAT(a, b) TF_CONCAT_INNER(a, b)

#define TEST_CASE(name)                                                        \
    static void TF_CONCAT(tf_test_fn_, __LINE__)();                            \
    static ::tfcore::test::Registrar TF_CONCAT(tf_test_reg_, __LINE__)(        \
        name, TF_CONCAT(tf_test_fn_, __LINE__));                               \
    static void TF_CONCAT(tf_test_fn_, __LINE__)()

#define REQUIRE(expr)                                                          \
    do {                                                                       \
        if (!(expr)) { ::tfcore::test::reportFailure(#expr, __FILE__, __LINE__); } \
    } while (0)

#define REQUIRE_FALSE(expr) REQUIRE(!(expr))

inline bool tf_approx(double a, double b, double eps = 1e-4) {
    return (a > b ? a - b : b - a) <= eps;
}

#define REQUIRE_APPROX(a, b) REQUIRE(tf_approx((a), (b)))
