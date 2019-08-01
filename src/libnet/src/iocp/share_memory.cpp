#include "share_memory.h"

namespace libnet {
	ShareMemory::~ShareMemory() {
		if (_buff)
			UnmapViewOfFile(_buff);

		if (_mapFile)
			CloseHandle(_mapFile);
	}

	bool ShareMemory::Open(const char* name, int32_t size, bool owner) {
		if (size & (size - 1))
			size = RoundupPowOfTwo(size) + sizeof(ShareMemoryHeader);

		if (owner) {
			_mapFile = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, size, name);
			if (_mapFile == NULL)
				return false;
		}
		else {
			_mapFile = OpenFileMapping(FILE_MAP_ALL_ACCESS, NULL, name);
			if (_mapFile == NULL)
				return false;
		}

		_buff = (char*)MapViewOfFile(_mapFile, FILE_MAP_ALL_ACCESS, 0, 0, size);
		if (!_buff)
			return false;

		if (owner) {
			ShareMemoryHeader& header = *(ShareMemoryHeader*)_buff;
			header.in = 0;
			header.out = 0;
			header.size = size;
		}

		_size = size;
		return true;
	}
}
