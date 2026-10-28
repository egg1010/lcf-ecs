#pragma once

#include <atomic>
#include <cstdint>
#include <new>

#include "part/operating_message.hpp"

namespace thread
{

enum class overload_policy : std::uint8_t
{
    reject = 0,
    drop_oldest = 1,
    coalesce = 2,
    degrade = 3,
};

struct admission_config
{
    std::uint32_t max_inflight_{0};
    std::uint32_t max_queue_{0};
    std::uint32_t tenant_slots_{1};
    overload_policy policy_{overload_policy::reject};
};

struct admission_metrics
{
    std::atomic<std::uint64_t> admitted_{0};
    std::atomic<std::uint64_t> rejected_{0};
    std::atomic<std::uint64_t> degraded_{0};
    std::atomic<std::uint64_t> dropped_{0};
    std::atomic<std::uint64_t> coalesced_{0};
    std::atomic<std::uint64_t> completed_{0};
};

// 每 manager 一个准入控制器：固定租户槽 + 原子计数，不引入锁
struct admission_controller
{
    struct tenant_slot
    {
        std::atomic<std::uint32_t> inflight_{0};
        std::atomic<std::uint32_t> queued_{0};
    };

    tenant_slot* tenants_{nullptr};
    std::uint32_t tenant_count_{0};
    admission_config config_{};
    admission_metrics metrics_{};
    std::atomic<std::uint32_t> total_inflight_{0};

    admission_controller() noexcept = default;

    admission_controller(const admission_controller&) = delete;
    admission_controller& operator=(const admission_controller&) = delete;

    ~admission_controller()
    {
        destroy();
    }

    bool create(const admission_config& config) noexcept
    {
        if (tenants_ != nullptr)
        {
            return false;
        }
        config_ = config;
        tenant_count_ = config.tenant_slots_ == 0 ? 1u : config.tenant_slots_;
        void* memory = ::operator new(sizeof(tenant_slot) * tenant_count_, std::align_val_t(64),
                                      std::nothrow);
        if (memory == nullptr)
        {
            return false;
        }
        tenants_ = static_cast<tenant_slot*>(memory);
        for (std::uint32_t i = 0; i < tenant_count_; ++i)
        {
            new (&tenants_[i]) tenant_slot{};
        }
        return true;
    }

    void destroy() noexcept
    {
        if (tenants_ == nullptr)
        {
            return;
        }
        for (std::uint32_t i = 0; i < tenant_count_; ++i)
        {
            tenants_[i].~tenant_slot();
        }
        ::operator delete(static_cast<void*>(tenants_), std::align_val_t(64));
        tenants_ = nullptr;
        tenant_count_ = 0;
    }

    bool created() const noexcept
    {
        return tenants_ != nullptr;
    }

    std::uint32_t total_inflight() const noexcept
    {
        return total_inflight_.load(std::memory_order_acquire);
    }

    bool try_admit(std::uint32_t tenant) noexcept
    {
        if (tenants_ == nullptr)
        {
            return true;
        }
        const std::uint32_t index = tenant % tenant_count_;
        const std::uint32_t inflight =
            total_inflight_.load(std::memory_order_acquire);
        if (config_.max_inflight_ != 0 && inflight >= config_.max_inflight_)
        {
            switch (config_.policy_)
            {
            case overload_policy::reject:
                metrics_.rejected_.fetch_add(1, std::memory_order_relaxed);
                return false;
            case overload_policy::drop_oldest:
                metrics_.dropped_.fetch_add(1, std::memory_order_relaxed);
                return false;
            case overload_policy::coalesce:
                metrics_.coalesced_.fetch_add(1, std::memory_order_relaxed);
                return false;
            case overload_policy::degrade:
                metrics_.degraded_.fetch_add(1, std::memory_order_relaxed);
                break;
            }
        }
        tenants_[index].inflight_.fetch_add(1, std::memory_order_acq_rel);
        total_inflight_.fetch_add(1, std::memory_order_acq_rel);
        metrics_.admitted_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    void on_complete(std::uint32_t tenant) noexcept
    {
        if (tenants_ == nullptr)
        {
            return;
        }
        const std::uint32_t index = tenant % tenant_count_;
        if (tenants_[index].inflight_.fetch_sub(1, std::memory_order_acq_rel) == 0)
        {
            tenants_[index].inflight_.store(0, std::memory_order_relaxed);
        }
        if (total_inflight_.fetch_sub(1, std::memory_order_acq_rel) == 0)
        {
            total_inflight_.store(0, std::memory_order_relaxed);
        }
        metrics_.completed_.fetch_add(1, std::memory_order_relaxed);
    }
};
} // namespace thread
