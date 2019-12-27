#include "net.h"
#include <mswsock.h> 
#include <ws2tcpip.h>
#include <thread>
#include <atomic>
#include "Connection.h"

#define NET_INIT_FRAME 1024
#define MAX_NET_THREAD 4
#define BACKLOG 128
#define LOCAL_IP "127.0.0.1"

LPFN_ACCEPTEX GetAcceptExFunc() {
	GUID acceptExGuild = WSAID_ACCEPTEX;
	DWORD bytes = 0;
	SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	LPFN_ACCEPTEX acceptFunc = nullptr;

	WSAIoctl(sock,
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&acceptExGuild,
		sizeof(acceptExGuild),
		&acceptFunc,
		sizeof(acceptFunc),
		&bytes, nullptr, nullptr);

	if (nullptr == acceptFunc) {
		LIBNET_ASSERT(false, "Get AcceptEx fun error, error code : %d", WSAGetLastError());
	}

	return acceptFunc;
}

LPFN_CONNECTEX GetConnectExFunc() {
	GUID conectExFunc = WSAID_CONNECTEX;
	DWORD bytes = 0;
	SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	LPFN_CONNECTEX connectFunc = nullptr;

	WSAIoctl(sock,
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&conectExFunc,
		sizeof(conectExFunc),
		&connectFunc,
		sizeof(connectFunc),
		&bytes, nullptr, nullptr);

	if (nullptr == connectFunc) {
		LIBNET_ASSERT(false, "Get ConnectEx fun error, error code : %d", WSAGetLastError());
	}

	return connectFunc;
}

LPFN_ACCEPTEX g_accept = nullptr;
LPFN_CONNECTEX g_connect = nullptr;

namespace libnet {
	NetEngine::NetEngine(HANDLE completionPort, int32_t threadCount) : _completionPort(completionPort) {
		for (int32_t i = 0; i < threadCount; ++i) {
			std::thread([this]() {
				ThreadProc();
			}).detach();
		}

		_terminate = false;
	}

	NetEngine::~NetEngine() {
		CloseHandle(_completionPort);
		WSACleanup();

		_terminate = true;
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}

	bool NetEngine::Listen(ITcpServer* server, const char * ip, const int32_t port, const int32_t sendSize, const int32_t recvSize, bool fast) {
		SOCKET sock = INVALID_SOCKET;
		if (INVALID_SOCKET == (sock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED))) {
			return false;
		}

