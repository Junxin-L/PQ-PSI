#pragma once

#include "Common/Defines.h"

#include <array>
#include <cstddef>
#include <stdexcept>

namespace pqperm::xoodoo_inv
{
	inline void check(size_t bytes)
	{
		if (bytes != 48)
		{
			throw std::invalid_argument("xoodoo expects exactly 48 bytes");
		}
	}

	inline u32 rot(u32 x, int n)
	{
		n &= 31;
		return n == 0 ? x : static_cast<u32>((x << n) | (x >> (32 - n)));
	}

	inline u32 rotr(u32 x, int n)
	{
		return rot(x, -n);
	}

	inline size_t xm(int x)
	{
		return static_cast<size_t>((x % 4 + 4) % 4);
	}

	inline void theta(std::array<u32, 12>& a)
	{
		std::array<u32, 4> parity{};
		for (size_t x = 0; x < parity.size(); ++x)
		{
			parity[x] = a[x] ^ a[4 + x] ^ a[8 + x];
		}

		std::array<u32, 4> effect = parity;
		for (size_t p = 1; p <= 31; p <<= 1)
		{
			std::array<u32, 4> prev = effect;
			for (int x = 0; x < 4; ++x)
			{
				const u32 v = prev[xm(x - static_cast<int>(p))];
				effect[static_cast<size_t>(x)] = prev[static_cast<size_t>(x)] ^
					rot(v, static_cast<int>(5 * p)) ^
					rot(v, static_cast<int>(14 * p));
			}
		}

		for (size_t x = 0; x < effect.size(); ++x)
		{
			effect[x] ^= parity[x];
			a[x] ^= effect[x];
			a[4 + x] ^= effect[x];
			a[8 + x] ^= effect[x];
		}
	}

	inline void rhoWest(std::array<u32, 12>& a)
	{
		for (size_t x = 0; x < 4; ++x)
		{
			a[8 + x] = rotr(a[8 + x], 11);
		}

		const u32 a10 = a[4];
		a[4] = a[5];
		a[5] = a[6];
		a[6] = a[7];
		a[7] = a10;
	}

	inline void rhoEast(std::array<u32, 12>& a)
	{
		for (size_t x = 0; x < 4; ++x)
		{
			a[4 + x] = rotr(a[4 + x], 1);
		}

		const u32 a20 = rotr(a[10], 8);
		const u32 a21 = rotr(a[11], 8);
		const u32 a22 = rotr(a[8], 8);
		const u32 a23 = rotr(a[9], 8);
		a[8] = a20;
		a[9] = a21;
		a[10] = a22;
		a[11] = a23;
	}

	inline void chi(std::array<u32, 12>& a)
	{
		for (size_t x = 0; x < 4; ++x)
		{
			u32 a0 = a[x];
			u32 a1 = a[4 + x];
			u32 a2 = a[8 + x];
			a0 ^= ~a1 & a2;
			a1 ^= ~a2 & a0;
			a2 ^= ~a0 & a1;
			a[x] = a0;
			a[4 + x] = a1;
			a[8 + x] = a2;
		}
	}

	inline void run(std::array<u32, 12>& a)
	{
		static constexpr std::array<u32, 12> rc{ {
			0x00000012, 0x000001A0, 0x000000F0, 0x00000380,
			0x0000002C, 0x00000060, 0x00000014, 0x00000120,
			0x000000D0, 0x000003C0, 0x00000038, 0x00000058
		} };

		for (u32 c : rc)
		{
			rhoEast(a);
			chi(a);
			a[0] ^= c;
			rhoWest(a);
			theta(a);
		}
	}
}
