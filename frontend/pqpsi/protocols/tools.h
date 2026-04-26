#pragma once

#include "../pqpsi.h"
#include "Common/Defines.h"
#include "Crypto/AES.h"
#include "../pi.h"
#include "obf-mlkem/backend/MlKem.h"
#include "obf-mlkem/codec/Kemeleon.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <exception>
#include <mutex>
#include <sstream>
#include <thread>

namespace tools
{
	using namespace osuCrypto;

	constexpr MlKem::Mode kMode = MlKem::Mode::MlKem512;
	constexpr u64 kKemBytes = KEM_key_block_size * sizeof(block);
	constexpr u64 kSeedBytes = MlKem::KeyGenSeedSize;
	constexpr u64 kMaxKeyTries = 64;

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

	inline void fillMask(const block& key, kemKey& out)
	{
		AES aes(key);
		for (u64 i = 0; i < KEM_key_block_size; ++i)
		{
			out[i] = aes.ecbEncBlock(toBlock(0, i + 1));
		}
	}

	inline void xorMask(const kemKey& mask, std::vector<block>& row)
	{
		if (row.size() != KEM_key_block_size)
		{
			throw std::invalid_argument("xorMask: wrong row block count");
		}

		for (u64 i = 0; i < KEM_key_block_size; ++i)
		{
			row[i] ^= mask[i];
		}
	}

	inline void copyRow(const kemKey& in, std::vector<block>& out)
	{
		out.resize(KEM_key_block_size);
		std::copy(in.begin(), in.end(), out.begin());
	}

	inline void permute(
		const Pi& pi,
		const kemKey& in,
		std::vector<block>& out,
		Bits& bits)
	{
		tools::toBits(in.data(), bits);
		bits = pi.encrypt(std::move(bits));
		out.resize(KEM_key_block_size);
		tools::toBlocks(bits, out.data());
	}

	inline void unpermute(
		const Pi& pi,
		std::vector<block>& row,
		Bits& bits)
	{
		if (row.size() != KEM_key_block_size)
		{
			throw std::invalid_argument("unpermute: wrong block count");
		}

		tools::toBits(row.data(), bits);
		bits = pi.decrypt(std::move(bits));
		tools::toBlocks(bits, row.data());
	}

	template <typename Fn>
	inline void parallelFor(size_t n, size_t minWork, bool multiThread, Fn&& fn)
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

		const size_t hw = std::max<size_t>(1, std::thread::hardware_concurrency());
		const size_t maxThreads = std::max<size_t>(1, n / std::max<size_t>(1, minWork));
		const size_t threadCount = std::max<size_t>(1, std::min(hw, maxThreads));
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

	inline void precomputeMasks(
		const std::vector<block>& set,
		std::vector<kemKey>& masks,
		bool multiThread)
	{
		masks.resize(set.size());
		parallelFor(set.size(), 16, multiThread, [&](size_t begin, size_t end)
		{
			for (size_t i = begin; i < end; ++i)
			{
				fillMask(set[i], masks[i]);
			}
		});
	}

	inline void storeTable(
		const std::vector<std::vector<block>>& table,
		std::vector<block>& flat,
		size_t rowSize,
		bool multiThread)
	{
		parallelFor(table.size(), 16, multiThread, [&](size_t begin, size_t end)
		{
			for (size_t j = begin; j < end; ++j)
			{
				std::memcpy(flat.data() + j * rowSize, table[j].data(), rowSize * sizeof(block));
			}
		});
	}

	inline void loadTable(
		const std::vector<block>& flat,
		std::vector<std::vector<block>>& table,
		size_t rowSize,
		bool multiThread)
	{
		parallelFor(table.size(), 16, multiThread, [&](size_t begin, size_t end)
		{
			for (size_t j = begin; j < end; ++j)
			{
				std::memcpy(table[j].data(), flat.data() + j * rowSize, rowSize * sizeof(block));
			}
		});
	}

	inline u64 countHits(const std::vector<u8>& mask, bool multiThread)
	{
		if (!multiThread || mask.size() < 64)
		{
			u64 hits = 0;
			for (u8 v : mask)
			{
				hits += static_cast<u64>(v != 0);
			}
			return hits;
		}

		const size_t hw = std::max<size_t>(1, std::thread::hardware_concurrency());
		const size_t chunk = (mask.size() + hw - 1) / hw;
		std::vector<u64> partial(hw, 0);
		parallelFor(hw, 1, true, [&](size_t begin, size_t end)
		{
			for (size_t t = begin; t < end; ++t)
			{
				const size_t from = t * chunk;
				const size_t to = std::min(mask.size(), from + chunk);
				u64 local = 0;
				for (size_t i = from; i < to; ++i)
				{
					local += static_cast<u64>(mask[i] != 0);
				}
				partial[t] = local;
			}
		});

		u64 hits = 0;
		for (u64 v : partial)
		{
			hits += v;
		}
		return hits;
	}

