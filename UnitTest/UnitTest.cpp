#include "pch.h"
#include "CppUnitTest.h"
#include "Common.h"
//#include "EQ_Tests.h"
//#include "OPPRF_Tests.h"
#include "UnitTest/obf-mlkem/Kemeleon_Tests.h"
#include "UnitTest/obf-mlkem/MlKem_Tests.h"
#include "frontend/kem/eckem/eckem.h"
#include "frontend/permutation/cons.h"
#include "frontend/pqpsi/pqpsi.h"

#include <array>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace osuCrypto;
using namespace tests_pqpsi;

namespace
{
	void EcKem_Test_Impl()
	{
		EcKem kem;
		const auto spec = EcKem::spec();
		if (spec.pkBytes != 32 || spec.skBytes != 32 || spec.ctBytes != 48 || spec.ssBytes != 16)
		{
			throw UnitTestFail("EcKem size contract changed");
		}

		std::array<u8, EcKem::KeySeedBytes> keySeed{};
		std::array<u8, EcKem::KeySeedBytes> otherSeed{};
		std::array<u8, EcKem::EncSeedBytes> encSeed{};
		for (u64 i = 0; i < keySeed.size(); ++i)
		{
			keySeed[i] = static_cast<u8>(0x11 + i);
			otherSeed[i] = static_cast<u8>(0x52 + i);
			encSeed[i] = static_cast<u8>(0x93 + i);
		}

		const auto pair = kem.keyGen(keySeed);
		const auto other = kem.keyGen(otherSeed);
		const auto enc = kem.encap(pair.pk, encSeed);

		std::array<u8, EcKem::TagBytes> tag{};
		if (!kem.decap(pair.sk, enc.ct, tag) || tag != enc.tag)
		{
			throw UnitTestFail("EcKem decapsulation failed");
		}
		if (kem.decap(other.sk, enc.ct, tag))
		{
			throw UnitTestFail("EcKem accepted ciphertext under the wrong key");
		}

		auto bad = enc.ct;
		bad[0] ^= 1;
		if (kem.decap(pair.sk, bad, tag))
		{
			throw UnitTestFail("EcKem accepted corrupted ciphertext");
		}
	}
}

namespace UnitTest
{
	TEST_CLASS(UnitTest)
	{
	public:
		
		TEST_METHOD(TestPermutationRoundTrip)
		{
			InitDebugPrinting();

			// round trip guard
			const int rc = permutation_Test();
			if (rc != 0)
			{
				throw UnitTestFail("Pi round-trip failed");
			}
		}

		TEST_METHOD(TestMlKemBackend)
		{
			InitDebugPrinting();
			MlKem_Backend_Test_Impl();
		}

		TEST_METHOD(TestKemeleonCodec)
		{
			InitDebugPrinting();
			Kemeleon_Test_Impl();
		}

		TEST_METHOD(TestEcKem)
		{
			InitDebugPrinting();
			EcKem_Test_Impl();
		}

		TEST_METHOD(TestPqPsiRbOkvsWithNetwork)
		{
			InitDebugPrinting();
			u64 got = 0, expected = 0;
			const bool ok = rbCheck(got, expected);
			std::wstringstream ss;
			ss << L"rbokvs+pqpsi mismatch: expected " << expected << L", got " << got;
			Assert::IsTrue(ok, ss.str().c_str());
		}
	};
}
