#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>

#include "thread/task_graph.hpp"

namespace thread
{

struct watchdog_config
{
    std::uint32_t period_ms_{100};
    std::uint64_t default_deadline_ns_{0};
    std::uint64_t hang_grace_ns_{1'000'000'000ull};
    std::uint64_t heartbeat_timeout_ns_{0};
    std::uint64_t queue_timeout_ns_{0};
};

struct watchdog_metrics
{
    std::atomic<std::uint64_t> scans_{0};
    std::atomic<std::uint64_t> deadline_misses_{0};
    std::atomic<std::uint64_t> hung_tasks_{0};
    std::atomic<std::uint64_t> heartbeat_misses_{0};
    std::atomic<std::uint64_t> queue_timeouts_{0};
    std::atomic<std::uint64_t> timed_wakes_{0};
    std::atomic<std::uint64_t> max_wait_ns_{0};
    std::atomic<std::uint64_t> max_run_ns_{0};
    std::atomic<std::uint64_t> heartbeat_age_ns_{0};
};

inline void watchdog_note_max(std::atomic<std::uint64_t>& target, std::uint64_t value) noexcept
{
    std::uint64_t current = target.load(std::memory_order_relaxed);
    while (value > current &&
           !target.compare_exchange_weak(current, value, std::memory_order_relaxed,
                                         std::memory_order_relaxed))
    {
    }
}

// 周期扫描：deadline / 心跳 / 排队超时 / 定时等待唤醒；不抢占、不强杀
struct task_watchdog
{
    task_graph* graph_{nullptr};
    watchdog_config config_{};
    watchdog_metrics metrics_{};
    std::jthread thread_{};
    std::atomic<bool> running_{false};

    task_watchdog() noexcept = default;

    task_watchdog(const task_watchdog&) = delete;
    task_watchdog& operator=(const task_watchdog&) = delete;

    ~task_watchdog()
    {
        stop();
    }

    bool start(task_graph& graph, const watchdog_config& config) noexcept
    {
        if (running_.load(std::memory_order_acquire) || !graph.created())
        {
            return false;
        }
        graph_ = &graph;
        config_ = config;
        if (config_.period_ms_ == 0)
        {
            config_.period_ms_ = 100;
        }
        if (config_.default_deadline_ns_ != 0)
        {
            graph_->set_default_deadline(config_.default_deadline_ns_);
        }
        running_.store(true, std::memory_order_release);
        thread_ = std::jthread([this](std::stop_token token) { run_loop(token); });
        return true;
    }

    void stop() noexcept
    {
        if (!running_.exchange(false, std::memory_order_acq_rel))
        {
            if (thread_.joinable())
            {
                thread_.request_stop();
                thread_.join();
            }
            return;
        }
        thread_.request_stop();
        if (thread_.joinable())
        {
            thread_.join();
        }
    }

    bool running() const noexcept
    {
        return running_.load(std::memory_order_acquire);
    }

    std::size_t scan(std::uint64_t now_ns) noexcept
    {
        if (graph_ == nullptr || !graph_->created())
        {
            return 0;
        }
        const std::uint32_t count = static_cast<std::uint32_t>(graph_->task_count());
        std::size_t actions = 0;
        const std::uint64_t grace = config_.hang_grace_ns_;

        for (std::uint32_t index = 0; index < count; ++index)
        {
            const task_id id{index};
            const task_state state = graph_->state_of(id);
            if (state == task_state::running)
            {
                const std::uint64_t start = graph_->start_ns_of(id);
                if (start == 0)
                {
                    continue;
                }
                const std::uint64_t deadline = graph_->deadline_of(id);
                const std::uint64_t heartbeat = graph_->heartbeat_of(id);
                const std::uint64_t last = heartbeat != 0 ? heartbeat : start;
                const std::uint64_t run_ns = now_ns > start ? now_ns - start : 0;
                watchdog_note_max(metrics_.max_run_ns_, run_ns);

                if (deadline != 0 && run_ns >= deadline)
                {
                    if (!graph_->cancelled(id))
                    {
                        (void)graph_->cancel(id);
                        graph_->raise(error_severity::error, errc::task_timeout, id,
                                      "task deadline exceeded");
                        metrics_.deadline_misses_.fetch_add(1, std::memory_order_relaxed);
                        ++actions;
                    }
                    else if (grace != 0 && run_ns >= deadline + grace && graph_->mark_hung(id))
                    {
                        graph_->raise(error_severity::error, errc::task_hung, id,
                                      "task hung after deadline");
                        metrics_.hung_tasks_.fetch_add(1, std::memory_order_relaxed);
                        ++actions;
                    }
                }
                else if (config_.heartbeat_timeout_ns_ != 0 && last != 0 &&
                         now_ns > last && now_ns - last > config_.heartbeat_timeout_ns_)
                {
                    watchdog_note_max(metrics_.heartbeat_age_ns_, now_ns - last);
                    if (!graph_->cancelled(id))
                    {
                        (void)graph_->cancel(id);
                        graph_->raise(error_severity::warning, errc::task_timeout, id,
                                      "task heartbeat timeout");
                        metrics_.heartbeat_misses_.fetch_add(1, std::memory_order_relaxed);
                        ++actions;
                    }
                }
                continue;
            }

            if (state != task_state::ready && state != task_state::waiting &&
                state != task_state::dormant)
            {
                continue;
            }
            const std::uint64_t since = graph_->wait_since_of(id);
            if (since == 0)
            {
                continue;
            }
            const std::uint64_t wait_ns = now_ns > since ? now_ns - since : 0;
            watchdog_note_max(metrics_.max_wait_ns_, wait_ns);
            if (config_.queue_timeout_ns_ != 0 && wait_ns > config_.queue_timeout_ns_ &&
                !graph_->cancelled(id))
            {
                (void)graph_->cancel(id);
                graph_->raise(error_severity::warning, errc::task_timeout, id, "task queue timeout");
                metrics_.queue_timeouts_.fetch_add(1, std::memory_order_relaxed);
                ++actions;
            }
        }

        const std::size_t timed = graph_->wake_timed(now_ns);
        if (timed != 0)
        {
            metrics_.timed_wakes_.fetch_add(timed, std::memory_order_relaxed);
            actions += timed;
        }
        metrics_.scans_.fetch_add(1, std::memory_order_relaxed);
        return actions;
    }

  private:
    void run_loop(std::stop_token token) noexcept
    {
        const auto period = std::chrono::milliseconds(config_.period_ms_);
        while (!token.stop_requested())
        {
            (void)scan(monotonic_now_ns());
            std::this_thread::sleep_for(period);
        }
        running_.store(false, std::memory_order_release);
    }
};
} // namespace thread