	inline void genKeys(std::vector<kemKey>& sk, std::vector<kemKey>& pk, bool multiThread)
	{
		std::atomic<bool> failed{ false };
		std::mutex errMutex;
		std::exception_ptr firstErr;

		parallelFor(sk.size(), 8, multiThread, [&](size_t begin, size_t end)
		{
			MlKem kem(kMode);
			Kemeleon codec(kMode);

			try
			{
				for (size_t i = begin; i < end; ++i)
				{
					if (failed.load(std::memory_order_relaxed))
					{
						break;
					}

					std::memset(bytesOf(sk[i]), 0, kKemBytes);
					std::memset(bytesOf(pk[i]), 0, kKemBytes);

					std::array<u8, MlKem::KeyGenSeedSize> seed{};
					std::vector<u8> code;
					bool ok = false;

					for (u64 t = 0; t < kMaxKeyTries; ++t)
					{
						for (u64 j = 0; j < seed.size(); ++j)
						{
							seed[j] = static_cast<u8>(((i + 1) * 131 + (j + 3) * 17 + t * 29) & 0xFF);
						}

						auto pair = kem.keyGen(seed);
						if (codec.encodeKey(pair.publicKey, code))
						{
							ok = true;
							break;
						}
					}

					if (!ok)
					{
						throw std::runtime_error("genKeys: encodeKey failed");
					}

					std::memcpy(bytesOf(sk[i]), seed.data(), seed.size());
					if (code.size() > kKemBytes)
					{
						throw std::runtime_error("genKeys: encoded pk too large");
					}
					std::memcpy(bytesOf(pk[i]), code.data(), code.size());
				}
			}
			catch (...)
			{
				failed.store(true, std::memory_order_relaxed);
				std::lock_guard<std::mutex> lock(errMutex);
				if (!firstErr)
				{
					firstErr = std::current_exception();
				}
			}
		});

		if (firstErr)
		{
			std::rethrow_exception(firstErr);
		}
	}

	inline void encap(const std::vector<std::vector<block>>& rows, std::vector<kemKey>& out, bool multiThread)
	{
		out.resize(rows.size());
		std::atomic<bool> failed{ false };
		std::mutex errMutex;
		std::exception_ptr firstErr;

		parallelFor(rows.size(), 8, multiThread, [&](size_t begin, size_t end)
		{
			MlKem kem(kMode);
			Kemeleon codec(kMode);
			const auto sizes = kem.sizes();

			try
			{
				for (size_t i = begin; i < end; ++i)
				{
					if (failed.load(std::memory_order_relaxed))
					{
						break;
					}

					if (rows[i].size() != KEM_key_block_size)
					{
						throw std::invalid_argument("encap: wrong row size");
					}

					std::vector<u8> rawPk;
					if (!codec.decodeKey(span<const u8>(bytesOf(rows[i]), codec.codeKeyBytes()), rawPk))
					{
						std::ostringstream oss;
						oss << "encap: decodeKey failed at row " << i;
						throw std::runtime_error(oss.str());
					}

					auto enc = kem.encaps(rawPk);
					u8* outBytes = bytesOf(out[i]);
					std::memset(outBytes, 0, kKemBytes);

					if (sizes.cipherTextBytes + MlKem::SharedSecretSize > kKemBytes)
					{
						throw std::runtime_error("encap: output row too small");
					}

					std::memcpy(outBytes, enc.cipherText.data(), enc.cipherText.size());
					std::memcpy(
						outBytes + sizes.cipherTextBytes,
						enc.sharedSecret.data(),
						enc.sharedSecret.size());
				}
			}
			catch (...)
			{
				failed.store(true, std::memory_order_relaxed);
				std::lock_guard<std::mutex> lock(errMutex);
				if (!firstErr)
				{
					firstErr = std::current_exception();
				}
			}
		});

		if (firstErr)
		{
			std::rethrow_exception(firstErr);
		}
	}

	inline bool decap(const kemKey& sk, const std::vector<block>& row)
	{
		MlKem kem(kMode);
		const auto sizes = kem.sizes();
		if (row.size() != KEM_key_block_size)
		{
			return false;
		}

		std::array<u8, MlKem::KeyGenSeedSize> seed{};
		std::memcpy(seed.data(), bytesOf(sk), kSeedBytes);
		auto pair = kem.keyGen(seed);

		const u8* rowData = bytesOf(row);
		if (sizes.cipherTextBytes + MlKem::SharedSecretSize > kKemBytes)
		{
			return false;
		}

		std::vector<u8> ct(sizes.cipherTextBytes);
		std::memcpy(ct.data(), rowData, ct.size());
		auto got = kem.decaps(ct, pair.secretKey);

		return std::equal(
			got.begin(),
			got.end(),
			rowData + sizes.cipherTextBytes);
	}

	inline u64 countDecapHits(
		const std::vector<kemKey>& sk,
		const std::vector<std::vector<block>>& rows,
		bool multiThread)
	{
		if (sk.size() != rows.size())
		{
			throw std::invalid_argument("countDecapHits: size mismatch");
		}

		if (!multiThread || rows.size() < 64)
		{
			u64 hits = 0;
			for (size_t i = 0; i < rows.size(); ++i)
			{
				hits += static_cast<u64>(decap(sk[i], rows[i]));
			}
			return hits;
		}

		const size_t hw = std::max<size_t>(1, std::thread::hardware_concurrency());
		const size_t chunk = (rows.size() + hw - 1) / hw;
		std::vector<u64> partial(hw, 0);
		parallelFor(hw, 1, true, [&](size_t begin, size_t end)
		{
			for (size_t t = begin; t < end; ++t)
			{
				const size_t from = t * chunk;
				const size_t to = std::min(rows.size(), from + chunk);
				u64 local = 0;
				for (size_t i = from; i < to; ++i)
				{
					local += static_cast<u64>(decap(sk[i], rows[i]));
				}
				partial[t] = local;
			}
		});

		u64 hits = 0;
		for (u64 v : partial)
		{
			hits += v;
		}
		return hits;
	}
}
