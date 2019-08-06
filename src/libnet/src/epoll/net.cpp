#include "net.h"
#include "util.h"
#include <thread>
#include <atomic>
#include "Connection.h"

#define NET_INIT_FRAME 1024
#define MAX_NET_THREAD 4
#define BACKLOG 128
#define EPOLL_BATCH_SIZE 1024
#define LOCAL_IP "127.0.0.1"

namespace libnet {
	NetEngine::NetEngine(int32_t threadCount) {
		for (int32_t i = 0; i < threadCount; ++i) {
			int32_t fd = epoll_create(1);
			_fds.emplace_back(fd);

			std::thread([this, fd]() {
				ThreadProc(fd);
			}).detach();
		}

		_terminate = false;
	}

	NetEngine::~NetEngine() {
		_terminate = true;
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}

	bool NetEngine::Listen(ITcpServer* server, const char * ip, const int32_t port, const int32_t sendSize, const int32_t recvSize, bool fast) {
		int32_t sock = -1;
		if (-1 == (sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP))) {
			return false;
		}

		if (fcntl(sock, F_SETFL, fcntl(sock, F_GETFD, 0) | O_NONBLOCK) == -1) {
			close(sock);
			return false;
		}

		int32_t flag = 1;
		if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&flag, sizeof(flag)) == -1) {
			close(sock);
			return false;
		}

		sockaddr_in addr;
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);
		if ((addr.sin_addr.s_addr = inet_addr(ip)) == INADDR_NONE) {
			close(sock);
			return false;
		}

		if (-1 == bind(sock, (sockaddr*)&addr, sizeof(sockaddr_in))) {
			close(sock);
			return false;
		}

		if (listen(sock, 128) == -1) {
			close(sock);
			return false;
		}

		EpollBase * acceptor = new EpollBase;
		memset(acceptor, 0, sizeof(EpollBase));
		acceptor->opt = EPOLL_OPT_ACCEPT;
		acceptor->sock = sock;
		acceptor->code = 0;
		acceptor->context = server;
		acceptor->sendSize = sendSize;
		acceptor->recvSize = recvSize;
		acceptor->fast = fast;

		if (!AddToWorker(acceptor)) {
			close(sock);
			delete acceptor;

			return false;
		}

		_servers[server] = sock;
		return true;
	}

	void NetEngine::Stop(ITcpServer* server) {
		auto itr = _servers.find(server);
		if (itr != _servers.end()) {
			close(itr->second);
			
			_servers.erase(itr);
		}
	}

	bool NetEngine::Connect(ITcpSession* session, const char * ip, const int32_t port, const int32_t sendSize, const int32_t recvSize, bool fast) {
		int32_t sock = -1;
		if (-1 == (sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP))) {
			return false;
		}

		if (fcntl(sock, F_SETFL, fcntl(sock, F_GETFD, 0) | O_NONBLOCK) == -1) {
			close(sock);
			return false;
		}

		const int8_t nodelay = 1;
		setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char*)& nodelay, sizeof(nodelay));

		sockaddr_in remote;
		memset(&remote, 0, sizeof(remote));

		remote.sin_family = AF_INET;
		if (-1 == bind(sock, (sockaddr*)& remote, sizeof(sockaddr_in)))
			return false;

		remote.sin_port = htons(port);
		if ((remote.sin_addr.s_addr = inet_addr(ip)) == INADDR_NONE)
			return false;

		int32_t ret = connect(sock, (sockaddr*)& remote, sizeof(remote));
		if (ret < 0 && errno != EINPROGRESS) {
			close(sock);
			return false;
		}

		EpollBase* connector = new EpollBase;
		memset(connector, 0, sizeof(EpollBase));
		connector->opt = EPOLL_OPT_CONNECT;
		connector->sock = sock;
		connector->code = 0;
		connector->context = session;
		connector->sendSize = sendSize;
		connector->recvSize = recvSize;
		connector->fast = fast;
		SafeSprintf(connector->remoteIp, sizeof(connector->remoteIp), "%s", ip);
		connector->remotePort = port;

		if (!AddToWorker(connector)) {
			close(sock);
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
			case NET_FAIL: ((Connection*)evt->context)->OnFail(); break;
			case NET_RECV: ((Connection*)evt->context)->OnRecv(); break;
			case NET_RECV_DONE: ((Connection*)evt->context)->OnRecvDone(); break;
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

	void NetEngine::ThreadProc(int32_t fd) {
		while (!_terminate) {
			epoll_event events[EPOLL_BATCH_SIZE];
			int32_t count = epoll_wait(fd, events, EPOLL_BATCH_SIZE, 0);
			if (count < 1) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				continue;
			}

			for (int32_t i = 0; i < count; ++i) {
				EpollBase * evt = (EpollBase * )events[i].data.ptr;
				if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
					evt->code = -1;

				switch (evt->opt) {
				case EPOLL_OPT_ACCEPT: DealAccept((EpollBase*)evt); break;
				case EPOLL_OPT_CONNECT: DealConnect((EpollBase*)evt); break;
				case EPOLL_OPT_IO: DealIO(evt, events[i].events); break;
				}
			}
		}
	}

	void NetEngine::DealAccept(EpollBase* evt) {
		if (evt->code == 0) {
			int32_t sock = -1;
			int32_t count = 0;
			sockaddr_in addr;
			socklen_t len = sizeof(addr);

			while (count++ < BACKLOG && (sock = accept(evt->sock, (sockaddr*)& addr, &len)) > 0) {
				if (fcntl(sock, F_SETFL, fcntl(sock, F_GETFD, 0) | O_NONBLOCK) == -1) {
					close(sock);
					continue;
				}

				const int8_t nodelay = 1;
				setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char*)& nodelay, sizeof(nodelay));

				PushAccept(sock, (ITcpServer*)evt->context, evt->sendSize, evt->recvSize, evt->fast);
			}

			if (errno == EAGAIN)
				return;
		}

		close(evt->sock);
		DelFromWorker(evt);

		delete evt;
	}

	void NetEngine::DealConnect(EpollBase* evt) {
		if (evt->code == 0) {
			const int8_t nodelay = 1;
			setsockopt(evt->sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&nodelay, sizeof(nodelay));

			PushConnectSuccess(evt->sock, (ITcpSession*)evt->context, evt->sendSize, evt->recvSize, evt->fast, evt->remoteIp, evt->remotePort);
		}
		else {
			close(evt->sock);
			
			PushConnectFail((ITcpSession*)evt->context);
		}

		DelFromWorker(evt);
		delete evt;
	}

	void NetEngine::DealIO(EpollBase* evt, int32_t flag) {
		Connection* connection = (Connection*)evt->context;
		if (evt->code != 0) {
			DelFromWorker(evt);

			PushFail(connection);
		}
		else {
			if (flag & EPOLLIN) {
				if (connection->IsAdjustRecvBuff()) {
					PushRecvDone(connection);
				}
				else {
					int32_t len = 0;

					while (true) {
						uint32_t size = 0;
						char* recvBuf = connection->GetRecvBuffer(size);

						if (recvBuf && size > 0) {
							len = recv(evt->sock, recvBuf, size, 0);
							if (len < 0 && errno == EAGAIN)
								break;
						}
						else
							len = -1;

						if (len <= 0) {
							DelFromWorker(evt);
							PushFail(connection);
							return;
						}

						connection->In(len);
						PushRecv(connection);
					}
				}
			}

			if (flag & EPOLLOUT) {
				int32_t left = DoSend(connection);
				if (left < 0) {
					DelFromWorker(evt);
					PushFail(connection);
				}
				else if (left == 0) {
					if (!RemoveSend(evt)) {
						DelFromWorker(evt);
						PushFail(connection);
					}
					else
						PushSendDone(connection);
				}
			}
		}
	}

	int32_t NetEngine::DoSend(Connection* connection) {
		int32_t left = 0;
		do {
			uint32_t size = 0;
			char* sendBuf = connection->GetSendBuffer(size);

			if (sendBuf && size > 0) {
				int32_t len = send(connection->GetSocket(), sendBuf, size, 0);
				if (len < 0) {
					if (errno != EAGAIN)
						return -1;
				}

				left = connection->Out(len);
			}
			else
				left = 0;

		} while (left > 0);
		return left;
	}

	void NetEngine::OnAccept(ITcpServer* server, int32_t sock, int32_t sendSize, int32_t recvSize, bool fast) {
		ITcpSession* session = server->MallocConnection();
		if (!session) {
			close(sock);
			return;
		}

		sockaddr_in remote;
		socklen_t size = sizeof(sockaddr_in);
		if (0 != getpeername(sock, (sockaddr*)&remote, &size)) {
			session->Release();

			close(sock);
			return;
		}

		char remoteIp[LIBNET_IP_SIZE];
		inet_ntop(AF_INET, &remote.sin_addr, remoteIp, sizeof(remoteIp));

		Connection * connection = new Connection(sock, this, sendSize, recvSize, fast ? strcmp(remoteIp, LOCAL_IP) == 0 : false);
		connection->Attach(session);

		connection->SetRemoteIp(remoteIp);
		connection->SetRemotePort(ntohs(remote.sin_port));

		if (!AddToWorker(&connection->GetEvent())) {
			session->Release();

			close(sock);
			delete connection;
			return;
		}

		connection->OnConnected(true);
		Add(connection);
	}

	void NetEngine::OnConnect(ITcpSession* session, int32_t sock, int32_t sendSize, int32_t recvSize, bool fast, const char* ip, int32_t port) {
		Connection* connection = new Connection(sock, this, sendSize, recvSize, fast ? strcmp(ip, LOCAL_IP) == 0 : false);
		connection->Attach(session);

		connection->SetRemoteIp(ip);
		connection->SetRemotePort(port);

		if (!AddToWorker(&connection->GetEvent())) {
			session->SetPipe(nullptr);
			session->OnConnectFailed();

			close(sock);
			delete connection;
			return;
		}

		connection->OnConnected(false);
		Add(connection);
	}

	void NetEngine::OnConnectFail(ITcpSession* session) {
		session->OnConnectFailed();
	}

	bool NetEngine::AddToWorker(EpollBase* evt) {
		evt->epollFd = SelectWorker();
		if (evt->epollFd <= 0)
			return false;

		epoll_event ev;
		ev.data.ptr = evt;
		switch (evt->opt) {
		case EPOLL_OPT_ACCEPT: ev.events = EPOLLIN; break;
		case EPOLL_OPT_CONNECT: ev.events = EPOLLOUT | EPOLLET; break;
		case EPOLL_OPT_IO: ev.events = EPOLLOUT | EPOLLET; break;
		}
		ev.events |= EPOLLERR | EPOLLHUP;

		return epoll_ctl(evt->epollFd, EPOLL_CTL_ADD, evt->sock, &ev) == 0;
	}

	bool NetEngine::DelFromWorker(EpollBase* evt) {
		return epoll_ctl(evt->epollFd, EPOLL_CTL_DEL, evt->sock, nullptr) == 0;
	}

	bool NetEngine::AddSend(EpollBase* evt) {
		LIBNET_ASSERT(evt->opt == EPOLL_OPT_IO, "wtf");

		epoll_event ev;
		ev.data.ptr = evt;
		ev.events = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLERR | EPOLLHUP;

		return epoll_ctl(evt->epollFd, EPOLL_CTL_MOD, evt->sock, &ev) == 0;
	}

	bool NetEngine::RemoveSend(EpollBase* evt) {
		LIBNET_ASSERT(evt->opt == EPOLL_OPT_IO, "wtf");

		epoll_event ev;
		ev.data.ptr = evt;
		ev.events = EPOLLIN | EPOLLET | EPOLLERR | EPOLLHUP;

		return epoll_ctl(evt->epollFd, EPOLL_CTL_MOD, evt->sock, &ev) == 0;
	}

	INetEngine * CreateNetEngine(int32_t threadCount) {
		return new NetEngine(threadCount);
	}
}
