#ifndef __NET_H__
#define __NET_H__
#include "libnet.h"
#include "lock_free_list.h"
#include <unordered_set>
#include <unordered_map>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <netinet/tcp.h>
#include <vector>
#include "util.h"

#define MIN_SEND_BUFF_SIZE 1024

namespace libnet {
	enum {
		EPOLL_OPT_CONNECT = 0,
		EPOLL_OPT_ACCEPT,
		EPOLL_OPT_IO,
	};

	struct EpollBase {
		int8_t opt;
		int32_t epollFd;
		int32_t code;
		int32_t sock;
		void* context;
		int32_t sendSize;
		int32_t recvSize;
		bool fast;
		char remoteIp[LIBNET_IP_SIZE];
		int32_t remotePort;
	};

	enum NetEventType {
		NET_ACCEPT,
		NET_CONNECT_SUCCESS,
		NET_CONNECT_FAIL,
		NET_SEND_DONE,
		NET_FAIL,
		NET_RECV,
	};

	struct NetEvent {
		int8_t evtType;
		void * context;
		int32_t sock;
		int32_t sendSize;
		int32_t recvSize;
		bool fast;
		char remoteIp[LIBNET_IP_SIZE];
		int32_t remotePort;

		AtomicIntrusiveLinkedListHook<NetEvent> next;
	};

	class Connection;
	class NetEngine : public INetEngine {
	public:
		NetEngine();
		~NetEngine();

		virtual bool Listen(ITcpServer* server, const char* ip, const int32_t port, const int32_t sendSize, const int32_t recvSize, bool fast);
		virtual void Stop(ITcpServer* server);
		virtual bool Connect(ITcpSession* session, const char* ip, const int32_t port, const int32_t sendSize, const int32_t recvSize, bool fast);

		virtual void Poll(int64_t frame);
		virtual void Release();

		void ThreadProc(int32_t fd);

		void DealAccept(EpollBase* evt);
		void DealConnect(EpollBase* evt);
		void DealIO(EpollBase* evt, int32_t flag);

		int32_t DoSend(Connection* connection);

		void OnAccept(ITcpServer* server, int32_t sock, int32_t sendSize, int32_t recvSize, bool fast);
		void OnConnect(ITcpSession* session, int32_t sock, int32_t sendSize, int32_t recvSize, bool fast, const char* ip, int32_t port);
		void OnConnectFail(ITcpSession* session);

		inline int32_t SelectWorker() {
			if (_fds.empty())
				return 0;

			_nextIdx = (_nextIdx + 1) % _fds.size();
			return _fds[_nextIdx];
		}
		bool AddToWorker(EpollBase* evt);
		bool DelFromWorker(EpollBase* evt);
		bool AddSend(EpollBase* evt);
		bool RemoveSend(EpollBase* evt);

		inline void PushAccept(int32_t sock, ITcpServer* server, int32_t sendSize, int32_t recvSize, bool fast) {
			NetEvent* evt = new NetEvent{ NET_ACCEPT, server, sock, sendSize, recvSize, fast };
			_eventQueue.InsertHead(evt);
		}

		inline void PushConnectSuccess(int32_t sock, ITcpSession* session, int32_t sendSize, int32_t recvSize, bool fast, const char* ip, int32_t port) {
			NetEvent* evt = new NetEvent{ NET_CONNECT_SUCCESS, session, sock, sendSize, recvSize, fast };
			SafeSprintf(evt->remoteIp, sizeof(evt->remoteIp), "%s", ip);
			evt->remotePort = port;

			_eventQueue.InsertHead(evt);
		}

		inline void PushConnectFail(ITcpSession* session) {
			NetEvent* evt = new NetEvent{ NET_CONNECT_FAIL, session };
			_eventQueue.InsertHead(evt);
		}

		inline void PushSendDone(Connection * connection) {
			NetEvent* evt = new NetEvent{ NET_SEND_DONE, connection };
			_eventQueue.InsertHead(evt);
		}

		inline void PushFail(Connection * connection) {
			NetEvent* evt = new NetEvent{ NET_FAIL, connection };
			_eventQueue.InsertHead(evt);
		}

		inline void PushRecv(Connection* connection) {
			NetEvent* evt = new NetEvent{ NET_RECV, connection };
			_eventQueue.InsertHead(evt);
		}

		inline void Add(Connection* conn) { _connections.insert(conn); }
		inline void Remove(Connection* conn) { _connections.erase(conn); }

	private:
		bool _terminate;

		int32_t _nextIdx = 0;
		std::vector<int32_t> _fds;
		AtomicIntrusiveLinkedList<NetEvent, &NetEvent::next> _eventQueue;

		std::unordered_set<Connection*> _connections;
		std::unordered_map<ITcpServer*, int32_t> _servers;
	};
}

#endif // !__NET_H__
