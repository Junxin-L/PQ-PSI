#pragma once

#include "cons.h"
#include "hctr2.h"

#include <memory>
#include <stdexcept>
#include <string>

namespace pqperm
{
	enum class Kind
	{
		ConsPi,
		Hctr,
	};

	struct Cfg
	{
		Kind kind = Kind::ConsPi;
		pi::Kind small = pi::Kind::Keccak1600;
		size_t lambda = 128;
	};

	inline const char* name(Kind kind)
	{
		switch (kind)
		{
		case Kind::ConsPi:
			return "conspi";
		case Kind::Hctr:
			return "hctr";
		default:
			return "unknown";
		}
	}

	inline void set(Cfg& cfg, const std::string& text)
	{
		if (text == "conspi" || text == "cons-pi" || text == "pi")
		{
			cfg.kind = Kind::ConsPi;
			return;
		}
		if (text == "hctr" || text == "hctr2")
		{
			cfg.kind = Kind::Hctr;
			return;
		}

		cfg.kind = Kind::ConsPi;
		cfg.small = pi::parseKind(text);
	}

	inline std::unique_ptr<Perm> make(const Cfg& cfg, u8 party = 0)
	{
		switch (cfg.kind)
		{
		case Kind::ConsPi:
			return std::unique_ptr<Perm>(new ConsPi(pi::defaults(cfg.small, KEM_key_size_bit, cfg.lambda), party));
		case Kind::Hctr:
			return std::unique_ptr<Perm>(new Hctr(party));
		default:
			throw std::invalid_argument("unknown PQ-PSI permutation");
		}
	}
}
