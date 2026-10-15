#pragma once

#include <cstdint>

#include "thread/coroutine.hpp"
#include "thread/job.hpp"
#include "thread/manager.hpp"
#include "thread/runtime.hpp"

namespace framework
{

inline bool spawn(manager& mgr, task<void>&& work,
                  std::uint8_t priority = static_cast<std::uint8_t>(priority::normal),
                  std::int32_t preferred_worker = -1) noexcept
{
    if (!work.valid())
    {
        return false;
    }

    runtime_context* const rt = mgr.runtime();
    std::uint32_t slot = slot_index_none;
    job* target = rt->jobs_.allocate(slot);
    if (target == nullptr)
    {
        return false;
    }
    target->set_slot_backed(true);
    reset_job_metadata(target);
    target->priority_ = priority;

    resume_payload payload{work.release()};
    fill_inline_job(target, [payload]() mutable noexcept
    {
        run_resume_payload(static_cast<void*>(&payload));
    });

    if (mgr.submit(target, preferred_worker) == slot_index_none)
    {
        release_job(target);
        if (payload.handle_ != nullptr)
        {
            payload.handle_.destroy();
        }
        return false;
    }
    return true;
}
} // namespace framework
