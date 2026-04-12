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
		explicit Kemeleon(MlKem::Mode mode = MlKem::Mode::MlKem768);

		void setMode(MlKem::Mode mode);
		MlKem::Mode mode() const;
		MlKem::Sizes sizes() const;

		u64 rawKeyBytes() const;
		u64 rawCipherBytes() const;
		u64 codeKeyBytes() const;
		u64 codeCipherBytes() const;

		bool encodeKey(span<const u8> key, std::vector<u8>& out) const;
		bool encodeCipher(span<const u8> cipher, std::vector<u8>& out) const;

		bool decodeKey(span<const u8> data, std::vector<u8>& key) const;
		bool decodeCipher(span<const u8> data, std::vector<u8>& cipher) const;

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
			std::vector<std::vector<u16>> vals;
		};

		// ML-KEM ring constants
		static constexpr u64 n = 256;
		static constexpr u64 q = 3329;
		static constexpr u64 rhoBytes = 32;

		// Mode-specific sizes
		static Info makeInfo(MlKem::Mode mode);
		static PreimageTable makePreimageTable(u64 bitsPerValue);
		static bool copyIfSize(span<const u8> src, u64 need, std::vector<u8>& dst);

		// 12-bit polynomial packing
		static void unpackPolyBytes(span<const u8> src, std::vector<u16>& out);
		static void packPolyBytes(span<const u16> src, std::vector<u8>& out);

		// du/dv bit packing
		static void unpackBits(span<const u8> src, u64 bitsPerValue, u64 count, std::vector<u16>& out);
		static void packBits(span<const u16> src, u64 bitsPerValue, std::vector<u8>& out);

		// Figure 3 compression steps
		static u16 compressValue(u16 x, u64 bitsPerValue);
		static u16 decompressValue(u16 x, u64 bitsPerValue);

		// Figure 3 vector codec
		static bool encodeVec(span<const u16> in, u64 bits, std::vector<u8>& out);
		static bool decodeVec(span<const u8> in, u64 bits, u64 count, std::vector<u16>& out);

		// Random preimage sampling
		u16 pickPreimage(u16 want) const;

		// c2 rejection step∂
		static bool shouldRejectZero(u64 bitsPerValue);

		MlKem mKem;
		Info mInfo;
		PreimageTable mPreimages;
	};
}
