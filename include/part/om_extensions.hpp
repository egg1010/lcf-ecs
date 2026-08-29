// om_extensions.hpp - operating_message 高级扩展
// 从 operating_message.hpp 分离: 本文件依赖 dense/ring_buffer/time/fnv1a,
// 基础消息模块保持零容器依赖 (错误码/开关/写入路径)
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <utility>
#include "operating_message.hpp"
#include "dense.hpp"
#include "ring_buffer.hpp"
#include "time.hpp"
#include "fnv1a.hpp"

// === 消息前缀栈 (模块化前缀, RAII 推入/弹出) ===

[[nodiscard]] inline dense<std::string_view>& om_prefix_stack() noexcept
{
    static dense<std::string_view> s;
    return s;
}

struct om_prefix
{
    om_prefix(std::string_view prefix) noexcept
    {
        dense<std::string_view>& s = om_prefix_stack();
        s.increase_capacity(s.size() + 1);
        s.push_back(prefix);
    }
    ~om_prefix() noexcept
    {
        om_prefix_stack().pop_back();
    }
    om_prefix(const om_prefix&) = delete;
    om_prefix& operator=(const om_prefix&) = delete;
};

// 带前缀写入: 依次追加所有前缀后调用 write
template<typename... Args>
void om_write_prefixed(operating_message& om, bool sw, Args&&... args) noexcept
{
    const dense<std::string_view>& s = om_prefix_stack();
    for (size_t i = 0; i < s.size(); ++i)
    {
        om += s[i];
    }
    om.write(sw, std::forward<Args>(args)...);
}

// === P1: 错误码映射表 (code → 可读名/描述) ===

struct om_error_entry
{
    uint32_t code;
    std::string_view name;
    std::string_view description;
};

[[nodiscard]] inline const dense<om_error_entry>& om_error_table() noexcept
{
    static const dense<om_error_entry> table = []() {
        dense<om_error_entry> t;
        t.reserve_exact(16);
        t.push_back({om_err_type_mismatch,    "type_mismatch",    "类型不匹配"});
        t.push_back({om_err_invalid_entity,   "invalid_entity",   "实体无效"});
        t.push_back({om_err_version_mismatch, "version_mismatch", "版本不匹配"});
        t.push_back({om_err_out_of_range,     "out_of_range",     "索引越界"});
        t.push_back({om_err_null_pointer,     "null_pointer",     "空指针"});
        t.push_back({om_err_capacity_exceeded,"capacity_exceeded","容量超限"});
        t.push_back({om_err_not_found,        "not_found",        "未找到"});
        t.push_back({om_err_already_exists,   "already_exists",   "已存在"});
        t.push_back({om_err_out_of_memory,    "out_of_memory",    "内存分配失败"});
        return t;
    }();
    return table;
}

[[nodiscard]] inline std::string_view om_error_name(uint32_t code) noexcept
{
    const dense<om_error_entry>& t = om_error_table();
    for (size_t i = 0; i < t.size(); ++i)
    {
        if (t[i].code == code)
        {
            return t[i].name;
        }
    }
    return "unknown";
}

[[nodiscard]] inline std::string_view om_error_desc(uint32_t code) noexcept
{
    const dense<om_error_entry>& t = om_error_table();
    for (size_t i = 0; i < t.size(); ++i)
    {
        if (t[i].code == code)
        {
            return t[i].description;
        }
    }
    return "未知错误";
}

// === P1: 一次性消息去重 (同 key 仅输出一次, 避免刷屏) ===

[[nodiscard]] inline dense<uint64_t>& om_dedup_set() noexcept
{
    static dense<uint64_t> s;
    return s;
}

// 返回 true 表示首次 (应输出), false 表示重复 (应跳过)
[[nodiscard]] inline bool om_once(std::string_view key) noexcept
{
    uint64_t h = fnv1a_runtime(key.data(), key.size());
    dense<uint64_t>& set = om_dedup_set();
    for (size_t i = 0; i < set.size(); ++i)
    {
        if (set[i] == h)
        {
            return false;
        }
    }
    set.increase_capacity(set.size() + 1);
    set.push_back(h);
    return true;
}

// 清空去重集合 (允许同 key 再次输出)
inline void om_dedup_clear() noexcept
{
    om_dedup_set().clear();
}

// === P1: 消息计数统计 ===

struct om_stats
{
    std::array<uint32_t, 4> count_by_level{};
    std::array<uint32_t, 4> size_by_level{};
    uint32_t success_count{0};
    uint32_t failure_count{0};

    void reset() noexcept
    {
        count_by_level = {};
        size_by_level = {};
        success_count = 0;
        failure_count = 0;
    }
};

[[nodiscard]] inline om_stats& global_om_stats() noexcept
{
    static om_stats s;
    return s;
}

