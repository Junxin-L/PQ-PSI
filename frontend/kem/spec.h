#pragma once

#include "cryptoTools/Common/Defines.h"

#include <string>

namespace osuCrypto
{
	enum class KemKind : u8
	{
		ObfMlKem,
		EcKem
	};

	struct KemSpec
	{
		std::string name;
		u64 pkBytes = 0;
		u64 skBytes = 0;
		u64 ctBytes = 0;
		u64 ssBytes = 0;
	};
}
