#pragma once

#include "frontend/kem/spec.h"

#include <array>

namespace osuCrypto
{
	class EcKem
	{
	public:
		static constexpr u64 PointBytes = 32;
		static constexpr u64 SecretBytes = 32;
		static constexpr u64 TagBytes = 16;
		static constexpr u64 PkBytes = PointBytes;
		static constexpr u64 SkBytes = SecretBytes;
		static constexpr u64 CtBytes = PointBytes + TagBytes;
		static constexpr u64 KeySeedBytes = 32;
		static constexpr u64 EncSeedBytes = 32;

		struct KeyPair
		{
			// Elligator representative of pk = g^sk.
			std::array<u8, PkBytes> pk{};
			std::array<u8, SkBytes> sk{};
		};

		struct Encap
		{
			// rep(R = g^r) || H(pk^r).
			std::array<u8, CtBytes> ct{};
			std::array<u8, TagBytes> tag{};
		};

		static KemSpec spec();

		KeyPair keyGen() const;
		KeyPair keyGen(const std::array<u8, KeySeedBytes>& seed) const;

		Encap encap(const std::array<u8, PkBytes>& pk) const;
		Encap encap(
			const std::array<u8, PkBytes>& pk,
			const std::array<u8, EncSeedBytes>& seed) const;

		bool decap(
			const std::array<u8, SkBytes>& sk,
			const std::array<u8, CtBytes>& ct,
			std::array<u8, TagBytes>& tag) const;

	private:
		static void random(std::array<u8, KeySeedBytes>& seed);
		static std::array<u8, PointBytes> map(const std::array<u8, PointBytes>& rep);
		static std::array<u8, TagBytes> kdf(const std::array<u8, SecretBytes>& shared);
	};
}
