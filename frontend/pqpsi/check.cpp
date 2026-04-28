#include "pqpsi.h"

#include "Crypto/PRNG.h"
#include "Common/Defines.h"
#include <atomic>
#include <cstring>
#include <limits>
#include <thread>

using namespace osuCrypto;

namespace
{
	bool sameBlock(const block& a, const block& b)
	{
		return std::memcmp(&a, &b, sizeof(block)) == 0;
	}

	bool hasBlock(const std::vector<block>& set, const block& x)
	{
		for (const auto& v : set)
		{
			if (sameBlock(v, x))
			{
				return true;
			}
		}
		return false;
	}

	block freshBlock(PRNG& prng, const std::vector<block>& forbidA, const std::vector<block>& forbidB)
	{
		for (;;)
		{
			block x = prng.get<block>();
			if (!hasBlock(forbidA, x) && !hasBlock(forbidB, x))
			{
				return x;
			}
		}
	}

	void makePairSets(u64 n, u64 hitTarget, std::vector<block>& set0, std::vector<block>& set1)
	{
		PRNG prng(_mm_set_epi32(4253465, 3434565, 234435, 23987045));
		set0.resize(n);
		set1.resize(n);
		for (u64 i = 0; i < n; ++i)
		{
			set1[i] = prng.get<block>();
			set0[i] = set1[i];
		}

		const u64 misses = (hitTarget > n) ? 0 : (n - hitTarget);
		for (u64 i = 0; i < misses; ++i)
		{
			set0[i] = freshBlock(prng, set0, set1);
		}
	}
}

void psiMain()
{
	u64 n = 1 << 2;
	PRNG prng(_mm_set_epi32(4253465, 3434565, 234435, 23987045));
	std::vector<block> set(n);
	for (u64 i = 0; i < n; ++i)
	{
		set[i] = prng.get<block>();
	}

	std::vector<std::thread> ths(2);
	for (u64 i = 0; i < ths.size(); ++i)
	{
		ths[i] = std::thread([&, i]()
		{
			pqpsi(i, n, set, nullptr);
		});
	}

	for (auto& th : ths)
	{
		th.join();
	}
}

void rbMain()
{
	u64 got = 0;
	u64 want = 0;
	(void)rbCheck(got, want);
}

bool rbCheck(u64& got, u64& want, const RbCfg* rb)
{
	return rbRun(1 << 2, got, want, rb);
}

bool rbRun(
	u64 n,
	u64& got,
	u64& want,
	const RbCfg* rb,
	PqPsiRunProfile* out,
	u64 hitTarget,
	const PiCfg* pi)
{
	if (n == 0)
	{
		got = 0;
		want = 0;
		return true;
	}

	if (hitTarget == std::numeric_limits<u64>::max())
	{
		hitTarget = n - 1;
	}
	if (hitTarget > n)
	{
		hitTarget = n;
	}

	want = hitTarget;
	got = 0;

	std::vector<block> set0;
	std::vector<block> set1;
	makePairSets(n, hitTarget, set0, set1);

	std::atomic<u64> hits{ 0 };
	PqPsiStageMs ms0{};
	PqPsiStageMs ms1{};
	std::vector<std::thread> ths(2);
	for (u64 i = 0; i < ths.size(); ++i)
	{
		ths[i] = std::thread([&, i]()
		{
			if (i == 0)
			{
				u64 localHits = 0;
				pqpsi(i, n, set0, rb, &localHits, &ms0, pi);
				hits.store(localHits, std::memory_order_relaxed);
			}
			else
			{
				pqpsi(i, n, set1, rb, nullptr, &ms1, pi);
			}
		});
	}

	for (auto& th : ths)
	{
		th.join();
	}

	got = hits.load(std::memory_order_relaxed);
	if (out)
	{
		out->party0 = ms0;
		out->party1 = ms1;
	}
	return got == want;
}
