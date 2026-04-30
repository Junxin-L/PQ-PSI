#pragma once

#include <algorithm>
#include <cstddef>
#include <exception>
#include <functional>
#include <thread>
#include <vector>

namespace osuCrypto
{
    inline size_t RBHwThreads()
    {
        return std::max<size_t>(1, std::thread::hardware_concurrency());
    }

    inline size_t RBWorkerThreads(bool multiThread, size_t requested = 0)
    {
        if (!multiThread)
        {
            return 1;
        }

        const size_t hw = RBHwThreads();
        if (requested == 0)
        {
            return hw;
        }
        return std::max<size_t>(1, std::min(requested, hw));
    }

    template<typename Fn>
    inline void RBFor(size_t n, bool multiThread, size_t workerThreads, Fn fn)
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

        const size_t thrN = std::min(RBWorkerThreads(multiThread, workerThreads), n);
        if (thrN <= 1 || n < 64)
        {
            fn(0, n);
            return;
        }

        std::vector<std::thread> thr;
        thr.reserve(thrN);
        std::exception_ptr err;
        const size_t chunk = (n + thrN - 1) / thrN;

        for (size_t t = 0; t < thrN; ++t)
        {
            const size_t begin = std::min(n, t * chunk);
            const size_t end = std::min(n, begin + chunk);
            if (begin >= end)
            {
                break;
            }

            thr.emplace_back([&, begin, end]()
            {
                try
                {
                    fn(begin, end);
                }
                catch (...)
                {
                    if (!err)
                    {
                        err = std::current_exception();
                    }
                }
            });
        }

        for (auto& t : thr)
        {
            t.join();
        }

        if (err)
        {
            std::rethrow_exception(err);
        }
    }
}
