#include "Connection.h"
#include "util.h"

namespace libnet {
	struct FastPipe {
		char recvName[LIBNET_SHARE_MEMORY_NAME_SIZE];
		int32_t recvSize;
		char sendName[LIBNET_SHARE_MEMORY_NAME_SIZE];
		int32_t sendSize;
	};

	Connection::Connection(int32_t fd, NetEngine* engine, int32_t sendSize, int32_t recvSize)
		: _fd(fd), _engine(engine), _sendSize(sendSize), _recvSize(recvSize), _sendBuffer(sendSize), _recvBuffer(recvSize) {
		memset(&_event, 0, sizeof(_event));
		_event.context = this;
		_event.sock = _fd;
		_event.opt = EPOLL_OPT_IO;
		_event.epollFd = 0;
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
						UpdateSend();
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
				UpdateSend();
			}
		}

		if (!_sending)
			Shutdown();
	}

	void Connection::Shutdown() {
		if (!_closed) {
			_closed = true;
			close(_fd);
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
			
			SafeSprintf(fastPipe.recvName, sizeof(fastPipe.recvName), "P%d:%lld:recv%d", getpid(), (int64_t)this, _version);
			fastPipe.recvSize = _recvSize;
			if (!_shareMemoryRecvBuffer.Open(fastPipe.recvName, _recvSize, true)) {
				Shutdown();
				return;
			}
			
			SafeSprintf(fastPipe.sendName, sizeof(fastPipe.sendName), "P%d:%lld:send%d", getpid(), (int64_t)this, _version);
			fastPipe.sendSize = _sendSize;
			if (!_shareMemorySendBuffer.Open(fastPipe.sendName, _sendSize, true)) {
				Shutdown();
				return;
			}
			
			Send((const char*)& fastPipe, sizeof(fastPipe));

			_fastConnected = true;
			_session->OnConnected();
		}
	}

	void Connection::UpdateSend() {
		int32_t left = _engine->DoSend(this);
		if (left < 0) {
			LIBNET_ASSERT(_recving, "wtf");
			Shutdown();
		}
		else if (left > 0) {
			_sending = true;
			if (!_engine->AddSend(&_event)) {
				LIBNET_ASSERT(_recving, "wtf");
				Shutdown();
			}
		}
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
			LIBNET_ASSERT(_recving, "wtf");
			return;
		}

		if (_closing) {
			LIBNET_ASSERT(_recving, "wtf");

			if (_sendBuffer.Size() > 0) {
				UpdateSend();
			}
			else
				Shutdown();
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

	void Connection::OnFail() {
		_recving = false;
		_sending = false;

		Shutdown();

		_session->OnDisconnect();
		_session->SetPipe(nullptr);

		_session->Release();
		_engine->Remove(this);
		delete this;
	}
}
