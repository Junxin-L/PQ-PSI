#include "Kemeleon.h"

#include <gmp.h>
#include <sodium.h>

#include <chrono>
#include <stdexcept>

namespace osuCrypto
{
	constexpr u64 Kemeleon::n;
	constexpr u64 Kemeleon::q;
	constexpr u64 Kemeleon::rhoBytes;

	namespace
	{
		using Clock = std::chrono::steady_clock;

		// Buffered RNG for cheaper bounded picks
		class WordSampler
		{
		public:
			u32 next(u32 bound)
			{
				if (bound == 0)
				{
					throw std::runtime_error("Kemeleon sampler got zero bound");
				}

				const u32 lim = static_cast<u32>(-bound) % bound;
				for (;;)
				{
					const u32 x = nextWord();
					if (x >= lim)
					{
						return x % bound;
					}
				}
			}

		private:
			u32 nextWord()
			{
				if (mPos == mBuf.size())
				{
					randombytes_buf(mBuf.data(), mBuf.size() * sizeof(u32));
					mPos = 0;
				}

				return mBuf[mPos++];
			}

			std::array<u32, 1024> mBuf{};
			size_t mPos = mBuf.size();
		};

		u64 bitLen(const mpz_t x)
		{
			if (mpz_sgn(x) == 0)
			{
				return 0;
			}

			u64 n = static_cast<u64>(mpz_sizeinbase(x, 2));
			while (n > 0 && mpz_tstbit(x, n - 1) == 0)
			{
				--n;
			}

			return n;
		}

		u8 hashByte(span<const u8> data)
		{
			u8 out = 0;
			crypto_generichash(&out, 1, data.data(), data.size(), nullptr, 0);
			return out;
		}
	}

		Kemeleon::Kemeleon(MlKem::Mode mode)
			: mKem(mode)
			, mInfo(makeInfo(mode))
			// Mode-local preimage table
			, mPreimages(makePreimageTable(mInfo.du))
		{
		}

		void Kemeleon::setMode(MlKem::Mode mode)
		{
			mKem.setMode(mode);
			mInfo = makeInfo(mode);
			// Rebuild the flat table on mode change
			mPreimages = makePreimageTable(mInfo.du);
		}

	MlKem::Mode Kemeleon::mode() const
	{
		return mKem.mode();
	}

	MlKem::Sizes Kemeleon::sizes() const
	{
		return mKem.sizes();
	}

	u64 Kemeleon::rawKeyBytes() const
	{
		return mKem.publicKeyBytes();
	}

	u64 Kemeleon::rawCipherBytes() const
	{
		return mKem.cipherTextBytes();
	}

	u64 Kemeleon::codeKeyBytes() const
	{
		return rhoBytes + mInfo.vecBytes;
	}

	u64 Kemeleon::codeCipherBytes() const
	{
		return mInfo.vecBytes + mInfo.c2Bytes;
	}

	bool Kemeleon::encodeKey(span<const u8> key, std::vector<u8>& out) const
	{
		if (key.size() != rawKeyBytes())
		{
			throw std::invalid_argument("Kemeleon key has unexpected size");
		}

		std::vector<u16> t;
		// Split pk into t and rho
		unpackPolyBytes(key.subspan(0, mInfo.polyBytes), t);

		std::vector<u8> r;
		// r <- VectorEncode(t)
		if (!encodeVec(t, mInfo.vecBits, r))
		{
			out.clear();
			return false;
		}

		out.resize(codeKeyBytes());
		std::copy(key.begin() + mInfo.polyBytes, key.end(), out.begin());
		std::copy(r.begin(), r.end(), out.begin() + rhoBytes);
		// Fill the spare key bits so the last byte looks less structured
		fillKeyHighBits(key, out);
		return true;
	}

	bool Kemeleon::encodeCipher(span<const u8> cipher, std::vector<u8>& out) const
	{
		return encodeCipherImpl(cipher, out, nullptr);
	}

	bool Kemeleon::encodeCipherProfiled(span<const u8> cipher, std::vector<u8>& out, EncodeCipherStats& stats) const
	{
		return encodeCipherImpl(cipher, out, &stats);
	}

