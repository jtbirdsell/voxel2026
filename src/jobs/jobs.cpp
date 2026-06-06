#include "jobs/jobs.hpp"

#include <TaskScheduler.h>

namespace jobs {

struct JobSystem::Impl {
	enki::TaskScheduler scheduler;
};

JobSystem::JobSystem(std::uint32_t threads) : m_impl(std::make_unique<Impl>())
{
	if (threads == 0)
		m_impl->scheduler.Initialize(); // one per hardware thread
	else
		m_impl->scheduler.Initialize(threads);
}

JobSystem::~JobSystem() = default;

std::uint32_t JobSystem::threadCount() const
{
	return m_impl->scheduler.GetNumTaskThreads();
}

void JobSystem::parallelFor(std::size_t count, std::size_t grain,
		const std::function<void(std::size_t)> &fn)
{
	if (count == 0)
		return;
	if (grain == 0)
		grain = 1;

	// enkiTS ranges are uint32; pool migrations operate on chunk batches
	// orders of magnitude below that. Guarded, not assumed.
	if (count > 0xFFFFFFFFull || grain > 0xFFFFFFFFull) {
		for (std::size_t i = 0; i < count; ++i)
			fn(i);
		return;
	}

	enki::TaskSet task(static_cast<std::uint32_t>(count),
			[&fn](enki::TaskSetPartition range, std::uint32_t) {
				for (std::uint32_t i = range.start; i < range.end; ++i)
					fn(i);
			});
	task.m_MinRange = static_cast<std::uint32_t>(grain);
	m_impl->scheduler.AddTaskSetToPipe(&task);
	m_impl->scheduler.WaitforTask(&task);
}

} // namespace jobs
