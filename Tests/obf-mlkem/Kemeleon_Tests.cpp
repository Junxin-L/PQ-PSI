#include "Kemeleon_Tests.h"

#include "../Common.h"
#include "libPSI/Tools/obf-mlkem/backend/MlKem.h"
#include "libPSI/Tools/obf-mlkem/codec/Kemeleon.h"

#include <boost/multiprecision/cpp_int.hpp>

#include <algorithm>
#include <array>
#include <set>
#include <vector>

using namespace osuCrypto;

namespace tests_libOTe
{
	namespace
	{
		using boost::multiprecision::cpp_int;

		std::vector<u8> makeBytes(u64 n, u8 start)
		{
			std::vector<u8> out(n);
			for (u64 i = 0; i < n; ++i)
			{
				out[i] = static_cast<u8>(start + i);
			}

			return out;
		}

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

		u64 vecBits(MlKem::Mode mode)
		{
			u64 k = 0;
			switch (mode)
			{
			case MlKem::Mode::MlKem512: k = 2; break;
			case MlKem::Mode::MlKem768: k = 3; break;
			case MlKem::Mode::MlKem1024: k = 4; break;
			default: throw UnitTestFail("Unexpected ML-KEM mode");
			}

			cpp_int top = 1;
			for (u64 i = 0; i < 256 * k; ++i)
			{
				top *= 3329;
			}
			top += 1;
			return bitLen(top) - 1;
		}

		void checkBadTopBits(MlKem::Mode mode, const std::vector<u8>& keyData, const std::vector<u8>& cipherData)
		{
			Kemeleon codec(mode);
			const u64 bits = vecBits(mode);
			const u64 vecBytes = (bits + 7) / 8;
			const u64 spare = vecBytes * 8 - bits;
			if (spare == 0)
			{
				return;
			}

			std::vector<u8> badKey = keyData;
			std::vector<u8> out;
			badKey[codec.codeKeyBytes() - 1] |= static_cast<u8>(1u << (8 - spare));
			std::vector<u8> keyOut;
			if (!codec.decodeKey(keyData, keyOut))
			{
				throw UnitTestFail("Kemeleon failed on clean key data");
			}
			if (!codec.decodeKey(badKey, out) || out != keyOut)
			{
				throw UnitTestFail("Kemeleon key decode did not ignore spare top bits");
			}

			std::vector<u8> badCipher = cipherData;
			badCipher[vecBytes - 1] |= static_cast<u8>(1u << (8 - spare));
			if (codec.decodeCipher(badCipher, out))
			{
				throw UnitTestFail("Kemeleon accepted cipher data with bad top bits");
			}
		}

