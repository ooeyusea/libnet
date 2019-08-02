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

		inline void In(const uint32_t size) {
			((ShareMemoryHeader*)_buff)->in += size;
		}

		char* Write(uint32_t& size) {
			uint32_t freeSize = ((ShareMemoryHeader*)_buff)->size - ((ShareMemoryHeader*)_buff)->in + ((ShareMemoryHeader*)_buff)->out;
			if (freeSize == 0)
				return nullptr;

			uint32_t realIn = ((ShareMemoryHeader*)_buff)->in & (((ShareMemoryHeader*)_buff)->size - 1);
			uint32_t realOut = ((ShareMemoryHeader*)_buff)->out & (((ShareMemoryHeader*)_buff)->size - 1);
			if (realIn >= realOut)
				size = ((ShareMemoryHeader*)_buff)->size - realIn;
			else
				size = realOut - realIn;

			return _buff + sizeof(ShareMemoryHeader) + realIn;
		}

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
			((ShareMemoryHeader*)_buff)->in += size;
			return true;
		}

		inline char* ReadTemp(char* temp, uint32_t tempSize, uint32_t* size) {
			uint32_t useSize = ((ShareMemoryHeader*)_buff)->in - ((ShareMemoryHeader*)_buff)->out;
			if (useSize == 0)
				return nullptr;

			uint32_t realIn = ((ShareMemoryHeader*)_buff)->in & (((ShareMemoryHeader*)_buff)->size - 1);
			uint32_t realOut = ((ShareMemoryHeader*)_buff)->out & (((ShareMemoryHeader*)_buff)->size - 1);
			if (realIn > realOut) {
				*size = useSize;
				return _buff + sizeof(ShareMemoryHeader) + realOut;
			}
			else {
				if (tempSize <= ((ShareMemoryHeader*)_buff)->size - realOut) {
					*size = ((ShareMemoryHeader*)_buff)->size - realOut;
					return _buff + sizeof(ShareMemoryHeader) + realOut;
				}

				memcpy(temp, _buff + sizeof(ShareMemoryHeader) + realOut, ((ShareMemoryHeader*)_buff)->size - realOut);

				tempSize -= ((ShareMemoryHeader*)_buff)->size - realOut;
				uint32_t headSize = realIn > tempSize ? tempSize : realIn;
				memcpy(temp + ((ShareMemoryHeader*)_buff)->size - realOut, _buff + sizeof(ShareMemoryHeader), headSize);
				*size = ((ShareMemoryHeader*)_buff)->size - realOut + headSize;

				return temp;
			}
		}

		inline char* Read(uint32_t& size) {
			uint32_t useSize = ((ShareMemoryHeader*)_buff)->in - ((ShareMemoryHeader*)_buff)->out;
			if (useSize == 0)
				return NULL;

			uint32_t realIn = ((ShareMemoryHeader*)_buff)->in & (((ShareMemoryHeader*)_buff)->size - 1);
			uint32_t realOut = ((ShareMemoryHeader*)_buff)->out & (((ShareMemoryHeader*)_buff)->size - 1);
			if (realIn > realOut)
				size = useSize;
			else
				size = ((ShareMemoryHeader*)_buff)->size - realOut;

			return _buff + sizeof(ShareMemoryHeader) + realOut;
		}

		inline void Out(const uint32_t size) {
			LIBNET_ASSERT(((ShareMemoryHeader*)_buff)->in - ((ShareMemoryHeader*)_buff)->out >= size, "wtf");
			((ShareMemoryHeader*)_buff)->out += size;
		}

		inline NetBuffer GetReadBuffer() {
			uint32_t realIn = ((ShareMemoryHeader*)_buff)->in & (((ShareMemoryHeader*)_buff)->size - 1);
			uint32_t realOut = ((ShareMemoryHeader*)_buff)->out & (((ShareMemoryHeader*)_buff)->size - 1);
			if (realIn > realOut)
				return NetBuffer(_buff + sizeof(ShareMemoryHeader) + realOut, realIn - realOut, nullptr, 0);
			else
				return NetBuffer(_buff + sizeof(ShareMemoryHeader) + realOut, ((ShareMemoryHeader*)_buff)->size - realOut, _buff + sizeof(ShareMemoryHeader), realIn);
		}

		ShareMemory() {}
		~ShareMemory();

		bool Open(const char* name, int32_t size, bool owner);

	private:
		int32_t _mapFile = -1;
		char _name[64];
		char* _buff = nullptr;
		int32_t _size = 0;
	};
}

#endif //__SHARE_MEMORY_H__
