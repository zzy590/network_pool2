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

#include "recv_buffer.h"
#include "cached_allocator.h"

namespace NETWORK_POOL
{
	class CjsonContext : public CrecvBuffer, public CcachedAllocator
	{
	private:
		std::mutex m_contextLock;

		size_t m_analysisIndex;
		enum __json_state
		{
			state_start = 0,
			state_object,
			state_array,
			state_done,
			state_bad
		} m_state;
		size_t m_depth;
		size_t m_start;

		void init()
		{
			m_analysisIndex = 0;
			m_state = state_start;
			m_depth = 0;
			m_start = 0;
		}

	public:
		CjsonContext(const size_t initialBufferSize = 0x1000, const size_t maxBufferSize = 0x10000) // 4KB-64KB
			:CrecvBuffer(initialBufferSize, maxBufferSize)
		{
			init();
		}

		std::mutex& getContextLock()
		{
			return m_contextLock;
		}

		bool analysis()
		{
			char *ptr = (char *)CrecvBuffer::buffer().getData();
		_again:
			if (state_done == m_state || state_bad == m_state)
				return true;
			if (CrecvBuffer::nowIndex() <= m_analysisIndex) // First check whether something to decode.
				return false;
			switch (m_state)
			{
			case state_start:
				do
				{
					char ch = ptr[m_analysisIndex];
					if (!isspace(ch))
					{
						if ('{' == ch)
						{
							m_state = state_object;
							++m_depth;
							m_start = m_analysisIndex;
							++m_analysisIndex;
							goto _again;
						}
						else if ('[' == ch)
						{
							m_state = state_array;
							++m_depth;
							m_start = m_analysisIndex;
							++m_analysisIndex;
							goto _again;
						}
						else
						{
							m_state = state_bad;
							return true;
						}
					}
				} while (++m_analysisIndex < CrecvBuffer::nowIndex());
				break;

			case state_object:
				do
				{
					char ch = ptr[m_analysisIndex];
					if ('{' == ch)
						++m_depth;
					else if ('}' == ch)
					{
						--m_depth;
						if (0 == m_depth)
						{
							m_state = state_done;
							++m_analysisIndex;
							return true;
						}
					}
				} while (++m_analysisIndex < CrecvBuffer::nowIndex());
				break;

			case state_array:
				do
				{
					char ch = ptr[m_analysisIndex];
					if ('[' == ch)
						++m_depth;
					else if (']' == ch)
					{
						--m_depth;
						if (0 == m_depth)
						{
							m_state = state_done;
							++m_analysisIndex;
							return true;
						}
					}
				} while (++m_analysisIndex < CrecvBuffer::nowIndex());
				break;

			default:
				break;
			}
			return false;
		}

		bool isGood() const
		{
			return m_state == state_done;
		}

		bool extract(Cbuffer& buffer) const
		{
			if (m_state != state_done)
				return false;
			buffer.set((const char *)CrecvBuffer::buffer().getData() + m_start, m_analysisIndex - m_start);
			return true;
		}

		bool referenceContent(const char *& data, size_t& length)
		{
			if (m_state != state_done)
				return false;
			data = (const char *)CrecvBuffer::buffer().getData() + m_start;
			length = m_analysisIndex - m_start;
			return true;
		}

		void restart()
		{
			m_state = state_start;
			m_depth = 0;
			m_start = m_analysisIndex;
		}

		void clear()
		{
			if (state_done == m_state)
			{
				// Move extra.
				size_t extra = CrecvBuffer::nowIndex() - m_analysisIndex;
				char *ptr = (char *)CrecvBuffer::buffer().getData();
				memmove(ptr, ptr + m_analysisIndex, extra);
				CrecvBuffer::nowIndex() = extra;

				m_analysisIndex = 0;
				m_state = state_start;
				m_depth = 0;
				m_start = 0;
			}
			else if (m_state != state_bad && m_start != 0)
			{
				// Move now and extra.
				size_t extra = CrecvBuffer::nowIndex() - m_start;
				char *ptr = (char *)CrecvBuffer::buffer().getData();
				memmove(ptr, ptr + m_start, extra);
				CrecvBuffer::nowIndex() = extra;

				m_analysisIndex -= m_start;
				m_start = 0;
			}
		}
	};
}
