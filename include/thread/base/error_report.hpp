#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "part/keyed_throttle.hpp"
#include "thread/base/error.hpp"
#include "thread/base/task_identity.hpp"

namespace thread
{

// 当前线程最近一次 API 失败原因（成功调用不清零）
struct error_state
{
    errc code_{errc::ok};
    error_severity severity_{error_severity::info};
    std::uint32_t site_{0};
};

inline error_state& last_error_slot() noexcept
{
    thread_local error_state state{};
    return state;
}

inline error_state last_error() noexcept
{
    return last_error_slot();
}

inline void set_last_error(error_state state) noexcept
{
    last_error_slot() = state;
}

inline void set_last_error(errc code, error_severity severity, std::uint32_t site) noexcept
{
    error_state& slot = last_error_slot();
    slot.code_ = code;
    slot.severity_ = severity;
    slot.site_ = site;
}

// 结构化上报消息；message_ 只在 sink 回调期间有效
struct error_report
{
    error_severity severity_{error_severity::error};
    errc code_{errc::ok};
    std::uint32_t site_{0};
    std::uint64_t sequence_{0};
    task_id task_{};
    thread::role role_{thread::role::general};
    std::uint32_t worker_{task_worker_none};
    std::string_view message_{};
};

using error_sink = void (*)(const error_report& report, void* user) noexcept;

inline constexpr std::uint32_t error_throttle_slots = 64;
inline constexpr std::uint32_t error_throttle_per_second = 8;

// 固定 64 槽限流器：秒窗口 + 键控合并；底层复用 keyed_throttle
struct error_throttle
{
    keyed_throttle<error_throttle_slots> inner_{};

    static std::uint64_t make_key(errc code, std::uint32_t site) noexcept
    {
        return (static_cast<std::uint64_t>(site) << 32) |
               static_cast<std::uint64_t>(static_cast<std::uint32_t>(code));
    }

    static std::uint64_t now_seconds() noexcept
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
    }

    bool allow(errc code, std::uint32_t site, error_severity severity) noexcept
    {
        if (severity == error_severity::fatal)
        {
            return true;
        }
        return inner_.allow(make_key(code, site), now_seconds(), error_throttle_per_second);
    }

    void clear() noexcept
    {
        inner_.clear();
    }
};

// 每个 manager 一个 reporter：默认写 stderr 并计数，可整体替换为用户的 sink
struct error_reporter
{
    std::atomic<error_sink> sink_{nullptr};
    std::atomic<void*> user_{nullptr};
    error_throttle throttle_{};
    std::atomic<std::uint64_t> sequence_{0};
    std::atomic<std::uint64_t> reported_{0};
    std::atomic<std::uint64_t> suppressed_{0};
    std::atomic<std::uint64_t> sink_faults_{0};
    std::atomic<bool> in_dispatch_{false};

    void set_sink(error_sink sink, void* user) noexcept
    {
        user_.store(user, std::memory_order_release);
        sink_.store(sink, std::memory_order_release);
    }

    static void default_sink(const error_report& report, void*) noexcept
    {
        const char* text = report.message_.data() != nullptr ? report.message_.data() : "";
        std::printf("[thread:%s] errc=%s site=%08x task=%u role=%u worker=%u %.*s\n",
                    severity_name(report.severity_), errc_name(report.code_),
                    static_cast<unsigned>(report.site_),
                    static_cast<unsigned>(report.task_.index_),
                    static_cast<unsigned>(report.role_), static_cast<unsigned>(report.worker_),
                    static_cast<int>(report.message_.size()), text);
    }

    void emit(error_severity severity, errc code, std::uint32_t site, task_id task,
              thread::role role_id, std::uint32_t worker, std::string_view message) noexcept
    {
        if (!throttle_.allow(code, site, severity))
        {
            suppressed_.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        error_report report{};
        report.severity_ = severity;
        report.code_ = code;
        report.site_ = site;
        report.sequence_ = sequence_.fetch_add(1, std::memory_order_relaxed) + 1;
        report.task_ = task;
        report.role_ = role_id;
        report.worker_ = worker;
        report.message_ = message;

        bool expected = false;
        if (!in_dispatch_.compare_exchange_strong(expected, true, std::memory_order_acq_rel,
                                                  std::memory_order_relaxed))
        {
            // sink 重入：只计数，回退默认 sink，不中断调度
            sink_faults_.fetch_add(1, std::memory_order_relaxed);
            default_sink(report, nullptr);
            reported_.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        error_sink sink = sink_.load(std::memory_order_acquire);
        void* user = user_.load(std::memory_order_acquire);
        if (sink == nullptr)
        {
            sink = &default_sink;
            user = nullptr;
        }

        sink(report, user);
        in_dispatch_.store(false, std::memory_order_release);
        reported_.fetch_add(1, std::memory_order_relaxed);
    }

    void reset() noexcept
    {
        sink_.store(nullptr, std::memory_order_release);
        user_.store(nullptr, std::memory_order_release);
        throttle_.clear();
        sequence_.store(0, std::memory_order_relaxed);
        reported_.store(0, std::memory_order_relaxed);
        suppressed_.store(0, std::memory_order_relaxed);
        sink_faults_.store(0, std::memory_order_relaxed);
        in_dispatch_.store(false, std::memory_order_relaxed);
    }
};
} // namespace thread
