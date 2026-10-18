#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "part/fnv1a.hpp"

namespace framework
{
enum class errc : std::uint32_t
{
    ok = 0,
    no_such_lane = 1,
    no_such_worker,
    no_such_tag,
    lane_destroyed,
    inject_queue_full,
    lane_queue_full,
    job_slot_exhausted,
    coroutine_frame_exhausted,
    thread_spawn_failed,
    not_running,
    shutdown_in_progress,
    already_registered,
    invalid_argument,
    timeout,
    invariant_broken,
    handler_faulted,
    cancelled,
};

enum class error_severity : std::uint8_t
{
    info = 0,
    warning = 1,
    error = 2,
    fatal = 3,
};

enum class error_action : std::uint8_t
{
    continue_ = 0,
    abort_frame,
    shutdown,
    terminate_,
};

struct error_info
{
    error_severity severity_{error_severity::info};
    errc code_{errc::ok};
    std::uint32_t site_{0};
    std::uint16_t tag_{0};
    std::uint16_t flags_{0};
    std::uint32_t lane_id_{0};
    std::uint32_t thread_{0};
};

inline constexpr std::uint16_t error_flag_in_worker = 1u << 0;
inline constexpr std::uint16_t error_flag_in_lane = 1u << 1;
inline constexpr std::uint16_t error_flag_external = 1u << 2;

using error_handler = error_action (*)(const error_info&) noexcept;
using fatal_handler = error_action (*)(const error_info&) noexcept;

inline constexpr std::uint32_t make_site(std::uint32_t file_hash, std::uint32_t line) noexcept
{
    return (file_hash * 2654435761u) ^ (line * 40503u);
}

#define FRAMEWORK_SITE()                                                                           \
    ::framework::make_site(static_cast<std::uint32_t>(::fnv1a_consteval(__FILE__)),                 \
                           static_cast<std::uint32_t>(__LINE__))

inline constexpr std::size_t severity_count = 4;
inline constexpr std::size_t tag_count_max = 64;

struct error_counters
{
    std::atomic<std::uint64_t> by_severity_[severity_count] = {};
    std::atomic<std::uint64_t> by_tag_[tag_count_max] = {};
    std::atomic<std::uint64_t> ignored_fatal_{0};
    std::atomic<std::uint64_t> handler_faults_{0};
    std::atomic<std::uint64_t> total_{0};

    void bump(error_severity severity, std::uint16_t tag) noexcept
    {
        const std::size_t level = static_cast<std::size_t>(severity);
        if (level < severity_count)
        {
            by_severity_[level].fetch_add(1, std::memory_order_relaxed);
        }
        if (static_cast<std::size_t>(tag) < tag_count_max)
        {
            by_tag_[tag].fetch_add(1, std::memory_order_relaxed);
        }
        total_.fetch_add(1, std::memory_order_relaxed);
    }

    void clear() noexcept
    {
        for (std::size_t i = 0; i < severity_count; ++i)
        {
            by_severity_[i].store(0, std::memory_order_relaxed);
        }
        for (std::size_t i = 0; i < tag_count_max; ++i)
        {
            by_tag_[i].store(0, std::memory_order_relaxed);
        }
        ignored_fatal_.store(0, std::memory_order_relaxed);
        handler_faults_.store(0, std::memory_order_relaxed);
        total_.store(0, std::memory_order_relaxed);
    }
};

inline error_counters& global_error_counters() noexcept
{
    static error_counters counters{};
    return counters;
}

enum class log_level : std::uint8_t
{
    info = 0,
    warning = 1,
    error = 2,
    fatal = 3,
};

using log_sink = void (*)(log_level level, const char* text, void* user) noexcept;

struct log_channel
{
    std::atomic<log_sink> sink_{nullptr};
    std::atomic<void*> user_{nullptr};