		void checkMode(MlKem::Mode mode)
		{
			MlKem kem(mode);
			Kemeleon codec(mode);

			std::array<u8, MlKem::KeyGenSeedSize> keySeed;
			std::array<u8, MlKem::EncapSeedSize> encSeed;
			for (u64 i = 0; i < keySeed.size(); ++i)
			{
				keySeed[i] = static_cast<u8>(0x10 + i);
			}
			for (u64 i = 0; i < encSeed.size(); ++i)
			{
				encSeed[i] = static_cast<u8>(0x80 + i);
			}

			MlKem::KeyPair pair;
			std::vector<u8> keyData;
			bool keyOk = false;
			for (u64 i = 0; i < 256 && !keyOk; ++i)
			{
				keySeed[0] = static_cast<u8>(0x10 + i);
				pair = kem.keyGen(keySeed);
				keyOk = codec.encodeKey(pair.publicKey, keyData);
			}
			if (!keyOk)
			{
				throw UnitTestFail("Kemeleon key encode kept failing");
			}

			std::vector<u8> keyData2;
			if (!codec.encodeKey(pair.publicKey, keyData2) || keyData2 != keyData)
			{
				throw UnitTestFail("Kemeleon key encode is not stable");
			}
			if (keyData.size() != codec.codeKeyBytes())
			{
				throw UnitTestFail("Kemeleon key code size is wrong");
			}

			MlKem::EncapResult enc;
			std::vector<u8> cipherData;
			bool cipherOk = false;
			for (u64 i = 0; i < 256 && !cipherOk; ++i)
			{
				encSeed[0] = static_cast<u8>(0x80 + i);
				enc = kem.encaps(pair.publicKey, encSeed);
				for (u64 j = 0; j < 4096 && !cipherOk; ++j)
				{
					cipherOk = codec.encodeCipher(enc.cipherText, cipherData);
				}
			}
			if (!cipherOk)
			{
				throw UnitTestFail("Kemeleon cipher encode kept failing");
			}
			if (cipherData.size() != codec.codeCipherBytes())
			{
				throw UnitTestFail("Kemeleon cipher code size is wrong");
			}

			std::set<std::vector<u8>> seen;
			for (u64 i = 0; i < 16; ++i)
			{
				std::vector<u8> one;
				for (u64 j = 0; j < 4096; ++j)
				{
					if (codec.encodeCipher(enc.cipherText, one))
					{
						break;
					}
				}

				if (one.empty())
				{
					throw UnitTestFail("Kemeleon cipher encode never succeeded in repeated test");
				}

				std::vector<u8> oneOut;
				if (!codec.decodeCipher(one, oneOut) || oneOut != enc.cipherText)
				{
					throw UnitTestFail("Kemeleon repeated cipher round-trip failed");
				}

				seen.insert(one);
			}

			Kemeleon::EncodeCipherStats stats;
			std::vector<u8> profiled;
			bool profiledOk = false;
			for (u64 i = 0; i < 4096 && !profiledOk; ++i)
			{
				profiledOk = codec.encodeCipherProfiled(enc.cipherText, profiled, stats);
			}
			if (!profiledOk)
			{
				throw UnitTestFail("Kemeleon profiled cipher encode kept failing");
			}
			if (stats.tries == 0)
			{
				throw UnitTestFail("Kemeleon profiled cipher encode did not count tries");
			}
			std::vector<u8> profiledOut;
			if (!codec.decodeCipher(profiled, profiledOut) || profiledOut != enc.cipherText)
			{
				throw UnitTestFail("Kemeleon profiled cipher round-trip failed");
			}

			std::vector<u8> keyOut;
			std::vector<u8> cipherOut;
			if (!codec.decodeKey(keyData, keyOut) || keyOut != pair.publicKey)
			{
				throw UnitTestFail("Kemeleon decodeKey failed");
			}

			if (!codec.decodeCipher(cipherData, cipherOut) || cipherOut != enc.cipherText)
			{
				throw UnitTestFail("Kemeleon decodeCipher failed");
			}

			checkBadTopBits(mode, keyData, cipherData);

			auto shortKey = makeBytes(codec.rawKeyBytes() - 1, 0x20);
			auto shortCipher = makeBytes(codec.rawCipherBytes() - 1, 0x40);
			if (codec.decodeKey(shortKey, keyOut))
			{
				throw UnitTestFail("Kemeleon accepted short key input");
			}

			if (codec.decodeCipher(shortCipher, cipherOut))
			{
				throw UnitTestFail("Kemeleon accepted short cipher input");
			}

			bool threw = false;
			try
			{
				std::vector<u8> tmp;
				(void)codec.encodeKey(shortKey, tmp);
			}
			catch (const std::invalid_argument&)
			{
				threw = true;
			}

			if (!threw)
			{
				throw UnitTestFail("Kemeleon encodeKey accepted short input");
			}

			threw = false;
			try
			{
				std::vector<u8> tmp;
				(void)codec.encodeCipher(shortCipher, tmp);
			}
			catch (const std::invalid_argument&)
			{
				threw = true;
			}

			if (!threw)
			{
				throw UnitTestFail("Kemeleon encodeCipher accepted short input");
			}
		}

		void checkCodeSizes(
			MlKem::Mode mode,
			u64 wantKeyBytes,
			u64 wantCipherBytes)
		{
			Kemeleon codec(mode);
			if (codec.codeKeyBytes() != wantKeyBytes)
			{
				throw UnitTestFail("Kemeleon code key size is not the expected value");
			}

			if (codec.codeCipherBytes() != wantCipherBytes)
			{
				throw UnitTestFail("Kemeleon code cipher size is not the expected value");
			}
		}
	}

	void Kemeleon_Test_Impl()
	{
		checkCodeSizes(MlKem::Mode::MlKem512, 781, 877);
		checkCodeSizes(MlKem::Mode::MlKem768, 1156, 1252);
		checkCodeSizes(MlKem::Mode::MlKem1024, 1530, 1658);

		checkMode(MlKem::Mode::MlKem512);
		checkMode(MlKem::Mode::MlKem768);
		checkMode(MlKem::Mode::MlKem1024);
	}
}
