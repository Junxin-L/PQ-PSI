#pragma once

#include "api.h"
#include "types.h"

#include "Crypto/AES.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace pqperm::hctr2
{
	using Byte = UINT8;
	using Block = std::array<Byte, 16>;

	struct U128
	{
		uint64_t lo = 0;
		uint64_t hi = 0;
	};

	inline uint64_t load64(const Byte* in)
	{
		uint64_t x = 0;
		for (size_t i = 0; i < 8; ++i)
		{
			x |= static_cast<uint64_t>(in[i]) << (8 * i);
		}
		return x;
	}

	inline void store64(Byte* out, uint64_t x)
	{
		for (size_t i = 0; i < 8; ++i)
		{
			out[i] = static_cast<Byte>((x >> (8 * i)) & 0xff);
		}
	}

	inline U128 load128(const Byte* in)
	{
		return U128{load64(in), load64(in + 8)};
	}

	inline void store128(Byte* out, U128 x)
	{
		store64(out, x.lo);
		store64(out + 8, x.hi);
	}

	inline bool getBit(const std::array<uint64_t, 4>& x, size_t bit)
	{
		return ((x[bit / 64] >> (bit % 64)) & 1) != 0;
	}

	inline void flipBit(std::array<uint64_t, 4>& x, size_t bit)
	{
		x[bit / 64] ^= (uint64_t(1) << (bit % 64));
	}

	inline bool getBit(U128 x, size_t bit)
	{
		if (bit < 64)
		{
			return ((x.lo >> bit) & 1) != 0;
		}
		return ((x.hi >> (bit - 64)) & 1) != 0;
	}

	inline U128 mul(U128 a, U128 b)
	{
		std::array<uint64_t, 4> p{};
		for (size_t i = 0; i < 128; ++i)
		{
			if (!getBit(a, i))
			{
				continue;
			}

			const size_t word = i / 64;
			const size_t shift = i % 64;
			p[word] ^= b.lo << shift;
			if (shift != 0)
			{
				p[word + 1] ^= b.lo >> (64 - shift);
			}
			p[word + 1] ^= b.hi << shift;
			if (shift != 0 && word + 2 < p.size())
			{
				p[word + 2] ^= b.hi >> (64 - shift);
			}
		}

		// POLYVAL works in GF(2^128) with x^128 + x^127 + x^126 + x^121 + 1.
		for (size_t d = 255; d >= 128; --d)
		{
			if (getBit(p, d))
			{
				flipBit(p, d);
				flipBit(p, d - 1);
				flipBit(p, d - 2);
				flipBit(p, d - 7);
				flipBit(p, d - 128);
			}
			if (d == 128)
			{
				break;
			}
		}

		return U128{p[0], p[1]};
	}

	inline U128 operator^(U128 a, U128 b)
	{
		return U128{a.lo ^ b.lo, a.hi ^ b.hi};
	}

	inline U128& operator^=(U128& a, U128 b)
	{
		a.lo ^= b.lo;
		a.hi ^= b.hi;
		return a;
	}

	using MulTable = std::array<std::array<U128, 256>, 16>;

	inline MulTable makeMulTable(U128 h)
	{
		MulTable table{};
		for (size_t pos = 0; pos < table.size(); ++pos)
		{
			const size_t bit = pos * 8;
			for (size_t v = 0; v < 256; ++v)
			{
				U128 x{};
				if (bit < 64)
				{
					x.lo = static_cast<uint64_t>(v) << bit;
				}
				else
				{
					x.hi = static_cast<uint64_t>(v) << (bit - 64);
				}
				table[pos][v] = mul(x, h);
			}
		}
		return table;
	}

	inline U128 mulFixed(U128 x, const MulTable& table)
	{
		U128 out{};
		for (size_t pos = 0; pos < 8; ++pos)
		{
			out ^= table[pos][(x.lo >> (pos * 8)) & 0xff];
			out ^= table[pos + 8][(x.hi >> (pos * 8)) & 0xff];
		}
		return out;
	}

	inline Block toBlock(U128 x)
	{
		Block out{};
		store128(out.data(), x);
		return out;
	}

	inline U128 fromBlock(const Block& b)
	{
		return load128(b.data());
	}

	inline void xorBlock(Byte* out, const Byte* a, const Byte* b)
	{
		for (size_t i = 0; i < 16; ++i)
		{
			out[i] = a[i] ^ b[i];
		}
	}

	inline block loadAesBlock(const Byte* in)
	{
		block out;
		std::memcpy(&out, in, 16);
		return out;
	}

	inline void storeAesBlock(Byte* out, block in)
	{
		std::memcpy(out, &in, 16);
	}

	inline Block fixedKey()
	{
		return Block{{'p', 'q', 'p', 's', 'i', '-', 'h', 'c', 't', 'r', '2', '-', 'k', 'e', 'y', '!'}};
	}

	class Aes
	{
	public:
		Aes()
			: Aes(fixedKey().data())
		{
		}

		explicit Aes(const Byte* key)
		{
			const block aesKey = loadAesBlock(key);
			enc_.setKey(aesKey);
			dec_.setKey(aesKey);
		}

		Block enc(const Block& in) const
		{
			Block out{};
			storeAesBlock(out.data(), enc_.ecbEncBlock(loadAesBlock(in.data())));
			return out;
		}

		void encBlocks(const block* in, size_t count, block* out) const
		{
			enc_.ecbEncBlocks(in, static_cast<u64>(count), out);
		}

		Block dec(const Block& in) const
		{
			Block out{};
			block plain = dec_.ecbDecBlock(loadAesBlock(in.data()));
			storeAesBlock(out.data(), plain);
			return out;
		}

	private:
		osuCrypto::AES enc_;
		mutable osuCrypto::AESDec dec_;
	};

	inline Block schedule(const Aes& aes, uint64_t i)
	{
		Block b{};
		store64(b.data(), i);
		return aes.enc(b);
	}

	inline U128 polyvalKey(const Block& hashKey)
	{
		const U128 c{uint64_t(1), (uint64_t(1) << 63) | (uint64_t(1) << 60) | (uint64_t(1) << 57) | (uint64_t(1) << 50)};
		return mul(fromBlock(hashKey), c);
	}

	inline void update(U128& acc, const MulTable& table, const Byte* blocks, size_t count)
	{
		for (size_t i = 0; i < count; ++i)
		{
			acc ^= load128(blocks + i * 16);
			acc = mulFixed(acc, table);
		}
	}

	inline void updatePaddedZero(U128& acc, const MulTable& table, const Byte* data, size_t bytes)
	{
		update(acc, table, data, bytes / 16);
		const size_t rem = bytes % 16;
		if (rem != 0)
		{
			Block last{};
			std::memcpy(last.data(), data + bytes - rem, rem);
			update(acc, table, last.data(), 1);
		}
	}

	inline U128 initHash(const MulTable& table, const Byte* tweak, size_t tweakBytes, uint64_t modifier)
	{
		U128 acc{};
		Block first{};
		store64(first.data(), tweakBytes * 8 * 2 + modifier);
		update(acc, table, first.data(), 1);
		updatePaddedZero(acc, table, tweak, tweakBytes);
		return acc;
	}

	inline Block fixedTweak()
	{
		return Block{{'p', 'q', 'p', 's', 'i', '-', 'h', 'c', 't', 'r', '2', '-', 't', 'w', 'k', '!'}};
	}

	inline Block partyKey(u8 party)
	{
		Block key{{'p', 'q', 'p', 's', 'i', '-', 'h', 'c', 't', 'r', '2', '-', 'k', 'e', '0', '!'}};
		key[14] = static_cast<Byte>('0' + (party & 1U));
		return key;
	}

	inline Block partyTweak(u8 party)
	{
		Block tweak{{'p', 'q', 'p', 's', 'i', '-', 'h', 'c', 't', 'r', '2', '-', 't', 'w', '0', '!'}};
		tweak[14] = static_cast<Byte>('0' + (party & 1U));
		return tweak;
	}

	struct Key
	{
		Key()
			: Key(fixedKey().data(), fixedTweak().data(), fixedTweak().size())
		{
		}

		Key(const Byte* keyBytes, const Byte* tweakBytes, size_t tweakLen)
			: aes(keyBytes)
			, hashKey(schedule(aes, 0))
			, l(schedule(aes, 1))
			, h(polyvalKey(hashKey))
			, table(makeMulTable(h))
			, initDiv(initHash(table, tweakBytes, tweakLen, 2))
			, initAwk(initHash(table, tweakBytes, tweakLen, 3))
		{
		}

		Aes aes;
		Block hashKey{};
		Block l{};
		U128 h{};
		MulTable table{};
		U128 initDiv{};
		U128 initAwk{};
	};

	inline Block hash(const Key& key, const Byte* msg, size_t msgBytes)
	{
		const size_t full = msgBytes / 16;
		const size_t rem = msgBytes % 16;
		U128 acc = (rem == 0) ? key.initDiv : key.initAwk;
		update(acc, key.table, msg, full);

		if (rem != 0)
		{
			Block last{};
			std::memcpy(last.data(), msg + full * 16, rem);
			last[rem] = 1;
			update(acc, key.table, last.data(), 1);
		}

		return toBlock(acc);
	}

	inline void xctr(const Aes& aes, Byte* out, const Byte* in, size_t bytes, const Block& nonce)
	{
		const size_t full = bytes / 16;
		const size_t rem = bytes % 16;
		std::array<block, KEM_key_block_size> counters{};
		std::array<block, KEM_key_block_size> stream{};

		for (size_t i = 0; i < full; ++i)
		{
			Block ctr{};
			store64(ctr.data(), static_cast<uint64_t>(i + 1));
			for (size_t j = 0; j < 16; ++j)
			{
				ctr[j] ^= nonce[j];
			}
			counters[i] = loadAesBlock(ctr.data());
		}

		if (full != 0)
		{
			aes.encBlocks(counters.data(), full, stream.data());
			const Byte* ks = reinterpret_cast<const Byte*>(stream.data());
			for (size_t i = 0; i < full * 16; ++i)
			{
				out[i] = in[i] ^ ks[i];
			}
		}

		if (rem != 0)
		{
			Block ctr{};
			store64(ctr.data(), static_cast<uint64_t>(full + 1));
			for (size_t i = 0; i < 16; ++i)
			{
				ctr[i] ^= nonce[i];
			}
			const Block last = aes.enc(ctr);
			const size_t off = full * 16;
			for (size_t i = 0; i < rem; ++i)
			{
				out[off + i] = in[off + i] ^ last[i];
			}
		}
	}

	inline void crypt(Byte* data, size_t bytes, const Key& key, bool encrypt)
	{
		if (bytes < 16)
		{
			throw std::invalid_argument("HCTR2 needs at least one AES block");
		}

		Byte* src = data;
		const size_t nBytes = bytes - 16;

		if (encrypt)
		{
			const Block d1 = hash(key, src + 16, nBytes);
			Block mm{};
			xorBlock(mm.data(), src, d1.data());
			const Block uu = key.aes.enc(mm);
			Block s{};
			for (size_t i = 0; i < 16; ++i)
			{
				s[i] = mm[i] ^ uu[i] ^ key.l[i];
			}
			xctr(key.aes, src + 16, src + 16, nBytes, s);
			const Block d2 = hash(key, src + 16, nBytes);
			xorBlock(src, uu.data(), d2.data());
		}
		else
		{
			const Block d1 = hash(key, src + 16, nBytes);
			Block uu{};
			xorBlock(uu.data(), src, d1.data());
			const Block mm = key.aes.dec(uu);
			Block s{};
			for (size_t i = 0; i < 16; ++i)
			{
				s[i] = mm[i] ^ uu[i] ^ key.l[i];
			}
			xctr(key.aes, src + 16, src + 16, nBytes, s);
			const Block d2 = hash(key, src + 16, nBytes);
			xorBlock(src, mm.data(), d2.data());
		}
	}
}

