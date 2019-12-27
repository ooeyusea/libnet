#ifndef __NET_H__
#define __NET_H__
#include "libnet.h"
#include "util.h"
#include "lock_free_list.h"
#include <unordered_set>
#include <unordered_map>

#define MIN_SEND_BUFF_SIZE 1024

namespace libnet {
	enum {
		IOCP_OPT_CONNECT = 0,
		IOCP_OPT_ACCEPT,
		IOCP_OPT_RECV,
		IOCP_OPT_SEND,
	};

	struct IocpEvent {
		OVERLAPPED ol;
		int8_t opt;
		int32_t code;
		WSABUF buf;
		DWORD bytes;
		SOCKET sock;
		void * context;
	};

	struct IocpConnector {
		IocpEvent connect;
		int32_t sendSize;
		int32_t recvSize;
		bool fast;
		char remoteIp[LIBNET_IP_SIZE];
		int32_t remotePort;
	};

	struct IocpAcceptor {
		IocpEvent accept;
		int32_t sendSize;
		int32_t recvSize;
		bool fast;
		SOCKET sock;
		char buf[128];
	};

	enum NetEventType {
		NET_ACCEPT,
		NET_CONNECT_SUCCESS,
		NET_CONNECT_FAIL,
		NET_SEND_DONE,
		NET_SEND_FAIL,
		NET_RECV,
		NET_RECV_DONE,
		NET_RECV_FAIL,
	};

	struct NetEvent {
		int8_t evtType;
		void * context;
		SOCKET sock;
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
		NetEngine(HANDLE completionPort, int32_t threadCount);
		~NetEngine();

		virtual bool Listen(ITcpServer* server, const char* ip, const int32_t port, const int32_t sendSize, const int32_t recvSize, bool fast);
		virtual void Stop(ITcpServer* server);
		virtual bool Connect(ITcpSession* session, const char* ip, const int32_t port, const int32_t sendSize, const int32_t recvSize, bool fast);

		virtual void Poll(int64_t frame);
		virtual void Release();

		void ThreadProc();

		bool DoSend(Connection* connection);
		bool DoRecv(Connection* connection);
		bool DoAccept(IocpAcceptor * evt);

		IocpEvent * GetQueueState(HANDLE completionPort);
		void DealAccept(IocpAcceptor * evt);
		void DealConnect(IocpConnector * evt);
		void DealSend(IocpEvent * evt);
		void DealRecv(IocpEvent * evt);

		void OnAccept(ITcpServer * server, SOCKET sock, int32_t sendSize, int32_t recvSize, bool fast);
		void OnConnect(ITcpSession* session, SOCKET sock, int32_t sendSize, int32_t recvSize, bool fast, const char * ip, int32_t port);
		void OnConnectFail(ITcpSession* session);

		inline void PushAccept(SOCKET sock, ITcpServer* server, int32_t sendSize, int32_t recvSize, bool fast) {
			NetEvent * evt = new NetEvent{ NET_ACCEPT, server, sock, sendSize, recvSize, fast };
			_eventQueue.InsertHead(evt);
		}

		inline void PushConnectSuccess(SOCKET sock, ITcpSession* session, int32_t sendSize, int32_t recvSize, bool fast, const char * ip, int32_t port) {
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

		inline void PushSendFail(Connection * connection) {
			NetEvent* evt = new NetEvent{ NET_SEND_FAIL, connection };
			_eventQueue.InsertHead(evt);
		}

		inline void PushRecv(Connection* connection) {
			NetEvent* evt = new NetEvent{ NET_RECV, connection };
			_eventQueue.InsertHead(evt);
		}

		inline void PushRecvDone(Connection* connection) {
			NetEvent* evt = new NetEvent{ NET_RECV_DONE, connection };
			_eventQueue.InsertHead(evt);
		}

		inline void PushRecvFail(Connection* connection) {
			NetEvent* evt = new NetEvent{ NET_RECV_FAIL, connection };
			_eventQueue.InsertHead(evt);
		}

		inline void Add(Connection* conn) { _connections.insert(conn); }
		inline void Remove(Connection* conn) { _connections.erase(conn); }

	private:
		bool _terminate = false;

		HANDLE _completionPort;
		AtomicIntrusiveLinkedList<NetEvent, &NetEvent::next> _eventQueue;

		std::unordered_set<Connection*> _connections;
		std::unordered_map<ITcpServer*, SOCKET> _servers;
	};
}

#endif // !__NET_H__
