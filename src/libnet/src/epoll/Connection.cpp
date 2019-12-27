#include "Connection.h"
#include "util.h"

#define LIBNET_FAST_PIPE_NAME_SIZE 64
#define FAST_SEND_SIZE 1024
#define FAST_RECV_SIZE 1024

namespace libnet {
	static const int32_t FAST_PIPE_CREATE = 0;
	static const int32_t FAST_PIPE_CREATE_OK = 1;

	struct FastPipe {
		char sendName[LIBNET_FAST_PIPE_NAME_SIZE];
		int32_t sendSize;
	};

	int64_t Connection::s_nextId = 0;

	Connection::Connection(int32_t fd, NetEngine* engine, int32_t sendSize, int32_t recvSize, bool fast)
		: _fd(fd), _engine(engine), _sendSize(sendSize), _recvSize(recvSize), _sendBuffer(fast ? FAST_SEND_SIZE : sendSize), _recvBuffer(fast ? FAST_RECV_SIZE : recvSize), _fast(fast) {
		memset(&_event, 0, sizeof(_event));
		_event.context = this;
		_event.sock = _fd;
		_event.opt = EPOLL_OPT_IO;
		_event.epollFd = 0;

		++s_nextId;
		if (s_nextId <= 0)
			s_nextId = 1;
		_id = s_nextId;
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

	void Connection::AdjustSendBuffSize(const int32_t size) {
		if (size > _sendSize) {
			_adjustSend = size;

			if (!_fast) {
				if (!_sending) {
					_sendBuffer.Realloc(_adjustSend);
					_adjustSend = 0;
				}
			}
			else
				DoAdjustFastSendBuffSize();
		}
	}

	void Connection::AdjustRecvBuffSize(const int32_t size) {
		if (size > _recvSize)
			_adjustRecv = size;
	}

	void Connection::DoAdjustFastSendBuffSize() {
		if (_fastSendConnected && _fast) {
			++_version;
			_sendSize = _adjustSend;

			FastPipe fastPipe;
			SafeSprintf(fastPipe.sendName, sizeof(fastPipe.sendName), "FTCPP%d:%lld:send%d", getpid(), _id, _version);
			fastPipe.sendSize = _sendSize;
			if (!_shareMemorySendBuffer.Open(fastPipe.sendName, _sendSize, true)) {
				Shutdown();
				return;
			}

			if (!_sendBuffer.WriteBlock(&FAST_PIPE_CREATE, (int32_t)sizeof(FAST_PIPE_CREATE)) || !_sendBuffer.WriteBlock(&fastPipe, (int32_t)sizeof(fastPipe))) {
				LIBNET_ASSERT(_recving, "wtf");
				Shutdown();
				return;
			}

			_fastSendConnected = false;
			_adjustSend = 0;
		}
	}

	void Connection::OnConnected(bool accept) {
		if (!_fast)
			_session->OnConnected();
		else {
			FastPipe fastPipe;
			SafeSprintf(fastPipe.sendName, sizeof(fastPipe.sendName), "FTCPP%d:%lld:send%d", getpid(), _id, _version);
			fastPipe.sendSize = _sendSize;
			if (!_shareMemorySendBuffer.Open(fastPipe.sendName, _sendSize, true)) {
				Shutdown();
				return;
			}

			Send((const char*)& FAST_PIPE_CREATE, (int32_t)sizeof(FAST_PIPE_CREATE));
			Send((const char*)&fastPipe, sizeof(fastPipe));
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

		if (!_fast) {
			if (_adjustSend > 0) {
				_sendBuffer.Realloc(_adjustSend);
				_adjustSend = 0;
			}
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
			while (_recvBuffer.Size() > 0) {
				if (_recvBuffer.Size() >= sizeof(int32_t)) {
					int32_t op = 0;
					NetBuffer buffer = _recvBuffer.GetReadBuffer();
					buffer.Read(0, op);

					if (op == FAST_PIPE_CREATE) {
						if (_recvBuffer.Size() >= sizeof(FastPipe) + sizeof(op)) {
							FastPipe fastPipe;
							buffer.Read(sizeof(op), fastPipe);
							_recvBuffer.Out(sizeof(FastPipe) + sizeof(op));

							if (!_fastConnected) {
								if (!_shareMemoryRecvBuffer.Open(fastPipe.sendName, fastPipe.sendSize, false)) {
									Shutdown();
									return;
								}
							}
							else {
								if (!_shareMemoryRecvBuffer.Plus(fastPipe.sendName, fastPipe.sendSize)) {
									Shutdown();
									return;
								}
							}

							if (!_sendBuffer.WriteBlock(&FAST_PIPE_CREATE_OK, (int32_t)sizeof(FAST_PIPE_CREATE_OK))) {
								LIBNET_ASSERT(_recving, "wtf");
								Shutdown();
								return;
							}

							if (!_fastConnected) {
								_fastConnected = true;
								_session->OnConnected();
							}
						}
						else
							break;
					}
					else if (op == FAST_PIPE_CREATE_OK) {
						_fastSendConnected = true;
						_recvBuffer.Out(sizeof(op));

						if (_adjustSend)
							DoAdjustFastSendBuffSize();
					}

				}
				else
					break;
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

	void Connection::OnRecvDone() {
		if (_adjustRecv > 0) {
			_recvBuffer.Realloc(_adjustRecv);
			_adjustRecv = 0;
		}

		int32_t len = 0;
		while (true) {
			uint32_t size = 0;
			char* recvBuf = GetRecvBuffer(size);

			if (recvBuf && size > 0) {
				len = recv(_fd, recvBuf, size, 0);
				if (len < 0 && errno == EAGAIN)
					break;
			}
			else
				len = -1;

			if (len <= 0) {
				Shutdown();
				return;
			}

			In(len);
		}

		OnRecv();
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
