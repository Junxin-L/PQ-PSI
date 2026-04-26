#include "pch.h"
#include "CppUnitTest.h"
#include "Common.h"
//#include "EQ_Tests.h"
#include "OT_Tests.h"
#include "nPSIv2.h"
//#include "OPPRF_Tests.h"
#include "frontend/pqpsi/pi.h"
#include "frontend/pqpsi/pqpsi.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTest
{
	TEST_CLASS(UnitTest)
	{
	public:
		
		TEST_METHOD(TestChannel)
		{
			InitDebugPrinting();
			//EQ_EmptrySet_Test_Impl();
			O1nPSI_Test();
		}

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
