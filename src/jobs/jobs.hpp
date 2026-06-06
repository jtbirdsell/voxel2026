// Job-system facade (issue #20, ADR-0012): the ONLY surface engine code
// sees — enkiTS lives behind this pimpl and its headers never escape
// src/jobs/ (that boundary is ADR-0012's exit strategy). The surface grows
// strictly with demonstrated need; today that is the thread count and a
// blocking parallel-for, which is what pool migrations consume.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace jobs {

class JobSystem {
public:
	// threads == 0: one worker per hardware thread (the scheduler counts the
	// calling thread as one of them, the enkiTS model).
	explicit JobSystem(std::uint32_t threads = 0);
	~JobSystem();

	JobSystem(const JobSystem &) = delete;
	JobSystem &operator=(const JobSystem &) = delete;

	std::uint32_t threadCount() const;

	// Blocking parallel-for over [0, count): fn(i) is called exactly once
	// per index, from worker threads and/or the calling thread, in no
	// defined order; returns when every index completed. fn must be safe to
	// invoke concurrently with itself on distinct indices. grain = indices
	// per task (amortizes scheduling; 1 = maximal spread).
	void parallelFor(std::size_t count, std::size_t grain,
			const std::function<void(std::size_t)> &fn);

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};

} // namespace jobs
