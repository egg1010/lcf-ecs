#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "part/dense.hpp"
#include "thread/queue/mpmc_queue.hpp"
#include "thread/base/platform.hpp"
#include "thread/execution/worker.hpp"

namespace thread
{

// 工作项路由：只依赖 worker 邮箱与可执行槽位表，不持有生命周期
struct job_router
{
    mpmc_queue* inject_{nullptr};
    dense<worker*>* workers_{nullptr};
    dense<std::uint32_t>* job_workers_{nullptr};
    dense<std::uint8_t>* job_capable_{nullptr};
    std::atomic<std::uint64_t> ticket_{0};

    void bind(mpmc_queue& inject, dense<worker*>& workers,
              dense<std::uint32_t>& job_workers, dense<std::uint8_t>& job_capable) noexcept
    {
        inject_ = &inject;
        workers_ = &workers;
        job_workers_ = &job_workers;
        job_capable_ = &job_capable;
        ticket_.store(0, std::memory_order_relaxed);
    }

    void reset() noexcept
    {
        inject_ = nullptr;
        workers_ = nullptr;
        job_workers_ = nullptr;
        job_capable_ = nullptr;
    }

    std::size_t capable_count() const noexcept
    {
        return job_workers_ != nullptr ? job_workers_->size() : 0;
    }

    bool capable(std::size_t index) const noexcept
    {
        return job_capable_ != nullptr && index < job_capable_->size() &&
               (*job_capable_)[index] != 0;
    }

    bool route(std::uint32_t index, std::int32_t preferred_worker,
               std::size_t& out_target) noexcept
    {
        if (workers_ == nullptr || inject_ == nullptr)
        {
            return false;
        }
        const std::size_t worker_count = workers_->size();
        if (worker_count == 0)
        {
            out_target = 0;
            return inject_->try_push(index);
        }
        const std::size_t count = capable_count();
        if (count == 0)
        {
            // 没有可执行工作项的槽位：退回全局注入队列
            out_target = 0;
            return inject_->try_push(index);
        }
        if (preferred_worker >= 0 && static_cast<std::size_t>(preferred_worker) < worker_count &&
            capable(static_cast<std::size_t>(preferred_worker)))
        {
            out_target = static_cast<std::size_t>(preferred_worker);
            return (*workers_)[out_target]->mailbox().try_push(index);
        }

        fast_rand rng{mix_bits(ticket_.fetch_add(1, std::memory_order_relaxed) + 1u)};
        const std::size_t a = (*job_workers_)[rng.next() % count];
        const std::size_t b = (*job_workers_)[rng.next() % count];
        const std::size_t first =
            (*workers_)[a]->mailbox().size() <= (*workers_)[b]->mailbox().size() ? a : b;
        const std::size_t second = (first == a) ? b : a;

        out_target = first;
        if ((*workers_)[first]->mailbox().try_push(index))
        {
            return true;
        }
        if (second != first)
        {
            out_target = second;
            if ((*workers_)[second]->mailbox().try_push(index))
            {
                return true;
            }
        }
        // 受激退化：顺序探测其余可执行槽位
        for (std::size_t k = 0; k < count; ++k)
        {
            const std::size_t candidate = (*job_workers_)[(k + 1u) % count];
            if (candidate == first || candidate == second)
            {
                continue;
            }
            out_target = candidate;
            if ((*workers_)[candidate]->mailbox().try_push(index))
            {
                return true;
            }
        }
        return false;
    }
};

} // namespace thread