	bool Kemeleon::encodeCipherImpl(span<const u8> cipher, std::vector<u8>& out, EncodeCipherStats* stats) const
	{
		if (cipher.size() != rawCipherBytes())
		{
			throw std::invalid_argument("Kemeleon cipher has unexpected size");
		}

		if (stats)
		{
			++stats->tries;
		}

		std::vector<u16> uComp;
		// Split c into c1 and c2
		auto t0 = Clock::now();
		unpackBits(cipher.subspan(0, mInfo.c1Bytes), mInfo.du, mInfo.vecSize, uComp);
		auto t1 = Clock::now();
		if (stats)
		{
			stats->unpackNs += static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
		}

		std::vector<u8> r;
		// r <- VectorEncode(u)
		const bool pickOk = stats
			? encodePickedVecProfiled(uComp, mInfo.vecBits, r, *stats)
			: encodePickedVec(uComp, mInfo.vecBits, r);
		if (!pickOk)
		{
			if (stats)
			{
				++stats->overflowFails;
			}
			out.clear();
			return false;
		}

		// No temp c2 vector here
		t0 = Clock::now();
		if (rejectZeroEntries(cipher.subspan(mInfo.c1Bytes, mInfo.c2Bytes), mInfo.dv, n))
		{
			if (stats)
			{
				const auto dt = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
				stats->rejectNs += static_cast<u64>(dt);
				++stats->zeroFails;
			}
			out.clear();
			return false;
		}
		t1 = Clock::now();
		if (stats)
		{
			stats->rejectNs += static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
		}

		t0 = Clock::now();
		out.resize(codeCipherBytes());
		std::copy(r.begin(), r.end(), out.begin());
		std::copy(cipher.begin() + mInfo.c1Bytes, cipher.end(), out.begin() + mInfo.vecBytes);
		t1 = Clock::now();
		if (stats)
		{
			stats->outputNs += static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
		}
		return true;
	}

	bool Kemeleon::decodeKey(span<const u8> data, std::vector<u8>& key) const
	{
		if (data.size() != codeKeyBytes())
		{
			key.clear();
			return false;
		}

		std::vector<u8> r(data.begin() + rhoBytes, data.begin() + rhoBytes + mInfo.vecBytes);
		// Drop the spare key bits before VectorDecode
		clearKeyHighBits(r);

		std::vector<u16> t;
		// t <- VectorDecode(r)
		if (!decodeVec(r, mInfo.vecBits, mInfo.vecSize, t))
		{
			key.clear();
			return false;
		}

		std::vector<u8> packedT;
		packPolyBytes(t, packedT);

		key.resize(rawKeyBytes());
		std::copy(packedT.begin(), packedT.end(), key.begin());
		std::copy(data.begin(), data.begin() + rhoBytes, key.begin() + mInfo.polyBytes);
		return true;
	}

	u8 Kemeleon::keyHighMask() const
	{
		const u64 rem = mInfo.vecBits % 8;
		if (rem == 0)
		{
			return 0;
		}

		return static_cast<u8>(~((1u << rem) - 1));
	}

	void Kemeleon::fillKeyHighBits(span<const u8> key, std::vector<u8>& out) const
	{
		const u8 mask = keyHighMask();
		if (mask == 0)
		{
			return;
		}

		out[rhoBytes + mInfo.vecBytes - 1] |= hashByte(key) & mask;
	}

	void Kemeleon::clearKeyHighBits(std::vector<u8>& in) const
	{
		const u8 mask = keyHighMask();
		if (mask == 0 || in.empty())
		{
			return;
		}

		in.back() &= static_cast<u8>(~mask);
	}

	bool Kemeleon::decodeCipher(span<const u8> data, std::vector<u8>& cipher) const
	{
		if (data.size() != codeCipherBytes())
		{
			cipher.clear();
			return false;
		}

		std::vector<u16> u;
		// u <- VectorDecode(r)
		if (!decodeVec(data.subspan(0, mInfo.vecBytes), mInfo.vecBits, mInfo.vecSize, u))
		{
			cipher.clear();
			return false;
		}

		std::vector<u16> uComp(mInfo.vecSize);
		for (u64 i = 0; i < mInfo.vecSize; ++i)
		{
			// c1 <- Compress(u, du)
			uComp[i] = compressValue(u[i], mInfo.du);
		}

		std::vector<u8> c1;
		packBits(uComp, mInfo.du, c1);

		cipher.resize(rawCipherBytes());
		std::copy(c1.begin(), c1.end(), cipher.begin());
		std::copy(data.begin() + mInfo.vecBytes, data.end(), cipher.begin() + mInfo.c1Bytes);
		return true;
	}

