#ifndef __ORINGBUFFER_h__
#define __ORINGBUFFER_h__
#include "libnet.h"
#include "util.h"

namespace libnet {
	class RingBuffer {
	public:
		RingBuffer(int32_t size) {
			if (size & (size - 1))
				size = RoundupPowOfTwo(size);

			_buffer = (char*)malloc(size);
			_in = 0;
			_out = 0;
			_size = size;
		}

		~RingBuffer() {
			free(_buffer);
		}

		inline uint32_t Fls(uint32_t size) {
			if (0 != size) {
				uint32_t position = 0;
				for (uint32_t i = (size >> 1); i != 0; ++position)
					i >>= 1;
				return position + 1;
			}
			else
				return 0;
		}

		inline uint32_t RoundupPowOfTwo(uint32_t size) {
			return 1 << Fls(size - 1);
		}

		inline uint32_t CalcSize(uint32_t size) {
			if (size & (size - 1))
				return RoundupPowOfTwo(size);
			return size;
		}

		inline uint32_t Size() const { return _in - _out; }

		inline void In(const uint32_t size) {
			_in += size;
		}

		char* Write(uint32_t& size) {
			uint32_t freeSize = _size - _in + _out;
			if (freeSize == 0)
				return nullptr;

			uint32_t realIn = _in & (_size - 1);
			uint32_t realOut = _out & (_size - 1);
			if (realIn >= realOut)
				size = _size - realIn;
			else
				size = realOut - realIn;

			return _buffer + realIn;
		}

		inline bool WriteBlock(const void* content, const uint32_t size) {
			uint32_t freeSize = _size - _in + _out;
			if (freeSize < size)
				return false;

			uint32_t realIn = _in & (_size - 1);
			if (size <= _size - realIn)
				memcpy(_buffer + realIn, content, size);
			else {
				memcpy(_buffer + realIn, content, _size - realIn);
				memcpy(_buffer, (const char*)content + _size - realIn, size - (_size - realIn));
			}
			_in += size;
			return true;
		}

		inline char* ReadTemp(char* temp, uint32_t tempSize, uint32_t* size) {
			uint32_t useSize = _in - _out;
			if (useSize == 0)
				return nullptr;

			uint32_t realIn = _in & (_size - 1);
			uint32_t realOut = _out & (_size - 1);
			if (realIn > realOut) {
				*size = useSize;
				return _buffer + realOut;
			}
			else {
				if (tempSize <= _size - realOut) {
					*size = _size - realOut;
					return _buffer + realOut;
				}

				memcpy(temp, _buffer + realOut, _size - realOut);

				tempSize -= _size - realOut;
				uint32_t headSize = realIn > tempSize ? tempSize : realIn;
				memcpy(temp + _size - realOut, _buffer, headSize);
				*size = _size - realOut + headSize;

				return temp;
			}
		}

		inline char* Read(uint32_t& size) {
			uint32_t useSize = _in - _out;
			if (useSize == 0)
				return NULL;

			uint32_t realIn = _in & (_size - 1);
			uint32_t realOut = _out & (_size - 1);
			if (realIn > realOut)
				size = useSize;
			else
				size = _size - realOut;

			return _buffer + realOut;
		}

		inline void Out(const uint32_t size) {
			LIBNET_ASSERT(_in - _out >= size, "wtf");
			_out += size;
		}

		inline void Realloc(uint32_t size) {
			if (size & (size - 1))
				size = RoundupPowOfTwo(size);

			uint32_t usedSize = _in - _out;
			if (usedSize > size)
				return;

			char* buffer = (char*)malloc(size);
			if (!buffer)
				return;

			if (usedSize > 0) {
				uint32_t realIn = _in & (_size - 1);
				uint32_t realOut = _out & (_size - 1);

				if (realIn > realOut)
					memcpy(buffer, _buffer + realOut, usedSize);
				else {
					memcpy(buffer, _buffer + realOut, _size - realOut);
					memcpy(buffer + _size - realOut, _buffer, realIn);
				}
			}

			free(_buffer);
			_buffer = buffer;
			_out = 0;
			_in = 0;
			_size = size;
		}

		inline NetBuffer GetReadBuffer() {
			uint32_t realIn = _in & (_size - 1);
			uint32_t realOut = _out & (_size - 1);
			if (realIn > realOut)
				return NetBuffer(_buffer + realOut, realIn - realOut, nullptr, 0);
			else
				return NetBuffer(_buffer + realOut, _size - realOut, _buffer, realIn);
		}

	private:
		char * _buffer;
		uint32_t _size;
		uint32_t _in;
		uint32_t _out;
	};
}

#endif //__ORINGBUFFER_h__
