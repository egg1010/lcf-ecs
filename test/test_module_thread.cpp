// test_module_thread.cpp - 模块功能测试: thread 多线程
#include "test_common.hpp"
#include "include/thread/thread_all.hpp"

#include <atomic>
#include <cstdint>
#include <string>

namespace
{
std::atomic<int> g_wakes{0};

void on_wake(thread::waiter_node*) noexcept
{
    g_wakes.fetch_add(1);
}

std::atomic<int> g_coro_done{0};

thread::task<void> coro_increment()
{
    g_coro_done.fetch_add(1);
    co_return;
}

thread::task<void> coro_yield_then_increment(std::atomic<int>* out)
{
    co_await thread::co_yield_now();
    out->fetch_add(1);
    co_return;
}

thread::task<int> coro_answer()
{
    co_return 42;
}
} // namespace

int main()
{
    t_begin("test_module_thread — 模块功能测试: thread");

    t_section(1, "并发队列 mpmc_queue");
    {
        thread::mpmc_queue q;
        t_check("创建队列", q.create(8));
        t_check("初始为空", q.empty());

        t_check("入队 1", q.try_push(1));
        t_check("入队 2", q.try_push(2));
        t_check("size == 2", q.size() == 2);

        std::uint32_t v = thread::mpmc_index_none;
        t_check("出队顺序 1", q.try_pop(v) && v == 1);
        t_check("出队顺序 2", q.try_pop(v) && v == 2);
        t_check("出队后为空", !q.try_pop(v));
        t_check("拒绝空索引入队", !q.try_push(thread::mpmc_index_none));
    }

    t_section(2, "工作窃取队列 work_deque");
    {
        thread::work_deque d;
        t_check("创建双端队列", d.create(4));

        thread::job a{};
        thread::job b{};
        t_check("底端入队 a", d.push(&a));
        t_check("底端入队 b", d.push(&b));
        t_check("size == 2", d.size() == 2);
        t_check("底端出队为 b", d.pop() == &b);

        thread::job* stolen = nullptr;
        const std::size_t got = d.steal(&stolen, 1);
        t_check("顶端窃取 1 个", got == 1 && stolen == &a);
        t_check("窃取后为空", d.empty());
    }

    t_section(3, "同步原语");
    {
        thread::counter c;
        c.init(2);
        t_check("计数器初始值", c.value() == 2 && !c.ready());
        t_check("减一未归零", c.complete_one() == false && c.value() == 1);
        t_check("减一后归零", c.complete_one() == true && c.ready());

        g_wakes.store(0);
        thread::counter wc;
        wc.init(1);
        thread::waiter_node node;
        thread::waiter_node_init(&node, &on_wake);
        t_check("attach_waiter 需挂起", wc.attach_waiter(&node));
        wc.complete_one();
        t_check("归零唤醒等待者", g_wakes.load() == 1);

        thread::event ev;
        ev.init();
        t_check("事件初始未触发", !ev.triggered());
        ev.trigger();
        t_check("触发后已触发", ev.triggered());

        thread::fence f;
        f.init(2);
        const std::uint32_t gen = f.generation();
        f.arrive();
        t_check("到达 1 次不换代", f.generation() == gen);
        f.arrive();
        t_check("到达 2 次换代", f.generation() == gen + 1);

        thread::spin_latch latch;
        latch.reset();
        t_check("闩初始未释放", !latch.ready());
        latch.release();
        t_check("释放后 ready", latch.ready());
        t_check("已释放等待立即返回", latch.wait(10));
    }

    t_section(4, "任务槽池与优先级");
    {
        thread::job_pool pool;
        t_check("创建槽池", pool.create(4));
        t_check("槽池容量", pool.capacity() == 4 && pool.available() == 4);

        std::uint32_t idx = thread::slot_index_none;
        thread::job* target = pool.allocate(idx);
        t_check("分配任务槽", target != nullptr && idx != thread::slot_index_none);
        t_check("分配后剩余 3", pool.available() == 3);
        t_check("槽与任务互查", pool.slot_of(idx) == target && pool.index_of(target) == idx);

        std::atomic<int> ran{0};
        thread::fill_inline_job(target, [&ran]() noexcept { ran.fetch_add(1); });
        thread::run_job(target);
        t_check("任务体执行", ran.load() == 1);
        thread::destroy_job(target);
        pool.release(idx);
        t_check("归还后剩余 4", pool.available() == 4);

        thread::priority_stack ps;
        t_check("创建优先级栈", ps.create(3, 4));
        thread::job low{};
        thread::job high{};
        low.priority_ = static_cast<std::uint8_t>(thread::priority::background);
        high.priority_ = static_cast<std::uint8_t>(thread::priority::critical);
        ps.push(&low);
        ps.push(&high);
        t_check("高优先级先出", ps.pop() == &high);
        t_check("低优先级后出", ps.pop() == &low);
    }

    t_section(5, "协程任务 task");
    {
        thread::task<int> t = coro_answer();
        t.start();
        t_check("task<int> 完成", t.done());
        t_check("task<int> 取值", t.value() == 42);

        std::atomic<int> out{0};
        thread::task<void> tv = coro_yield_then_increment(&out);
        tv.start();
        t_check("非 worker 让出为直通并完成", tv.done() && out.load() == 1);

        std::atomic<bool> flag{false};
        thread::cancel_token token(&flag);
        t_check("取消前不取消", !token.cancelled());
        flag.store(true);
        t_check("取消后已取消", token.cancelled() && token.poll());
    }

    t_section(6, "线程池 manager 端到端");
    {
        thread::manager_config config = thread::profile::server(2);
        config.job_slot_capacity = 512;
        config.frame_pool_blocks = 64;

        thread::manager mgr;
        t_check("配置管理器", mgr.configure(config));
        t_check("工作线程数 2", mgr.worker_count() == 2);
        mgr.start();
        t_check("已启动", mgr.running());

        std::atomic<int> done_jobs{0};
        int submitted = 0;
        for (int i = 0; i < 128; ++i)
        {
            std::uint32_t slot = thread::slot_index_none;
            thread::job* j = mgr.take_job(slot);
            if (j == nullptr)
            {
                break;
            }
            thread::fill_inline_job(j, [&done_jobs]() noexcept { done_jobs.fetch_add(1); });
            if (mgr.submit(j) != thread::slot_index_none)
            {
                ++submitted;
            }
            else
            {
                mgr.release_job_local(j);
            }
        }
        mgr.drain();
        t_check("提交任务数匹配", submitted == 128);
        t_check("任务全部完成", done_jobs.load() == 128);
        t_check("completed_count 匹配", mgr.completed_count() == 128);

        std::atomic<int> lane_done{0};
        thread::lane* l = mgr.create_lane(thread::lane_config::exclusive(64));
        t_check("创建通道", l != nullptr);
        int lane_submitted = 0;
        for (int i = 0; i < 32; ++i)
        {
            std::uint32_t slot = thread::slot_index_none;
            thread::job* j = mgr.take_job(slot);
            if (j == nullptr)
            {
                break;
            }
            thread::fill_inline_job(j, [&lane_done]() noexcept { lane_done.fetch_add(1); });
            if (mgr.submit_to_lane(l, j) != thread::slot_index_none)
            {
                ++lane_submitted;
            }
            else
            {
                mgr.release_job_local(j);
            }
        }
        mgr.drain();
        t_check("通道任务全部执行", lane_done.load() == lane_submitted);
        t_check("通道统计 executed", l->stats().executed_.load() ==
                                         static_cast<std::uint64_t>(lane_submitted));

        g_coro_done.store(0);
        t_check("提交协程", thread::spawn(mgr, coro_increment()));
        mgr.drain();
        t_check("协程已执行", g_coro_done.load() == 1);

        thread::metrics_snapshot snap = thread::collect_metrics(mgr);
        t_check("指标 worker_count == 2", snap.worker_count == 2);
        t_check("指标 completed 匹配", snap.manager.completed == mgr.completed_count());
        t_check("指标 executed 匹配", snap.total_executed == mgr.completed_count());
        t_check("指标 lane_count == 1", snap.lane_count == 1);

        mgr.stop();
        t_check("已停止", !mgr.running());
        t_check("关闭阶段为 stopped", mgr.phase() == thread::shutdown_phase::stopped);
    }

    t_section(7, "错误与日志");
    {
        thread::reset_error_state();
        t_check("errc 名称查询",
                std::string(thread::errc_name(thread::errc::job_slot_exhausted)) ==
                    "job_slot_exhausted");
        t_check("severity 名称查询",
                std::string(thread::severity_name(thread::error_severity::fatal)) == "fatal");

        (void)thread::report(thread::error_severity::warning, thread::errc::timeout);
        t_check("错误计数自增", thread::global_error_counters().total_.load() == 1);
    }

    t_section(8, "平台工具");
    {
        t_check("连接尾零计数", thread::count_trailing_zeros(8) == 3);
        t_check("位混合确定性", thread::mix_bits(1) == thread::mix_bits(1));
        thread::fast_rand rng(12345);
        t_check("随机数产生非零", rng.next() != 0);
        t_check("平台停放可用", thread::platform_check_required());
    }

    return t_end("test_module_thread");
}
