#ifndef __CONNECTION_H__
#define __CONNECTION_H__
#include "libnet.h"
#include "net.h"
#include "RingBuffer.h"

namespace libnet {
	class Connection : public IPipe {
		friend class NetEngine;
	public:
		Connection(SOCKET fd, NetEngine* engine, int32_t sendSize, int32_t recvSize);
		virtual ~Connection() {}

		inline void Attach(ITcpSession* session) {
			_session = session;
			session->SetPipe(this);
		}

		virtual void Send(const char* context, const int32_t size);
		virtual void Close();
		virtual void Shutdown();
		virtual IPreSendContext* PreAllocContext(int32_t size);

		inline SOCKET GetSocket() const { return _fd; }
		inline IocpEvent& GetSendEvent() { return _sendEvent; }
		inline IocpEvent& GetRecvEvent() { return _recvEvent; }

		inline void In(int32_t size) { _recvBuffer.In(size); }
		inline int32_t Out(int32_t size) { _sendBuffer.Out(size); return _sendBuffer.Size(); }

		inline char* GetSendBuffer(uint32_t& size) { return _sendBuffer.Read(size); }
		inline char* GetRecvBuffer(uint32_t& size) { return _recvBuffer.Write(size); }

		inline bool IsClosing() const { return _closing; }
		inline bool NeedUpdateSend() const { return !_closing && !_closed && !_sending && _sendBuffer.Size() > 0; }

		inline void SetRemoteIp(const char * ip) const { SafeSprintf((char*)_remoteIp, sizeof(_remoteIp), "%s", ip); }
		inline void SetRemotePort(int32_t port) { _remotePort = port; }

		void UpdateSend();
		void OnSendDone();
		void OnSendFail();
		void OnRecv();
		void OnRecvFail();

	private:
		SOCKET _fd;
		NetEngine* _engine;
		ITcpSession * _session = nullptr;
		int32_t _sendSize;
		int32_t _recvSize;

		RingBuffer _sendBuffer;
		RingBuffer _recvBuffer;

		bool _closing = false;
		bool _closed = false;
		bool _recving = true;
		bool _sending = false;

		IocpEvent _sendEvent;
		IocpEvent _recvEvent;
	};
}

#endif //__CONNECTION_H__