// 记录一条操作结果到全局统计
inline void om_stats_record(const operating_message& om,
                            msg_level lv = msg_level::info) noexcept
{
    om_stats& s = global_om_stats();
    uint8_t idx = static_cast<uint8_t>(lv);
    if (idx < 4)
    {
        ++s.count_by_level[idx];
        s.size_by_level[idx] += static_cast<uint32_t>(om.message_size());
    }
    if (om)
    {
        ++s.success_count;
    }
    else
    {
        ++s.failure_count;
    }
}

// === P1: RAII 作用域计时器 (墙钟时间) ===

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
        om_.write(true, msg::fmt_rt("[{}] 耗时 {:.1f}us", name_, us));
    }
    om_scope_timer(const om_scope_timer&) = delete;
    om_scope_timer& operator=(const om_scope_timer&) = delete;
};

// RAII 作用域计时器 (CPU 周期, 精度更高)
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

// === P2: 消息历史记录 (ring_buffer 存储, 静态池复用) ===

// 历史记录条目: trivially copyable, ring_buffer 快路径零析构
struct om_record
{
    msg_level level;
    uint32_t code;
    uint16_t msg_len;
    char msg_buf[256];
};

using om_history = ring_buffer<om_record, 1024>;

// 推入一条消息到历史 (槽位原地填充, 不经过栈临时对象)
inline void om_history_push(om_history& hist, const operating_message& om,
                            msg_level lv = msg_level::info) noexcept
{
    om_record* rec = hist.emplace_get();
    if (!rec) [[unlikely]] { return; }
    rec->level = lv;
    rec->code = om.code();
    std::string_view sv = om.read_message();
    rec->msg_len = static_cast<uint16_t>(sv.size() < 256 ? sv.size() : 256);
    std::memcpy(rec->msg_buf, sv.data(), rec->msg_len);
}

// 排空历史, 逐条调用 handler(level, code, msg_view)
template<typename Func>
size_t om_history_flush(om_history& hist, Func&& handler) noexcept
{
    return hist.drain([&](const om_record& rec) {
        std::string_view sv(rec.msg_buf, rec.msg_len);
        handler(rec.level, rec.code, sv);
    });
}

// === P2: 消息订阅/回调 (包装器模式, 不修改 operating_message) ===

using om_sink_fn = void (*)(msg_level, uint32_t, std::string_view);

// 消息记录器: 包装 operating_message, 每次 write 后触发回调 + 推入历史
class om_logger
{
    operating_message om_;
    om_history* sink_;
    om_sink_fn callback_;

public:
    om_logger(om_history* sink = nullptr, om_sink_fn cb = nullptr) noexcept
        : sink_(sink), callback_(cb) {}

    operating_message& inner() noexcept { return om_; }

    // 代理 write + level 标签, 写入后触发订阅
    template<typename... Args>
    void write(msg_level lv, bool sw, Args&&... args) noexcept
    {
        om_.write(sw, msg::level_tag{lv}, std::forward<Args>(args)...);
        if (callback_)
        {
            callback_(lv, om_.code(), om_.read_message());
        }
        if (sink_)
        {
            om_history_push(*sink_, om_, lv);
        }
    }

    // 代理 write + level 标签 + 运行时格式串, 写入后触发订阅
    template<typename... Args>
    void write_fmt(msg_level lv, bool sw,
                   std::string_view fmt, Args&&... args) noexcept
    {
        om_.write(sw, msg::level_tag{lv},
                  msg::fmt_rt(fmt, std::forward<Args>(args)...));
        if (callback_)
        {
            callback_(lv, om_.code(), om_.read_message());
        }
        if (sink_)
        {
            om_history_push(*sink_, om_, lv);
        }
    }

    [[nodiscard]] operator bool() const noexcept { return (bool)om_; }
    [[nodiscard]] uint32_t code() const noexcept { return om_.code(); }
    [[nodiscard]] std::string_view read_message() const noexcept { return om_.read_message(); }
    [[nodiscard]] operating_message&& take() noexcept { return std::move(om_); }
};

// === P2: 错误恢复策略表 ===

using om_recovery_fn = bool (*)(const operating_message&);

struct om_recovery_entry
{
    uint32_t code;
    om_recovery_fn fn;
};

[[nodiscard]] inline dense<om_recovery_entry>& om_recovery_table() noexcept
{
    static dense<om_recovery_entry> t;
    return t;
}

inline void om_register_recovery(uint32_t code, om_recovery_fn fn) noexcept
{
    dense<om_recovery_entry>& t = om_recovery_table();
    t.increase_capacity(t.size() + 1);
    t.push_back({code, fn});
}

// 尝试恢复: 查表调用匹配 code 的恢复函数, 返回是否恢复成功
[[nodiscard]] inline bool om_try_recover(const operating_message& err) noexcept
{
    dense<om_recovery_entry>& t = om_recovery_table();
    for (size_t i = 0; i < t.size(); ++i)
    {
        if (err.is_code(t[i].code))
        {
            return t[i].fn(err);
        }
    }
    return false;
}
