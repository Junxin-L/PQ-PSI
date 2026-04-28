#pragma once

#include "../pqpsi.h"
#include "Common/Defines.h"
#include "Crypto/AES.h"
#include "obf-mlkem/backend/MlKem.h"
#include "obf-mlkem/codec/Kemeleon.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <exception>
#include <mutex>
#include <sodium.h>
#include <sstream>
#include <thread>

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

	inline void xorMask(const kemKey& mask, block* row)
	{
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

	inline size_t workerLimit(bool multiThread, size_t requested = 0)
	{
		if (!multiThread)
		{
			return 1;
		}

		const size_t hw = std::max<size_t>(1, std::thread::hardware_concurrency());
		if (requested == 0)
		{
			return hw;
		}
		return std::max<size_t>(1, std::min(requested, hw));
	}

	inline size_t workerCount(size_t n, size_t minWork, bool multiThread, size_t workerThreads = 0)
	{
		if (n == 0 || !multiThread)
		{
			return 1;
		}

		const size_t maxThreads = std::max<size_t>(1, n / std::max<size_t>(1, minWork));
		return std::max<size_t>(1, std::min(workerLimit(multiThread, workerThreads), maxThreads));
	}

	template <typename Fn>
	inline void parallelFor(size_t n, size_t minWork, bool multiThread, size_t workerThreads, Fn&& fn)
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

		const size_t threadCount = workerCount(n, minWork, multiThread, workerThreads);
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
		bool multiThread,
		size_t workerThreads)
	{
		masks.resize(set.size());
		parallelFor(set.size(), 16, multiThread, workerThreads, [&](size_t begin, size_t end)
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
		bool multiThread,
		size_t workerThreads)
	{
		parallelFor(table.size(), 16, multiThread, workerThreads, [&](size_t begin, size_t end)
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
		bool multiThread,
		size_t workerThreads)
	{
		parallelFor(table.size(), 16, multiThread, workerThreads, [&](size_t begin, size_t end)
		{
			for (size_t j = begin; j < end; ++j)
			{
				std::memcpy(table[j].data(), flat.data() + j * rowSize, rowSize * sizeof(block));
			}
		});
	}

	inline u64 countHits(const std::vector<u8>& mask, bool multiThread, size_t workerThreads)
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

		const size_t threads = std::min(workerLimit(multiThread, workerThreads), mask.size());
		const size_t chunk = (mask.size() + threads - 1) / threads;
		std::vector<u64> partial(threads, 0);
		parallelFor(threads, 1, true, threads, [&](size_t begin, size_t end)
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

	inline void genKeys(
		std::vector<RawKey>& sk,
		std::vector<kemKey>& pk,
		bool multiThread,
		size_t workerThreads)
	{
		const MlKem kemInfo(kMode);
		const auto sizes = kemInfo.sizes();
		sk.resize(pk.size());
		for (auto& key : sk)
		{
			key.resize(sizes.secretKeyBytes);
		}

		std::atomic<bool> failed{ false };
		std::mutex errMutex;
		std::exception_ptr firstErr;

		parallelFor(sk.size(), 8, multiThread, workerThreads, [&](size_t begin, size_t end)
		{
			MlKem kem(kMode);
			Kemeleon codec(kMode);
			std::array<u8, MlKem::KeyGenSeedSize> seed{};
			std::vector<u8> rawPk(sizes.publicKeyBytes);
			std::vector<u8> code;
			Kemeleon::EncodeKeyWork keyWork;

			try
			{
				for (size_t i = begin; i < end; ++i)
				{
					if (failed.load(std::memory_order_relaxed))
					{
						break;
					}

					std::memset(bytesOf(pk[i]), 0, kKemBytes);

					bool ok = false;

					for (u64 t = 0; t < kMaxKeyTries; ++t)
					{
						for (u64 j = 0; j < seed.size(); ++j)
						{
							seed[j] = static_cast<u8>(((i + 1) * 131 + (j + 3) * 17 + t * 29) & 0xFF);
						}

						kem.keyGen(seed, rawPk, sk[i]);
						if (codec.encodeKey(rawPk, code, keyWork))
						{
							ok = true;
							break;
						}
					}

					if (!ok)
					{
						throw std::runtime_error("genKeys: encodeKey failed");
					}

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

	inline void encap(
		const std::vector<std::vector<block>>& rows,
		std::vector<kemKey>& out,
		bool multiThread,
		size_t workerThreads)
	{
		out.resize(rows.size());
		std::atomic<bool> failed{ false };
		std::mutex errMutex;
		std::exception_ptr firstErr;

		parallelFor(rows.size(), 8, multiThread, workerThreads, [&](size_t begin, size_t end)
		{
			MlKem kem(kMode);
			Kemeleon codec(kMode);
			const auto sizes = kem.sizes();
			std::array<u8, MlKem::EncapSeedSize> seed{};
			std::vector<u8> rawPk(sizes.publicKeyBytes);
			std::vector<u8> cipher(sizes.cipherTextBytes);
			std::array<u8, MlKem::SharedSecretSize> shared{};
			Kemeleon::DecodeKeyWork keyWork;

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

					if (!codec.decodeKey(
						span<const u8>(bytesOf(rows[i]), codec.codeKeyBytes()),
						span<u8>(rawPk.data(), rawPk.size()),
						keyWork))
					{
						std::ostringstream oss;
						oss << "encap: decodeKey failed at row " << i;
						throw std::runtime_error(oss.str());
					}

					randombytes_buf(seed.data(), seed.size());
					kem.encaps(rawPk, seed, cipher, shared);
					u8* outBytes = bytesOf(out[i]);
					std::memset(outBytes, 0, kKemBytes);

					if (sizes.cipherTextBytes + MlKem::SharedSecretSize > kKemBytes)
					{
						throw std::runtime_error("encap: output row too small");
					}

					std::memcpy(outBytes, cipher.data(), cipher.size());
					std::memcpy(
						outBytes + sizes.cipherTextBytes,
						shared.data(),
						shared.size());
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

	inline void encap(
		const std::vector<block>& rows,
		size_t rowSize,
		std::vector<kemKey>& out,
		bool multiThread,
		size_t workerThreads)
	{
		if (rowSize != KEM_key_block_size || (rows.size() % rowSize) != 0)
		{
			throw std::invalid_argument("encap: flat row size mismatch");
		}

		const size_t count = rows.size() / rowSize;
		out.resize(count);
		std::atomic<bool> failed{ false };
		std::mutex errMutex;
		std::exception_ptr firstErr;

		parallelFor(count, 8, multiThread, workerThreads, [&](size_t begin, size_t end)
		{
			MlKem kem(kMode);
			Kemeleon codec(kMode);
			const auto sizes = kem.sizes();
			std::array<u8, MlKem::EncapSeedSize> seed{};
			std::vector<u8> rawPk(sizes.publicKeyBytes);
			std::vector<u8> cipher(sizes.cipherTextBytes);
			std::array<u8, MlKem::SharedSecretSize> shared{};
			Kemeleon::DecodeKeyWork keyWork;

			try
			{
				for (size_t i = begin; i < end; ++i)
				{
					if (failed.load(std::memory_order_relaxed))
					{
						break;
					}

					if (!codec.decodeKey(
						span<const u8>(bytesOf(rowPtr(rows, rowSize, i)), codec.codeKeyBytes()),
						span<u8>(rawPk.data(), rawPk.size()),
						keyWork))
					{
						std::ostringstream oss;
						oss << "encap: decodeKey failed at row " << i;
						throw std::runtime_error(oss.str());
					}

					randombytes_buf(seed.data(), seed.size());
					kem.encaps(rawPk, seed, cipher, shared);
					u8* outBytes = bytesOf(out[i]);
					std::memset(outBytes, 0, kKemBytes);

					if (sizes.cipherTextBytes + MlKem::SharedSecretSize > kKemBytes)
					{
						throw std::runtime_error("encap: output row too small");
					}

					std::memcpy(outBytes, cipher.data(), cipher.size());
					std::memcpy(
						outBytes + sizes.cipherTextBytes,
						shared.data(),
						shared.size());
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

	inline bool decapBytes(const MlKem& kem, const RawKey& sk, const u8* rowData, size_t rowSize)
	{
		const auto sizes = kem.sizes();
		if (rowSize != KEM_key_block_size || sk.size() != sizes.secretKeyBytes)
		{
			return false;
		}

		if (sizes.cipherTextBytes + MlKem::SharedSecretSize > kKemBytes)
		{
			return false;
		}

		auto got = kem.decaps(span<const u8>(rowData, sizes.cipherTextBytes), sk);

		return std::equal(
			got.begin(),
			got.end(),
			rowData + sizes.cipherTextBytes);
	}

	inline bool decap(const RawKey& sk, const std::vector<block>& row)
	{
		MlKem kem(kMode);
		return decapBytes(kem, sk, bytesOf(row), row.size());
	}

	inline bool decap(const RawKey& sk, const block* row, size_t rowSize)
	{
		MlKem kem(kMode);
		return decapBytes(kem, sk, bytesOf(row), rowSize);
	}

	inline bool decapWith(const MlKem& kem, const RawKey& sk, const std::vector<block>& row)
	{
		return decapBytes(kem, sk, bytesOf(row), row.size());
	}

	inline bool decapWith(const MlKem& kem, const RawKey& sk, const block* row, size_t rowSize)
	{
		return decapBytes(kem, sk, bytesOf(row), rowSize);
	}

	inline u64 countDecapHits(
		const std::vector<RawKey>& sk,
		const std::vector<std::vector<block>>& rows,
		bool multiThread,
		size_t workerThreads)
	{
		if (sk.size() != rows.size())
		{
			throw std::invalid_argument("countDecapHits: size mismatch");
		}

		if (!multiThread || rows.size() < 64)
		{
			MlKem kem(kMode);
			u64 hits = 0;
			for (size_t i = 0; i < rows.size(); ++i)
			{
				hits += static_cast<u64>(decapWith(kem, sk[i], rows[i]));
			}
			return hits;
		}

		const size_t threads = std::min(workerLimit(multiThread, workerThreads), rows.size());
		const size_t chunk = (rows.size() + threads - 1) / threads;
		std::vector<u64> partial(threads, 0);
		parallelFor(threads, 1, true, threads, [&](size_t begin, size_t end)
		{
			for (size_t t = begin; t < end; ++t)
			{
				MlKem kem(kMode);
				const size_t from = t * chunk;
				const size_t to = std::min(rows.size(), from + chunk);
				u64 local = 0;
				for (size_t i = from; i < to; ++i)
				{
					local += static_cast<u64>(decapWith(kem, sk[i], rows[i]));
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

	inline u64 countDecapHits(
		const std::vector<RawKey>& sk,
		const std::vector<block>& rows,
		size_t rowSize,
		bool multiThread,
		size_t workerThreads)
	{
		if (rowSize != KEM_key_block_size || rows.size() != sk.size() * rowSize)
		{
			throw std::invalid_argument("countDecapHits: flat size mismatch");
		}

		if (!multiThread || sk.size() < 64)
		{
			MlKem kem(kMode);
			u64 hits = 0;
			for (size_t i = 0; i < sk.size(); ++i)
			{
				hits += static_cast<u64>(decapWith(kem, sk[i], rowPtr(rows, rowSize, i), rowSize));
			}
			return hits;
		}

		const size_t threads = std::min(workerLimit(multiThread, workerThreads), sk.size());
		const size_t chunk = (sk.size() + threads - 1) / threads;
		std::vector<u64> partial(threads, 0);
		parallelFor(threads, 1, true, threads, [&](size_t begin, size_t end)
		{
			for (size_t t = begin; t < end; ++t)
			{
				MlKem kem(kMode);
				const size_t from = t * chunk;
				const size_t to = std::min(sk.size(), from + chunk);
				u64 local = 0;
				for (size_t i = from; i < to; ++i)
				{
					local += static_cast<u64>(decapWith(kem, sk[i], rowPtr(rows, rowSize, i), rowSize));
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
