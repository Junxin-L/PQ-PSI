#pragma once

#include "cryptoTools/Common/Defines.h"
#include "../common/SpanLite.h"

#include <array>
#include <vector>

namespace osuCrypto
{
	class MlKem
	{
	public:
		enum class Mode : u16
		{
			MlKem512 = 512,
			MlKem768 = 768,
			MlKem1024 = 1024
		};

		static constexpr u64 SharedSecretSize = 32;
		static constexpr u64 EncapSeedSize = 32;
		static constexpr u64 KeyGenSeedSize = 64;

		struct Sizes
		{
			u64 publicKeyBytes;
			u64 secretKeyBytes;
			u64 cipherTextBytes;
		};

		// Key pair bytes
		struct KeyPair
		{
			std::vector<u8> publicKey;
			std::vector<u8> secretKey;
		};

		// Encapsulation output∂
		struct EncapResult
		{
			std::vector<u8> cipherText;
			std::array<u8, SharedSecretSize> sharedSecret;
		};

		explicit MlKem(Mode mode = Mode::MlKem768);

		void setMode(Mode mode);
		Mode mode() const;

		Sizes sizes() const;
		u64 publicKeyBytes() const;
		u64 secretKeyBytes() const;
		u64 cipherTextBytes() const;

		KeyPair keyGen() const;
		KeyPair keyGen(span<const u8> seed) const;
		void keyGen(span<const u8> seed, span<u8> publicKey, span<u8> secretKey) const;

		EncapResult encaps(span<const u8> publicKey) const;
		EncapResult encaps(span<const u8> publicKey, span<const u8> seed) const;
		void encaps(
			span<const u8> publicKey,
			span<const u8> seed,
			span<u8> cipherText,
			span<u8> sharedSecret) const;

		std::array<u8, SharedSecretSize> decaps(span<const u8> cipherText, span<const u8> secretKey) const;

	private:

		static Sizes getSizes(Mode mode);
		static void checkSeedSize(span<const u8> seed, u64 expectedSize, const char* label);

		Mode mMode;
		Sizes mSizes;
	};
}
