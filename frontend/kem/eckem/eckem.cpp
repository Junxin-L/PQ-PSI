#include "eckem.h"

#include "raw.h"

namespace osuCrypto
{
	KemSpec EcKem::spec()
	{
		return { "eckem", PkBytes, SkBytes, CtBytes, TagBytes };
	}

	void EcKem::random(std::array<u8, KeySeedBytes>& seed)
	{
		eckem_raw::random(seed.data(), seed.size());
	}

	std::array<u8, EcKem::PointBytes> EcKem::map(const std::array<u8, PointBytes>& rep)
	{
		std::array<u8, PointBytes> pk{};
		eckem_raw::map(pk.data(), rep.data());
		return pk;
	}

	std::array<u8, EcKem::TagBytes> EcKem::kdf(const std::array<u8, SecretBytes>& shared)
	{
		std::array<u8, TagBytes> out{};
		eckem_raw::kdf(out.data(), shared.data());
		return out;
	}

	EcKem::KeyPair EcKem::keyGen() const
	{
		std::array<u8, KeySeedBytes> seed{};
		random(seed);
		return keyGen(seed);
	}

	EcKem::KeyPair EcKem::keyGen(const std::array<u8, KeySeedBytes>& seed) const
	{
		KeyPair pair;
		eckem_raw::keyGen(pair.pk.data(), pair.sk.data(), seed.data());
		return pair;
	}

	EcKem::Encap EcKem::encap(const std::array<u8, PkBytes>& pk) const
	{
		std::array<u8, EncSeedBytes> seed{};
		random(seed);
		return encap(pk, seed);
	}

	EcKem::Encap EcKem::encap(
		const std::array<u8, PkBytes>& pk,
		const std::array<u8, EncSeedBytes>& seed) const
	{
		Encap out;
		eckem_raw::encap(out.ct.data(), out.tag.data(), pk.data(), seed.data());
		return out;
	}

	bool EcKem::decap(
		const std::array<u8, SkBytes>& sk,
		const std::array<u8, CtBytes>& ct,
		std::array<u8, TagBytes>& tag) const
	{
		return eckem_raw::decap(tag.data(), sk.data(), ct.data());
	}
}