	Kemeleon::Info Kemeleon::makeInfo(MlKem::Mode mode)
	{
		Info info;
		switch (mode)
		{
		case MlKem::Mode::MlKem512:
			info.k = 2;
			info.du = 10;
			info.dv = 4;
			break;
		case MlKem::Mode::MlKem768:
			info.k = 3;
			info.du = 10;
			info.dv = 4;
			break;
		case MlKem::Mode::MlKem1024:
			info.k = 4;
			info.du = 11;
			info.dv = 5;
			break;
		default:
			throw std::invalid_argument("Unsupported ML-KEM mode");
		}

		info.polyBytes = 384 * info.k;
		info.c1Bytes = info.k * info.du * n / 8;
		info.c2Bytes = info.dv * n / 8;
		info.vecSize = info.k * n;

		// GMP power once per mode
		mpz_t top;
		mpz_init(top);
		mpz_ui_pow_ui(top, q, info.vecSize);
		mpz_add_ui(top, top, 1);

		// Size of the VectorEncode output
		info.vecBits = bitLen(top) - 1;
		info.vecBytes = (info.vecBits + 7) / 8;
		mpz_clear(top);
		return info;
	}

	Kemeleon::PreimageTable Kemeleon::makePreimageTable(u64 bitsPerValue)
	{
		const u64 scale = 1ull << bitsPerValue;
		PreimageTable table;
		// Flat layout for tighter cache use
		table.offsets.resize(scale);
		table.counts.resize(scale);

		std::vector<u16> counts(scale, 0);
		for (u16 x = 0; x < q; ++x)
		{
			++counts[compressValue(x, bitsPerValue)];
		}

		u16 offset = 0;
		for (u64 i = 0; i < scale; ++i)
		{
			table.offsets[i] = offset;
			table.counts[i] = counts[i];
			offset = static_cast<u16>(offset + counts[i]);
		}

		table.vals.resize(offset);
		std::vector<u16> used(scale, 0);

		for (u16 x = 0; x < q; ++x)
		{
			const u16 idx = compressValue(x, bitsPerValue);
			const u16 pos = static_cast<u16>(table.offsets[idx] + used[idx]);
			table.vals[pos] = x;
			++used[idx];
		}

		return table;
	}

	bool Kemeleon::copyIfSize(span<const u8> src, u64 need, std::vector<u8>& dst)
	{
		if (src.size() != need)
		{
			dst.clear();
			return false;
		}

		dst.assign(src.begin(), src.end());
		return true;
	}

	void Kemeleon::unpackPolyBytes(span<const u8> src, std::vector<u16>& out)
	{
		if (src.size() % 3 != 0)
		{
			throw std::invalid_argument("Kemeleon poly bytes have unexpected size");
		}

		out.resize(src.size() / 3 * 2);
		for (u64 i = 0; i < src.size() / 3; ++i)
		{
			const u16 b0 = src[3 * i + 0];
			const u16 b1 = src[3 * i + 1];
			const u16 b2 = src[3 * i + 2];
			out[2 * i + 0] = static_cast<u16>(b0 | ((b1 & 0x0F) << 8));
			out[2 * i + 1] = static_cast<u16>((b1 >> 4) | (b2 << 4));
		}
	}

	void Kemeleon::packPolyBytes(span<const u16> src, std::vector<u8>& out)
	{
		if (src.size() % 2 != 0)
		{
			throw std::invalid_argument("Kemeleon coeff count has unexpected size");
		}

		out.resize(src.size() / 2 * 3);
		for (u64 i = 0; i < src.size() / 2; ++i)
		{
			const u16 a0 = src[2 * i + 0];
			const u16 a1 = src[2 * i + 1];
			out[3 * i + 0] = static_cast<u8>(a0 & 0xFF);
			out[3 * i + 1] = static_cast<u8>((a0 >> 8) | ((a1 & 0x0F) << 4));
			out[3 * i + 2] = static_cast<u8>(a1 >> 4);
		}
	}