namespace pqperm
{
	class Hctr final : public Perm
	{
	public:
		explicit Hctr(u8 party = 0)
			: key_(makeKey(party))
		{
		}

		const char* name() const override
		{
			return "hctr";
		}

		const char* detail() const override
		{
			return "aes128-hctr2";
		}

		size_t n() const override
		{
			return KEM_key_size_bit;
		}

		size_t s() const override
		{
			return 0;
		}

		size_t rounds() const override
		{
			return 1;
		}

		void encryptBytes(u8* data, size_t bytes) const override
		{
			crypt(data, bytes, true);
		}

		void decryptBytes(u8* data, size_t bytes) const override
		{
			crypt(data, bytes, false);
		}

	private:
		static hctr2::Key makeKey(u8 party)
		{
			const auto key = hctr2::partyKey(party);
			const auto tweak = hctr2::partyTweak(party);
			return hctr2::Key(key.data(), tweak.data(), tweak.size());
		}

		void crypt(u8* data, size_t bytes, bool encrypt) const
		{
			const size_t want = KEM_key_size_bit / 8;
			if (bytes != want)
			{
				throw std::invalid_argument("HCTR state size mismatch");
			}

			hctr2::crypt(data, bytes, key_, encrypt);
		}

		hctr2::Key key_;
	};
}
