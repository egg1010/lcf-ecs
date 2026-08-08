#pragma once

// 字符串类: 拥有内存, SSO + 码点偏移缓存, 实现拆分到 detail/*.hpp

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>
#include <cerrno>
#include <compare>
#include <string_view>
#include <string>
#include <array>
#include <vector>
#include <span>
#include <iterator>
#include <iostream>
#include <limits>
#include "../force_inline.hpp"
#include "../dense.hpp"
#include "utf8_codec.hpp"
#include "utf8_view.hpp"
#include "unicode_data.hpp"
#include "../../config/utf8pp_config.hpp"

// 内存分配层: 启用时接入 layered_allocator, 否则回退 malloc/free
#if defined(UTF8PP_USE_LAYERED_ALLOCATOR)
#include "../memory/layered_allocator.hpp"
#include <bit>

inline memory::layered_allocator utf8pp_pool_{};

// 无分支 slab 定位: ≤128 按位宽分桶, >128 统一大块
[[nodiscard]] static constexpr size_t utf8pp_slab_index(size_t n) noexcept
{
    if (n <= 128) return std::bit_width(n - 1) - 4;
    return 4;
}

[[nodiscard]] FORCE_INLINE void* utf8pp_alloc(size_t n) noexcept { return utf8pp_pool_.allocate(n); }

// 带大小释放: slab 定位 + owns 验证, slab 满时安全回退 big_pool
FORCE_INLINE void utf8pp_free(void* p, size_t n) noexcept
{
    if (!p) [[unlikely]] return;
    if (n > 128) { utf8pp_pool_.big_pool().soft_deallocate(p); return; }
    size_t idx = utf8pp_slab_index(n);
    if (utf8pp_pool_.slab(idx).owns(p)) [[likely]]
    {
        utf8pp_pool_.slab(idx).deallocate(p);
        return;
    }
    // 满降级回退 big_pool
    utf8pp_pool_.big_pool().soft_deallocate(p);
}

// 无大小释放: 地址扫描定位
FORCE_INLINE void utf8pp_free(void* p) noexcept { return utf8pp_pool_.deallocate(p); }
#else
[[nodiscard]] FORCE_INLINE void* utf8pp_alloc(size_t n) noexcept { return std::malloc(n); }
FORCE_INLINE void utf8pp_free(void* p) noexcept { return std::free(p); }
FORCE_INLINE void utf8pp_free(void* p, size_t /*n*/) noexcept { return std::free(p); }
#endif

class utf8pp
{
public:
    #include "detail/iterators.hpp"
    #include "detail/construct.hpp"
    #include "detail/capacity.hpp"
    #include "detail/modify.hpp"
    #include "detail/search.hpp"
    #include "detail/predicates.hpp"
    #include "detail/convert.hpp"
    #include "detail/string_ops.hpp"
    #include "detail/unicode.hpp"
    #include "detail/private.hpp"
};

#include "nonmember.hpp"
