#include "ObfSession_Tests.h"

#include "../Common.h"
#include "libPSI/Tools/obf-mlkem/session/ObfSession.h"

#include <algorithm>

using namespace osuCrypto;

namespace tests_libOTe
{
	namespace
	{
		void checkMode(MlKem::Mode mode)
		{
			ObfSession sender(mode);
			ObfSession receiver(mode);

			ObfSession::Hello hello;
			if (!sender.makeHello(hello))
			{
				throw UnitTestFail("ObfSession failed to create hello");
			}

			ObfSession::Reply reply;
			if (!receiver.handleHello(hello.data, reply))
			{
				throw UnitTestFail("ObfSession failed to handle hello");
			}

			if (!sender.handleReply(reply.data))
			{
				throw UnitTestFail("ObfSession failed to handle reply");
			}

			const auto& a = sender.state();
			const auto& b = receiver.state();
			if (!a.ready || !b.ready)
			{
				throw UnitTestFail("ObfSession did not become ready");
			}

			if (!std::equal(a.sessionKey.begin(), a.sessionKey.end(), b.sessionKey.begin()))
			{
				throw UnitTestFail("ObfSession session keys do not match");
			}

			if (a.transcript != b.transcript)
			{
				throw UnitTestFail("ObfSession transcripts do not match");
			}

			std::vector<u8> badReply = reply.data;
			if (!badReply.empty())
			{
				badReply.pop_back();
				ObfSession again(mode);
				ObfSession::Hello hello2;
				if (!again.makeHello(hello2))
				{
					throw UnitTestFail("ObfSession failed to create second hello");
				}
				if (again.handleReply(badReply))
				{
					throw UnitTestFail("ObfSession accepted a short reply");
				}
			}

			std::vector<u8> badHello = hello.data;
			if (!badHello.empty())
			{
				badHello.pop_back();
				ObfSession third(mode);
				ObfSession::Reply badReply2;
				if (third.handleHello(badHello, badReply2))
				{
					throw UnitTestFail("ObfSession accepted a short hello");
				}
			}

			ObfSession empty(mode);
			if (empty.handleReply(reply.data))
			{
				throw UnitTestFail("ObfSession accepted a reply before hello");
			}
		}
	}

	void ObfSession_Test_Impl()
	{
		checkMode(MlKem::Mode::MlKem512);
		checkMode(MlKem::Mode::MlKem768);
		checkMode(MlKem::Mode::MlKem1024);
	}
}
