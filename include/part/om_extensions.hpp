#pragma once

// om_extensions.hpp - 错误码目录 / 首错工具 / 用户自持的诊断辅助
// 无全局状态: 目录为编译期常量, 历史与日志由用户持有实例

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <utility>
#include "operating_message.hpp"
#include "ring_buffer.hpp"
#include "time/timer.hpp"

struct om_error_entry
{
    uint32_t code;
    std::string_view name;
    std::string_view description;
};

inline constexpr om_error_entry om_error_catalog[] = {
    {om_err_type_mismatch.code,     "type_mismatch",     "类型不匹配"},
    {om_err_invalid_entity.code,    "invalid_entity",    "实体无效"},
    {om_err_version_mismatch.code,  "version_mismatch",  "版本不匹配"},
    {om_err_out_of_range.code,      "out_of_range",      "索引越界"},
    {om_err_null_pointer.code,      "null_pointer",      "空指针"},
    {om_err_capacity_exceeded.code, "capacity_exceeded", "容量超限"},
    {om_err_not_found.code,         "not_found",         "未找到"},
    {om_err_already_exists.code,    "already_exists",    "已存在"},
    {om_err_out_of_memory.code,     "out_of_memory",     "内存分配失败"},
    {om_err_invalid_argument.code,  "invalid_argument",  "参数非法"},
    {om_err_unsupported_operation.code, "unsupported_operation", "当前模式不支持"},
    {om_err_cancelled.code,         "cancelled",         "已取消"},
    {om_err_timeout.code,           "timeout",           "等待超时"},
    {om_err_not_running.code,       "not_running",       "目标未启动"},
    {om_err_shutting_down.code,     "shutting_down",     "正在关闭"},
    {om_err_queue_full.code,        "queue_full",        "队列已满"},
    {om_err_decode_failed.code,     "decode_failed",     "解码失败"},
    {om_err_io_failed.code,         "io_failed",         "IO 失败"},
    {om_err_checksum_failed.code,   "checksum_failed",   "校验失败"},
    {om_err_spawn_failed.code,      "spawn_failed",      "执行体创建失败"},
    {om_err_internal_invariant.code, "internal_invariant", "内部不变量破坏"},
    {om_err_task_body_faulted.code, "thread_task_body_faulted", "任务体执行异常"},
};

inline constexpr size_t om_error_catalog_count =
    sizeof(om_error_catalog) / sizeof(om_error_catalog[0]);

