#ifndef __LIBNET_H__
#define __LIBNET_H__
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef WIN32
#ifndef _WINSOCK2API_
#include <WinSock2.h>
#else
#include <Windows.h>
#endif
#include <Shlwapi.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "shlwapi.lib")
#else
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <dlfcn.h>
#include <limits.h>
#include <stdarg.h>
#include <dirent.h>
#include <sys/stat.h>
#endif

#define LIBNET_IP_SIZE 64

namespace libnet {
	class NetBuffer {
	public:
		NetBuffer(char* buff, int32_t size, char* buffPlus, int32_t sizePlus) {
			_buff = buff;
			_size = size;
			_buffPlus = buffPlus;
			_sizePlus = sizePlus;
		}

		template <typename T>
		inline bool Read(int32_t offset, T& t) const {
			if (offset + (int32_t)sizeof(T) > _size + _sizePlus)
				return false;

			if (offset >= _size) {
				if (!_buffPlus)
					return false;

				memcpy(&t, _buffPlus + (offset - _size), sizeof(T));
			}
			else if (offset + (int32_t)sizeof(T) > _size) {
				if (!_buffPlus)
					return false;

				memcpy(&t, _buff + offset, _size - offset);
				memcpy(&t + (_size - offset), _buffPlus, sizeof(T) - (_size - offset));
			}
			else
				memcpy(&t, _buff + offset, sizeof(T));

			return true;
		}

		template <typename T>
		inline bool Write(int32_t offset, T& t) {
			if (offset + (int32_t)sizeof(T) > _size + _sizePlus)
				return false;

			if (offset >= _size) {
				if (!_buffPlus)
					return false;

				memcpy(_buffPlus + (offset - _size), &t, sizeof(T));
			}
			else if (offset + (int32_t)sizeof(T) > _size) {
				if (!_buffPlus)
					return false;

				memcpy(_buff + offset, &t, _size - offset);
				memcpy(_buffPlus, &t + (_size - offset), sizeof(T) - (_size - offset));
			}
			else
				memcpy(_buff + offset, &t, sizeof(T));

			return true;
		}

		inline int32_t Size() const { return _size + _sizePlus; }

	private:
		char * _buff = nullptr;
		int32_t _size = 0;
		char * _buffPlus = nullptr;
		int32_t _sizePlus = 0;
	};

	class IPipe {
	public:
		virtual ~IPipe() {}

		virtual void Send(const char* context, const int32_t size) = 0;
		virtual void Close() = 0;
		virtual void Shutdown() = 0;

		virtual void AdjustSendBuffSize(const int32_t size) = 0;
		virtual void AdjustRecvBuffSize(const int32_t size) = 0;

		inline const char* GetRemoteIp() const { return _remoteIp; }
		inline int32_t GetRemotePort() { return _remotePort; }

	protected:
		char _remoteIp[LIBNET_IP_SIZE];
		int32_t _remotePort;
	};

	class ITcpSession {
	public:
		ITcpSession() {}
		virtual ~ITcpSession() {}

		virtual int32_t OnRecv(const NetBuffer& buffer) = 0;

		virtual void OnConnected() = 0;
		virtual void OnConnectFailed() = 0;
		virtual void OnDisconnect() = 0;

		virtual void Release() = 0;

		inline void SetPipe(IPipe* pipe) { _pipe = pipe; }
		inline IPipe* GetPipe() const { return _pipe; }

		inline void Send(const char* context, const int32_t size) {
			if (_pipe)
				_pipe->Send(context, size);
		}

		inline void Close() {
			if (_pipe)
				_pipe->Close();
		}

		inline void Shutdown() {
			if (_pipe)
				_pipe->Shutdown();
		}

		inline void AdjustSendBuffSize(const int32_t size) {
			if (_pipe)
				_pipe->AdjustSendBuffSize(size);
		}

		inline void AdjustRecvBuffSize(const int32_t size) {
			if (_pipe)
				_pipe->AdjustRecvBuffSize(size);
		}

	protected:
		IPipe* _pipe = nullptr;
	};


	struct ITcpServer {
	public:
		ITcpServer() {}
		virtual ~ITcpServer() {}

		virtual ITcpSession* MallocConnection() = 0;
	};

	struct INetEngine {
		virtual ~INetEngine() {}

		virtual bool Listen(ITcpServer* server, const char* ip, const int32_t port, const int32_t sendSize, const int32_t recvSize, bool fast) = 0;
		virtual void Stop(ITcpServer* server) = 0;
		virtual bool Connect(ITcpSession* session, const char* ip, const int32_t port, const int32_t sendSize, const int32_t recvSize, bool fast) = 0;

		virtual void Poll(int64_t frame) = 0;
		virtual void Release() = 0;
	};

	INetEngine* CreateNetEngine(int32_t threadCount);
}

#endif //__LIBNET_H__
