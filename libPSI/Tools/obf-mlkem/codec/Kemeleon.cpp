#include "Kemeleon.h"

#include <boost/multiprecision/cpp_int.hpp>
#include <sodium.h>

#include <stdexcept>

namespace osuCrypto
{
	using boost::multiprecision::cpp_int;

	namespace
	{
		// We only need the bit length to decide whether VectorEncode overflows its target size
		u64 bitLen(cpp_int x)
		{
			u64 n = 0;
			while (x != 0)
			{
				x >>= 1;
				++n;
			}

			return n;
		}
	}

	Kemeleon::Kemeleon(MlKem::Mode mode)
		: mKem(mode)
		, mInfo(makeInfo(mode))
		, mPreimages(makePreimageTable(mInfo.du))
	{
	}

	void Kemeleon::setMode(MlKem::Mode mode)
	{
		mKem.setMode(mode);
		mInfo = makeInfo(mode);
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
		return true;
	}

	bool Kemeleon::encodeCipher(span<const u8> cipher, std::vector<u8>& out) const
	{
		if (cipher.size() != rawCipherBytes())
		{
			throw std::invalid_argument("Kemeleon cipher has unexpected size");
		}

		std::vector<u16> uComp;
		// Split c into c1 and c2
		unpackBits(cipher.subspan(0, mInfo.c1Bytes), mInfo.du, mInfo.vecSize, uComp);

		std::vector<u16> u(mInfo.vecSize);
		for (u64 i = 0; i < mInfo.vecSize; ++i)
		{
			// Pick a random preimage for each compressed c1 value
			const u16 want = uComp[i];
			u[i] = pickPreimage(want);
		}

		std::vector<u8> r;
		// r <- VectorEncode(u)
		if (!encodeVec(u, mInfo.vecBits, r))
		{
			out.clear();
			return false;
		}

		std::vector<u16> c2;
		unpackBits(cipher.subspan(mInfo.c1Bytes, mInfo.c2Bytes), mInfo.dv, n, c2);
		for (u64 i = 0; i < c2.size(); ++i)
		{
			// Reject some zero entries in c2
			if (c2[i] == 0 && shouldRejectZero(mInfo.dv))
			{
				out.clear();
				return false;
			}
		}

		out.resize(codeCipherBytes());
		std::copy(r.begin(), r.end(), out.begin());
		std::copy(cipher.begin() + mInfo.c1Bytes, cipher.end(), out.begin() + mInfo.vecBytes);
		return true;
	}

	bool Kemeleon::decodeKey(span<const u8> data, std::vector<u8>& key) const
	{
		if (data.size() != codeKeyBytes())
		{
			key.clear();
			return false;
		}

		std::vector<u16> t;
		// t <- VectorDecode(r)
		if (!decodeVec(data.subspan(rhoBytes, mInfo.vecBytes), mInfo.vecBits, mInfo.vecSize, t))
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

		cpp_int top = 1;
		for (u64 i = 0; i < info.vecSize; ++i)
		{
			top *= q;
		}
		top += 1;

		// Size of the VectorEncode output
		info.vecBits = bitLen(top) - 1;
		info.vecBytes = (info.vecBits + 7) / 8;
		return info;
	}

	Kemeleon::PreimageTable Kemeleon::makePreimageTable(u64 bitsPerValue)
	{
		const u64 scale = 1ull << bitsPerValue;
		PreimageTable table;
		table.vals.resize(scale);

		for (u16 x = 0; x < q; ++x)
		{
			table.vals[compressValue(x, bitsPerValue)].push_back(x);
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
		cpp_int x = 0;
		for (u64 i = in.size(); i-- > 0;)
		{
			// Build the base-q integer from the vector
			x *= q;
			x += in[i];
		}

		if (bitLen(x) > bits)
		{
			out.clear();
			return false;
		}

		out.assign((bits + 7) / 8, 0);
		for (u64 i = 0; i < out.size(); ++i)
		{
			out[i] = static_cast<u8>((x >> (8 * i)) & 0xFF);
		}

		if (bits % 8 != 0)
		{
			out.back() &= static_cast<u8>((1u << (bits % 8)) - 1);
		}

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

		cpp_int x = 0;
		for (u64 i = in.size(); i-- > 0;)
		{
			x <<= 8;
			x += in[i];
		}

		out.resize(count);
		for (u64 i = 0; i < count; ++i)
		{
			// Read one base-q digit at a time
			out[i] = static_cast<u16>(x % q);
			x /= q;
		}

		return x == 0;
	}

		u16 Kemeleon::pickPreimage(u16 want) const
		{
			if (want >= mPreimages.vals.size())
			{
				throw std::runtime_error("Kemeleon preimage set is empty");
			}

			const auto& hits = mPreimages.vals[want];
			if (hits.empty())
			{
				throw std::runtime_error("Kemeleon preimage set is empty");
			}

			return hits[randombytes_uniform(static_cast<u32>(hits.size()))];
		}

	bool Kemeleon::shouldRejectZero(u64 bitsPerValue)
	{
		const u32 scale = 1u << bitsPerValue;
		const u32 p = (q + scale - 1) / scale;
		return randombytes_uniform(p) == 0;
	}
}
