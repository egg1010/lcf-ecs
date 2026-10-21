#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

#include "part/bit_ops.hpp"
#include "part/time/timer.hpp"

#if defined(_WIN32)

#if !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0602
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if defined(_MSC_VER) || defined(__clang__)
#pragma warning(push, 0)
#endif
#include <windows.h>
#if defined(_MSC_VER) || defined(__clang__)
#pragma warning(pop)
#endif

#else
#if defined(__linux__)
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#else
#include <chrono>
#include <mutex>
#endif
#endif

namespace thread
{

using ::count_leading_zeros;
using ::count_trailing_zeros;
using ::fast_rand;
using ::mix_bits;
using ::monotonic_now_ns;
using ::popcount;

enum class platform_kind : std::uint8_t
{
    windows = 0,
    linux_futex = 1,
    posix_condvar = 2,
};

inline constexpr platform_kind current_platform() noexcept
{
#if defined(_WIN32)
    return platform_kind::windows;
#elif defined(__linux__)
    return platform_kind::linux_futex;
#else
    return platform_kind::posix_condvar;
#endif
}

inline constexpr std::uint32_t infinite_park_ms = 0xFFFFFFFFu;

inline constexpr std::uint32_t default_park_ms = 1u;

#if defined(_WIN32)

// x64 无寄存器保留约定；函数指针经 void* 双跳转型，全 ABI 安全

using wait_on_address_fn = BOOL(WINAPI*)(volatile void*, const void*, SIZE_T, DWORD);
using wake_by_address_single_fn = void(WINAPI*)(volatile void*);
using wake_by_address_all_fn = void(WINAPI*)(volatile void*);

struct wait_on_address_api
{
    wait_on_address_fn wait_on_address_{nullptr};
    wake_by_address_single_fn wake_by_address_single_{nullptr};
    wake_by_address_all_fn wake_by_address_all_{nullptr};
};

inline wait_on_address_api resolve_wait_on_address_api() noexcept
{
    wait_on_address_api api{};

    HMODULE const synch = GetModuleHandleW(L"api-ms-win-core-synch-l1-2-0.dll");
    HMODULE const kernel32 = GetModuleHandleW(L"kernel32.dll");
    HMODULE const provider = (synch != nullptr) ? synch : kernel32;

    if (provider == nullptr)
    {
        return api;
    }

    api.wait_on_address_ = reinterpret_cast<wait_on_address_fn>(
        reinterpret_cast<void*>(GetProcAddress(provider, "WaitOnAddress")));
    api.wake_by_address_single_ = reinterpret_cast<wake_by_address_single_fn>(
        reinterpret_cast<void*>(GetProcAddress(provider, "WakeByAddressSingle")));
    api.wake_by_address_all_ = reinterpret_cast<wake_by_address_all_fn>(
        reinterpret_cast<void*>(GetProcAddress(provider, "WakeByAddressAll")));

    return api;
}

inline const wait_on_address_api& wait_on_address_api_instance() noexcept
{
    static const wait_on_address_api api = resolve_wait_on_address_api();
    return api;
}

inline void platform_wait(volatile void* address, const void* compare, std::size_t size,
                          std::uint32_t timeout_ms) noexcept
{

    DWORD const budget = (timeout_ms == infinite_park_ms) ? INFINITE : timeout_ms;
    (void)wait_on_address_api_instance().wait_on_address_(address, compare, size, budget);
}

inline void platform_wake_one(volatile void* address) noexcept
{
    wake_by_address_single_fn const wake = wait_on_address_api_instance().wake_by_address_single_;
    if (wake != nullptr)
    {
        wake(address);
    }
}

inline void platform_wake_all(volatile void* address) noexcept
{
    wake_by_address_all_fn const wake = wait_on_address_api_instance().wake_by_address_all_;
    if (wake != nullptr)
    {
        wake(address);
    }
}

#elif defined(__linux__)

inline int futex_syscall(volatile std::uint32_t* address, int operation,
                         std::uint32_t value) noexcept
{
    return static_cast<int>(::syscall(SYS_futex, reinterpret_cast<std::uint32_t*>(address),
                                      operation, value, nullptr, nullptr, 0));
}

inline void platform_wait(volatile void* address, const void* compare, std::size_t size,
                          std::uint32_t timeout_ms) noexcept
{

    if (size == sizeof(std::uint32_t) && timeout_ms == infinite_park_ms)
    {
        (void)futex_syscall(static_cast<volatile std::uint32_t*>(address), FUTEX_WAIT_PRIVATE,
                            *static_cast<const std::uint32_t*>(compare));
        return;
    }

    if (size == sizeof(std::uint64_t))
    {
        (void)address;
        (void)compare;
        struct timespec span{};
        span.tv_nsec = static_cast<long>(timeout_ms) * 1000000L;
        ::nanosleep(&span, nullptr);
        return;
    }

    (void)futex_syscall(static_cast<volatile std::uint32_t*>(address), FUTEX_WAIT_PRIVATE,
                        *static_cast<const std::uint32_t*>(compare));
}

inline void platform_wake_one(volatile void* address) noexcept
{
    (void)futex_syscall(static_cast<volatile std::uint32_t*>(address), FUTEX_WAKE_PRIVATE, 1);
}

inline void platform_wake_all(volatile void* address) noexcept
{
    (void)futex_syscall(static_cast<volatile std::uint32_t*>(address), FUTEX_WAKE_PRIVATE,
                        0x7FFFFFFF);
}

#else

struct posix_condvar_fallback
{
    posix_condvar_fallback() noexcept = default;
};

inline std::mutex& fallback_mutex() noexcept
{
    static std::mutex locked{};
    return locked;
}

inline void platform_wait(volatile void* address, const void* compare, std::size_t size,
                          std::uint32_t timeout_ms) noexcept
{
    (void)address;
    (void)compare;
    (void)size;
    (void)timeout_ms;
}

inline void platform_wake_one(volatile void* address) noexcept
{
    (void)address;
}

inline void platform_wake_all(volatile void* address) noexcept
{
    (void)address;
}

#endif

inline bool platform_check_required() noexcept
{
#if defined(_WIN32)
    const wait_on_address_api& api = wait_on_address_api_instance();
    return api.wait_on_address_ != nullptr && api.wake_by_address_single_ != nullptr &&
           api.wake_by_address_all_ != nullptr;
#else
    return true;
#endif
}

enum class park_result : std::uint8_t
{
    woken = 0,
    timed_out = 1,
};

struct park_state
{
    std::atomic<std::uint32_t> word_{0};
    std::atomic<std::uint32_t> parks_{0};
    std::uint32_t reserved_{0};
    std::uint32_t reserved2_{0};
};

inline void park_state_init(park_state* state) noexcept
{
    state->word_.store(0, std::memory_order_relaxed);
    state->parks_.store(0, std::memory_order_relaxed);
}

inline void platform_relax_pause() noexcept
{
    // 只用标准库：编译屏障即可表达“暂停提示”，不依赖编译器内置或内联汇编，
    // 保证 GCC / Clang / MSVC 及其他 C++20 编译器行为一致。
    std::atomic_signal_fence(std::memory_order_seq_cst);
}

#if defined(_WIN32)
inline void platform_yield() noexcept
{
    SwitchToThread();
}
#else
inline void platform_yield() noexcept
{
    ::sched_yield();
}
#endif

inline park_result park_until_changed(volatile void* address, const void* expected,
                                      std::size_t size,
                                      std::uint32_t timeout_ms = default_park_ms) noexcept
{
    platform_wait(address, expected, size, timeout_ms);

    const std::uint32_t observed = *static_cast<volatile std::uint32_t*>(address);
    (void)observed;
    return park_result::woken;
}

inline park_result park_once(park_state* state, std::uint32_t expected,
                             std::uint32_t timeout_ms = default_park_ms) noexcept
{

    if (state->word_.load(std::memory_order_acquire) != expected)
    {
        return park_result::woken;
    }

    std::uint32_t parks = state->parks_.load(std::memory_order_relaxed);
    while (parks != 0xFFFFFFFFu &&
           !state->parks_.compare_exchange_weak(parks, parks + 1, std::memory_order_acquire,
                                                std::memory_order_relaxed))
    {
    }

    std::uint32_t const snapshot = state->word_.load(std::memory_order_relaxed);
    if (snapshot == expected)
    {
        platform_wait(reinterpret_cast<volatile void*>(&state->word_),
                      static_cast<const void*>(&snapshot), sizeof(std::uint32_t), timeout_ms);
    }

    const std::uint32_t observed = state->word_.load(std::memory_order_acquire);
    if (observed != expected)
    {
        (void)state->parks_.fetch_sub(1, std::memory_order_release);
        return park_result::woken;
    }

    (void)state->parks_.fetch_sub(1, std::memory_order_release);
    return park_result::timed_out;
}

inline void unpark_other(park_state* state, std::uint32_t new_word) noexcept
{
    state->word_.store(new_word, std::memory_order_release);

    const std::uint32_t parked = state->parks_.load(std::memory_order_acquire);
    if (parked == 0)
    {

        return;
    }

    if (parked == 1)
    {
        platform_wake_one(reinterpret_cast<volatile void*>(&state->word_));
    }
    else
    {

        platform_wake_all(reinterpret_cast<volatile void*>(&state->word_));
    }
}

inline void unpark_all(park_state* state, std::uint32_t new_word) noexcept
{
    state->word_.store(new_word, std::memory_order_release);
    platform_wake_all(reinterpret_cast<volatile void*>(&state->word_));
}

inline void park_self_wake_fallback(std::uint32_t timeout_ms) noexcept
{
    platform_wait(nullptr, nullptr, 0, timeout_ms);
}

inline bool set_current_thread_name(const char* name) noexcept
{
    if (name == nullptr)
    {
        return false;
    }

#if defined(_WIN32)
    using set_thread_description_fn = HRESULT(WINAPI*)(HANDLE, PCWSTR);
    HMODULE const kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (kernel32 == nullptr)
    {
        return false;
    }
    auto const set_description = reinterpret_cast<set_thread_description_fn>(
        reinterpret_cast<void*>(GetProcAddress(kernel32, "SetThreadDescription")));
    if (set_description == nullptr)
    {
        return false;
    }

    wchar_t wide[64] = {};
    int written = 0;
    for (; name[written] != '\0' && written < 63; ++written)
    {
        wide[written] = static_cast<wchar_t>(static_cast<unsigned char>(name[written]));
    }
    wide[written] = L'\0';

    return SUCCEEDED(set_description(GetCurrentThread(), wide));
#else
    char trimmed[16] = {};
    for (int i = 0; i < 15 && name[i] != '\0'; ++i)
    {
        trimmed[i] = name[i];
    }
#if defined(__linux__)
    return ::pthread_setname_np(::pthread_self(), trimmed) == 0;
#else
    (void)trimmed;
    return false;
#endif
#endif
}

inline bool pin_current_thread_to_core(std::uint32_t core_index) noexcept
{
#if defined(_WIN32)
    if (core_index >= 64)
    {
        using set_group_affinity_fn = BOOL(WINAPI*)(HANDLE, const GROUP_AFFINITY*, PGROUP_AFFINITY);
        HMODULE const kernel32 = GetModuleHandleW(L"kernel32.dll");
        if (kernel32 == nullptr)
        {
            return false;
        }
        auto const set_group = reinterpret_cast<set_group_affinity_fn>(
            reinterpret_cast<void*>(GetProcAddress(kernel32, "SetThreadGroupAffinity")));
        if (set_group == nullptr)
        {
            return false;
        }
        WORD const groups = GetActiveProcessorGroupCount();
        WORD const group = static_cast<WORD>(core_index / 64);
        if (group >= groups)
        {
            return false;
        }
        GROUP_AFFINITY affinity{};
        affinity.Group = group;
        affinity.Mask = static_cast<KAFFINITY>(1) << (core_index % 64);
        return set_group(GetCurrentThread(), &affinity, nullptr) != 0;
    }
    DWORD_PTR const mask = static_cast<DWORD_PTR>(1) << core_index;
    return SetThreadAffinityMask(GetCurrentThread(), mask) != 0;
#elif defined(__linux__)
    cpu_set_t set{};
    CPU_ZERO(&set);
    CPU_SET(static_cast<int>(core_index), &set);
    return ::sched_setaffinity(0, sizeof(set), &set) == 0;
#else
    (void)core_index;
    return false;
#endif
}

inline std::uint64_t process_thread_id() noexcept
{
#if defined(_WIN32)
    return static_cast<std::uint64_t>(GetCurrentThreadId());
#elif defined(__linux__)
    return static_cast<std::uint64_t>(::syscall(SYS_gettid));
#else
    return static_cast<std::uint64_t>(0);
#endif
}

} // namespace thread
