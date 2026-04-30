#pragma once

#include "base.h"
#include "parallel.h"
#include "Crypto/AES.h"

namespace tools
{
	inline void fillMask(const block& key, kemKey& out)
	{
		AES aes(key);
		for (u64 i = 0; i < KEM_key_block_size; ++i)
		{
			out[i] = aes.ecbEncBlock(toBlock(0, i + 1));
		}
	}

	inline void fillMask(const block& key, block* out, size_t rowSize)
	{
		AES aes(key);
		for (u64 i = 0; i < rowSize; ++i)
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

	inline void xorMask(const block* mask, block* row, size_t rowSize)
	{
		for (u64 i = 0; i < rowSize; ++i)
		{
			row[i] ^= mask[i];
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

	inline void precomputeMasks(
		const std::vector<block>& set,
		size_t rowSize,
		std::vector<block>& masks,
		bool multiThread,
		size_t workerThreads)
	{
		masks.resize(set.size() * rowSize);
		parallelFor(set.size(), 16, multiThread, workerThreads, [&](size_t begin, size_t end)
		{
			for (size_t i = begin; i < end; ++i)
			{
				fillMask(set[i], rowPtr(masks, rowSize, i), rowSize);
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
}
