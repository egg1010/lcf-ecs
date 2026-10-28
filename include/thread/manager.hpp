#pragma once

// manager.hpp - thread 模块统一入口：manager / lane / job / task / 同步 / MPMC

#include "part/time/timing_wheel.hpp"

// base 基础层：错误、平台、同步、任务对象与身份
#include "part/operating_message.hpp"
#include "thread/base/job.hpp"
#include "thread/base/platform.hpp"
#include "thread/base/sync.hpp"
#include "thread/base/task_identity.hpp"

// queue 并发容器层：MPMC 队列与环、工作窃取双端队列
#include "thread/queue/mpmc_queue.hpp"
#include "thread/queue/mpmc_ring.hpp"
#include "thread/queue/work_deque.hpp"

// execution 执行层：运行时上下文、Worker、Lane、协程与优先级栈
#include "thread/execution/coroutine.hpp"
#include "thread/execution/lane.hpp"
#include "thread/execution/runtime.hpp"
#include "thread/execution/scheduler.hpp"
#include "thread/execution/worker.hpp"

// task 任务图层：节点、表、队列、依赖、图、构建与流控
#include "thread/task/task_admission.hpp"
#include "thread/task/task_blocking.hpp"
#include "thread/task/task_builder.hpp"
#include "thread/task/task_context.hpp"
#include "thread/task/task_dependency.hpp"
#include "thread/task/task_flow.hpp"
#include "thread/task/task_graph.hpp"
#include "thread/task/task_io.hpp"
#include "thread/task/task_node.hpp"
#include "thread/task/task_queue.hpp"
#include "thread/task/task_table.hpp"
#include "thread/task/task_trace.hpp"
#include "thread/task/task_watchdog.hpp"

// pool 线程池层：管理器、分组、预设、指标、协程派生与应用骨架
#include "thread/pool/app.hpp"
#include "thread/pool/manager.hpp"
#include "thread/pool/manager_group.hpp"
#include "thread/pool/metrics.hpp"
#include "thread/pool/profile.hpp"
#include "thread/pool/spawn.hpp"

