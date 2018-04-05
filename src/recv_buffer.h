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

#include <mutex>
#include <deque>
#include <utility>

#include "buffer.h"
#include "cached_allocator.h"

namespace NETWORK_POOL
{
	#define RECV_BUFFER_SIZE (0xC00)

	class CrecvBuffer
	{
	private:
		size_t m_initialBufferSize;
		size_t m_maxBufferSize;

		Cbuffer m_buffer;
		size_t m_nowIndex;
		bool m_bOverflow;

		std::mutex m_lock;
		std::deque<std::pair<void *, size_t>> m_rawBuffers;

	public:
		CrecvBuffer(const size_t initialBufferSize, const size_t maxBufferSize)
			:m_initialBufferSize(initialBufferSize), m_maxBufferSize(maxBufferSize), m_nowIndex(0), m_bOverflow(false) {}

		~CrecvBuffer()
		{
			for (const auto& pair : m_rawBuffers)
				__free(pair.first);
			m_rawBuffers.clear();
		}

		//
		// Following 3 functions should be called in event loop.
		//

		static void allocateBuffer(const size_t suggestedSize, void *& buffer, size_t& length)
		{
			buffer = __alloc(RECV_BUFFER_SIZE);
			length = suggestedSize < RECV_BUFFER_SIZE ? suggestedSize : RECV_BUFFER_SIZE;
		}

		void pushBuffer(const void * const data, const size_t length)
		{
			if (length > 0)
			{
				std::lock_guard<std::mutex> guard(m_lock);
				m_rawBuffers.push_back(std::make_pair((void *)data, length));
			}
		}

		static void deallocateBuffer(void * const buffer, const size_t length, const size_t dataLength)
		{
			if (0 == dataLength)
				__free(buffer);
		}

		//
		// Following functions should be called in a single thread or worker thread.
		//

		void merge()
		{
			if (0 == m_buffer.getMaxLength())
			{
				if (m_maxBufferSize < m_initialBufferSize)
					m_maxBufferSize = m_initialBufferSize;
				m_buffer.resize(m_initialBufferSize);
			}
			{
				std::lock_guard<std::mutex> guard(m_lock);
				size_t totalAppend = 0;
				for (const auto& pair : m_rawBuffers)
					totalAppend += pair.second;
				if (totalAppend + m_nowIndex > m_maxBufferSize)
					m_bOverflow = true;
				else
				{
					size_t targetSize = m_buffer.getLength();
					while (targetSize - m_nowIndex < totalAppend)
						targetSize *= 2;
					if (targetSize > m_maxBufferSize)
						targetSize = m_maxBufferSize;
					m_buffer.resize(targetSize, m_nowIndex);
					char *ptr = (char *)m_buffer.getData() + m_nowIndex;
					for (const auto& pair : m_rawBuffers)
					{
						memcpy(ptr, pair.first, pair.second);
						ptr += pair.second;
						m_nowIndex += pair.second;
					}
				}
				for (const auto& pair : m_rawBuffers)
					__free(pair.first);
				m_rawBuffers.clear();
			}
		}

		const size_t& initialBufferSize() const
		{
			return m_initialBufferSize;
		}
		size_t& initialBufferSize()
		{
			return m_initialBufferSize;
		}

		const size_t& maxBufferSize() const
		{
			return m_maxBufferSize;
		}
		size_t& maxBufferSize()
		{
			return m_maxBufferSize;
		}

		const Cbuffer& buffer() const
		{
			return m_buffer;
		}
		Cbuffer& buffer()
		{
			return m_buffer;
		}

		const size_t& nowIndex() const
		{
			return m_nowIndex;
		}
		size_t& nowIndex()
		{
			return m_nowIndex;
		}

		const bool& bOverflow() const
		{
			return m_bOverflow;
		}
		bool& bOverflow()
		{
			return m_bOverflow;
		}
	};
}
