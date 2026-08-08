#pragma once

// utf8pp.hpp - utf8pp 字符串类 (拥有内存, SSO + 码点偏移缓存)
// 实现拆分到 utf8pp/detail/*.hpp

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
#include "force_inline.hpp"
#include "dense.hpp"
#include "utf8_codec.hpp"
#include "utf8_view.hpp"
#include "unicode_data.hpp"
#include "../config/utf8pp_config.hpp"

#if UTF8PP_ENABLE_ALLOCATOR
#include "memory_pool.hpp"
#if UTF8PP_ALLOCATOR_TYPE == UTF8PP_ALLOC_LAYERED
#include "layered_allocator.hpp"
#endif
#endif

// 内存分配层 (默认 std::malloc/std::free; 启用配置接入项目分配器)
#if UTF8PP_ENABLE_ALLOCATOR
#if UTF8PP_ALLOCATOR_TYPE == UTF8PP_ALLOC_LAYERED
inline layered_allocator utf8pp_pool_{};
#elif UTF8PP_ALLOCATOR_TYPE == UTF8PP_ALLOC_MEMORY_POOL
inline memory_pool utf8pp_pool_{};
#endif
[[nodiscard]] FORCE_INLINE void* utf8pp_alloc(size_t n) noexcept { return utf8pp_pool_.allocate(n); }
FORCE_INLINE void utf8pp_free(void* p) noexcept { return utf8pp_pool_.deallocate(p); }
FORCE_INLINE void utf8pp_free(void* p, size_t n) noexcept { return utf8pp_pool_.deallocate(p, n); }
#else
[[nodiscard]] FORCE_INLINE void* utf8pp_alloc(size_t n) noexcept { return std::malloc(n); }
FORCE_INLINE void utf8pp_free(void* p) noexcept { return std::free(p); }
FORCE_INLINE void utf8pp_free(void* p, size_t /*n*/) noexcept { return std::free(p); }
#endif

class utf8pp
{
public:
    #include "utf8pp/detail/iterators.hpp"
    #include "utf8pp/detail/construct.hpp"
    #include "utf8pp/detail/capacity.hpp"
    #include "utf8pp/detail/modify.hpp"
    #include "utf8pp/detail/search.hpp"
    #include "utf8pp/detail/predicates.hpp"
    #include "utf8pp/detail/convert.hpp"
    #include "utf8pp/detail/string_ops.hpp"
    #include "utf8pp/detail/unicode.hpp"
    #include "utf8pp/detail/private.hpp"
};

#include "utf8pp/nonmember.hpp"