    void set(log_sink sink, void* user) noexcept
    {
        user_.store(user, std::memory_order_release);
        sink_.store(sink, std::memory_order_release);
    }
};

inline log_channel& global_log_channel() noexcept
{
    static log_channel channel{};
    return channel;
}

inline const char* severity_name(error_severity severity) noexcept
{
    switch (severity)
    {
    case error_severity::info:
        return "info";
    case error_severity::warning:
        return "warning";
    case error_severity::error:
        return "error";
    case error_severity::fatal:
        return "fatal";
    }
    return "unknown";
}

inline const char* errc_name(errc code) noexcept
{
    switch (code)
    {
    case errc::ok:
        return "ok";
    case errc::no_such_lane:
        return "no_such_lane";
    case errc::no_such_worker:
        return "no_such_worker";
    case errc::no_such_tag:
        return "no_such_tag";
    case errc::lane_destroyed:
        return "lane_destroyed";
    case errc::inject_queue_full:
        return "inject_queue_full";
    case errc::lane_queue_full:
        return "lane_queue_full";
    case errc::job_slot_exhausted:
        return "job_slot_exhausted";
    case errc::coroutine_frame_exhausted:
        return "coroutine_frame_exhausted";
    case errc::thread_spawn_failed:
        return "thread_spawn_failed";
    case errc::not_running:
        return "not_running";
    case errc::shutdown_in_progress:
        return "shutdown_in_progress";
    case errc::already_registered:
        return "already_registered";
    case errc::invalid_argument:
        return "invalid_argument";
    case errc::timeout:
        return "timeout";
    case errc::invariant_broken:
        return "invariant_broken";
    case errc::handler_faulted:
        return "handler_faulted";
    case errc::cancelled:
        return "cancelled";
    }
    return "unknown";
}

enum class stop_request : std::uint8_t
{
    none = 0,
    abort_frame = 1,
    shutdown = 2,
    terminate_ = 3,
};

struct stop_signal
{
    std::atomic<std::uint8_t> request_{0};
    std::atomic<std::uint32_t> frame_abort_count_{0};

