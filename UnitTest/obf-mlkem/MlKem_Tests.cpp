#include "MlKem_Tests.h"

#include "../Common.h"
#include "frontend/kem/obf-mlkem/backend/MlKem.h"

#include <algorithm>
#include <array>

using namespace osuCrypto;

namespace tests_pqpsi
{
	namespace
	{
		void fillSeed(span<u8> seed, u8 base)
		{
			for (u64 i = 0; i < seed.size(); ++i)
			{
				seed[i] = static_cast<u8>(base + i);
			}
		}

		void checkMode(
			MlKem::Mode mode,
			u64 expectedPkBytes,
			u64 expectedSkBytes,
			u64 expectedCtBytes)
		{
			MlKem kem(mode);
			auto sizes = kem.sizes();

			if (sizes.publicKeyBytes != expectedPkBytes ||
				sizes.secretKeyBytes != expectedSkBytes ||
				sizes.cipherTextBytes != expectedCtBytes)
			{
				throw UnitTestFail("MlKem sizes do not match expected values");
			}

			std::array<u8, MlKem::KeyGenSeedSize> keySeed;
			std::array<u8, MlKem::EncapSeedSize> encSeed;
			fillSeed(keySeed, static_cast<u8>(expectedPkBytes & 0xFF));
			fillSeed(encSeed, static_cast<u8>(expectedCtBytes & 0xFF));

			auto keyPair = kem.keyGen(keySeed);
			if (keyPair.publicKey.size() != expectedPkBytes ||
				keyPair.secretKey.size() != expectedSkBytes)
			{
				throw UnitTestFail("MlKem key material has unexpected size");
			}

			auto encap = kem.encaps(keyPair.publicKey, encSeed);
			if (encap.cipherText.size() != expectedCtBytes)
			{
				throw UnitTestFail("MlKem ciphertext has unexpected size");
			}

			auto decap = kem.decaps(encap.cipherText, keyPair.secretKey);
			if (!std::equal(encap.sharedSecret.begin(), encap.sharedSecret.end(), decap.begin()))
			{
				throw UnitTestFail("MlKem shared secrets do not match");
			}
		}
	}

	void MlKem_Backend_Test_Impl()
	{
		checkMode(MlKem::Mode::MlKem512, 800, 1632, 768);
		checkMode(MlKem::Mode::MlKem768, 1184, 2400, 1088);
		checkMode(MlKem::Mode::MlKem1024, 1568, 3168, 1568);
	}
}
