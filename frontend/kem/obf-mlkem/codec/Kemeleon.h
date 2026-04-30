#pragma once

#include "../backend/MlKem.h"
#include "cryptoTools/Common/Defines.h"

#include <array>
#include <vector>

namespace osuCrypto
{
	class Kemeleon
	{
	public:
		struct EncodeCipherStats
		{
			u64 unpackNs = 0;
			u64 pickNs = 0;
			u64 mpzNs = 0;
			u64 outputNs = 0;
			u64 rejectNs = 0;
			u64 tries = 0;
			u64 overflowFails = 0;
			u64 zeroFails = 0;
		};

		struct DecodeKeyWork
		{
			std::vector<u8> r;
			std::vector<u16> t;
			std::vector<u8> packedT;
		};

		struct EncodeKeyWork
		{
			std::vector<u16> t;
			std::vector<u8> r;
		};

		explicit Kemeleon(MlKem::Mode mode = MlKem::Mode::MlKem768);

		void setMode(MlKem::Mode mode);
		MlKem::Mode mode() const;
		MlKem::Sizes sizes() const;

		u64 rawKeyBytes() const;
		u64 rawCipherBytes() const;
		u64 codeKeyBytes() const;
		u64 codeCipherBytes() const;

		bool encodeKey(span<const u8> key, std::vector<u8> &out) const;
		bool encodeKey(span<const u8> key, std::vector<u8> &out, EncodeKeyWork &work) const;
		bool encodeCipher(span<const u8> cipher, std::vector<u8> &out) const;
		bool encodeCipherProfiled(span<const u8> cipher, std::vector<u8> &out, EncodeCipherStats &stats) const;

		bool decodeKey(span<const u8> data, std::vector<u8> &key) const;
		bool decodeKey(span<const u8> data, span<u8> key, DecodeKeyWork &work) const;
		bool decodeCipher(span<const u8> data, std::vector<u8> &cipher) const;

	private:
		struct Info
		{
			u64 k;
			u64 du;
			u64 dv;
			u64 polyBytes;
			u64 c1Bytes;
			u64 c2Bytes;
			u64 vecSize;
			u64 vecBits;
			u64 vecBytes;
		};

		struct PreimageTable
		{
			std::vector<u16> vals;
			std::vector<u16> offsets;
			std::vector<u16> counts;
		};

		static constexpr u64 n = 256;
		static constexpr u64 q = 3329;
		static constexpr u64 rhoBytes = 32;

		// sizes
		static Info makeInfo(MlKem::Mode mode);
		static PreimageTable makePreimageTable(u64 bitsPerValue);
		static bool copyIfSize(span<const u8> src, u64 need, std::vector<u8> &dst);

		static void unpackPolyBytes(span<const u8> src, std::vector<u16> &out);
		static void packPolyBytes(span<const u16> src, std::vector<u8> &out);

		// du/dv bit packing
		static void unpackBits(span<const u8> src, u64 bitsPerValue, u64 count, std::vector<u16> &out);
		static void packBits(span<const u16> src, u64 bitsPerValue, std::vector<u8> &out);

		// compression
		static u16 compressValue(u16 x, u64 bitsPerValue);
		static u16 decompressValue(u16 x, u64 bitsPerValue);

		// GMP vector codec
		static bool encodeVec(span<const u16> in, u64 bits, std::vector<u8> &out);
		static bool decodeVec(span<const u8> in, u64 bits, u64 count, std::vector<u16> &out);
		bool encodePickedVec(span<const u16> in, u64 bits, std::vector<u8> &out) const;
		bool encodePickedVecProfiled(span<const u16> in, u64 bits, std::vector<u8> &out, EncodeCipherStats &stats) const;

		// Extra key bits above vecBits
		u8 keyHighMask() const;
		void fillKeyHighBits(span<const u8> key, std::vector<u8> &out) const;
		void clearKeyHighBits(std::vector<u8> &in) const;

		u16 pickPreimage(u16 want) const;

		static bool rejectZeroEntries(span<const u8> src, u64 bitsPerValue, u64 count);
		static bool shouldRejectZero(u64 bitsPerValue);
		bool encodeCipherImpl(span<const u8> cipher, std::vector<u8> &out, EncodeCipherStats *stats) const;

		MlKem mKem;
		Info mInfo;
		PreimageTable mPreimages;
	};
}
