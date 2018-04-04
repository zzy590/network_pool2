/* Copyright (c) 2018 Zhenyu Zhang. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <atomic>
#include <mutex>
#include <thread>
#include <cstdlib>

#include "uv.h"

#include "cached_allocator.h"
#include "buffer.h"
#include "network_node.h"
#include "uv_wrapper.h"
#include "network_pool.h"
#include "recv_buffer.h"

#define CA_DBG 0
#if CA_DBG
	#include <stdio.h>
	#include <cstring>
	#define CA_FPRINTF(_x) { fprintf _x; }
#else
	#define CA_FPRINTF(_x) {}
#endif

namespace NETWORK_POOL
{
	static const size_t s_maxAllocatorSlot = 0x1000; // Only cache block which small than 4KB.

	// Usage data.
	static std::atomic<size_t> s_count(0);
	static std::atomic<size_t> s_size(0);

	// Memory cache.
	static std::mutex s_globalLock;
	static void *s_allocatorStore[s_maxAllocatorSlot] = { 0 };
	static volatile size_t s_allocatorStoreCount[s_maxAllocatorSlot] = { 0 };
	static volatile size_t s_maxAllocatorStoreNumber[s_maxAllocatorSlot] = { 0 };

	static std::once_flag s_storeNumberInit;

	static inline void initStoreNumber()
	{
		std::call_once(s_storeNumberInit, []()
		{
			#define set_max_store_number(_s, _n) { if ((_s) < s_maxAllocatorSlot) s_maxAllocatorStoreNumber[(_s)] = (_n); }
			set_max_store_number(sizeof(uv_shutdown_t), 1024);
			set_max_store_number(sizeof(uv_connect_t), 1024);
			set_max_store_number(sizeof(Cbuffer), 512);
			set_max_store_number(sizeof(Csockaddr), 512);
			set_max_store_number(sizeof(CnetworkNode), 512);
			set_max_store_number(sizeof(CnetworkPair), 0);
			set_max_store_number(sizeof(Casync), 0);
			set_max_store_number(sizeof(CtcpServer), 0);
			set_max_store_number(sizeof(Ctcp), 16384);
			set_max_store_number(sizeof(Cudp), 0);
			set_max_store_number(sizeof(CnetworkPool::__write_with_info), 4096);
			set_max_store_number(sizeof(CnetworkPool::__udp_send_with_info), 4096);
			set_max_store_number(RECV_BUFFER_SIZE, 4096);
			#undef set_max_store_number
		});
	}

	void *__alloc(const size_t size)
	{
		initStoreNumber();
		CA_FPRINTF((stderr, "fa alloc %u.\n", size));
		size_t allocSize = sizeof(size_t) + size;
		if (allocSize < size) // In case of overflow.
		{
			CA_FPRINTF((stderr, "malloc_no_throw size overflow.\n"));
			std::terminate();
		}
		void *ptr = nullptr;
		if (size >= s_maxAllocatorSlot || (0 == s_maxAllocatorStoreNumber[size] && 0 == s_allocatorStoreCount[size])) // Just a prob(Accurate calculate will hold the lock).
			ptr = malloc(allocSize);
		else
		{
			CA_FPRINTF((stderr, "fa alloc use store.\n"));
			s_globalLock.lock();
			if (s_allocatorStore[size] != nullptr)
			{
				ptr = s_allocatorStore[size];
				s_allocatorStore[size] = *(void **)ptr;
				--s_allocatorStoreCount[size];
			}
			s_globalLock.unlock();
			if (nullptr == ptr)
				ptr = malloc(allocSize);
		}
		if (nullptr == ptr)
			return nullptr;
		*(size_t *)ptr = allocSize;
		++s_count;
		s_size += allocSize;
	#if CA_DBG
		memset((size_t *)ptr + 1, -1, size);
	#endif
		return (size_t *)ptr + 1;
	}

	void *__alloc_throw(const size_t size)
	{
		void *ptr = __alloc(size);
		if (nullptr == ptr)
			throw std::bad_alloc();
		return ptr;
	}

	void __free(void * const ptr)
	{
		if (nullptr == ptr)
			return;
		CA_FPRINTF((stderr, "fa free.\n"));
		void *org = (size_t *)ptr - 1;
		size_t allocSize = *(size_t *)org;
		size_t orgSize = allocSize - sizeof(size_t);
		--s_count;
		s_size -= allocSize;
	#if CA_DBG
		memset(org, -1, allocSize);
	#endif
		if (orgSize >= s_maxAllocatorSlot || 0 == s_maxAllocatorStoreNumber[orgSize])
			return free(org);
		CA_FPRINTF((stderr, "fa free use store.\n"));
		s_globalLock.lock();
		if (s_allocatorStoreCount[orgSize] < s_maxAllocatorStoreNumber[orgSize])
		{
			*(void **)org = s_allocatorStore[orgSize];
			s_allocatorStore[orgSize] = org;
			++s_allocatorStoreCount[orgSize];
			org = nullptr;
		}
		s_globalLock.unlock();
		free(org); // Free can deal with nullptr.
	}

	bool __dynamic_set_cache(const size_t size, const size_t cacheNumber)
	{
		if (size >= s_maxAllocatorSlot)
			return false;
		s_globalLock.lock();
		s_maxAllocatorStoreNumber[size] = cacheNumber;
		s_globalLock.unlock();
		return true;
	}

	void __get_usage_data(size_t& count, size_t& size)
	{
		count = s_count;
		size = s_size;
	}
}
