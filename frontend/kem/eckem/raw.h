#pragma once

#include "eckem.h"

#include "thirdparty/monocypher/src/monocypher.h"

#include <sodium.h>

#include <array>
#include <cstring>
#include <stdexcept>

namespace osuCrypto::eckem_raw
{
	inline void init()
	{
		static const bool ok = []()
		{
			return sodium_init() >= 0;
		}();

		if (!ok)
		{
			throw std::runtime_error("eckem: libsodium init failed");
		}
	}

	inline void random(u8* out, size_t n)
	{
		init();
		randombytes_buf(out, n);
	}

	inline void map(u8 curve[EcKem::PointBytes], const u8 rep[EcKem::PointBytes])
	{
		crypto_elligator_map(curve, rep);
	}

	inline void kdf(u8 tag[EcKem::TagBytes], const u8 shared[EcKem::SecretBytes])
	{
		static constexpr u8 kLabel[] = {
			'e', 'c', 'k', 'e', 'm', '/', 't', 'a', 'g'
		};

		crypto_blake2b_ctx ctx;
		crypto_blake2b_init(&ctx, EcKem::TagBytes);
		crypto_blake2b_update(&ctx, kLabel, sizeof(kLabel));
		crypto_blake2b_update(&ctx, shared, EcKem::SecretBytes);
		crypto_blake2b_final(&ctx, tag);
	}

	inline void keyGen(
		u8 pk[EcKem::PkBytes],
		u8 sk[EcKem::SkBytes],
		const u8 seed[EcKem::KeySeedBytes])
	{
		std::array<u8, EcKem::KeySeedBytes> seedCopy{};
		std::memcpy(seedCopy.data(), seed, seedCopy.size());
		crypto_elligator_key_pair(pk, sk, seedCopy.data());
	}

	inline void keyGen(u8 pk[EcKem::PkBytes], u8 sk[EcKem::SkBytes])
	{
		std::array<u8, EcKem::KeySeedBytes> seed{};
		random(seed.data(), seed.size());
		crypto_elligator_key_pair(pk, sk, seed.data());
	}

	inline void encap(
		u8 ct[EcKem::CtBytes],
		u8 tag[EcKem::TagBytes],
		const u8 pk[EcKem::PkBytes],
		const u8 seed[EcKem::EncSeedBytes])
	{
		std::array<u8, EcKem::PointBytes> pkCurve{};
		std::array<u8, EcKem::PointBytes> repR{};
		std::array<u8, EcKem::SecretBytes> skR{};
		std::array<u8, EcKem::SecretBytes> shared{};
		std::array<u8, EcKem::EncSeedBytes> seedCopy{};

		map(pkCurve.data(), pk);
		std::memcpy(seedCopy.data(), seed, seedCopy.size());
		crypto_elligator_key_pair(repR.data(), skR.data(), seedCopy.data());
		crypto_x25519(shared.data(), skR.data(), pkCurve.data());
		kdf(tag, shared.data());

		std::memcpy(ct, repR.data(), repR.size());
		std::memcpy(ct + repR.size(), tag, EcKem::TagBytes);
	}

	inline void encap(
		u8 ct[EcKem::CtBytes],
		u8 tag[EcKem::TagBytes],
		const u8 pk[EcKem::PkBytes])
	{
		std::array<u8, EcKem::PointBytes> pkCurve{};
		std::array<u8, EcKem::PointBytes> repR{};
		std::array<u8, EcKem::SecretBytes> skR{};
		std::array<u8, EcKem::SecretBytes> shared{};
		std::array<u8, EcKem::EncSeedBytes> seed{};

		random(seed.data(), seed.size());
		map(pkCurve.data(), pk);
		crypto_elligator_key_pair(repR.data(), skR.data(), seed.data());
		crypto_x25519(shared.data(), skR.data(), pkCurve.data());
		kdf(tag, shared.data());

		std::memcpy(ct, repR.data(), repR.size());
		std::memcpy(ct + repR.size(), tag, EcKem::TagBytes);
	}

	inline void encap(u8 ct[EcKem::CtBytes], const u8 pk[EcKem::PkBytes])
	{
		std::array<u8, EcKem::TagBytes> tag{};
		encap(ct, tag.data(), pk);
	}

	inline bool decap(
		u8 tag[EcKem::TagBytes],
		const u8 sk[EcKem::SkBytes],
		const u8 ct[EcKem::CtBytes])
	{
		std::array<u8, EcKem::PointBytes> rCurve{};
		std::array<u8, EcKem::SecretBytes> shared{};

		map(rCurve.data(), ct);
		crypto_x25519(shared.data(), sk, rCurve.data());
		kdf(tag, shared.data());

		return crypto_verify16(tag, ct + EcKem::PointBytes) == 0;
	}

	inline bool decap(const u8 sk[EcKem::SkBytes], const u8 ct[EcKem::CtBytes])
	{
		std::array<u8, EcKem::TagBytes> tag{};
		return decap(tag.data(), sk, ct);
	}
}