    void raise(error_action action) noexcept
    {
        const std::uint8_t wanted = static_cast<std::uint8_t>(action) + 1u;
        if (wanted == 1u)
        {
            return;
        }
        std::uint8_t current = request_.load(std::memory_order_relaxed);
        while (wanted > current &&
               !request_.compare_exchange_weak(current, wanted, std::memory_order_acq_rel,
                                               std::memory_order_relaxed))
        {
        }
        if (action == error_action::abort_frame)
        {
            frame_abort_count_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    stop_request take() noexcept
    {
        const std::uint8_t value = request_.exchange(0, std::memory_order_acq_rel);
        return static_cast<stop_request>(value == 0 ? 0 : value - 1u);
    }

    stop_request peek() const noexcept
    {
        const std::uint8_t value = request_.load(std::memory_order_acquire);
        return static_cast<stop_request>(value == 0 ? 0 : value - 1u);
    }

    void clear() noexcept
    {
        request_.store(0, std::memory_order_release);
    }
};

inline stop_signal& global_stop_signal() noexcept
{
    static stop_signal signal{};
    return signal;
}

struct handler_registry
{
    std::atomic<error_handler> error_{nullptr};
    std::atomic<fatal_handler> fatal_{nullptr};
    std::atomic<bool> dispatching_{false};

    void set_error(error_handler handler) noexcept
    {
        error_.store(handler, std::memory_order_release);
    }

    void set_fatal(fatal_handler handler) noexcept
    {
        fatal_.store(handler, std::memory_order_release);
    }
};

inline handler_registry& global_handler_registry() noexcept
{
    static handler_registry registry{};
    return registry;
}

inline void set_error_handler(error_handler handler) noexcept
{
    global_handler_registry().set_error(handler);
}

inline void set_fatal_handler(fatal_handler handler) noexcept
{
    global_handler_registry().set_fatal(handler);
}

inline void set_log_sink(log_sink sink, void* user) noexcept
{
    global_log_channel().set(sink, user);
}

[[noreturn]] inline void terminate_now() noexcept
{
    std::fflush(nullptr);
    std::abort();
}

namespace detail
{
inline void emit_fallback_log(const error_info& info) noexcept
{
    log_sink const sink = global_log_channel().sink_.load(std::memory_order_acquire);
    if (sink == nullptr)
    {
        return;
    }
    char buffer[160] = {};
    std::snprintf(buffer, sizeof(buffer),
                  "[framework:%s] errc=%s site=%08x tag=%u lane=%u thread=%u",
                  severity_name(info.severity_), errc_name(info.code_),
                  static_cast<unsigned>(info.site_), static_cast<unsigned>(info.tag_),
                  static_cast<unsigned>(info.lane_id_), static_cast<unsigned>(info.thread_));
    sink(static_cast<log_level>(static_cast<std::uint8_t>(info.severity_)), buffer,
         global_log_channel().user_.load(std::memory_order_acquire));
}

inline error_action report_impl(const error_info& info) noexcept
{
    error_counters& counters = global_error_counters();
    counters.bump(info.severity_, info.tag_);
    emit_fallback_log(info);

    handler_registry& registry = global_handler_registry();

    bool expected = false;
    if (!registry.dispatching_.compare_exchange_strong(expected, true, std::memory_order_acq_rel,
                                                       std::memory_order_relaxed))
    {
        counters.handler_faults_.fetch_add(1, std::memory_order_relaxed);
        const error_info faulted{
            error_severity::fatal, errc::handler_faulted, info.site_,  info.tag_,
            info.flags_,           info.lane_id_,         info.thread_};
        counters.bump(error_severity::fatal, info.tag_);
        emit_fallback_log(faulted);
        terminate_now();
    }

    error_action action = error_action::continue_;

    if (info.severity_ == error_severity::fatal)
    {
        fatal_handler const handler = registry.fatal_.load(std::memory_order_acquire);
        action = (handler != nullptr) ? handler(info) : error_action::terminate_;
        if (action == error_action::continue_)
        {
            counters.ignored_fatal_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    else if (info.severity_ == error_severity::error)
    {
        error_handler const handler = registry.error_.load(std::memory_order_acquire);
        if (handler != nullptr)
        {
            action = handler(info);
        }
    }

    registry.dispatching_.store(false, std::memory_order_release);

    if (action == error_action::abort_frame || action == error_action::shutdown ||
        action == error_action::terminate_)
    {
        global_stop_signal().raise(action);
    }
    if (action == error_action::terminate_)
    {
        terminate_now();
    }
    return action;
}
} // namespace detail

inline error_action report(const error_info& info) noexcept
{
    return detail::report_impl(info);
}

inline error_action report(error_severity severity, errc code, std::uint16_t tag = 0,
                           std::uint32_t lane_id = 0, std::uint16_t flags = 0,
                           std::uint32_t site = 0, std::uint32_t thread = 0) noexcept
{
    error_info info{};
    info.severity_ = severity;
    info.code_ = code;
    info.site_ = site;
    info.tag_ = tag;
    info.flags_ = flags;
    info.lane_id_ = lane_id;
    info.thread_ = thread;
    return detail::report_impl(info);
}

#define FRAMEWORK_REPORT(severity, code)                                                           \
    ::framework::report((severity), (code), 0, 0, 0, FRAMEWORK_SITE())

#define FRAMEWORK_REPORT_TAGGED(severity, code, tag, lane, flags)                                  \
    ::framework::report((severity), (code), (tag), (lane), (flags), FRAMEWORK_SITE())

inline void invariant_check(bool condition, errc code, std::uint32_t site) noexcept
{
    if (!condition)
    {
        (void)report(error_severity::fatal, code, 0, 0, 0, site);
        terminate_now();
    }
}

#define FRAMEWORK_INVARIANT(condition, code)                                                       \
    ::framework::invariant_check((condition), (code), FRAMEWORK_SITE())

inline void reset_error_state() noexcept
{
    global_error_counters().clear();
    handler_registry& registry = global_handler_registry();
    registry.error_.store(nullptr, std::memory_order_release);
    registry.fatal_.store(nullptr, std::memory_order_release);
    registry.dispatching_.store(false, std::memory_order_release);
    global_log_channel().set(nullptr, nullptr);
    global_stop_signal().clear();
    global_stop_signal().frame_abort_count_.store(0, std::memory_order_relaxed);
}
} // namespace framework
