#pragma once
// OOM 统一处理: 项目禁止异常, 分配失败时输出诊断后 std::abort

#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <source_location>
#include "../force_inline.hpp"
#include "../operating_message.hpp"

namespace memory {

[[noreturn]] LCF_FORCE_INLINE void handle_oom(
    size_t requested_bytes,
    const char* allocator_name,
    std::source_location loc = std::source_location::current()) noexcept
{
    std::fprintf(stderr,
        "[OOM] 内存分配失败\n"
        "  分配器: %s\n"
        "  请求字节: %zu\n"
        "  位置: %s:%lu\n"
        "  函数: %s\n",
        allocator_name ? allocator_name : "unknown",
        requested_bytes,
        loc.file_name(), static_cast<unsigned long>(loc.line()), loc.function_name());
    std::fflush(stderr);
    std::abort();
}

[[noreturn]] LCF_FORCE_INLINE void handle_oom(
    size_t requested_bytes,
    const char* allocator_name,
    operating_message& error,
    std::source_location loc = std::source_location::current()) noexcept
{
    error.write_failure(om_err_out_of_memory.code, msg_level::error, loc);
    std::fprintf(stderr,
        "[OOM] 内存分配失败: %s, 请求 %zu 字节, 位置 %s:%lu\n",
        allocator_name ? allocator_name : "unknown",
        requested_bytes,
        loc.file_name(), static_cast<unsigned long>(loc.line()));
    std::fflush(stderr);
    std::abort();
}

} // namespace memory
