#include "jobs/jobs.hpp"

#include <atomic>
#include <thread>

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

	// The facade OWNS the completion-visibility contract: "parallelFor
	// returns happens-after every fn invocation" is established by this
	// release/acquire pair, not trusted from the scheduler. (TSan finding
	// on the first CI run: enkiTS v1.11's WaitforTask return path carries
	// no sanitizer-visible edge from worker execution to the waiter, so
	// caller writes after return raced — really or apparently — with
	// worker reads inside fn. Either way, the dependency's internals are
	// not where our memory-ordering guarantee should live.)
	std::atomic<std::size_t> completed{0};
	enki::TaskSet task(static_cast<std::uint32_t>(count),
			[&fn, &completed](enki::TaskSetPartition range, std::uint32_t) {
				for (std::uint32_t i = range.start; i < range.end; ++i)
					fn(i);
				completed.fetch_add(range.end - range.start,
						std::memory_order_release);
			});
	task.m_MinRange = static_cast<std::uint32_t>(grain);
	m_impl->scheduler.AddTaskSetToPipe(&task);
	m_impl->scheduler.WaitforTask(&task);
	// In practice satisfied on the first load (the scheduler did wait);
	// the loop is the defensive form in case the dependency's edge is
	// genuinely absent rather than merely invisible.
	while (completed.load(std::memory_order_acquire) != count)
		std::this_thread::yield();
}

} // namespace jobs