		int32_t flag = 1;
		if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&flag, sizeof(flag)) == -1) {
			closesocket(sock);
			return false;
		}

		sockaddr_in addr;
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);
		if (inet_pton(AF_INET, ip, &addr.sin_addr.s_addr) < 0) {
			closesocket(sock);
			return false;
		}

		if (SOCKET_ERROR == bind(sock, (sockaddr*)&addr, sizeof(sockaddr_in))) {
			closesocket(sock);
			return false;
		}

		if (listen(sock, 128) == SOCKET_ERROR) {
			closesocket(sock);
			return false;
		}

		if (_completionPort != CreateIoCompletionPort((HANDLE)sock, _completionPort, sock, 0)) {
			closesocket(sock);
			return false;
		}

		for (int32_t i = 0; i < BACKLOG; ++i) {
			IocpAcceptor * acceptor = new IocpAcceptor;

			memset(&acceptor->accept, 0, sizeof(acceptor->accept));
			acceptor->accept.opt = IOCP_OPT_ACCEPT;
			acceptor->accept.sock = sock;
			acceptor->accept.code = 0;
			acceptor->accept.context = server;
			acceptor->sendSize = sendSize;
			acceptor->recvSize = recvSize;
			acceptor->fast = fast;

			if (!DoAccept(acceptor)) {
				closesocket(sock);
				delete acceptor;

				return false;
			}
		}

		_servers[server] = sock;
		return true;
	}

	void NetEngine::Stop(ITcpServer* server) {
		auto itr = _servers.find(server);
		if (itr != _servers.end()) {
			closesocket(itr->second);
			
			_servers.erase(itr);
		}
	}

	bool NetEngine::Connect(ITcpSession* session, const char * ip, const int32_t port, const int32_t sendSize, const int32_t recvSize, bool fast) {
		SOCKET sock = INVALID_SOCKET;
		if (INVALID_SOCKET == (sock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED))) {
			LIBNET_ASSERT(false, "Connect error %d", ::WSAGetLastError());
			return false;
		}

		DWORD value = 0;
		if (SOCKET_ERROR == ioctlsocket(sock, FIONBIO, &value)) {
			closesocket(sock);
			return false;
		}

		const int8_t nodelay = 1;
		setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));

		if (_completionPort != CreateIoCompletionPort((HANDLE)sock, _completionPort, sock, 0)) {
			closesocket(sock);
			return false;
		}

		sockaddr_in remote;
		memset(&remote, 0, sizeof(remote));

		remote.sin_family = AF_INET;
		if (SOCKET_ERROR == bind(sock, (sockaddr *)&remote, sizeof(sockaddr_in)))
			return false;

		remote.sin_port = htons(port);
		if (inet_pton(AF_INET, ip, &remote.sin_addr.s_addr) < 0)
			return false;

		IocpConnector* connector = new IocpConnector;
		memset(&connector->connect, 0, sizeof(connector->connect));
		connector->connect.opt = IOCP_OPT_CONNECT;
		connector->connect.sock = sock;
		connector->connect.code = 0;
		connector->connect.context = session;
		connector->sendSize = sendSize;
		connector->recvSize = recvSize;
		SafeSprintf(connector->remoteIp, sizeof(connector->remoteIp), "%s", ip);
		connector->remotePort = port;
		connector->fast = fast;

		DWORD len;
		BOOL res = g_connect(sock, (sockaddr *)&remote, sizeof(sockaddr_in), nullptr, 0, &len, (LPOVERLAPPED)&connector->connect);
		int32_t errCode = WSAGetLastError();
		if (!res && errCode != WSA_IO_PENDING) {
			closesocket(sock);
			delete connector;

			return false;
		}

		return true;
	}

	void NetEngine::Poll(int64_t frame) {
		_eventQueue.SweepOnce([this](NetEvent* evt) {
			switch (evt->evtType) {
			case NET_ACCEPT: OnAccept((ITcpServer*)evt->context, evt->sock, evt->sendSize, evt->recvSize, evt->fast); break;
			case NET_CONNECT_SUCCESS: OnConnect((ITcpSession*)evt->context, evt->sock, evt->sendSize, evt->recvSize, evt->fast, evt->remoteIp, evt->remotePort); break;
			case NET_CONNECT_FAIL : OnConnectFail((ITcpSession*)evt->context); break;
			case NET_SEND_DONE: ((Connection*)evt->context)->OnSendDone(); break;
			case NET_SEND_FAIL: ((Connection*)evt->context)->OnSendFail(); break;
			case NET_RECV: ((Connection*)evt->context)->OnRecv(); break;
			case NET_RECV_DONE: ((Connection*)evt->context)->OnRecvDone(); break;
			case NET_RECV_FAIL: ((Connection*)evt->context)->OnRecvFail(); break;
			}

			delete evt;
		});

		for (auto* conn : _connections) {
			if (conn->NeedUpdateSend())
				conn->UpdateSend();

			if (conn->IsFastConnected())
				conn->UpdateFast();
		}
	}

	void NetEngine::Release() {
		delete this;
	}

	void NetEngine::ThreadProc() {
		while (!_terminate) {
			IocpEvent * evt = GetQueueState(_completionPort);
			if (evt) {
				switch (evt->opt) {
				case IOCP_OPT_ACCEPT: DealAccept((IocpAcceptor *)evt); break;
				case IOCP_OPT_CONNECT: DealConnect((IocpConnector *)evt); break;
				case IOCP_OPT_RECV: DealRecv(evt); break;
				case IOCP_OPT_SEND: DealSend(evt); break;
				}
			}
			else {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		}
	}

	void NetEngine::DealAccept(IocpAcceptor * evt) {
		if (evt->accept.code == ERROR_SUCCESS) {
			BOOL res = setsockopt(evt->sock, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (const char *)&evt->accept.sock, sizeof(evt->accept.sock));

			sockaddr_in remote;
			int32_t size = sizeof(sockaddr_in);
			if (res != 0 || 0 != getpeername(evt->sock, (sockaddr*)&remote, &size)) {
				//OASSERT(false, "complete accept error %d", GetLastError());
				closesocket(evt->sock);

				evt->sock = INVALID_SOCKET;
			}
			else {
				DWORD value = 0;
				if (SOCKET_ERROR == ioctlsocket(evt->sock, FIONBIO, &value)) {
					closesocket(evt->sock);

					evt->sock = INVALID_SOCKET;
				}
				else {
					HANDLE ret = CreateIoCompletionPort((HANDLE)evt->sock, _completionPort, evt->sock, 0);
					if (_completionPort != ret) {
						closesocket(evt->sock);

						evt->sock = INVALID_SOCKET;
					}
					else {
						const int8_t nodelay = 1;
						setsockopt(evt->sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&nodelay, sizeof(nodelay));
					}
				}
			}

			SOCKET sock = evt->sock;
			if (sock != INVALID_SOCKET)
				PushAccept(sock, (ITcpServer*)evt->accept.context, evt->sendSize, evt->recvSize, evt->fast);

			if (DoAccept(evt))
				return;
		}

		closesocket(evt->sock);
		delete evt;
	}

	void NetEngine::DealConnect(IocpConnector * evt) {
		if (evt->connect.code == ERROR_SUCCESS) {
			const int8_t nodelay = 1;
			setsockopt(evt->connect.sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&nodelay, sizeof(nodelay));

			PushConnectSuccess(evt->connect.sock, (ITcpSession*)evt->connect.context, evt->sendSize, evt->recvSize, evt->fast, evt->remoteIp, evt->remotePort);
		}
		else {
			closesocket(evt->connect.sock);
			PushConnectFail((ITcpSession*)evt->connect.context);
			delete evt;
		}
	}

	void NetEngine::DealSend(IocpEvent * evt) {
		Connection * connection = (Connection*)evt->context;
		if (evt->code == ERROR_SUCCESS) {
			int32_t left = connection->Out(evt->bytes);
			if (left > 0) {
				if (left > MIN_SEND_BUFF_SIZE || connection->IsClosing()) {
					if (!DoSend(connection)) {
						closesocket(evt->sock);
						PushSendFail(connection);
					}
					return;
				}
			}

			PushSendDone(connection);
		}
		else {
			if (evt->code != ERROR_IO_PENDING) {
				closesocket(evt->sock);
				PushSendFail(connection);
			}
			else {
				if (!DoSend(connection)) {
					closesocket(evt->sock);
					PushSendFail(connection);
				}
			}
		}
	}

	void NetEngine::DealRecv(IocpEvent * evt) {
		Connection * connection = (Connection * )evt->context;
		if (evt->code == ERROR_SUCCESS && evt->bytes > 0) {
			connection->In(evt->bytes);
			PushRecv(connection);

			if (connection->IsAdjustRecvBuff()) {
				PushRecvDone(connection);
			}
			else {
				if (!DoRecv(connection)) {
					closesocket(evt->sock);
					PushRecvFail(connection);
				}
			}
		}
		else {
			closesocket(evt->sock);
			PushRecvFail(connection);
		}
	}

	bool NetEngine::DoSend(Connection* connection) {
		uint32_t size = 0;
		char * sendBuf = connection->GetSendBuffer(size);

		IocpEvent& evtSend = connection->GetSendEvent();
		evtSend.buf.buf = sendBuf;
		evtSend.buf.len = size;
		evtSend.code = 0;
		evtSend.bytes = 0;
		if (SOCKET_ERROR == WSASend(connection->GetSocket(), &evtSend.buf, 1, nullptr, 0, (LPWSAOVERLAPPED)&evtSend, nullptr)) {
			evtSend.code = WSAGetLastError();
			if (WSA_IO_PENDING != evtSend.code)
				return false;
		}
		return true;
	}

	bool NetEngine::DoRecv(Connection* connection) {
		uint32_t size = 0;
		char* recvBuf = connection->GetRecvBuffer(size);

		if (size <= 0)
			return false;

		IocpEvent& evtRecv = connection->GetRecvEvent();
		DWORD flags = 0;
		evtRecv.buf.buf = recvBuf;
		evtRecv.buf.len = size;
		evtRecv.code = 0;
		evtRecv.bytes = 0;
		if (SOCKET_ERROR == WSARecv(connection->GetSocket(), &evtRecv.buf, 1, nullptr, &flags, (LPWSAOVERLAPPED)&evtRecv, nullptr)) {
			evtRecv.code = WSAGetLastError();
			if (WSA_IO_PENDING != evtRecv.code)
				return false;
		}

		return true;
	}

	bool NetEngine::DoAccept(IocpAcceptor * acceptor) {
		acceptor->sock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
		if (acceptor->sock == INVALID_SOCKET) {
			LIBNET_ASSERT(false, "do accept failed %d", WSAGetLastError());
			delete acceptor;
			return false;
		}

		//hn_info("DoAccept {} {}", acceptor->accept.sock, acceptor->sock);

		DWORD bytes = 0;
		int32_t res = g_accept(acceptor->accept.sock,
			acceptor->sock,
			acceptor->buf,
			0,
			sizeof(sockaddr_in) + 16,
			sizeof(sockaddr_in) + 16,
			&bytes,
			(LPOVERLAPPED)&acceptor->accept
		);

		int32_t errCode = WSAGetLastError();
		if (!res && errCode != WSA_IO_PENDING) {
			LIBNET_ASSERT(false, "do accept failed %d", errCode);
			closesocket(acceptor->sock);
			delete acceptor;
			return false;
		}

		return true;
	}

	IocpEvent * NetEngine::GetQueueState(HANDLE completionPort) {
		DWORD bytes = 0;
		SOCKET socket = INVALID_SOCKET;
		IocpEvent * evt = nullptr;

		SetLastError(0);
		BOOL ret = GetQueuedCompletionStatus(completionPort, &bytes, (PULONG_PTR)&socket, (LPOVERLAPPED *)&evt, 10);

		if (nullptr == evt)
			return nullptr;

		evt->code = GetLastError();
		evt->bytes = bytes;
		if (!ret) {
			if (WAIT_TIMEOUT == evt->code)
				return nullptr;
		}
		return evt;
	}

	void NetEngine::OnAccept(ITcpServer* server, SOCKET sock, int32_t sendSize, int32_t recvSize, bool fast) {
		ITcpSession* session = server->MallocConnection();
		if (!session) {
			closesocket(sock);
			return;
		}

		sockaddr_in remote;
		int32_t size = sizeof(sockaddr_in);
		if (0 != getpeername(sock, (sockaddr*)&remote, &size)) {
			session->Release();

			closesocket(sock);
			return;
		}

		char remoteIp[LIBNET_IP_SIZE];
		inet_ntop(AF_INET, &remote.sin_addr, remoteIp, sizeof(remoteIp));

		Connection * connection = new Connection(sock, this, sendSize, recvSize, fast ? strcmp(remoteIp, LOCAL_IP) == 0 : false);
		connection->Attach(session);

		connection->SetRemoteIp(remoteIp);
		connection->SetRemotePort(ntohs(remote.sin_port));

		if (!DoRecv(connection)) {
			session->Release();

			closesocket(sock);
			delete connection;
			return;
		}

		connection->OnConnected(true);

		Add(connection);
	}

	void NetEngine::OnConnect(ITcpSession* session, SOCKET sock, int32_t sendSize, int32_t recvSize, bool fast, const char* ip, int32_t port) {
		Connection* connection = new Connection(sock, this, sendSize, recvSize, fast ? strcmp(ip, LOCAL_IP) == 0 : false);
		connection->Attach(session);

		connection->SetRemoteIp(ip);
		connection->SetRemotePort(port);

		if (!DoRecv(connection)) {
			session->SetPipe(nullptr);
			session->OnConnectFailed();

			closesocket(sock);
			delete connection;
			return;
		}

		connection->OnConnected(false);

		Add(connection);
	}

	void NetEngine::OnConnectFail(ITcpSession* session) {
		session->OnConnectFailed();
	}

	INetEngine * CreateNetEngine(int32_t threadCount) {
		WSADATA wsaData;
		if (0 != WSAStartup(MAKEWORD(2, 2), &wsaData))
			return nullptr;

		if (nullptr == (g_accept = GetAcceptExFunc()))
			return nullptr;

		if (nullptr == (g_connect = GetConnectExFunc()))
			return nullptr;

		HANDLE completionPort;
		if (nullptr == (completionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0)))
			return nullptr;

		return new NetEngine(completionPort, threadCount);
	}
}
