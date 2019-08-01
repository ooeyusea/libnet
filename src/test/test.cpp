#include "libnet.h"
#include <thread>
#include <string>

using namespace libnet;

bool g_terminate = false;

class TestSession : public ITcpSession {
public:
	TestSession() {}
	~TestSession() {}

	virtual int32_t OnRecv(const NetBuffer& buffer) {
		std::string data;
		data.reserve(buffer.Size());

		int32_t offset = 0;
		char c;
		while (buffer.Read(offset++, c)) {
			if (c != '\r')
				data += c;

			if (c == '\n') {
				if (data == "quit\n")
					g_terminate = true;
				else if (data == "close\n") {
					Send(data.c_str(), (int32_t)data.size());
					Close();
				}
				else {
					printf("recv %s\n", data.c_str());
					Send(data.c_str(), (int32_t)data.size());
				}
				return offset;
			}
		}

		return 0;
	}

	virtual void OnConnected() {}
	virtual void OnConnectFailed() {}
	virtual void OnDisconnect() {}

	virtual void Release() {
		delete this;
	}

private:

};

struct TestServer : public ITcpServer {
	virtual ITcpSession* MallocConnection() {
		return new TestSession;
	}
};

int main(int argc, char** argv) {
	INetEngine* engine = CreateNetEngine();
	if (!engine) {
		return -1;
	}

	engine->Listen(new TestServer, "0.0.0.0", 5500, 1024, 1024);
	while (!g_terminate) {
		engine->Poll(1);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	engine->Release();
	return 0;
}
