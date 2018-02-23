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

#pragma once

#include <cstddef>

namespace NETWORK_POOL
{
	void *__alloc(const size_t size);
	void *__alloc_throw(const size_t size);
	void __free(void * const ptr);

	bool __dynamic_set_cache(const size_t size, const size_t cacheNumber);
	void __get_usage_data(size_t& count, size_t& size);

	class CcachedAllocator
	{
	public:
		void *operator new(size_t size)
		{
			return __alloc_throw(size);
		}
		void *operator new(size_t size, const std::nothrow_t& nothrow_value)
		{
			return __alloc(size);
		}

		void operator delete(void *ptr)
		{
			__free(ptr);
		}
		void operator delete(void *ptr, const std::nothrow_t& nothrow_constant)
		{
			__free(ptr);
		}
		// For C++14.
		void operator delete(void *ptr, size_t size)
		{
			__free(ptr);
		}
		void operator delete(void *ptr, size_t size, const std::nothrow_t& nothrow_constant)
		{
			__free(ptr);
		}
	};
}
