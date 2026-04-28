#pragma once

#include "types.h"

#include <cstddef>

namespace pqperm
{
	class Perm
	{
	public:
		virtual ~Perm() = default;

		virtual const char* name() const = 0;
		virtual const char* detail() const = 0;
		virtual size_t n() const = 0;
		virtual size_t s() const = 0;
		virtual size_t rounds() const = 0;
		virtual void encryptBytes(u8* data, size_t bytes) const = 0;
		virtual void decryptBytes(u8* data, size_t bytes) const = 0;
	};
}
