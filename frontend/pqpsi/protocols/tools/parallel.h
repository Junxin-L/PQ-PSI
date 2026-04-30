#pragma once

#include "base.h"
#include <algorithm>
#include <thread>

namespace tools
{
	inline size_t workerLimit(bool multiThread, size_t requested = 0)
	{
		if (!multiThread)
		{
			return 1;
		}

		const size_t hw = std::max<size_t>(1, std::thread::hardware_concurrency());
		if (requested == 0)
		{
			return hw;
		}
		return std::max<size_t>(1, std::min(requested, hw));
	}

	inline size_t workerCount(size_t n, size_t minWork, bool multiThread, size_t workerThreads = 0)
	{
		if (n == 0 || !multiThread)
		{
			return 1;
		}

		const size_t maxThreads = std::max<size_t>(1, n / std::max<size_t>(1, minWork));
		return std::max<size_t>(1, std::min(workerLimit(multiThread, workerThreads), maxThreads));
	}

	template <typename Fn>
	inline void parallelFor(size_t n, size_t minWork, bool multiThread, size_t workerThreads, Fn&& fn)
	{
		if (n == 0)
		{
			return;
		}

		if (!multiThread)
		{
			fn(0, n);
			return;
		}

		const size_t threadCount = workerCount(n, minWork, multiThread, workerThreads);
		if (threadCount <= 1)
		{
			fn(0, n);
			return;
		}

		const size_t chunk = (n + threadCount - 1) / threadCount;
		std::vector<std::thread> workers;
		workers.reserve(threadCount);

		for (size_t t = 0; t < threadCount; ++t)
		{
			const size_t begin = t * chunk;
			if (begin >= n)
			{
				break;
			}
			const size_t end = std::min(n, begin + chunk);
			workers.emplace_back([&, begin, end]()
			{
				fn(begin, end);
			});
		}

		for (auto& worker : workers)
		{
			worker.join();
		}
	}
}
