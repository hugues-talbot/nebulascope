// Minimal test harness — no framework dependency. Each test file defines
// TESTS(...) cases and links this main. Failures print file:line and the
// expression; exit code = failure count (CTest interprets non-zero as FAIL).
#pragma once
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace nstest {

struct Case { std::string name; std::function<void()> fn; };
inline std::vector<Case>& cases() { static std::vector<Case> c; return c; }
inline int& failures() { static int f = 0; return f; }

struct Register {
    Register(const char* name, std::function<void()> fn) { cases().push_back({name, std::move(fn)}); }
};

#define NS_TEST(name) \
    static void nstest_##name(); \
    static nstest::Register nstest_reg_##name(#name, nstest_##name); \
    static void nstest_##name()

#define NS_CHECK(expr) \
    do { if (!(expr)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); ++nstest::failures(); } } while (0)

#define NS_CHECK_NEAR(a, b, tol) \
    do { const double _a=(a), _b=(b); if (!(std::fabs(_a-_b) <= (tol))) { \
        std::printf("FAIL %s:%d  %s=%g vs %s=%g (tol %g)\n", __FILE__, __LINE__, #a, _a, #b, _b, double(tol)); \
        ++nstest::failures(); } } while (0)

inline int runAll() {
    for (auto& c : cases()) {
        const int before = failures();
        c.fn();
        std::printf("%-40s %s\n", c.name.c_str(), failures() == before ? "ok" : "FAILED");
    }
    if (failures()) std::printf("\n%d check(s) failed\n", failures());
    else std::printf("\nall tests passed\n");
    return failures();
}

} // namespace nstest
