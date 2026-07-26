#pragma once

// ============================================================================
// 断言宏
//
// 对齐 Cubium 规范 1.3：用断言替代冗余的防御性 if 检查，迅速暴露架构与
// 逻辑缺陷。Release 下 MC_ASSERT_RELEASE 仍生效（开销极低），Debug 下额外
// 触发断点。本文不引入对 MC_ASSERT 的实现细节依赖，自包含实现。
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

// Release 仍生效的断言：用于校验"前置条件本应满足"的不变量。
#define TR_ASSERT(expr) \
    do { \
        if (!(expr)) { \
            ::trace_replay::detail::assertionFailed(#expr, __FILE__, __LINE__); \
        } \
    } while (false)

// Debug-only 断言：开销敏感、仅开发期需要的检查。
#ifdef NDEBUG
    #define TR_ASSERT_DEBUG(expr) ((void)0)
#else
    #define TR_ASSERT_DEBUG(expr) TR_ASSERT(expr)
#endif