	void Kemeleon::unpackBits(span<const u8> src, u64 bitsPerValue, u64 count, std::vector<u16>& out)
	{
		out.resize(count);
		u64 bitPos = 0;
		for (u64 i = 0; i < count; ++i)
		{
			u16 x = 0;
			for (u64 j = 0; j < bitsPerValue; ++j)
			{
				const u64 pos = bitPos + j;
				const u8 bit = (src[pos / 8] >> (pos % 8)) & 1;
				x |= static_cast<u16>(bit) << j;
			}
			out[i] = x;
			bitPos += bitsPerValue;
		}
	}

	void Kemeleon::packBits(span<const u16> src, u64 bitsPerValue, std::vector<u8>& out)
	{
		const u64 totalBits = src.size() * bitsPerValue;
		out.assign((totalBits + 7) / 8, 0);

		u64 bitPos = 0;
		for (u64 i = 0; i < src.size(); ++i)
		{
			const u16 x = src[i];
			for (u64 j = 0; j < bitsPerValue; ++j)
			{
				if ((x >> j) & 1)
				{
					const u64 pos = bitPos + j;
					out[pos / 8] |= static_cast<u8>(1u << (pos % 8));
				}
			}
			bitPos += bitsPerValue;
		}
	}

	u16 Kemeleon::compressValue(u16 x, u64 bitsPerValue)
	{
		const u32 scale = 1u << bitsPerValue;
		return static_cast<u16>((static_cast<u32>(x) * scale + q / 2) / q) % scale;
	}

	u16 Kemeleon::decompressValue(u16 x, u64 bitsPerValue)
	{
		const u32 half = 1u << (bitsPerValue - 1);
		return static_cast<u16>(((static_cast<u32>(x) * q) + half) >> bitsPerValue);
	}

	bool Kemeleon::encodeVec(span<const u16> in, u64 bits, std::vector<u8>& out)
	{
		// GMP path instead of cpp_int
		mpz_t x;
		mpz_init_set_ui(x, 0);
		for (u64 i = in.size(); i-- > 0;)
		{
			// Build the base-q integer from the vector
			mpz_mul_ui(x, x, q);
			mpz_add_ui(x, x, in[i]);
		}

		if (bitLen(x) > bits)
		{
			mpz_clear(x);
			out.clear();
			return false;
		}

		out.assign((bits + 7) / 8, 0);
		size_t wrote = 0;
		mpz_export(out.data(), &wrote, -1, 1, 0, 0, x);

		if (bits % 8 != 0)
		{
			out.back() &= static_cast<u8>((1u << (bits % 8)) - 1);
		}

		mpz_clear(x);
		return true;
	}

	bool Kemeleon::encodePickedVec(span<const u16> in, u64 bits, std::vector<u8>& out) const
	{
		// Stream picks into the GMP state
		WordSampler sampler;
		mpz_t x;
		mpz_init_set_ui(x, 0);
		for (u64 i = in.size(); i-- > 0;)
		{
			// Pick a random preimage for each compressed c1 value
			const u16 want = in[i];
			if (want >= mPreimages.counts.size())
			{
				mpz_clear(x);
				throw std::runtime_error("Kemeleon preimage set is empty");
			}

			const u16 count = mPreimages.counts[want];
			if (count == 0)
			{
				mpz_clear(x);
				throw std::runtime_error("Kemeleon preimage set is empty");
			}

			const u16 offset = mPreimages.offsets[want];
			const u16 val = mPreimages.vals[offset + sampler.next(count)];
			mpz_mul_ui(x, x, q);
			mpz_add_ui(x, x, val);
		}

		if (bitLen(x) > bits)
		{
			mpz_clear(x);
			out.clear();
			return false;
		}

		out.assign((bits + 7) / 8, 0);
		size_t wrote = 0;
		mpz_export(out.data(), &wrote, -1, 1, 0, 0, x);

		if (bits % 8 != 0)
		{
			out.back() &= static_cast<u8>((1u << (bits % 8)) - 1);
		}

		mpz_clear(x);
		return true;
	}

