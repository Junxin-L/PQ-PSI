#pragma once

#include "types.h"
#include "../../thirdparty/sneik/sneik_f512.h"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace pi
{
	namespace sneik
	{
		constexpr size_t WidthBits = 512;
		constexpr size_t WidthBytes = WidthBits / 8;
		constexpr u8 Domain = 0;
		constexpr u8 Rounds = 8;

		inline u32 rotr(u32 x, unsigned n)
		{
			return (x >> n) | (x << (32U - n));
		}

		inline u32 linA(u32 x)
		{
			return x ^ rotr(x, 7) ^ rotr(x, 8);
		}

		inline u32 linB(u32 x)
		{
			return x ^ rotr(x, 15) ^ rotr(x, 23);
		}

		inline std::array<u32, 32> invMatrix(u32 (*fn)(u32))
		{
			std::array<u32, 32> rows{};
			for (unsigned col = 0; col < 32; ++col)
			{
				const u32 out = fn(1U << col);
				for (unsigned row = 0; row < 32; ++row)
				{
					if (((out >> row) & 1U) != 0)
					{
						rows[row] |= 1U << col;
					}
				}
			}

			std::array<u32, 32> inv{};
			for (unsigned bit = 0; bit < 32; ++bit)
			{
				auto a = rows;
				std::array<u8, 32> rhs{};
				rhs[bit] = 1;

				for (unsigned col = 0; col < 32; ++col)
				{
					unsigned pivot = col;
					while (pivot < 32 && (((a[pivot] >> col) & 1U) == 0))
					{
						++pivot;
					}
					if (pivot == 32)
					{
						throw std::runtime_error("SNEIK inverse matrix is singular");
					}
					if (pivot != col)
					{
						std::swap(a[pivot], a[col]);
						std::swap(rhs[pivot], rhs[col]);
					}

					for (unsigned row = 0; row < 32; ++row)
					{
						if (row != col && (((a[row] >> col) & 1U) != 0))
						{
							a[row] ^= a[col];
							rhs[row] ^= rhs[col];
						}
					}
				}

				u32 x = 0;
				for (unsigned col = 0; col < 32; ++col)
				{
					if (rhs[col] != 0)
					{
						x |= 1U << col;
					}
				}
				inv[bit] = x;
			}
			return inv;
		}

		inline u32 applyInvSlow(u32 y, const std::array<u32, 32>& inv)
		{
			u32 x = 0;
			for (unsigned bit = 0; bit < 32; ++bit)
			{
				if (((y >> bit) & 1U) != 0)
				{
					x ^= inv[bit];
				}
			}
			return x;
		}

		using InvTable = std::array<std::array<u32, 256>, 4>;

		inline InvTable invTable(const std::array<u32, 32>& inv)
		{
			InvTable table{};
			for (unsigned lane = 0; lane < 4; ++lane)
			{
				for (unsigned byte = 0; byte < 256; ++byte)
				{
					const u32 y = static_cast<u32>(byte) << (8U * lane);
					table[lane][byte] = applyInvSlow(y, inv);
				}
			}
			return table;
		}

		inline u32 applyInv(u32 y, const InvTable& table)
		{
			return table[0][y & 0xFFU]
				^ table[1][(y >> 8) & 0xFFU]
				^ table[2][(y >> 16) & 0xFFU]
				^ table[3][(y >> 24) & 0xFFU];
		}

		inline u32 load32(const u8* p)
		{
			return static_cast<u32>(p[0])
				| (static_cast<u32>(p[1]) << 8)
				| (static_cast<u32>(p[2]) << 16)
				| (static_cast<u32>(p[3]) << 24);
		}

		inline void store32(u8* p, u32 x)
		{
			p[0] = static_cast<u8>(x);
			p[1] = static_cast<u8>(x >> 8);
			p[2] = static_cast<u8>(x >> 16);
			p[3] = static_cast<u8>(x >> 24);
		}

		inline void invert(u8* state, u8 dom = Domain, u8 rounds = Rounds)
		{
			static const std::array<u8, 16> rc = {{
				0xEF, 0xE0, 0xD9, 0xD6, 0xBA, 0xB5, 0x8C, 0x83,
				0x10, 0x1F, 0x26, 0x29, 0x45, 0x4A, 0x73, 0x7C,
			}};
			static const auto invA = invTable(invMatrix(linA));
			static const auto invB = invTable(invMatrix(linB));

			std::array<u32, 16> v{};
			for (size_t i = 0; i < v.size(); ++i)
			{
				v[i] = load32(state + 4 * i);
			}

			for (int round = static_cast<int>(rounds) - 1; round >= 0; --round)
			{
				for (int j = 15; j >= 0; --j)
				{
					u32 x = v[static_cast<size_t>(j)] ^ v[static_cast<size_t>((j + 1) & 0xF)];
					x = applyInv(x, invB);
					x -= v[static_cast<size_t>((j + 2) & 0xF)];
					x ^= v[static_cast<size_t>((j + 14) & 0xF)];
					x = applyInv(x, invA);
					x -= v[static_cast<size_t>((j + 15) & 0xF)];
					v[static_cast<size_t>(j)] = x;
				}

				v[1] ^= static_cast<u32>(dom);
				v[0] ^= static_cast<u32>(rc[static_cast<size_t>(round)]);
			}

			for (size_t i = 0; i < v.size(); ++i)
			{
				store32(state + 4 * i, v[i]);
			}
		}
	}
}
