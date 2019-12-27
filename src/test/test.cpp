#include "libnet.h"
#include "libhttp.h"
#include <thread>
#include <string>

using namespace libnet;
using namespace libhttp;

bool g_terminate = false;
INetEngine* g_engine = nullptr;

class TestConnectSession : public ITcpSession {
	virtual int32_t OnRecv(const NetBuffer& buffer) {
		int32_t offset = 0;
		char c;
		while (buffer.Read(offset++, c)) {
			_data += c;
		}

		if (_data.back() == '\n') {
			printf("count %d : %s", _count, _data.c_str());

			Send(_data.c_str(), (int32_t)_data.size());
			_data.clear();

			if (++_count > 20)
				Close();
		}

		return offset - 1;
	}

	virtual void OnConnected() {
		std::string data;
		for (int32_t i = 0; i < 137; ++i)
			data += ('a' + (rand() % 26));
	
		Send(data.c_str(), (int32_t)data.size());
		AdjustSendBuffSize(2048);
		data = "\n";
		Send(data.c_str(), (int32_t)data.size());
	}

	virtual void OnConnectFailed() {}
	virtual void OnDisconnect() {
		
	}

	virtual void Release() {
		delete this;
	}

	std::string _data;
	int32_t _count = 0;
};

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
				else if (data == "test\n") {
					g_engine->Connect(new TestConnectSession, "127.0.0.1", 5500, 1024, 1024, true);
				}
				else {
					printf("recv %s\n", data.c_str());
					Send(data.c_str(), (int32_t)data.size());
					AdjustSendBuffSize(2048);
				}
				return offset;
			}
		}

		return 0;
	}

	virtual void OnConnected() {
		AdjustRecvBuffSize(2048);
	}
	virtual void OnConnectFailed() {}
	virtual void OnDisconnect() {
		printf("disconnect\n");
	}

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


class TestHttpSession : public ITcpSession {
public:
	TestHttpSession() {}
	~TestHttpSession() {}

	virtual int32_t OnRecv(const NetBuffer& buffer) {
		int32_t ret = ParseHttpRequest(buffer, _status, _req);
		if (_status == PS_COMPLETE) {
			
			//deal http

			if (!_req.keepAlive)
				Close();

			_status = PS_NONE;
			_req.Clear();
		}

		return ret;
	}

	virtual void OnConnected() {
	}
	virtual void OnConnectFailed() {}
	virtual void OnDisconnect() {
	}

	virtual void Release() {
		delete this;
	}

private:
	int8_t _status = 0;
	HttpRequest _req;
};

struct HttpServer : public ITcpServer {
	virtual ITcpSession* MallocConnection() {
		return new TestHttpSession;
	}
};

int main(int argc, char** argv) {
	std::string testStr = "POST /abc/iii HTTP1.1\r\nHost: www.test.com\r\nuser-agent : testIE 10.x \r\n "
		"Content-Type: application/x-www-form-urlencoded\r\nContent-Length";
	std::string testStr2 = ": 40\r\nConnectioN: keep-alive\r\n"
		"cookie: a=1;b = 2;c= 3; d= 5\r\n\r\n"
		"name=Professional%20Ajax&publisher=wiley";

	HttpRequest req;
	int8_t status = 0;
	int32_t parsed = ParseHttpRequest(libnet::NetBuffer((char*)testStr.c_str(), (int32_t)testStr.size(), nullptr, 0), status, req);
	printf("parsed %d/%d %d", (int32_t)testStr.size(), parsed, status);
	int32_t parsed2 = ParseHttpRequest(libnet::NetBuffer((char*)testStr.c_str() + parsed, (int32_t)testStr.size() - parsed, (char*)testStr2.c_str(), (int32_t)testStr2.size()), status, req);
	printf("parsed %d/%d %d", (int32_t)testStr2.size(), parsed2, status);


	srand((uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
	printf("sizeof(int64_t) %d\n", (int32_t)sizeof(int64_t));
	
	g_engine = CreateNetEngine(4);
	if (!g_engine) {
		return -1;
	}
	
	if (strcmp(argv[1], "server") == 0)
		g_engine->Listen(new TestServer, "0.0.0.0", 5500, 1024, 1024, true);
	if (strcmp(argv[1], "http") == 0)
		g_engine->Listen(new HttpServer, "0.0.0.0", 7999, 1024 * 1024, 1024 * 1024, false);
	else
		g_engine->Connect(new TestConnectSession, "127.0.0.1", 5500, 1024, 1024, true);
	
	while (!g_terminate) {
		g_engine->Poll(1);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	
	g_engine->Release();
	return 0;
}
