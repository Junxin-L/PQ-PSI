#pragma once

#include "api.h"
#include "params.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <random>
#include <stdexcept>

class ConsPi final : public pqperm::Perm
{
public:
	explicit ConsPi(pi::Params params, u8 party = 0)
		: params_(params)
		, small_(pi::makePerm(params.kind))
		, party_(static_cast<u8>(party & 1U))
	{
		checkParams();
		t_ = (params_.N - params_.n) / params_.s + 1;
		r_ = 5 * t_ - 2;
		roundBits_ = bitsNeeded(r_ - 1);
	}

	ConsPi(pi::Kind kind, size_t totalBits = KEM_key_size_bit, size_t lambdaBits = 128, u8 party = 0)
		: ConsPi(pi::defaults(kind, totalBits, lambdaBits), party)
	{
	}

	ConsPi(size_t nBits, size_t totalBits, size_t stepBits, size_t lambdaBits = 128, u8 party = 0)
		: ConsPi(legacyParams(nBits, totalBits, stepBits, lambdaBits), party)
	{
	}

	Bits encrypt(Bits x) const
	{
		checkState(x);
		auto bytes = bitsToBytes(x);
		encryptBytes(bytes.data(), bytes.size());
		bytesToBits(bytes, x);
		return x;
	}

	Bits decrypt(Bits x) const
	{
		checkState(x);
		auto bytes = bitsToBytes(x);
		decryptBytes(bytes.data(), bytes.size());
		bytesToBits(bytes, x);
		return x;
	}

	void encryptBytes(u8* data, size_t bytes) const override
	{
		checkBytes(bytes);

		size_t off = 0;
		pi::Perm::Buf buf(small_->bytes(), 0);

		for (size_t round = 1; round < r_; ++round)
		{
			applyShift(data, off, buf);
			xorRound(data, off, round);
			off = (off + params_.s) % params_.N;
		}

		applyShift(data, off, buf);
		materialize(data, bytes, off);
	}

	void decryptBytes(u8* data, size_t bytes) const override
	{
		checkBytes(bytes);

		size_t off = 0;
		pi::Perm::Buf buf(small_->bytes(), 0);

		applyInvShift(data, off, buf);
		for (size_t round = r_ - 1; round >= 1; --round)
		{
			off = (off + params_.N - params_.s) % params_.N;
			xorRound(data, off, round);
			applyInvShift(data, off, buf);
			if (round == 1)
			{
				break;
			}
		}

		materialize(data, bytes, off);
	}

	const pi::Params& params() const
	{
		return params_;
	}

	const char* name() const override
	{
		return "conspi";
	}

	const char* detail() const override
	{
		return small_->label();
	}

	pi::Kind kind() const
	{
		return params_.kind;
	}

	const char* permName() const
	{
		return small_->label();
	}

	size_t n() const override
	{
		return params_.n;
	}

	size_t s() const override
	{
		return params_.s;
	}

	size_t rounds() const override
	{
		return r_;
	}

private:
	pi::Params params_{};
	std::unique_ptr<pi::Perm> small_;
	u8 party_ = 0;
	size_t t_ = 0;
	size_t r_ = 0;
	size_t roundBits_ = 0;

	static pi::Params legacyParams(
		size_t nBits,
		size_t totalBits,
		size_t stepBits,
		size_t lambdaBits)
	{
		pi::Params p;
		p.kind = (nBits == 800) ? pi::Kind::Keccak800 : pi::Kind::Keccak1600;
		p.n = nBits;
		p.N = totalBits;
		p.s = stepBits;
		p.lambda = lambdaBits;
		return p;
	}

	void checkParams() const
	{
		if (params_.n == 0 || params_.N == 0 || params_.s == 0)
		{
			throw std::invalid_argument("Pi params must be positive");
		}
		if (params_.n != small_->bits())
		{
			throw std::invalid_argument("Pi n must match the small permutation width");
		}
		if (params_.N <= params_.n)
		{
			throw std::invalid_argument("Pi needs N > n");
		}
		if ((params_.N - params_.n) % params_.s != 0)
		{
			throw std::invalid_argument("Pi needs step to divide N - n");
		}
		if (params_.s > params_.n)
		{
			throw std::invalid_argument("Pi needs s <= n");
		}

		const size_t minGap = 2 * params_.lambda;
		if (!(params_.s >= minGap && (params_.n - params_.s) >= minGap))
		{
			throw std::invalid_argument("Pi lambda check failed");
		}
	}

	static size_t bitsNeeded(size_t x)
	{
		size_t bits = 0;
		do
		{
			++bits;
			x >>= 1U;
		} while (x != 0);
		return bits;
	}

