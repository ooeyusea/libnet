#ifndef __LIBNET_SPIN_LOCK_H__
#define __LIBNET_SPIN_LOCK_H__
#include "libnet.h"
#include <atomic>

namespace libnet {
	class MCSSpinMutex {
		struct SpinNode {
			bool spin = false;
			std::atomic<SpinNode*> next = nullptr;
		};
	public:
		MCSSpinMutex() {}
		~MCSSpinMutex() {}

		MCSSpinMutex(const MCSSpinMutex&) = delete;
		MCSSpinMutex& operator=(const MCSSpinMutex&) = delete;

		inline bool try_lock() {
			SpinNode* expect = nullptr;
			if (_tail.compare_exchange_weak(expect, &_node, std::memory_order::memory_order_acquire))
				return true;

			return false;
		}

		void lock() {
			SpinNode* last = _tail.exchange(&_node, std::memory_order::memory_order_acquire);
			if (last != nullptr) {
				_node.spin = true;
				last->next.exchange(&_node, std::memory_order::memory_order_release));
				while (_node.spin)
					;
			}
		}

		void unlock() {
			SpinNode* next = nullptr;
			if (_node.next.load(std::memory_order::memory_order_relaxed) == nullptr) {
				SpinNode* expect = &_node;
				if (_tail.compare_exchange_weak(expect, nullptr, std::memory_order::memory_order_acquire))
					return;
				
				while ((next = _node.next.load(std::memory_order::memory_order_relaxed)) == nullptr)
					;
			}

			next->spin = false;
			_node.next = nullptr;
		}

	private:
		std::atomic<SpinNode*> _tail = nullptr;
		thread_local SpinNode _node;
	};
}

#endif //__LIBHTTP_H__
