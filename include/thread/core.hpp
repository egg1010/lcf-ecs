#pragma once

// 核心入口：线程池、任务图、协程等待与声明式接口
// 不包含 app / io / blocking / manager_group / timing_wheel

#include "thread/manager.hpp"
#include "thread/profile.hpp"
#include "thread/spawn.hpp"
#include "thread/task_builder.hpp"
#include "thread/task_flow.hpp"

namespace td = thread;
