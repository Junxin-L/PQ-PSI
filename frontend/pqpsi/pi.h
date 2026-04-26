#pragma once

#include "Crypto/PRNG.h"
#include <util.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <vector>

#include "../thirdparty/KeccakTools/Sources/Keccak-f.h"

using namespace osuCrypto;

using Bits = std::vector<uint8_t>;

inline constexpr size_t KEM_key_block_size = 100;
inline constexpr size_t KEM_key_size_bit = 12800;
inline constexpr size_t Keccak_size_bit = 1600;

using kemKey = std::array<block, KEM_key_block_size>;

namespace K1600
{
	inline constexpr size_t kBits = 1600;
	inline constexpr size_t kBytes = kBits / 8;
	using Buf = std::array<UINT8, kBytes>;

	inline void checkBits(const Bits& bits)
	{
		if (bits.size() != kBits)
		{
			throw std::invalid_argument("K1600 bit size mismatch");
		}
	}

	inline Buf pack(const Bits& bits)
	{
		checkBits(bits);

		Buf buf{};
		buf.fill(0);
		for (size_t i = 0; i < kBits; ++i)
		{
			if (bits[i] & 1U)
			{
				buf[i / 8] |= static_cast<UINT8>(1U << (i % 8));
			}
		}
		return buf;
	}

	inline Bits unpack(const Buf& buf)
	{
		Bits bits(kBits, 0);
		for (size_t i = 0; i < kBits; ++i)
		{
			bits[i] = static_cast<uint8_t>((buf[i / 8] >> (i % 8)) & 1U);
		}
		return bits;
	}

	inline const KeccakF& inst()
	{
		static thread_local const KeccakF k(1600);
		return k;
	}

	inline Bits pi(const Bits& bits)
	{
		auto buf = pack(bits);
		inst()(buf.data());
		return unpack(buf);
	}

	inline Bits pi_inv(const Bits& bits)
	{
		auto buf = pack(bits);
		inst().inverse(buf.data());
		return unpack(buf);
	}

	inline void permute(Buf& buf)
	{
		inst()(buf.data());
	}

	inline void inverse(Buf& buf)
	{
		inst().inverse(buf.data());
	}
}

namespace Keccak1600Adapter = K1600;

class Pi
{
public:
	Pi(
		size_t nBits,
		size_t totalBits,
		size_t stepBits,
		size_t lambdaBits = 128)
		: n_(nBits)
		, N_(totalBits)
		, s_(stepBits)
		, lambda_(lambdaBits)
	{
		checkParams();
		t_ = (N_ - n_) / s_ + 1;
		r_ = 5 * t_ - 2;
	}

	Bits encrypt(Bits x) const
	{
		checkState(x);

		size_t off = 0;
		K1600::Buf buf{};

		// round walk
		for (size_t round = 1; round < r_; ++round)
		{
			applyShift(x, off, buf);
			xorRound(x, off, round);
			off = (off + s_) % N_;
		}

		// final pi
		applyShift(x, off, buf);
		materialize(x, off);
		return x;
	}

	Bits decrypt(Bits x) const
	{
		checkState(x);

		size_t off = 0;
		K1600::Buf buf{};

		// first pi inv
		applyInvShift(x, off, buf);
		// reverse walk
		for (size_t round = r_ - 1; round >= 1; --round)
		{
			off = (off + N_ - s_) % N_;
			xorRound(x, off, round);
			applyInvShift(x, off, buf);
			if (round == 1)
			{
				break;
			}
		}

		materialize(x, off);
		return x;
	}

	size_t rounds() const
	{
		return r_;
	}

private:
	size_t n_ = 0;
	size_t N_ = 0;
	size_t s_ = 0;
	size_t t_ = 0;
	size_t r_ = 0;
	size_t lambda_ = 128;

