#ifndef __NET_H__
#define __NET_H__
#include "libnet.h"
#include "lock_free_list.h"
#include <unordered_set>
#include <unordered_map>

#define MIN_SEND_BUFF_SIZE 1024

namespace libnet {
	enum {
		EPOLL_OPT_CONNECT = 0,
		EPOLL_OPT_ACCEPT,
		EPOLL_OPT_IO,
	};

	struct EpollBase {
		int8_t opt;
		int32_t code;
		int32_t sock;
		void* context;
		int32_t sendSize;
		int32_t recvSize;
	};

	enum NetEventType {
		NET_ACCEPT,
		NET_CONNECT_SUCCESS,
		NET_CONNECT_FAIL,
		NET_SEND_DONE,
		NET_SEND_FAIL,
		NET_RECV,
		NET_RECV_FAIL,
	};

	struct NetEvent {
		int8_t evtType;
		void * context;
		int32_t sock;
		int32_t sendSize;
		int32_t recvSize;

		AtomicIntrusiveLinkedListHook<NetEvent> next;
	};

	class Connection;
	class NetEngine : public INetEngine {
	public:
		NetEngine();
		~NetEngine();

		virtual bool Listen(ITcpServer* server, const char* ip, const int32_t port, const int32_t sendSize, const int32_t recvSize);
		virtual void Stop(ITcpServer* server);
		virtual bool Connect(ITcpSession* session, const char* ip, const int32_t port, const int32_t sendSize, const int32_t recvSize);

		virtual void Poll(int64_t frame);
		virtual void Release();

		void ThreadProc();

		bool DoSend(Connection* connection);
		bool DoRecv(Connection* connection);
		bool DoAccept(IocpAcceptor * evt);

		EpollBase* GetQueueState(int32_t fd);
		void DealAccept(IocpAcceptor * evt);
		void DealConnect(IocpConnector * evt);
		void DealSend(IocpEvent * evt);
		void DealRecv(IocpEvent * evt);

		void OnAccept(ITcpServer * server, SOCKET sock, int32_t sendSize, int32_t recvSize);
		void OnConnect(ITcpSession* session, SOCKET sock, int32_t sendSize, int32_t recvSize);
		void OnConnectFail(ITcpSession* session);

		inline void PushAccept(SOCKET sock, ITcpServer* server, int32_t sendSize, int32_t recvSize) {
			NetEvent * evt = new NetEvent{ NET_ACCEPT, server, sock, sendSize, recvSize };
			_eventQueue.InsertHead(evt);
		}

		inline void PushConnectSuccess(SOCKET sock, ITcpSession* session, int32_t sendSize, int32_t recvSize) {
			NetEvent* evt = new NetEvent{ NET_CONNECT_SUCCESS, session, sock, sendSize, recvSize };
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

		inline void PushSendFail(Connection * connection) {
			NetEvent* evt = new NetEvent{ NET_SEND_FAIL, connection };
			_eventQueue.InsertHead(evt);
		}

		inline void PushRecv(Connection* connection) {
			NetEvent* evt = new NetEvent{ NET_RECV, connection };
			_eventQueue.InsertHead(evt);
		}

		inline void PushRecvFail(Connection* connection) {
			NetEvent* evt = new NetEvent{ NET_RECV_FAIL, connection };
			_eventQueue.InsertHead(evt);
		}

		inline void Add(Connection* conn) { _connections.insert(conn); }
		inline void Remove(Connection* conn) { _connections.erase(conn); }

	private:
		bool _terminate;

		int32_t _fd;
		AtomicIntrusiveLinkedList<NetEvent, &NetEvent::next> _eventQueue;

		std::unordered_set<Connection*> _connections;
		std::unordered_map<ITcpServer*, int32_t> _servers;
	};
}

#endif // !__NET_H__
