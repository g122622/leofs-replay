#pragma once

// ============================================================================
// Assertion macros
//
// Aligned with Cubium spec 1.3: use assertions instead of redundant defensive
// if-checks to quickly surface architectural and logic flaws. MC_ASSERT_RELEASE
// stays in effect even in Release (very low cost); in Debug it additionally
// triggers a breakpoint. This file is self-contained and does not depend on the
// implementation details of MC_ASSERT.
// ============================================================================

#include <cassert>
#include <cstdio>
#include <cstdlib>

namespace trace_replay::detail {

[[noreturn]] inline void assertionFailed(const char* expr, const char* file, int line)
{
    std::fprintf(stderr, "[trace_replay] assertion failed: %s (%s:%d)\n",
                 expr, file, line);
    std::abort();
}

}  // namespace trace_replay::detail

// Assertion that stays in effect in Release: used to verify invariants whose
// preconditions should already hold.
#define TR_ASSERT(expr) \
    do { \
        if (!(expr)) { \
            ::trace_replay::detail::assertionFailed(#expr, __FILE__, __LINE__); \
        } \
    } while (false)

// Debug-only assertion: for cost-sensitive checks needed only during development.
#ifdef NDEBUG
    #define TR_ASSERT_DEBUG(expr) ((void)0)
#else
    #define TR_ASSERT_DEBUG(expr) TR_ASSERT(expr)
#endif
