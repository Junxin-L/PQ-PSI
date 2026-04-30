#pragma once

#include "small.h"

#include <stdexcept>

namespace pi
{
	struct Params
	{
		Kind kind = Kind::Keccak1600;
		size_t n = 1600;
		size_t N = KEM_key_size_bit;
		size_t s = 1120;
		size_t lambda = 128;
	};

	inline size_t widthOf(Kind kind)
	{
		switch (kind)
		{
		case Kind::Keccak800:
			return 800;
		case Kind::Keccak1600:
		case Kind::Keccak1600R12:
			return 1600;
		case Kind::SneikF512:
			return 512;
		default:
			throw std::invalid_argument("unknown pi width");
		}
	}

	inline size_t bestStep(size_t n, size_t N, size_t lambda)
	{
		if (N <= n)
		{
			throw std::invalid_argument("Pi needs N > n");
		}

		const size_t gap = N - n;
		const size_t minGap = 2 * lambda;
		size_t best = 0;
		for (size_t s = 1; s <= n; ++s)
		{
			if ((gap % s) != 0)
			{
				continue;
			}
			if (s >= minGap && (n - s) >= minGap)
			{
				best = s;
			}
		}

		if (best == 0)
		{
			throw std::invalid_argument("no valid Pi step for this small permutation");
		}
		return best;
	}

	inline Params defaults(Kind kind, size_t N = KEM_key_size_bit, size_t lambda = 128)
	{
		Params p;
		p.kind = kind;
		p.n = widthOf(kind);
		p.N = N;
		p.lambda = lambda;
		p.s = bestStep(p.n, p.N, p.lambda);
		return p;
	}
}