	void checkParams() const
	{
		if (n_ == 0 || N_ == 0 || s_ == 0)
		{
			throw std::invalid_argument("Pi params must be positive");
		}
		if (N_ <= n_)
		{
			throw std::invalid_argument("Pi needs N > n");
		}
		if ((N_ - n_) % s_ != 0)
		{
			throw std::invalid_argument("Pi needs step to divide N - n");
		}
		if (s_ > n_)
		{
			throw std::invalid_argument("Pi needs s <= n");
		}

		const size_t minGap = 3 * lambda_;
		if (!(s_ > minGap && (n_ - s_) > minGap))
		{
			throw std::invalid_argument("Pi lambda check failed");
		}
	}

	void checkState(const Bits& x) const
	{
		if (x.size() != N_)
		{
			throw std::invalid_argument("Pi state size mismatch");
		}
		for (u8 bit : x)
		{
			if (bit != 0 && bit != 1)
			{
				throw std::invalid_argument("Pi state must be binary");
			}
		}
	}

	static void clearBuf(K1600::Buf& buf)
	{
		buf.fill(0);
	}

	static void setBufBit(K1600::Buf& buf, size_t idx, u8 bit)
	{
		if ((bit & 1U) != 0)
		{
			buf[idx / 8] |= static_cast<UINT8>(1U << (idx % 8));
		}
	}

	static u8 getBufBit(const K1600::Buf& buf, size_t idx)
	{
		return static_cast<u8>((buf[idx / 8] >> (idx % 8)) & 1U);
	}

	void packShifted(const Bits& x, size_t off, K1600::Buf& buf) const
	{
		clearBuf(buf);
		for (size_t i = 0; i < n_; ++i)
		{
			const size_t src = off + i;
			const size_t idx = (src < N_) ? src : (src - N_);
			setBufBit(buf, i, x[idx]);
		}
	}

	void unpackShifted(const K1600::Buf& buf, Bits& x, size_t off) const
	{
		for (size_t i = 0; i < n_; ++i)
		{
			const size_t dst = off + i;
			const size_t idx = (dst < N_) ? dst : (dst - N_);
			x[idx] = getBufBit(buf, i);
		}
	}

	void materialize(Bits& x, size_t off) const
	{
		if (off == 0)
		{
			return;
		}
		std::rotate(x.begin(), x.begin() + off, x.end());
	}

	void applyShift(Bits& x, size_t off, K1600::Buf& buf) const
	{
		packShifted(x, off, buf);
		K1600::permute(buf);
		unpackShifted(buf, x, off);
	}

	void applyInvShift(Bits& x, size_t off, K1600::Buf& buf) const
	{
		packShifted(x, off, buf);
		K1600::inverse(buf);
		unpackShifted(buf, x, off);
	}

	void xorRound(Bits& x, size_t off, size_t round) const
	{
		const size_t len = n_ - s_;
		size_t start = off + s_;
		if (start >= N_)
		{
			start -= N_;
		}

		size_t bit = 0;
		size_t v = round;
		while (v != 0 && bit < len)
		{
			if ((v & 1U) != 0)
			{
				const size_t pos = start + bit;
				x[(pos < N_) ? pos : (pos - N_)] ^= 1U;
			}
			v >>= 1U;
			++bit;
		}
	}
};

using ConsPi = Pi;
using ConstructionPermutation = Pi;

inline int piTest()
{
	const size_t n = 1600;
	const size_t N = n * 8;
	const std::array<size_t, 1> sVals{1120};
	std::mt19937_64 rng(0xC001D00DuLL);

	for (size_t s : sVals)
	{
		Pi pi(n, N, s);

		Bits sparse(N, 0);
		sparse[0] = 1;
		sparse[5] = 1;
		sparse[N - 1] = 1;
		if (pi.decrypt(pi.encrypt(sparse)) != sparse)
		{
			return 1;
		}

		for (size_t t = 0; t < 8; ++t)
		{
			Bits x(N, 0);
			for (auto& bit : x)
			{
				bit = static_cast<u8>(rng() & 1ULL);
			}
			if (pi.decrypt(pi.encrypt(x)) != x)
			{
				return 10 + static_cast<int>(t);
			}
		}
	}

	return 0;
}

inline int permutation_Test()
{
	return piTest();
}
