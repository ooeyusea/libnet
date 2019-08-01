#include "Connection.h"
#include "util.h"

namespace libnet {
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

	IPreSendContext* Connection::PreAllocContext(int32_t size) {
		return nullptr;
	}

	void Connection::UpdateSend() {
		if (!_engine->DoSend(this)) {
			LIBNET_ASSERT(_recving, "wtf");
			Shutdown();
			return;
		}
	}

	void Connection::OnSendDone() {
		_sending = false;

		if (_closed) {
			if (!_recving) {
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
			_session->Release();
			_engine->Remove(this);
			delete this;
		}
	}

	void Connection::OnRecv() {
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

	void Connection::OnRecvFail() {
		_recving = false;
		Shutdown();

		if (!_sending) {
			_session->Release();
			_engine->Remove(this);
			delete this;
		}
	}
}
