#ifndef __SHARE_MEMORY_H__
#define __SHARE_MEMORY_H__
#include "libnet.h"
#include "util.h"
#include <sys/mman.h>

#define LIBNET_SHARE_MEMORY_NAME_SIZE 64

namespace libnet {
	class ShareMemory {
		struct ShareMemoryHeader {
			uint32_t in;
			uint32_t out;
			uint32_t size;
		};

	public:

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

		inline uint32_t Size() const { return ((ShareMemoryHeader*)_buff)->in - ((ShareMemoryHeader*)_buff)->out; }

		inline bool WriteBlock(const void* content, const uint32_t size) {
			uint32_t freeSize = ((ShareMemoryHeader*)_buff)->size - ((ShareMemoryHeader*)_buff)->in + ((ShareMemoryHeader*)_buff)->out;
			if (freeSize < size)
				return false;

			uint32_t realIn = ((ShareMemoryHeader*)_buff)->in & (((ShareMemoryHeader*)_buff)->size - 1);
			if (size <= ((ShareMemoryHeader*)_buff)->size - realIn)
				memcpy(_buff + sizeof(ShareMemoryHeader) + realIn, content, size);
			else {
				memcpy(_buff + sizeof(ShareMemoryHeader) + realIn, content, ((ShareMemoryHeader*)_buff)->size - realIn);
				memcpy(_buff + sizeof(ShareMemoryHeader), (const char*)content + ((ShareMemoryHeader*)_buff)->size - realIn, size - (((ShareMemoryHeader*)_buff)->size - realIn));
			}

			__atomic_exchange_n(&((ShareMemoryHeader*)_buff)->in, ((ShareMemoryHeader*)_buff)->in + size, __ATOMIC_RELEASE);
			return true;
		}

		inline void Out(const uint32_t size) {
			LIBNET_ASSERT(((ShareMemoryHeader*)_buff)->in - ((ShareMemoryHeader*)_buff)->out + (_tempSize - _tempOffset) >= size, "wtf");
			if (_tempSize > 0) {
				LIBNET_ASSERT(_temp && _tempOffset < _tempSize, "wtf");
				_tempOffset += size;

				if (_tempOffset >= _tempSize) {
					free(_temp);
					_temp = nullptr;

					_tempOffset -= _tempSize;
					_tempSize = 0;

					if (_tempOffset > 0) {
						__atomic_exchange_n(&((ShareMemoryHeader*)_buff)->out, ((ShareMemoryHeader*)_buff)->out + _tempOffset, __ATOMIC_RELEASE);
						_tempOffset = 0;
					}
				}
			}
			else
				__atomic_exchange_n(&((ShareMemoryHeader*)_buff)->out, ((ShareMemoryHeader*)_buff)->out + size, __ATOMIC_RELEASE);
		}

		inline NetBuffer GetReadBuffer() {
			uint32_t realIn = ((ShareMemoryHeader*)_buff)->in & (((ShareMemoryHeader*)_buff)->size - 1);
			uint32_t realOut = ((ShareMemoryHeader*)_buff)->out & (((ShareMemoryHeader*)_buff)->size - 1);
			if (realIn > realOut)
				return NetBuffer(_buff + sizeof(ShareMemoryHeader) + realOut, realIn - realOut, nullptr, 0);
			else
				return NetBuffer(_buff + sizeof(ShareMemoryHeader) + realOut, ((ShareMemoryHeader*)_buff)->size - realOut, _buff + sizeof(ShareMemoryHeader), realIn);
		}

		inline void CopyToTemp() {
			LIBNET_ASSERT(!_temp, "temp is not empty");

			_tempSize = Size();
			LIBNET_ASSERT(_tempSize > 0, "no buff left");

			_tempOffset = 0;
			_temp = (char*)malloc(Size());

			uint32_t realIn = ((ShareMemoryHeader*)_buff)->in & (((ShareMemoryHeader*)_buff)->size - 1);
			uint32_t realOut = ((ShareMemoryHeader*)_buff)->out & (((ShareMemoryHeader*)_buff)->size - 1);
			if (realIn > realOut) {
				memcpy(_temp, _buff + sizeof(ShareMemoryHeader) + realOut, realIn - realOut);
			}
			else {
				uint32_t partSize = ((ShareMemoryHeader*)_buff)->size - realOut;
				memcpy(_temp, _buff + sizeof(ShareMemoryHeader) + realOut, partSize);
				memcpy(_temp + partSize, _buff + sizeof(ShareMemoryHeader), realIn);
			}

		}

		ShareMemory() {}
		~ShareMemory();

		bool Open(const char* name, int32_t size, bool owner);
		bool Plus(const char* name, int32_t size);

	private:
		int32_t _mapFile = -1;
		char _name[64];
		char* _buff = nullptr;
		int32_t _size = 0;

		char* _temp = nullptr;
		int32_t _tempOffset = 0;
		int32_t _tempSize = 0;
	};
}

#endif //__SHARE_MEMORY_H__
