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

#include <atomic>

#include "cached_allocator.h"

namespace NETWORK_POOL
{
	class CatomicCounter : public std::atomic<size_t>, public CcachedAllocator {};

	template<class T>
	class CmtSharedPtr : public CcachedAllocator
	{
	private:
		CatomicCounter *m_count;
		T* m_ptr;

	public:
		CmtSharedPtr()
			:m_count(nullptr), m_ptr(nullptr) {}
		CmtSharedPtr(T *ptr)
		{
			if (nullptr == ptr)
			{
				m_count = nullptr;
				m_ptr = nullptr;
			}
			else
			{
				m_count = new CatomicCounter();
				m_ptr = ptr;
				++*m_count;
			}
		}
		CmtSharedPtr(const CmtSharedPtr& another)
			:m_count(another.m_count), m_ptr(another.m_ptr)
		{
			if (m_count != nullptr)
				++*m_count;
		}
		CmtSharedPtr(CmtSharedPtr&& another)
			:m_count(another.m_count), m_ptr(another.m_ptr)
		{
			another.m_count = nullptr;
			another.m_ptr = nullptr;
		}
		~CmtSharedPtr()
		{
			if (m_count != nullptr && 0 == --*m_count)
			{
				delete m_count;
				delete m_ptr;
			}
		}

		const CmtSharedPtr& operator=(const CmtSharedPtr& another)
		{
			// Remove old.
			if (m_count != nullptr && 0 == --*m_count)
			{
				delete m_count;
				delete m_ptr;
			}
			// Copy new.
			m_count = another.m_count;
			m_ptr = another.m_ptr;
			if (m_count != nullptr)
				++*m_count;
		}
		const CmtSharedPtr& operator=(CmtSharedPtr&& another)
		{
			// Remove old.
			if (m_count != nullptr && 0 == --*m_count)
			{
				delete m_count;
				delete m_ptr;
			}
			// Move new.
			m_count = another.m_count;
			m_ptr = another.m_ptr;
			another.m_count = nullptr;
			another.m_ptr = nullptr;
		}

		T& operator*()
		{
			return *m_ptr;
		}
		T* operator->()
		{
			return m_ptr;
		}

		operator bool() const
		{
			return m_count != nullptr;
		}

		T* get() const
		{
			return m_ptr;
		}
		size_t count() const
		{
			return *m_count;
		}
		bool unique() const
		{
			return 1 == *m_count;
		}

		void reset(T *ptr = nullptr)
		{
			// Remove old.
			if (m_count != nullptr && 0 == --*m_count)
			{
				delete m_count;
				delete m_ptr;
			}
			// allocate new.
			if (nullptr == ptr)
			{
				m_count = nullptr;
				m_ptr = nullptr;
			}
			else
			{
				m_count = new CatomicCounter();
				m_ptr = ptr;
				++*m_count;
			}
		}
	};
}
