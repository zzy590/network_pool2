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

#include <cstring>

#include "uv.h"

#include "cached_allocator.h"

namespace NETWORK_POOL
{
	class Cbuffer : public CcachedAllocator
	{
	private:
		void *m_data;
		size_t m_length;
		size_t m_maxLength;

		inline void transfer(uv_buf_t& buf) // Internal use only.
		{
			buf.base = (char *)m_data;
		#ifdef _MSC_VER
			buf.len = (ULONG)m_length;
		#else
			buf.len = m_length;
		#endif
			m_data = nullptr;
			m_maxLength = m_length = 0;
		}

		friend class CnetworkPool;

	public:
		Cbuffer()
			:m_data(nullptr), m_length(0), m_maxLength(0) {}
		Cbuffer(const size_t length)
		{
			if (0 == length)
			{
				m_data = nullptr;
				m_maxLength = m_length = 0;
			}
			else
			{
				m_data = __alloc_throw(length);
				m_maxLength = m_length = length;
			}
		}
		Cbuffer(const void * const data, const size_t length)
		{
			if (0 == length)
			{
				m_data = nullptr;
				m_maxLength = m_length = 0;
			}
			else
			{
				m_data = __alloc_throw(length);
				memcpy(m_data, data, length);
				m_maxLength = m_length = length;
			}
		}
		Cbuffer(const Cbuffer& another)
		{
			if (0 == another.m_length)
			{
				m_data = nullptr;
				m_maxLength = m_length = 0;
			}
			else
			{
				m_data = __alloc_throw(another.m_length);
				memcpy(m_data, another.m_data, another.m_length);
				m_maxLength = m_length = another.m_length;
			}
		}
		Cbuffer(Cbuffer&& another)
			:m_data(another.m_data), m_length(another.m_length), m_maxLength(another.m_maxLength)
		{
			another.m_data = nullptr;
			another.m_maxLength = another.m_length = 0;
		}
		~Cbuffer()
		{
			__free(m_data); // No need to check nullptr.
		}

		const Cbuffer& operator=(const Cbuffer& another)
		{
			if (another.m_length <= m_maxLength)
			{
				if (another.m_length > 0)
					memcpy(m_data, another.m_data, another.m_length);
				m_length = another.m_length;
			}
			else
			{
				__free(m_data); // No need to check nullptr.
				m_data = __alloc_throw(another.m_length);
				memcpy(m_data, another.m_data, another.m_length);
				m_maxLength = m_length = another.m_length;
			}
			return *this;
		}
		const Cbuffer& operator=(Cbuffer&& another)
		{
			__free(m_data); // No need to check nullptr.
			m_data = another.m_data;
			m_length = another.m_length;
			m_maxLength = another.m_maxLength;
			another.m_data = nullptr;
			another.m_maxLength = another.m_length = 0;
			return *this;
		}

		inline void set(const void * const data, const size_t length)
		{
			if (length <= m_maxLength)
			{
				if (length > 0)
					memcpy(m_data, data, length);
				m_length = length;
			}
			else
			{
				__free(m_data); // No need to check nullptr.
				m_data = __alloc_throw(length);
				memcpy(m_data, data, length);
				m_maxLength = m_length = length;
			}
		}

		inline void resize(const size_t preferLength, const size_t validLength = 0)
		{
			if (preferLength <= m_maxLength)
			{
				m_length = preferLength;
				return;
			}
			// We need to enlarge the buffer.
			size_t copy = validLength > m_length ? m_length : validLength;
			if (copy > 0)
			{
				void *newBuffer = __alloc_throw(preferLength);
				memcpy(newBuffer, m_data, copy);
				__free(m_data);
				m_data = newBuffer;
				m_maxLength = m_length = preferLength;
			}
			else
			{
				__free(m_data); // No need to check nullptr.
				m_data = __alloc_throw(preferLength);
				m_maxLength = m_length = preferLength;
			}
		}

		inline void *getData() const
		{
			return m_data;
		}
		inline size_t getLength() const
		{
			return m_length;
		}
		inline size_t getMaxLength() const
		{
			return m_maxLength;
		}
	};
}
