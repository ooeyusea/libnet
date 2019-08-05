#include "Connection.h"
#include "util.h"

#define LIBNET_FAST_PIPE_NAME_SIZE 64

namespace libnet {
	struct FastPipe {
		char recvName[LIBNET_FAST_PIPE_NAME_SIZE];
		int32_t recvSize;
		char sendName[LIBNET_FAST_PIPE_NAME_SIZE];
		int32_t sendSize;
	};

	Connection::Connection(SOCKET fd, NetEngine* engine, int32_t sendSize, int32_t recvSize)
		: _fd(fd), _engine(engine), _sendSize(sendSize), _recvSize(recvSize), _sendBuffer(sendSize), _recvBuffer(recvSize) {
		memset(&_sendEvent, 0, sizeof(_sendEvent));
		_sendEvent.context = this;
		_sendEvent.sock = _fd;
		_sendEvent.opt = IOCP_OPT_SEND;

		memset(&_recvEvent, 0, sizeof(_recvEvent));
		_recvEvent.context = this;
		_recvEvent.sock = _fd;
		_recvEvent.opt = IOCP_OPT_RECV;

	}

	void Connection::Send(const char* context, const int32_t size) {
		if (!_closing && !_closed) {
			if (_fast && _fastConnected) {
				if (!_shareMemorySendBuffer.WriteBlock(context, size)) {
					LIBNET_ASSERT(_recving || _sending, "wtf"); 
					Shutdown();
					return;
				}
			}
			else {
				if (!_sendBuffer.WriteBlock(context, size)) {
					LIBNET_ASSERT(_recving, "wtf");
					Shutdown();
					return;
				}

				if (!_sending) {
					if (_sendBuffer.Size() > MIN_SEND_BUFF_SIZE) {
						if (!_engine->DoSend(this)) {
							LIBNET_ASSERT(_recving, "wtf");
							Shutdown();
							return;
						}

						_sending = true;
					}
				}
			}
		}
	}

	void Connection::Close() {
		if (_closed)
			return;

		_closing = true;
		if (!_sending) {
			if (_sendBuffer.Size() > 0) {
				if (!_engine->DoSend(this)) {
					LIBNET_ASSERT(_recving, "wtf");
					Shutdown();
					return;
				}

				_sending = true;
			}
		}

		if (!_sending)
			Shutdown();
	}

	void Connection::Shutdown() {
		if (!_closed) {
			_closed = true;
			closesocket(_fd);
		}
	}

	void Connection::Fast() {
		if (strcmp(_remoteIp, "127.0.0.1") == 0)
			_fast = true;
	}

	void Connection::OnConnected(bool accept) {
		if (!_fast)
			_session->OnConnected();
		else if (!accept) {
			FastPipe fastPipe;
			
			SafeSprintf(fastPipe.recvName, sizeof(fastPipe.recvName), "P%d:%lld:recv%d", GetCurrentProcessId(), (int64_t)this, _version);
			fastPipe.recvSize = _recvSize;
			if (!_shareMemoryRecvBuffer.Open(fastPipe.recvName, _recvSize, true)) {
				Shutdown();
				return;
			}

			SafeSprintf(fastPipe.sendName, sizeof(fastPipe.sendName), "P%d:%lld:send%d", GetCurrentProcessId(), (int64_t)this, _version);
			fastPipe.sendSize = _sendSize;
			if (!_shareMemorySendBuffer.Open(fastPipe.sendName, _sendSize, true)) {
				Shutdown();
				return;
			}

			Send((const char*)&fastPipe, sizeof(fastPipe));

			_fastConnected = true;
			_session->OnConnected();
		}
	}

	void Connection::UpdateSend() {
		if (!_engine->DoSend(this)) {
			LIBNET_ASSERT(_recving, "wtf");
			Shutdown();
			return;
		}

		_sending = true;
	}

	void Connection::UpdateFast() {
		if (_shareMemoryRecvBuffer.Size() > 0) {
			auto buffer = _shareMemoryRecvBuffer.GetReadBuffer();
			LIBNET_ASSERT(_session, "Connection::OnRecv session is empty");

			int32_t len = -1;
			if (_session)
				len = _session->OnRecv(buffer);

			if (len > 0)
				_shareMemoryRecvBuffer.Out(len);
			else if (len < 0)
				Shutdown();
		}
	}

	void Connection::OnSendDone() {
		_sending = false;

		if (_closed) {
			if (!_recving) {
				_session->OnDisconnect();
				_session->SetPipe(nullptr);

				_session->Release();
				_engine->Remove(this);
				delete this;
			}
			return;
		}

		if (_closing) {
			LIBNET_ASSERT(_recving, "wtf");

			if (_sendBuffer.Size() > 0) {
				if (!_engine->DoSend(this)) {
					Shutdown();
					return;
				}

				_sending = true;
			}
			else
				Shutdown();
		}
	}

	void Connection::OnSendFail() {
		_sending = false;
		Shutdown();

		if (!_recving) {
			_session->OnDisconnect();
			_session->SetPipe(nullptr);

			_session->Release();
			_engine->Remove(this);
			delete this;
		}
	}

	void Connection::OnRecv() {
		if (_fast) {
			if (_recvBuffer.Size() >= sizeof(FastPipe)) {
				FastPipe fastPipe;
				_recvBuffer.GetReadBuffer().Read(0, fastPipe);
				_recvBuffer.Out(sizeof(FastPipe));

				if (!_shareMemoryRecvBuffer.Open(fastPipe.sendName, fastPipe.sendSize, false)) {
					Shutdown();
					return;
				}

				if (!_shareMemorySendBuffer.Open(fastPipe.recvName, fastPipe.recvSize, false)) {
					Shutdown();
					return;
				}

				_fastConnected = true;
				_session->OnConnected();
			}
		}
		else {
			if (_recvBuffer.Size() > 0) {
				auto buffer = _recvBuffer.GetReadBuffer();
				LIBNET_ASSERT(_session, "Connection::OnRecv session is empty");

				int32_t len = -1;
				if (_session)
					len = _session->OnRecv(buffer);

				if (len > 0)
					_recvBuffer.Out(len);
				else if (len < 0)
					Shutdown();
			}
		}
	}

	void Connection::OnRecvFail() {
		_recving = false;
		Shutdown();

		if (!_sending) {
			_session->OnDisconnect();
			_session->SetPipe(nullptr);

			_session->Release();
			_engine->Remove(this);
			delete this;
		}
	}
}
