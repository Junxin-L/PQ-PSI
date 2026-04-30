#pragma once

#include "api.h"
#include "xoodoo_inv.h"

extern "C"
{
#include "Xoodoo-plain.h"
}

#include <array>
#include <cstring>

namespace pqperm
{
	class Xoodoo final : public Perm
	{
	public:
		static constexpr size_t Bytes = 48;
		static constexpr size_t Bits = Bytes * 8;
		static constexpr size_t Rounds = 12;

		const char* name() const override { return "xoodoo"; }
		const char* detail() const override { return "xoodoo-12"; }
		size_t n() const override { return Bits; }
		size_t s() const override { return 0; }
		size_t rounds() const override { return Rounds; }

		void encryptBytes(u8* data, size_t bytes) const override
		{
			xoodoo_inv::check(bytes);
			Xoodoo_plain32_state state;
			std::memcpy(&state, data, Bytes);
			Xoodoo_plain_Permute_12rounds(&state);
			std::memcpy(data, &state, Bytes);
		}

		void decryptBytes(u8* data, size_t bytes) const override
		{
			xoodoo_inv::check(bytes);
			std::array<u32, 12> a{};
			std::memcpy(a.data(), data, Bytes);
			xoodoo_inv::run(a);
			std::memcpy(data, a.data(), Bytes);
		}
	};
}
