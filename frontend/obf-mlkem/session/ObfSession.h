#pragma once

#include "../backend/MlKem.h"
#include "../codec/Kemeleon.h"
#include "cryptoTools/Common/Defines.h"

#include <array>
#include <vector>

namespace osuCrypto
{
	class ObfSession
	{
	public:
		static constexpr u64 SharedSecretSize = MlKem::SharedSecretSize;
		static constexpr u64 SessionKeySize = 32;

		struct Hello
		{
			std::vector<u8> data;
		};

		struct Reply
		{
			std::vector<u8> data;
		};

		struct State
		{
			bool hasHello = false;
			bool hasReply = false;
			bool ready = false;
			std::vector<u8> transcript;
			std::array<u8, SharedSecretSize> sharedSecret;
			std::array<u8, SessionKeySize> sessionKey;
		};

		explicit ObfSession(MlKem::Mode mode = MlKem::Mode::MlKem768);

		void setMode(MlKem::Mode mode);
		MlKem::Mode mode() const;

		void reset();

		bool makeHello(Hello& hello);
		bool handleHello(span<const u8> helloData, Reply& reply);
		bool handleReply(span<const u8> replyData);

		const State& state() const;

	private:
		static constexpr u64 maxHelloTries = 8;
		static constexpr u64 maxReplyTries = 32;

		// Transcript framing.
		void addPart(u8 tag, span<const u8> data);
		void makeKey();

		MlKem mKem;
		Kemeleon mCodec;
		MlKem::KeyPair mLocalKeys;
		State mState;
	};
}