	void checkState(const Bits& x) const
	{
		if (x.size() != params_.N)
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

	void checkBytes(size_t bytes) const
	{
		if (bytes * 8 != params_.N)
		{
			throw std::invalid_argument("Pi byte state size mismatch");
		}
	}

	static u8 getByteBit(const u8* data, size_t idx)
	{
		return static_cast<u8>((data[idx / 8] >> (idx % 8)) & 1U);
	}

	static void setByteBit(u8* data, size_t idx, u8 bit)
	{
		const u8 mask = static_cast<u8>(1U << (idx % 8));
		if ((bit & 1U) != 0)
		{
			data[idx / 8] |= mask;
		}
		else
		{
			data[idx / 8] &= static_cast<u8>(~mask);
		}
	}

	static std::vector<u8> bitsToBytes(const Bits& bits)
	{
		std::vector<u8> bytes((bits.size() + 7) / 8, 0);
		for (size_t i = 0; i < bits.size(); ++i)
		{
			if ((bits[i] & 1U) != 0)
			{
				bytes[i / 8] |= static_cast<u8>(1U << (i % 8));
			}
		}
		return bytes;
	}

	static void bytesToBits(const std::vector<u8>& bytes, Bits& bits)
	{
		for (size_t i = 0; i < bits.size(); ++i)
		{
			bits[i] = getByteBit(bytes.data(), i);
		}
	}

	static void setBufBit(pi::Perm::Buf& buf, size_t idx, u8 bit)
	{
		if ((bit & 1U) != 0)
		{
			buf[idx / 8] |= static_cast<UINT8>(1U << (idx % 8));
		}
	}

	static u8 getBufBit(const pi::Perm::Buf& buf, size_t idx)
	{
		return static_cast<u8>((buf[idx / 8] >> (idx % 8)) & 1U);
	}

	void packShifted(const u8* data, size_t off, pi::Perm::Buf& buf) const
	{
		const size_t nBytes = params_.n / 8;
		const size_t totalBytes = params_.N / 8;
		if ((off % 8) == 0)
		{
			const size_t start = off / 8;
			const size_t first = std::min(nBytes, totalBytes - start);
			std::memcpy(buf.data(), data + start, first);
			if (first < nBytes)
			{
				std::memcpy(buf.data() + first, data, nBytes - first);
			}
			return;
		}

		std::fill(buf.begin(), buf.end(), 0);
		for (size_t i = 0; i < params_.n; ++i)
		{
			const size_t src = off + i;
			const size_t idx = (src < params_.N) ? src : (src - params_.N);
			setBufBit(buf, i, getByteBit(data, idx));
		}
	}

	void unpackShifted(const pi::Perm::Buf& buf, u8* data, size_t off) const
	{
		const size_t nBytes = params_.n / 8;
		const size_t totalBytes = params_.N / 8;
		if ((off % 8) == 0)
		{
			const size_t start = off / 8;
			const size_t first = std::min(nBytes, totalBytes - start);
			std::memcpy(data + start, buf.data(), first);
			if (first < nBytes)
			{
				std::memcpy(data, buf.data() + first, nBytes - first);
			}
			return;
		}

		for (size_t i = 0; i < params_.n; ++i)
		{
			const size_t dst = off + i;
			const size_t idx = (dst < params_.N) ? dst : (dst - params_.N);
			setByteBit(data, idx, getBufBit(buf, i));
		}
	}

	void materialize(u8* data, size_t bytes, size_t off) const
	{
		if (off == 0)
		{
			return;
		}

		if ((off % 8) == 0)
		{
			std::rotate(data, data + (off / 8), data + bytes);
			return;
		}

		std::vector<u8> tmp(bytes, 0);
		for (size_t i = 0; i < params_.N; ++i)
		{
			const size_t src = i + off;
			setByteBit(tmp.data(), i, getByteBit(data, (src < params_.N) ? src : (src - params_.N)));
		}
		std::memcpy(data, tmp.data(), bytes);
	}

	void applyShift(u8* data, size_t off, pi::Perm::Buf& buf) const
	{
		packShifted(data, off, buf);
		small_->apply(buf);
		unpackShifted(buf, data, off);
	}

	void applyInvShift(u8* data, size_t off, pi::Perm::Buf& buf) const
	{
		packShifted(data, off, buf);
		small_->invert(buf);
		unpackShifted(buf, data, off);
	}

	void xorRound(u8* data, size_t off, size_t round) const
	{
		const size_t len = params_.n - params_.s;
		size_t start = off + params_.s;
		if (start >= params_.N)
		{
			start -= params_.N;
		}

		size_t bit = 0;
		size_t v = round;
		while (v != 0 && bit < len)
		{
			if ((v & 1U) != 0)
			{
				const size_t pos = start + bit;
				const size_t idx = (pos < params_.N) ? pos : (pos - params_.N);
				data[idx / 8] ^= static_cast<u8>(1U << (idx % 8));
			}
			v >>= 1U;
			++bit;
		}

		if (party_ != 0 && roundBits_ < len)
		{
			const size_t pos = start + roundBits_;
			const size_t idx = (pos < params_.N) ? pos : (pos - params_.N);
			data[idx / 8] ^= static_cast<u8>(1U << (idx % 8));
		}
	}
};

using Pi = ConsPi;
using ConstructionPermutation = ConsPi;

inline int piTest()
{
	const std::array<pi::Kind, 2> kinds{pi::Kind::Keccak800, pi::Kind::Keccak1600};
	std::mt19937_64 rng(0xC001D00DuLL);

	for (auto kind : kinds)
	{
		ConsPi pi(kind, KEM_key_size_bit, 128, 0);
		ConsPi peer(kind, KEM_key_size_bit, 128, 1);
		const size_t N = pi.params().N;

		Bits sparse(N, 0);
		sparse[0] = 1;
		sparse[5] = 1;
		sparse[N - 1] = 1;
		if (pi.decrypt(pi.encrypt(sparse)) != sparse)
		{
			return 1;
		}
		if (peer.decrypt(peer.encrypt(sparse)) != sparse)
		{
			return 2;
		}
		if (pi.encrypt(sparse) == peer.encrypt(sparse))
		{
			return 3;
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
			if (peer.decrypt(peer.encrypt(x)) != x)
			{
				return 20 + static_cast<int>(t);
			}
		}
	}

	return 0;
}

inline int permutation_Test()
{
	return piTest();
}
