#include "share_memory.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

namespace libnet {
	ShareMemory::~ShareMemory() {
		if (_buff)
			munmap(_buff, _size);

		if (_mapFile) {
			close(_mapFile);

			shm_unlink(_name);
		}
	}

	bool ShareMemory::Open(const char* name, int32_t size, bool owner) {
		if (size & (size - 1))
			size = RoundupPowOfTwo(size);

		_size = size + sizeof(ShareMemoryHeader);
		SafeSprintf(_name, sizeof(_name), "%s", name);

		_mapFile = shm_open(name, O_RDWR | O_CREAT, 0777);
		if (_mapFile < 0)
			return false;

		if (ftruncate(_mapFile, _size) < 0)
			return false;

		_buff = (char*)mmap(NULL, _size, PROT_READ | PROT_WRITE, MAP_SHARED, _mapFile, SEEK_SET);
		if (!_buff)
			return false;

		if (owner) {
			ShareMemoryHeader& header = *(ShareMemoryHeader*)_buff;
			header.in = 0;
			header.out = 0;
			header.size = size;
		}

		return true;
	}
}
