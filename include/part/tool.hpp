#pragma once

// tool.hpp - 通用工具函数
// 跨平台 (GCC / MSVC / Clang) 工作区路径获取

#include <cstring>
#include <cstdlib>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#elif defined(__linux__)
    #include <unistd.h>
    #include <limits.h>
    #include <sys/types.h>
    #include <pwd.h>
#elif defined(__APPLE__)
    #include <unistd.h>
    #include <limits.h>
    #include <mach-o/dyld.h>
#else
    #include <unistd.h>
    #include <limits.h>
#endif

// 返回工作区路径 (UTF-8 编码)
// Windows: 返回当前进程的工作目录
// Linux/macOS: 返回当前进程的工作目录
// 失败时返回 nullptr, 调用方需检查返回值
// 注意: 返回的 char* 指向静态缓冲区, 每次调用会覆盖前一次结果
//       若需保留结果, 调用方应立即复制
[[nodiscard]] inline char* workspace_path() noexcept
{
    static char buffer[4096];

#if defined(_WIN32)
    DWORD len = ::GetCurrentDirectoryA(sizeof(buffer), buffer);
    if (len == 0 || len >= sizeof(buffer)) return nullptr;
    buffer[len] = '\0';
    return buffer;
#else
    if (::getcwd(buffer, sizeof(buffer)) == nullptr) return nullptr;
    return buffer;
#endif
}
