#ifndef __CONNECTION_H__
#define __CONNECTION_H__
#include "libnet.h"
#include "net.h"
#include "RingBuffer.h"
#include "share_memory.h"

namespace libnet {
	class Connection : public IPipe {
		friend class NetEngine;
	public:
		Connection(int32_t fd, NetEngine* engine, int32_t sendSize, int32_t recvSize, bool fast);
		virtual ~Connection() {}

		inline void Attach(ITcpSession* session) {
			_session = session;
			session->SetPipe(this);
		}

		virtual void Send(const char* context, const int32_t size);
		virtual void Close();
		virtual void Shutdown();

		virtual void AdjustSendBuffSize(const int32_t size);
		virtual void AdjustRecvBuffSize(const int32_t size);

		void DoAdjustFastSendBuffSize();

		inline bool IsAdjustRecvBuff() const { return !_fast && _adjustRecv > 0; }

		void OnConnected(bool accept);

		inline int32_t GetSocket() const { return _fd; }
		inline EpollBase& GetEvent() { return _event; }

		inline void In(int32_t size) { _recvBuffer.In(size); }
		inline int32_t Out(int32_t size) { _sendBuffer.Out(size); return _sendBuffer.Size(); }

		inline char* GetSendBuffer(uint32_t& size) { return _sendBuffer.Read(size); }
		inline char* GetRecvBuffer(uint32_t& size) { return _recvBuffer.Write(size); }

		inline bool IsClosing() const { return _closing; }
		inline bool NeedUpdateSend() const { return !_closing && !_closed && !_sending && _sendBuffer.Size() > 0; }
		inline bool IsFastConnected() const { return !_closing && !_closed && _fast && _fastConnected; }

		inline void SetRemoteIp(const char * ip) const { SafeSprintf((char*)_remoteIp, sizeof(_remoteIp), "%s", ip); }
		inline void SetRemotePort(int32_t port) { _remotePort = port; }

		void UpdateSend();
		void UpdateFast();
		void OnSendDone();
		void OnRecv();
		void OnRecvDone();
		void OnFail();

	private:
		int32_t _fd;
		NetEngine* _engine;
		ITcpSession * _session = nullptr;
		int32_t _sendSize;
		int32_t _recvSize;

		RingBuffer _sendBuffer;
		RingBuffer _recvBuffer;

		ShareMemory _shareMemorySendBuffer;
		ShareMemory _shareMemoryRecvBuffer;

		int32_t _adjustSend = 0;
		int32_t _adjustRecv = 0;

		bool _closing = false;
		bool _closed = false;
		bool _recving = true;
		bool _sending = false;

		EpollBase _event;

		bool _fast;
		bool _fastConnected = false;
		bool _fastSendConnected = false;

		int32_t _version = 0;

		int64_t _id = 0;
		static int64_t s_nextId;
	};
}

#endif //__CONNECTION_H__
