#pragma once

#include "mask.h"
#include "kem/eckem/eckem.h"
#include "kem/eckem/raw.h"
#include "kem/obf-mlkem/backend/MlKem.h"
#include "kem/obf-mlkem/codec/Kemeleon.h"
#include <atomic>
#include <cstdlib>
#include <exception>
#include <mutex>
#include <sodium.h>
#include <sstream>
#include <string>

namespace tools
{
	inline bool deterministicKemSeeds()
	{
		const char* v = std::getenv("PQPSI_DETERMINISTIC_KEM");
		return v && *v && std::string(v) != "0" && std::string(v) != "false" && std::string(v) != "off";
	}

	inline void fillMlKemKeySeed(
		std::array<u8, MlKem::KeyGenSeedSize>& seed,
		size_t row,
		u64 attempt,
		bool deterministic)
	{
		if (!deterministic)
		{
			randombytes_buf(seed.data(), seed.size());
			return;
		}

		for (u64 j = 0; j < seed.size(); ++j)
		{
			seed[j] = static_cast<u8>(((row + 1) * 131 + (j + 3) * 17 + attempt * 29) & 0xFF);
		}
	}

	inline void genKeys(
		std::vector<RawKey>& sk,
		std::vector<kemKey>& pk,
		bool multiThread,
		size_t workerThreads)
	{
		const MlKem kemInfo(kMode);
		const auto sizes = kemInfo.sizes();
		const bool deterministic = deterministicKemSeeds();
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
						fillMlKemKeySeed(seed, i, t, deterministic);
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

	inline void genMlKemKeys(
		std::vector<RawKey>& sk,
		std::vector<block>& pk,
		size_t rowSize,
		bool multiThread,
		size_t workerThreads)
	{
		if (rowSize != KEM_key_block_size || (pk.size() % rowSize) != 0)
		{
			throw std::invalid_argument("genMlKemKeys: wrong row size");
		}

		const size_t count = pk.size() / rowSize;
		const MlKem kemInfo(kMode);
		const auto sizes = kemInfo.sizes();
		const bool deterministic = deterministicKemSeeds();
		sk.resize(count);
		for (auto& key : sk)
		{
			key.resize(sizes.secretKeyBytes);
		}

		std::atomic<bool> failed{ false };
		std::mutex errMutex;
		std::exception_ptr firstErr;

		parallelFor(count, 8, multiThread, workerThreads, [&](size_t begin, size_t end)
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

					block* dst = rowPtr(pk, rowSize, i);
					std::memset(dst, 0, rowBytes(rowSize));

					bool ok = false;
					for (u64 t = 0; t < kMaxKeyTries; ++t)
					{
						fillMlKemKeySeed(seed, i, t, deterministic);
						kem.keyGen(seed, rawPk, sk[i]);
						if (codec.encodeKey(rawPk, code, keyWork))
						{
							ok = true;
							break;
						}
					}

					if (!ok)
					{
						throw std::runtime_error("genMlKemKeys: encodeKey failed");
					}

					if (code.size() > rowBytes(rowSize))
					{
						throw std::runtime_error("genMlKemKeys: encoded pk too large");
					}
					std::memcpy(dst, code.data(), code.size());
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

	inline void storeMlKemRows(
		const std::vector<kemKey>& keyed,
		size_t rowSize,
		std::vector<block>& out,
		bool multiThread,
		size_t workerThreads)
	{
		out.resize(keyed.size() * rowSize);
		parallelFor(keyed.size(), 16, multiThread, workerThreads, [&](size_t begin, size_t end)
		{
			for (size_t i = begin; i < end; ++i)
			{
				block* dst = rowPtr(out, rowSize, i);
				std::memset(dst, 0, rowBytes(rowSize));
				std::memcpy(dst, keyed[i].data(), kKemBytes);
			}
		});
	}

	inline void genEcKeys(
		std::vector<RawKey>& sk,
		std::vector<block>& pk,
		size_t rowSize,
		bool multiThread,
		size_t workerThreads)
	{
		if (rowSize != kemRowBlocks(KemCfg{ PsiKemKind::EcKem }) || (pk.size() % rowSize) != 0)
		{
			throw std::invalid_argument("genEcKeys: wrong row size");
		}

		const size_t count = pk.size() / rowSize;
		sk.resize(count);
		for (auto& key : sk)
		{
			key.resize(EcKem::SkBytes);
		}

		parallelFor(count, 8, multiThread, workerThreads, [&](size_t begin, size_t end)
		{
			for (size_t i = begin; i < end; ++i)
			{
				block* row = rowPtr(pk, rowSize, i);
				std::memset(row, 0, rowBytes(rowSize));
				eckem_raw::keyGen(bytesOf(row), sk[i].data());
			}
		});
	}

	inline void genRows(
		const KemCfg& kem,
		std::vector<RawKey>& sk,
		std::vector<block>& pk,
		size_t rowSize,
		bool multiThread,
		size_t workerThreads)
	{
		switch (kem.kind)
		{
		case PsiKemKind::ObfMlKem:
			genMlKemKeys(sk, pk, rowSize, multiThread, workerThreads);
			return;
		case PsiKemKind::EcKem:
			genEcKeys(sk, pk, rowSize, multiThread, workerThreads);
			return;
		default:
			throw std::invalid_argument("genRows: unknown KEM");
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

	inline void encapMlKem(
		const std::vector<block>& rows,
		size_t rowSize,
		std::vector<block>& out,
		bool multiThread,
		size_t workerThreads)
	{
		if (rowSize != KEM_key_block_size || (rows.size() % rowSize) != 0)
		{
			throw std::invalid_argument("encapMlKem: flat row size mismatch");
		}

		const size_t count = rows.size() / rowSize;
		out.resize(rows.size());
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
						oss << "encapMlKem: decodeKey failed at row " << i;
						throw std::runtime_error(oss.str());
					}

					randombytes_buf(seed.data(), seed.size());
					kem.encaps(rawPk, seed, cipher, shared);

					block* dst = rowPtr(out, rowSize, i);
					std::memset(dst, 0, rowBytes(rowSize));

					if (sizes.cipherTextBytes + MlKem::SharedSecretSize > rowBytes(rowSize))
					{
						throw std::runtime_error("encapMlKem: output row too small");
					}

					u8* outBytes = bytesOf(dst);
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

	inline void encapEc(
		const std::vector<block>& rows,
		size_t rowSize,
		std::vector<block>& out,
		bool multiThread,
		size_t workerThreads)
	{
		if (rowSize != kemRowBlocks(KemCfg{ PsiKemKind::EcKem }) || (rows.size() % rowSize) != 0)
		{
			throw std::invalid_argument("encapEc: wrong row size");
		}

		const size_t count = rows.size() / rowSize;
		out.resize(rows.size());
		parallelFor(count, 8, multiThread, workerThreads, [&](size_t begin, size_t end)
		{
			for (size_t i = begin; i < end; ++i)
			{
				block* dst = rowPtr(out, rowSize, i);
				eckem_raw::encap(bytesOf(dst), bytesOf(rowPtr(rows, rowSize, i)));
			}
		});
	}

	inline void encapRows(
		const KemCfg& kem,
		const std::vector<block>& rows,
		size_t rowSize,
		std::vector<block>& out,
		bool multiThread,
		size_t workerThreads)
	{
		switch (kem.kind)
		{
		case PsiKemKind::ObfMlKem:
			encapMlKem(rows, rowSize, out, multiThread, workerThreads);
			return;
		case PsiKemKind::EcKem:
			encapEc(rows, rowSize, out, multiThread, workerThreads);
			return;
		default:
			throw std::invalid_argument("encapRows: unknown KEM");
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

	inline u64 countEcHits(
		const std::vector<RawKey>& sk,
		const std::vector<block>& rows,
		size_t rowSize,
		bool multiThread,
		size_t workerThreads)
	{
		if (rowSize != kemRowBlocks(KemCfg{ PsiKemKind::EcKem }) || rows.size() != sk.size() * rowSize)
		{
			throw std::invalid_argument("countEcHits: flat size mismatch");
		}

		const auto one = [&](size_t i)
		{
			if (sk[i].size() != EcKem::SkBytes)
			{
				return false;
			}
			return eckem_raw::decap(sk[i].data(), bytesOf(rowPtr(rows, rowSize, i)));
		};

		if (!multiThread || sk.size() < 64)
		{
			u64 hits = 0;
			for (size_t i = 0; i < sk.size(); ++i)
			{
				hits += static_cast<u64>(one(i));
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
				const size_t from = t * chunk;
				const size_t to = std::min(sk.size(), from + chunk);
				u64 local = 0;
				for (size_t i = from; i < to; ++i)
				{
					local += static_cast<u64>(one(i));
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

	inline u64 countHits(
		const KemCfg& kem,
		const std::vector<RawKey>& sk,
		const std::vector<block>& rows,
		size_t rowSize,
		bool multiThread,
		size_t workerThreads)
	{
		switch (kem.kind)
		{
		case PsiKemKind::ObfMlKem:
			return countDecapHits(sk, rows, rowSize, multiThread, workerThreads);
		case PsiKemKind::EcKem:
			return countEcHits(sk, rows, rowSize, multiThread, workerThreads);
		default:
			throw std::invalid_argument("countHits: unknown KEM");
		}
	}
}
