/* A test harness small enough to read in one sitting.
 *
 * CHECK records a failure and carries on, so one run reports every broken
 * assertion rather than the first; REQUIRE stops the case, for the ones where
 * continuing would only produce noise (a file that did not load has nothing
 * worth asserting about). */
#ifndef DRAGOMAN_TEST_CHECK_H
#define DRAGOMAN_TEST_CHECK_H

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

namespace check {

inline int& failures() {
    static int n = 0;
    return n;
}

inline void report(const char* file, int line, const std::string& expr,
                   const std::string& detail) {
    std::fprintf(stderr, "  FAIL %s:%d  %s\n", file, line, expr.c_str());
    if (!detail.empty()) std::fprintf(stderr, "       %s\n", detail.c_str());
    ++failures();
}

template <typename A, typename B>
std::string describe(const A& a, const B& b) {
    std::ostringstream os;
    os << "got " << a << ", wanted " << b;
    return os.str();
}

inline int finish(const char* suite) {
    if (failures() == 0) {
        std::printf("ok   %s\n", suite);
        return 0;
    }
    std::printf("FAIL %s (%d assertion%s)\n", suite, failures(), failures() == 1 ? "" : "s");
    return 1;
}

}  // namespace check

#define CHECK(expr)                                                     \
    do {                                                                \
        if (!(expr)) check::report(__FILE__, __LINE__, #expr, "");      \
    } while (0)

#define CHECK_EQ(a, b)                                                          \
    do {                                                                        \
        const auto _a = (a);                                                    \
        const auto _b = (b);                                                    \
        if (!(_a == _b))                                                        \
            check::report(__FILE__, __LINE__, #a " == " #b,                     \
                          check::describe(_a, _b));                             \
    } while (0)

#define REQUIRE(expr)                                                            \
    do {                                                                         \
        if (!(expr)) {                                                           \
            check::report(__FILE__, __LINE__, #expr, "cannot continue");         \
            return;                                                              \
        }                                                                        \
    } while (0)

#endif