	bool Kemeleon::encodePickedVecProfiled(span<const u16> in, u64 bits, std::vector<u8>& out, EncodeCipherStats& stats) const
	{
		// Same path with split timings
		WordSampler sampler;
		mpz_t x;
		mpz_init_set_ui(x, 0);
		for (u64 i = in.size(); i-- > 0;)
		{
			auto t0 = Clock::now();
			const u16 want = in[i];
			if (want >= mPreimages.counts.size())
			{
				mpz_clear(x);
				throw std::runtime_error("Kemeleon preimage set is empty");
			}

			const u16 count = mPreimages.counts[want];
			if (count == 0)
			{
				mpz_clear(x);
				throw std::runtime_error("Kemeleon preimage set is empty");
			}

			const u16 offset = mPreimages.offsets[want];
			const u16 val = mPreimages.vals[offset + sampler.next(count)];
			auto t1 = Clock::now();
			stats.pickNs += static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());

			t0 = Clock::now();
			mpz_mul_ui(x, x, q);
			mpz_add_ui(x, x, val);
			t1 = Clock::now();
			stats.mpzNs += static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
		}

		if (bitLen(x) > bits)
		{
			mpz_clear(x);
			out.clear();
			return false;
		}

		auto t0 = Clock::now();
		out.assign((bits + 7) / 8, 0);
		size_t wrote = 0;
		mpz_export(out.data(), &wrote, -1, 1, 0, 0, x);

		if (bits % 8 != 0)
		{
			out.back() &= static_cast<u8>((1u << (bits % 8)) - 1);
		}
		auto t1 = Clock::now();
		stats.mpzNs += static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());

		mpz_clear(x);
		return true;
	}

	bool Kemeleon::decodeVec(span<const u8> in, u64 bits, u64 count, std::vector<u16>& out)
	{
		if (in.size() != (bits + 7) / 8)
		{
			out.clear();
			return false;
		}

		if (bits % 8 != 0)
		{
			const u8 mask = static_cast<u8>(~((1u << (bits % 8)) - 1));
			if ((in[in.size() - 1] & mask) != 0)
			{
				out.clear();
				return false;
			}
		}

		// GMP import plus small-int division
		mpz_t x;
		mpz_t qx;
		mpz_init(x);
		mpz_init(qx);
		mpz_import(x, in.size(), -1, 1, 0, 0, in.data());

		out.resize(count);
		for (u64 i = 0; i < count; ++i)
		{
			// Read one base-q digit at a time
			out[i] = static_cast<u16>(mpz_tdiv_q_ui(qx, x, q));
			mpz_swap(x, qx);
		}

		const bool ok = mpz_sgn(x) == 0;
		mpz_clear(qx);
		mpz_clear(x);
		return ok;
	}

	u16 Kemeleon::pickPreimage(u16 want) const
	{
		if (want >= mPreimages.counts.size())
		{
			throw std::runtime_error("Kemeleon preimage set is empty");
		}

		const u16 count = mPreimages.counts[want];
		if (count == 0)
		{
			throw std::runtime_error("Kemeleon preimage set is empty");
		}

		const u16 offset = mPreimages.offsets[want];
		const u32 pick = randombytes_uniform(static_cast<u32>(count));
		return mPreimages.vals[offset + pick];
	}

	bool Kemeleon::rejectZeroEntries(span<const u8> src, u64 bitsPerValue, u64 count)
	{
		// Scan packed c2 in place
		u64 bitPos = 0;
		for (u64 i = 0; i < count; ++i)
		{
			u16 x = 0;
			for (u64 j = 0; j < bitsPerValue; ++j)
			{
				const u64 pos = bitPos + j;
				const u8 bit = (src[pos / 8] >> (pos % 8)) & 1;
				x |= static_cast<u16>(bit) << j;
			}

			if (x == 0 && shouldRejectZero(bitsPerValue))
			{
				return true;
			}

			bitPos += bitsPerValue;
		}

		return false;
	}

	bool Kemeleon::shouldRejectZero(u64 bitsPerValue)
	{
		const u32 scale = 1u << bitsPerValue;
		const u32 p = (q + scale - 1) / scale;
		return randombytes_uniform(p) == 0;
	}
}
