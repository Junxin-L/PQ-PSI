#pragma once

#include "sneik.h"
#include "types.h"
#include "../../../thirdparty/KeccakTools/Sources/Keccak-f.h"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace pi
{
	enum class Kind
	{
		Keccak800,
		Keccak1600,
		Keccak1600R12,
		SneikF512,
	};

	inline const char* name(Kind kind)
	{
		switch (kind)
		{
		case Kind::Keccak800:
			return "keccak800";
		case Kind::Keccak1600:
			return "keccak1600";
		case Kind::Keccak1600R12:
			return "keccak1600-12";
		case Kind::SneikF512:
			return "sneik-f512";
		default:
			return "unknown";
		}
	}

	inline Kind parseKind(const std::string& text)
	{
		if (text == "keccak800" || text == "k800")
		{
			return Kind::Keccak800;
		}
		if (text == "keccak1600" || text == "k1600")
		{
			return Kind::Keccak1600;
		}
		if (text == "keccak1600-12" || text == "keccak-p1600-12" ||
			text == "k1600-12" || text == "k1600r12")
		{
			return Kind::Keccak1600R12;
		}
		if (text == "sneik" || text == "sneik512" || text == "sneik-f512" || text == "f512")
		{
			return Kind::SneikF512;
		}
		throw std::invalid_argument("unknown pi permutation: " + text);
	}

	class Perm
	{
	public:
		using Buf = std::vector<UINT8>;

		virtual ~Perm() = default;
		virtual Kind kind() const = 0;
		virtual size_t bits() const = 0;
		virtual size_t rounds() const = 0;
		virtual void apply(Buf& buf) const = 0;
		virtual void invert(Buf& buf) const = 0;

		size_t bytes() const
		{
			return bits() / 8;
		}

		const char* label() const
		{
			return name(kind());
		}
	};

	class Keccak final : public Perm
	{
	public:
		explicit Keccak(size_t widthBits, size_t rounds = 0)
			: width_(widthBits)
			, rounds_(rounds == 0 ? nominalRounds(widthBits) : rounds)
		{
			if (width_ != 800 && width_ != 1600)
			{
				throw std::invalid_argument("Keccak adapter supports 800 or 1600 bits");
			}
			if (rounds_ == 0 || rounds_ > nominalRounds(width_))
			{
				throw std::invalid_argument("Keccak round count is invalid");
			}
		}

		Kind kind() const override
		{
			if (width_ == 1600 && rounds_ == 12)
			{
				return Kind::Keccak1600R12;
			}
			return (width_ == 800) ? Kind::Keccak800 : Kind::Keccak1600;
		}

		size_t bits() const override
		{
			return width_;
		}

		size_t rounds() const override
		{
			return rounds_;
		}

		void apply(Buf& buf) const override
		{
			check(buf);
			inst()(buf.data());
		}

		void invert(Buf& buf) const override
		{
			check(buf);
			inst().inverse(buf.data());
		}

	private:
		size_t width_ = 0;
		size_t rounds_ = 0;

		static size_t nominalRounds(size_t width)
		{
			switch (width)
			{
			case 800:
				return 22;
			case 1600:
				return 24;
			default:
				throw std::invalid_argument("unknown Keccak width");
			}
		}

		void check(const Buf& buf) const
		{
			if (buf.size() != bytes())
			{
				throw std::invalid_argument("Keccak buffer size mismatch");
			}
		}

		const KeccakF& inst() const
		{
			if (width_ == 800)
			{
				static thread_local const KeccakF k800(800);
				return k800;
			}
			if (rounds_ == 12)
			{
				static thread_local const KeccakP k1600r12(1600, 12);
				return k1600r12;
			}

			static thread_local const KeccakF k1600(1600);
			return k1600;
		}
	};

	class Sneik final : public Perm
	{
	public:
		Kind kind() const override
		{
			return Kind::SneikF512;
		}

		size_t bits() const override
		{
			return sneik::WidthBits;
		}

		size_t rounds() const override
		{
			return sneik::Rounds;
		}

		void apply(Buf& buf) const override
		{
			check(buf);
			sneik_f512(buf.data(), sneik::Domain, sneik::Rounds);
		}

		void invert(Buf& buf) const override
		{
			check(buf);
			sneik::invert(buf.data(), sneik::Domain, sneik::Rounds);
		}

	private:
		static void check(const Buf& buf)
		{
			if (buf.size() != sneik::WidthBytes)
			{
				throw std::invalid_argument("SNEIK-f512 buffer size mismatch");
			}
		}
	};

	inline std::unique_ptr<Perm> makePerm(Kind kind)
	{
		switch (kind)
		{
		case Kind::Keccak800:
			return std::unique_ptr<Perm>(new Keccak(800));
		case Kind::Keccak1600:
			return std::unique_ptr<Perm>(new Keccak(1600));
		case Kind::Keccak1600R12:
			return std::unique_ptr<Perm>(new Keccak(1600, 12));
		case Kind::SneikF512:
			return std::unique_ptr<Perm>(new Sneik());
		default:
			throw std::invalid_argument("unknown pi adapter");
		}
	}
}
