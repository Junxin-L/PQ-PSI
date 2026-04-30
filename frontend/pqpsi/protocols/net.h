#pragma once

#include "party.h"

#include <algorithm>
#include <exception>
#include <future>
#include <thread>
#include <utility>
#include <vector>

namespace pqpsi_proto
{
	namespace net
	{
		struct PendingRecv
		{
			std::vector<std::future<void>> jobs;

			void wait()
			{
				for (auto& job : jobs)
				{
					job.get();
				}
			}
		};

		inline std::vector<std::pair<u64, u64>> chunks(u64 bytes, size_t n)
		{
			std::vector<std::pair<u64, u64>> out;
			if (bytes == 0 || n == 0)
			{
				return out;
			}

			constexpr u64 blockBytes = sizeof(osuCrypto::block);
			if (bytes % blockBytes == 0)
			{
				const u64 blocks = bytes / blockBytes;
				const u64 use = std::min<u64>(static_cast<u64>(n), blocks);
				const u64 per = (blocks + use - 1) / use;
				for (u64 i = 0; i < use; ++i)
				{
					const u64 begin = i * per;
					const u64 end = std::min(blocks, begin + per);
					if (begin < end)
					{
						out.push_back({ begin * blockBytes, (end - begin) * blockBytes });
					}
				}
				return out;
			}

			const u64 use = std::min<u64>(static_cast<u64>(n), bytes);
			const u64 per = (bytes + use - 1) / use;
			for (u64 i = 0; i < use; ++i)
			{
				const u64 begin = i * per;
				const u64 end = std::min(bytes, begin + per);
				if (begin < end)
				{
					out.push_back({ begin, end - begin });
				}
			}
			return out;
		}

		template <typename Fn>
		inline void eachChunk(size_t n, Fn fn)
		{
			if (n == 0)
			{
				return;
			}

			if (n == 1)
			{
				fn(0);
				return;
			}

			std::vector<std::thread> th;
			std::vector<std::exception_ptr> err(n);
			th.reserve(n - 1);

			for (size_t i = 1; i < n; ++i)
			{
				th.emplace_back([&, i]()
				{
					try
					{
						fn(i);
					}
					catch (...)
					{
						err[i] = std::current_exception();
					}
				});
			}

			try
			{
				fn(0);
			}
			catch (...)
			{
				err[0] = std::current_exception();
			}

			for (auto& t : th)
			{
				t.join();
			}
			for (auto& e : err)
			{
				if (e)
				{
					std::rethrow_exception(e);
				}
			}
		}

		inline bool shouldSplit(const Ctx& ctx, u64 peer, u64 bytes)
		{
			constexpr u64 minParallelBytes = 16 * 1024;
			return ctx.multiThread &&
				peer < ctx.chls.size() &&
				ctx.chls[peer].size() > 1 &&
				bytes >= minParallelBytes;
		}

		inline void send(Ctx& ctx, u64 peer, void* data, u64 bytes)
		{
			if (!shouldSplit(ctx, peer, bytes))
			{
				ctx.chls[peer][0]->send(data, bytes);
				return;
			}

			u8* base = static_cast<u8*>(data);
			const auto parts = chunks(bytes, ctx.chls[peer].size());
			eachChunk(parts.size(), [&](size_t i)
			{
				ctx.chls[peer][i]->send(base + parts[i].first, parts[i].second);
			});
		}

		inline void recv(Ctx& ctx, u64 peer, void* data, u64 bytes)
		{
			if (!shouldSplit(ctx, peer, bytes))
			{
				ctx.chls[peer][0]->recv(data, bytes);
				return;
			}

			u8* base = static_cast<u8*>(data);
			const auto parts = chunks(bytes, ctx.chls[peer].size());
			eachChunk(parts.size(), [&](size_t i)
			{
				ctx.chls[peer][i]->recv(base + parts[i].first, parts[i].second);
			});
		}

		inline PendingRecv asyncRecv(Ctx& ctx, u64 peer, void* data, u64 bytes)
		{
			PendingRecv pending;
			if (!shouldSplit(ctx, peer, bytes))
			{
				pending.jobs.emplace_back(ctx.chls[peer][0]->asyncRecv(data, bytes));
				return pending;
			}

			u8* base = static_cast<u8*>(data);
			const auto parts = chunks(bytes, ctx.chls[peer].size());
			pending.jobs.reserve(parts.size());
			for (size_t i = 0; i < parts.size(); ++i)
			{
				pending.jobs.emplace_back(
					ctx.chls[peer][i]->asyncRecv(base + parts[i].first, parts[i].second));
			}
			return pending;
		}
	}
}
