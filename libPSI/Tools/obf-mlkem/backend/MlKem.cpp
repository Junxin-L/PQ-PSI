#include "MlKem.h"

#include "../native/mlkem_native_all.h"

#include <sodium.h>

#include <stdexcept>

namespace osuCrypto
{
	namespace
	{
		void requireSize(span<const u8> input, u64 expectedSize, const char* label)
		{
			if (input.size() != expectedSize)
			{
				throw std::invalid_argument(std::string(label) + " has unexpected size");
			}
		}

		void requireSuccess(int rc, const char* label)
		{
			if (rc != 0)
			{
				throw std::runtime_error(std::string(label) + " failed");
			}
		}
	}

	MlKem::MlKem(Mode mode)
		: mMode(mode)
		, mSizes(getSizes(mode))
	{
	}

	void MlKem::setMode(Mode mode)
	{
		mMode = mode;
		mSizes = getSizes(mode);
	}

	MlKem::Mode MlKem::mode() const
	{
		return mMode;
	}

	MlKem::Sizes MlKem::sizes() const
	{
		return mSizes;
	}

	u64 MlKem::publicKeyBytes() const
	{
		return mSizes.publicKeyBytes;
	}

	u64 MlKem::secretKeyBytes() const
	{
		return mSizes.secretKeyBytes;
	}

	u64 MlKem::cipherTextBytes() const
	{
		return mSizes.cipherTextBytes;
	}

	MlKem::KeyPair MlKem::keyGen() const
	{
		std::array<u8, KeyGenSeedSize> seed;
		randombytes_buf(seed.data(), seed.size());
		return keyGen(seed);
	}

	MlKem::KeyPair MlKem::keyGen(span<const u8> seed) const
	{
		checkSeedSize(seed, KeyGenSeedSize, "MlKem::keyGen seed");

		KeyPair keyPair;
		keyPair.publicKey.resize(publicKeyBytes());
		keyPair.secretKey.resize(secretKeyBytes());

		switch (mMode)
		{
		case Mode::MlKem512:
			requireSuccess(
				mlkem512_keypair_derand(keyPair.publicKey.data(), keyPair.secretKey.data(), seed.data()),
				"MlKem512 keypair_derand");
			break;
		case Mode::MlKem768:
			requireSuccess(
				mlkem768_keypair_derand(keyPair.publicKey.data(), keyPair.secretKey.data(), seed.data()),
				"MlKem768 keypair_derand");
			break;
		case Mode::MlKem1024:
			requireSuccess(
				mlkem1024_keypair_derand(keyPair.publicKey.data(), keyPair.secretKey.data(), seed.data()),
				"MlKem1024 keypair_derand");
			break;
		default:
			throw std::invalid_argument("Unsupported ML-KEM mode");
		}

		return keyPair;
	}

	MlKem::EncapResult MlKem::encaps(span<const u8> publicKey) const
	{
		std::array<u8, EncapSeedSize> seed;
		randombytes_buf(seed.data(), seed.size());
		return encaps(publicKey, seed);
	}

	MlKem::EncapResult MlKem::encaps(span<const u8> publicKey, span<const u8> seed) const
	{
		requireSize(publicKey, publicKeyBytes(), "MlKem public key");
		checkSeedSize(seed, EncapSeedSize, "MlKem::encaps seed");

		EncapResult result;
		result.cipherText.resize(cipherTextBytes());

		switch (mMode)
		{
		case Mode::MlKem512:
			requireSuccess(
				mlkem512_enc_derand(result.cipherText.data(), result.sharedSecret.data(), publicKey.data(), seed.data()),
				"MlKem512 enc_derand");
			break;
		case Mode::MlKem768:
			requireSuccess(
				mlkem768_enc_derand(result.cipherText.data(), result.sharedSecret.data(), publicKey.data(), seed.data()),
				"MlKem768 enc_derand");
			break;
		case Mode::MlKem1024:
			requireSuccess(
				mlkem1024_enc_derand(result.cipherText.data(), result.sharedSecret.data(), publicKey.data(), seed.data()),
				"MlKem1024 enc_derand");
			break;
		default:
			throw std::invalid_argument("Unsupported ML-KEM mode");
		}

		return result;
	}

	std::array<u8, MlKem::SharedSecretSize> MlKem::decaps(span<const u8> cipherText, span<const u8> secretKey) const
	{
		requireSize(cipherText, cipherTextBytes(), "MlKem ciphertext");
		requireSize(secretKey, secretKeyBytes(), "MlKem secret key");

		std::array<u8, SharedSecretSize> sharedSecret;

		switch (mMode)
		{
		case Mode::MlKem512:
			requireSuccess(
				mlkem512_dec(sharedSecret.data(), cipherText.data(), secretKey.data()),
				"MlKem512 dec");
			break;
		case Mode::MlKem768:
			requireSuccess(
				mlkem768_dec(sharedSecret.data(), cipherText.data(), secretKey.data()),
				"MlKem768 dec");
			break;
		case Mode::MlKem1024:
			requireSuccess(
				mlkem1024_dec(sharedSecret.data(), cipherText.data(), secretKey.data()),
				"MlKem1024 dec");
			break;
		default:
			throw std::invalid_argument("Unsupported ML-KEM mode");
		}

		return sharedSecret;
	}

	MlKem::Sizes MlKem::getSizes(Mode mode)
	{
		switch (mode)
		{
		case Mode::MlKem512:
			return { MLKEM512_PUBLICKEYBYTES, MLKEM512_SECRETKEYBYTES, MLKEM512_CIPHERTEXTBYTES };
		case Mode::MlKem768:
			return { MLKEM768_PUBLICKEYBYTES, MLKEM768_SECRETKEYBYTES, MLKEM768_CIPHERTEXTBYTES };
		case Mode::MlKem1024:
			return { MLKEM1024_PUBLICKEYBYTES, MLKEM1024_SECRETKEYBYTES, MLKEM1024_CIPHERTEXTBYTES };
		default:
			throw std::invalid_argument("Unsupported ML-KEM mode");
		}
	}

	void MlKem::checkSeedSize(span<const u8> seed, u64 expectedSize, const char* label)
	{
		requireSize(seed, expectedSize, label);
	}
}
