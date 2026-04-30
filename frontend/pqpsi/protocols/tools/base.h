#pragma once

#include "../../pqpsi.h"
#include "Common/Defines.h"
#include "kem/obf-mlkem/backend/MlKem.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace tools
{
	using namespace osuCrypto;

	constexpr MlKem::Mode kMode = MlKem::Mode::MlKem512;
	constexpr u64 kKemBytes = KEM_key_block_size * sizeof(block);
	constexpr u64 kMlKem512RowBytes = 800; // max(raw pk, ct || ss) = max(800, 768 + 32)
	static_assert(kKemBytes == kMlKem512RowBytes, "PQ-PSI row width must match the minimal ML-KEM-512 payload width");
	constexpr u64 kMaxKeyTries = 64;
	using RawKey = std::vector<u8>;

	inline std::array<u8, kKemBytes> toBytes(const kemKey& key)
	{
		std::array<u8, kKemBytes> out{};
		std::memcpy(out.data(), key.data(), kKemBytes);
		return out;
	}

	inline u8* bytesOf(kemKey& key)
	{
		return reinterpret_cast<u8*>(key.data());
	}

	inline const u8* bytesOf(const kemKey& key)
	{
		return reinterpret_cast<const u8*>(key.data());
	}

	inline const u8* bytesOf(const std::vector<block>& row)
	{
		return reinterpret_cast<const u8*>(row.data());
	}

	inline u8* bytesOf(block* row)
	{
		return reinterpret_cast<u8*>(row);
	}

	inline const u8* bytesOf(const block* row)
	{
		return reinterpret_cast<const u8*>(row);
	}

	inline size_t rowBytes(size_t rowSize)
	{
		return rowSize * sizeof(block);
	}

	inline block* rowPtr(std::vector<block>& rows, size_t rowSize, size_t row)
	{
		return rows.data() + row * rowSize;
	}

	inline const block* rowPtr(const std::vector<block>& rows, size_t rowSize, size_t row)
	{
		return rows.data() + row * rowSize;
	}

	inline std::array<u8, kKemBytes> rowBytes(const std::vector<block>& row)
	{
		if (row.size() != KEM_key_block_size)
		{
			throw std::invalid_argument("rowBytes: wrong block count");
		}

		std::array<u8, kKemBytes> out{};
		std::memcpy(out.data(), row.data(), kKemBytes);
		return out;
	}

	inline void fromBytes(span<const u8> src, kemKey& key)
	{
		if (src.size() != kKemBytes)
		{
			throw std::invalid_argument("fromBytes: wrong byte count");
		}

		std::memcpy(key.data(), src.data(), kKemBytes);
	}

	inline void toBits(const block* in, Bits& out)
	{
		out.resize(KEM_key_size_bit);
		const u8* bytes = reinterpret_cast<const u8*>(in);
		for (u64 i = 0; i < kKemBytes; ++i)
		{
			const u8 b = bytes[i];
			const u64 off = i * 8;
			out[off + 0] = static_cast<u8>((b >> 0) & 1U);
			out[off + 1] = static_cast<u8>((b >> 1) & 1U);
			out[off + 2] = static_cast<u8>((b >> 2) & 1U);
			out[off + 3] = static_cast<u8>((b >> 3) & 1U);
			out[off + 4] = static_cast<u8>((b >> 4) & 1U);
			out[off + 5] = static_cast<u8>((b >> 5) & 1U);
			out[off + 6] = static_cast<u8>((b >> 6) & 1U);
			out[off + 7] = static_cast<u8>((b >> 7) & 1U);
		}
	}

	inline void toBlocks(const Bits& in, block* out)
	{
		if (in.size() != KEM_key_size_bit)
		{
			throw std::invalid_argument("toBlocks: wrong bit count");
		}

		u8* bytes = reinterpret_cast<u8*>(out);
		for (u64 i = 0; i < kKemBytes; ++i)
		{
			const u64 off = i * 8;
			bytes[i] =
				static_cast<u8>((in[off + 0] & 1U) << 0) |
				static_cast<u8>((in[off + 1] & 1U) << 1) |
				static_cast<u8>((in[off + 2] & 1U) << 2) |
				static_cast<u8>((in[off + 3] & 1U) << 3) |
				static_cast<u8>((in[off + 4] & 1U) << 4) |
				static_cast<u8>((in[off + 5] & 1U) << 5) |
				static_cast<u8>((in[off + 6] & 1U) << 6) |
				static_cast<u8>((in[off + 7] & 1U) << 7);
		}
	}

	inline void copyRow(const kemKey& in, std::vector<block>& out)
	{
		out.resize(KEM_key_block_size);
		std::copy(in.begin(), in.end(), out.begin());
	}

	inline void permute(
		const pqperm::Perm& pi,
		const kemKey& in,
		std::vector<block>& out)
	{
		out.resize(KEM_key_block_size);
		std::memcpy(out.data(), in.data(), kKemBytes);
		pi.encryptBytes(reinterpret_cast<u8*>(out.data()), kKemBytes);
	}

	inline void permute(
		const pqperm::Perm& pi,
		const kemKey& in,
		block* out)
	{
		std::memcpy(out, in.data(), kKemBytes);
		pi.encryptBytes(reinterpret_cast<u8*>(out), kKemBytes);
	}

	inline void permute(
		const pqperm::Perm& pi,
		const block* in,
		size_t rowSize,
		block* out)
	{
		const size_t bytes = rowBytes(rowSize);
		std::memcpy(out, in, bytes);
		pi.encryptBytes(reinterpret_cast<u8*>(out), bytes);
	}

	inline void unpermute(
		const pqperm::Perm& pi,
		std::vector<block>& row)
	{
		if (row.size() != KEM_key_block_size)
		{
			throw std::invalid_argument("unpermute: wrong block count");
		}

		pi.decryptBytes(reinterpret_cast<u8*>(row.data()), kKemBytes);
	}

	inline void unpermute(
		const pqperm::Perm& pi,
		block* row)
	{
		pi.decryptBytes(reinterpret_cast<u8*>(row), kKemBytes);
	}

	inline void unpermute(
		const pqperm::Perm& pi,
		block* row,
		size_t rowSize)
	{
		pi.decryptBytes(reinterpret_cast<u8*>(row), rowBytes(rowSize));
	}
}
