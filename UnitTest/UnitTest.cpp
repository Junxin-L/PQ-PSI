#include "pch.h"
#include "CppUnitTest.h"
#include "Common.h"
//#include "EQ_Tests.h"
#include "OT_Tests.h"
#include "nPSIv2.h"
//#include "OPPRF_Tests.h"
#include "frontend/permutation.h"

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
				throw UnitTestFail("ConstructionPermutation round-trip failed");
			}
		}
	};
}
