#pragma once

// 通用固定容量环形缓冲区, 堆分配, 容量 N 须为 2 的幂
// 不做元素零初始化, 调用方仅在 [read_, write_) 区间读取

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>

template <typename T, size_t N>
class ring_buffer
{
    static_assert((N & (N - 1)) == 0, "ring_buffer capacity must be power of 2");
    static_assert(N >= 2, "ring_buffer capacity must be >= 2");

    std::unique_ptr<T[]> buf_;
    uint32_t write_{0};
    uint32_t read_{0};

public:
    // nothrow new: 内存不足返回 nullptr (极端边界, 假设不发生)
    // new T[N] 默认初始化: 对 trivial 类型不 memset
    ring_buffer() noexcept : buf_(new (std::nothrow) T[N]) {}

    ring_buffer(ring_buffer&&) noexcept = default;
    ring_buffer& operator=(ring_buffer&&) noexcept = default;
    ring_buffer(const ring_buffer&) = delete;
    ring_buffer& operator=(const ring_buffer&) = delete;

    [[nodiscard]] bool push(const T& event) noexcept
    {
        uint32_t next = (write_ + 1) & static_cast<uint32_t>(N - 1);
        if (next == read_) [[unlikely]] { return false; }
        buf_[write_] = event;
        write_ = next;
        return true;
    }

    [[nodiscard]] bool push(T&& event) noexcept
    {
        uint32_t next = (write_ + 1) & static_cast<uint32_t>(N - 1);
        if (next == read_) [[unlikely]] { return false; }
        buf_[write_] = std::move(event);
        write_ = next;
        return true;
    }

    template <typename... Args>
    [[nodiscard]] bool emplace(Args&&... args) noexcept
    {
        uint32_t next = (write_ + 1) & static_cast<uint32_t>(N - 1);
        if (next == read_) [[unlikely]] { return false; }
        buf_[write_] = T(std::forward<Args>(args)...);
        write_ = next;
        return true;
    }

    template <typename Func>
    size_t drain(Func&& handler) noexcept
    {
        size_t count = 0;
        while (read_ != write_)
        {
            handler(buf_[read_]);
            read_ = (read_ + 1) & static_cast<uint32_t>(N - 1);
            ++count;
        }
        return count;
    }

    // 带预算的 drain, 防止 handler 内追加导致无限循环
    template <typename Func>
    size_t drain_with_budget(size_t budget, Func&& handler) noexcept
    {
        size_t count = 0;
        while (count < budget && read_ != write_)
        {
            handler(buf_[read_]);
            read_ = (read_ + 1) & static_cast<uint32_t>(N - 1);
            ++count;
        }
        return count;
    }

    // 仅读队首 (不推进), 空返回 nullptr
    [[nodiscard]] const T* peek() const noexcept
    {
        if (read_ == write_) { return nullptr; }
        return &buf_[read_];
    }

    [[nodiscard]] bool pop() noexcept
    {
        if (read_ == write_) { return false; }
        read_ = (read_ + 1) & static_cast<uint32_t>(N - 1);
        return true;
    }

    [[nodiscard]] bool empty() const noexcept { return read_ == write_; }
    [[nodiscard]] bool has_pending() const noexcept { return read_ != write_; }

    [[nodiscard]] size_t pending_count() const noexcept
    {
        return (write_ - read_) & static_cast<uint32_t>(N - 1);
    }

    [[nodiscard]] static constexpr size_t capacity() noexcept { return N; }

    void clear() noexcept { read_ = write_ = 0; }
};
