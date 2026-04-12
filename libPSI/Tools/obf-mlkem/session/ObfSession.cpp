#include "ObfSession.h"

#include <sodium.h>

#include <stdexcept>

namespace osuCrypto
{
	namespace
	{
		void writeU64(std::vector<u8>& out, u64 x)
		{
			for (u64 i = 0; i < 8; ++i)
			{
				out.push_back(static_cast<u8>((x >> (8 * i)) & 0xFF));
			}
		}
	}

	ObfSession::ObfSession(MlKem::Mode mode)
		: mKem(mode)
		, mCodec(mode)
	{
		reset();
	}

	void ObfSession::setMode(MlKem::Mode mode)
	{
		mKem.setMode(mode);
		mCodec.setMode(mode);
		reset();
	}

	MlKem::Mode ObfSession::mode() const
	{
		return mKem.mode();
	}

	void ObfSession::reset()
	{
		mLocalKeys.publicKey.clear();
		mLocalKeys.secretKey.clear();
		mState = State{};
		mState.sharedSecret.fill(0);
		mState.sessionKey.fill(0);
	}

	bool ObfSession::makeHello(Hello& hello)
	{
		reset();
		for (u64 i = 0; i < maxHelloTries; ++i)
		{
			mLocalKeys = mKem.keyGen();
			if (mCodec.encodeKey(mLocalKeys.publicKey, hello.data))
			{
				addPart(1, hello.data);
				mState.hasHello = true;
				return true;
			}
		}

		reset();
		return false;
	}

	bool ObfSession::handleHello(span<const u8> helloData, Reply& reply)
	{
		reset();

		std::vector<u8> peerKey;
		if (!mCodec.decodeKey(helloData, peerKey))
		{
			return false;
		}

		addPart(1, helloData);
		mState.hasHello = true;

		auto enc = mKem.encaps(peerKey);
		mState.sharedSecret = enc.sharedSecret;

		for (u64 i = 0; i < maxReplyTries; ++i)
		{
			if (mCodec.encodeCipher(enc.cipherText, reply.data))
			{
				addPart(2, reply.data);
				mState.hasReply = true;
				makeKey();
				return true;
			}
		}

		reset();
		return false;
	}

	bool ObfSession::handleReply(span<const u8> replyData)
	{
		if (!mState.hasHello || mLocalKeys.secretKey.empty())
		{
			return false;
		}

		std::vector<u8> cipher;
		if (!mCodec.decodeCipher(replyData, cipher))
		{
			return false;
		}

		mState.sharedSecret = mKem.decaps(cipher, mLocalKeys.secretKey);
		addPart(2, replyData);
		mState.hasReply = true;
		makeKey();
		return true;
	}

	const ObfSession::State& ObfSession::state() const
	{
		return mState;
	}

	void ObfSession::addPart(u8 tag, span<const u8> data)
	{
		mState.transcript.push_back(tag);
		writeU64(mState.transcript, data.size());
		mState.transcript.insert(mState.transcript.end(), data.begin(), data.end());
	}

	void ObfSession::makeKey()
	{
		static const u8 label[] = "obf-mlkem-session-v1";
		crypto_generichash_state st;
		crypto_generichash_init(&st, nullptr, 0, SessionKeySize);
		crypto_generichash_update(&st, label, sizeof(label) - 1);
		crypto_generichash_update(&st, mState.sharedSecret.data(), mState.sharedSecret.size());
		crypto_generichash_update(&st, mState.transcript.data(), mState.transcript.size());
		crypto_generichash_final(&st, mState.sessionKey.data(), mState.sessionKey.size());
		mState.ready = true;
	}
}
