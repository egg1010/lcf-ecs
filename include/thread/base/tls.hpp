#pragma once

namespace thread
{

struct task_context;
struct worker;
struct runtime_context;
struct job_pool;

// 线程执行上下文：任务、工作线程、运行时与工作项池的唯一入口
struct tls_state
{
    task_context* task_{nullptr};
    worker* worker_{nullptr};
    runtime_context* runtime_{nullptr};
    job_pool* job_pool_{nullptr};
};

inline tls_state& tls() noexcept
{
    thread_local tls_state state{};
    return state;
}

} // namespace thread