[[nodiscard]] constexpr bool om_catalog_codes_unique() noexcept
{
    for (size_t i = 0; i < om_error_catalog_count; ++i)
    {
        for (size_t j = i + 1; j < om_error_catalog_count; ++j)
        {
            if (om_error_catalog[i].code == om_error_catalog[j].code)
            {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] constexpr bool om_catalog_names_unique() noexcept
{
    for (size_t i = 0; i < om_error_catalog_count; ++i)
    {
        for (size_t j = i + 1; j < om_error_catalog_count; ++j)
        {
            if (om_error_catalog[i].name == om_error_catalog[j].name)
            {
                return false;
            }
        }
    }
    return true;
}

static_assert(om_catalog_codes_unique(), "duplicate error code in catalog");
static_assert(om_catalog_names_unique(), "duplicate error name in catalog");

[[nodiscard]] constexpr std::string_view om_error_name(uint32_t code) noexcept
{
    for (const om_error_entry& entry : om_error_catalog)
    {
        if (entry.code == code)
        {
            return entry.name;
        }
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view om_error_desc(uint32_t code) noexcept
{
    for (const om_error_entry& entry : om_error_catalog)
    {
        if (entry.code == code)
        {
            return entry.description;
        }
    }
    return "未知错误";
}

// === 首错工具 (仅失败路径调用) ===

[[nodiscard]] inline bool om_write_first_failure(
    operating_message* dst,
    bool failed,
    msg_level level,
    msg::code_tag code,
    std::source_location site,
    std::string_view text = {}) noexcept
{
    if (!failed || dst == nullptr || !static_cast<bool>(*dst))
    {
        return false;
    }
    if (text.empty())
    {
        dst->write_failure(code.code, level, site);
    }
    else
    {
        dst->write_failure_text(code.code, level, site, text);
    }
    return true;
}

[[nodiscard]] inline bool om_merge_first_failure(
    operating_message& dst,
    const operating_message& src) noexcept
{
    if (static_cast<bool>(dst) && !static_cast<bool>(src))
    {
        dst = src;
        return true;
    }
    return false;
}

// === 作用域计时 (用户自持错误对象, 需 formatted 模式) ===

struct om_scope_timer
{
    operating_message& om_;
    const char* name_;
    timer timer_;

    om_scope_timer(operating_message& om, const char* name) noexcept
        : om_(om), name_(name), timer_() {}

    ~om_scope_timer() noexcept
    {
        double us = timer_.elapsed_microseconds();
        om_.write(true, msg::fmt_rt("[{}] {:.1f}us", name_, us));
    }
    om_scope_timer(const om_scope_timer&) = delete;
    om_scope_timer& operator=(const om_scope_timer&) = delete;
};

struct om_scope_cycles
{
    operating_message& om_;
    const char* name_;
    timer timer_;

    om_scope_cycles(operating_message& om, const char* name) noexcept
        : om_(om), name_(name), timer_() {}

    ~om_scope_cycles() noexcept
    {
        uint64_t cyc = timer_.elapsed_cycles();
        om_.write(true, msg::fmt_rt("[{}] {} cycles", name_, cyc));
    }
    om_scope_cycles(const om_scope_cycles&) = delete;
    om_scope_cycles& operator=(const om_scope_cycles&) = delete;
};

// === 用户自持的环形历史 ===

struct om_record
{
    msg_level level;
    uint32_t code;
    uint16_t msg_len;
    char msg_buf[256];
};

using om_history = ring_buffer<om_record, 1024>;

inline void om_history_push(om_history& hist, const operating_message& om,
                            msg_level lv = msg_level::info) noexcept
{
    om_record* rec = hist.emplace_overwrite();
    rec->level = lv;
    rec->code = om.code();
    std::string_view sv = om.text();
    rec->msg_len = static_cast<uint16_t>(sv.size() < 256 ? sv.size() : 256);
    std::memcpy(rec->msg_buf, sv.data(), rec->msg_len);
}

template<typename Func>
size_t om_history_visit(const om_history& hist, Func&& handler) noexcept
{
    return hist.visit([&](const om_record& rec) {
        std::string_view sv(rec.msg_buf, rec.msg_len);
        handler(rec.level, rec.code, sv);
    });
}

template<typename Func>
size_t om_history_flush(om_history& hist, Func&& handler) noexcept
{
    size_t total = 0;
    om_record* run = nullptr;
    size_t len = 0;
    while (hist.front_run(run, len) > 0)
    {
        for (size_t i = 0; i < len; ++i)
        {
            std::string_view sv(run[i].msg_buf, run[i].msg_len);
            handler(run[i].level, run[i].code, sv);
        }
        total += hist.pop_run(len);
    }
    return total;
}

[[nodiscard]] inline uint64_t om_history_dropped(const om_history& hist) noexcept
{
    return hist.dropped_count();
}

// === 用户自持的消息记录器 ===

using om_sink_fn = void (*)(msg_level, uint32_t, std::string_view);

class om_logger
{
    operating_message om_;
    om_history* sink_;
    om_sink_fn callback_;

    void publish_(msg_level lv) noexcept
    {
        if (callback_)
        {
            callback_(lv, om_.code(), om_.text());
        }
        if (sink_)
        {
            om_history_push(*sink_, om_, lv);
        }
    }

public:
    om_logger(om_history* sink = nullptr, om_sink_fn cb = nullptr) noexcept
        : sink_(sink), callback_(cb)
    {
        om_.set_text_mode(om_text_mode::formatted);
    }

    operating_message& inner() noexcept { return om_; }

    template<typename... Args>
    void write(msg_level lv, bool sw, Args&&... args) noexcept
    {
        om_.clear_message();
        om_.write(sw, msg::level_tag{lv}, std::forward<Args>(args)...);
        publish_(lv);
    }

    template<typename... Args>
    void write_fmt(msg_level lv, bool sw,
                   std::string_view fmt, Args&&... args) noexcept
    {
        om_.clear_message();
        om_.write(sw, msg::level_tag{lv},
                  msg::fmt_rt(fmt, std::forward<Args>(args)...));
        publish_(lv);
    }

    [[nodiscard]] operator bool() const noexcept { return static_cast<bool>(om_); }
    [[nodiscard]] uint32_t code() const noexcept { return om_.code(); }
    [[nodiscard]] std::string_view text() const noexcept { return om_.text(); }
    [[nodiscard]] operating_message take() noexcept { return om_; }
};
