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
#include <string>
#include <vector>
#include <unordered_map>
#include <utility>

#include "recv_buffer.h"

namespace NETWORK_POOL
{
	class ChttpContext : public CrecvBuffer
	{
	private:
		size_t m_analysisIndex;
		enum __http_state
		{
			state_start = 0,
			state_read_header,
			state_read_body,
			state_read_chunk_header,
			state_read_chunk_body,
			state_read_chunk_footer,
			state_done,
			state_bad
		} m_state;
		std::vector<std::pair<size_t, size_t>> m_lines; // <startIndex, length>
		size_t m_headerSize;
		bool m_bKeepAlive;
		bool m_bChunked;
		size_t m_contentLength;
		size_t m_nowChunkSize;
		bool m_bChunkSizeStart;
		bool m_bChunkSizeDone;
		std::vector<std::pair<size_t, size_t>> m_chunks; // <startIndex, length>

		void init()
		{
			m_analysisIndex = 0;
			m_state = state_start;
			m_lines.clear();
			m_lines.reserve(16);
			m_headerSize = 0;
			m_bKeepAlive = false;
			m_bChunked = false;
			m_contentLength = 0;
			m_nowChunkSize = 0;
			m_bChunkSizeStart = false;
			m_bChunkSizeDone = false;
			m_chunks.clear();
		}

		#ifndef _MSC_VER
			#define _strnicmp strncasecmp
		#endif

		void kvDecoder(const char *name, const size_t nameLength, const char *value, const size_t valueLength)
		{
			if (10 == nameLength && 0 == _strnicmp("Connection", name, 10))
				m_bKeepAlive = (10 == valueLength && 0 == _strnicmp("Keep-Alive", value, 10));
			else if (14 == nameLength && 0 == _strnicmp("Content-Length", name, 14))
				m_contentLength = atoi(value); // Each line will have null-terminator so it's safe to do it.
			else if (17 == nameLength && 0 == _strnicmp("Transfer-Encoding", name, 17))
				m_bChunked = (7 == valueLength && 0 == _strnicmp("chunked", value, 7));
		}

		void decoderHeaderAndUpdateState()
		{
			const char *ptr = (const char *)CrecvBuffer::buffer().getData();
			for (const auto& lineInfo : m_lines)
			{
				if (lineInfo.first > m_headerSize) // Only deal with header.
					break;
				if ((size_t)-1 == lineInfo.second) // Unknown length.
					continue;
				const char *name_head = ptr + lineInfo.first;
				while (isspace(*name_head))
					++name_head;
				const char *colon = strchr(name_head, ':');
				if (nullptr == colon || colon == name_head) // Colon not found or empty header.
					continue;
				const char *name_tail = colon;
				while (isspace(*(name_tail - 1)))
					--name_tail;
				const char *value_head = colon + 1;
				while (isspace(*value_head))
					++value_head;
				const char *value_tail = ptr + lineInfo.first + lineInfo.second;
				if (value_tail == value_head) // Empty value.
					continue;
				while (isspace(*(value_tail - 1)))
					--value_tail;
				kvDecoder(name_head, name_tail - name_head, value_head, value_tail - value_head);
			}
			// Switch state.
			if (m_bChunked)
			{
				m_state = state_read_chunk_header;
				m_nowChunkSize = 0;
				m_bChunkSizeStart = false;
				m_bChunkSizeDone = false;
			}
			else if (m_contentLength > 0)
				m_state = state_read_body;
			else
				m_state = state_done;
		}

	public:
		ChttpContext(const size_t maxBufferSize = 0x1000000) // 16MB
			:CrecvBuffer(maxBufferSize)
		{
			init();
		}

		bool analysis()
		{
			if (CrecvBuffer::bOverflow())
			{
				m_state = state_bad;
				return true;
			}
		_again:
			if (state_done == m_state || state_bad == m_state)
				return true;
			if (CrecvBuffer::nowIndex() <= m_analysisIndex) // First check whether something to decode.
				return false;
			char *ptr = (char *)CrecvBuffer::buffer().getData();
			switch (m_state)
			{
			case state_start:
				if (m_analysisIndex != 0 || '\n' == ptr[0])
				{
					m_state = state_bad;
					return true;
				}
				else
				{
					m_state = state_read_header;
					m_lines.push_back(std::make_pair(0, -1));
					goto _again;
				}
				break;

			case state_read_header:
				do // At least one mew byte(checked before).
				{
					if ('\n' == ptr[m_analysisIndex])
					{
						if (ptr[m_analysisIndex - 1] != '\r') // ptr[0] != '\n' checked before.
						{
							m_state = state_bad;
							return true;
						}
						ptr[m_analysisIndex - 1] = ptr[m_analysisIndex] = 0; // Null-terminate the line.
						auto& lastLine = m_lines.back();
						lastLine.second = m_analysisIndex - 1 - lastLine.first;
						if (0 == lastLine.second)
						{
							m_lines.pop_back();
							m_headerSize = ++m_analysisIndex;
							decoderHeaderAndUpdateState();
							goto _again;
						}
						m_lines.push_back(std::make_pair(m_analysisIndex + 1, -1));
					}
				} while (++m_analysisIndex < CrecvBuffer::nowIndex());
				break;

			case state_read_body:
				if (CrecvBuffer::nowIndex() - m_analysisIndex >= m_contentLength)
				{
					m_chunks.push_back(std::make_pair(m_analysisIndex, m_contentLength));
					m_analysisIndex += m_contentLength;
					m_state = state_done;
					return true;
				}
				break;

			case state_read_chunk_header:
				do
				{
					char ch = ptr[m_analysisIndex];
					if ('\n' == ch)
					{
						if (ptr[m_analysisIndex - 1] != '\r') // ptr[0] != '\n' checked before.
						{
							m_state = state_bad;
							return true;
						}
						ptr[m_analysisIndex - 1] = ptr[m_analysisIndex] = 0; // Null-terminate the line.
						++m_analysisIndex;
						if (m_nowChunkSize > 0)
							m_state = state_read_chunk_body;
						else
						{
							m_state = state_read_chunk_footer;
							m_lines.push_back(std::make_pair(m_analysisIndex, -1));
						}
						goto _again;
					}
					else if(!m_bChunkSizeDone)
					{
						if (ch >= '0' && ch <= '9')
						{
							m_nowChunkSize = (m_nowChunkSize << 4) + ch - '0';
							m_bChunkSizeStart = true;
						}
						else if (ch >= 'a' && ch <= 'f')
						{
							m_nowChunkSize = (m_nowChunkSize << 4) + ch - 'a' + 10;
							m_bChunkSizeStart = true;
						}
						else if (ch >= 'A' && ch <= 'F')
						{
							m_nowChunkSize = (m_nowChunkSize << 4) + ch - 'A' + 10;
							m_bChunkSizeStart = true;
						}
						else
						{
							if (m_bChunkSizeStart || !isspace(ch))
								m_bChunkSizeDone = true;
						}
					}
				} while (++m_analysisIndex < CrecvBuffer::nowIndex());
				break;

			case state_read_chunk_body:
				if (CrecvBuffer::nowIndex() - m_analysisIndex >= m_nowChunkSize + 2) // With the ending '\r\n'.
				{
					m_chunks.push_back(std::make_pair(m_analysisIndex, m_nowChunkSize));
					m_analysisIndex += m_nowChunkSize + 2;
					m_state = state_read_chunk_header;
					m_nowChunkSize = 0;
					m_bChunkSizeStart = false;
					m_bChunkSizeDone = false;
					goto _again;
				}
				break;

			case state_read_chunk_footer:
				do // At least one mew byte(checked before).
				{
					if ('\n' == ptr[m_analysisIndex])
					{
						if (ptr[m_analysisIndex - 1] != '\r') // ptr[0] != '\n' checked before.
						{
							m_state = state_bad;
							return true;
						}
						ptr[m_analysisIndex - 1] = ptr[m_analysisIndex] = 0; // Null-terminate the line.
						auto& lastLine = m_lines.back();
						lastLine.second = m_analysisIndex - 1 - lastLine.first;
						if (0 == lastLine.second)
						{
							m_lines.pop_back();
							++m_analysisIndex;
							m_state = state_done;
							return true;
						}
						m_lines.push_back(std::make_pair(m_analysisIndex + 1, -1));
					}
				} while (++m_analysisIndex < CrecvBuffer::nowIndex());
				break;

			case state_done:
			case state_bad:
				return true;

			default:
				break;
			}
			return false;
		}

		bool isGood() const
		{
			return m_state == state_done;
		}

		bool isKeepAlive() const
		{
			return m_bKeepAlive;
		}

		// For request. (method, uri, version)
		// For response. (version, code, status)
		bool getInfo(std::string& first, std::string& second, std::string& thrid) const
		{
			if (m_state != state_done)
				return false;
			const char *line = (const char *)CrecvBuffer::buffer().getData() + m_lines[0].first; // First line.
			const char *b1 = strchr(line, ' ');
			if (nullptr == b1)
				return false;
			const char *b2 = strchr(b1 + 1, ' ');
			if (nullptr == b1)
				return false;
			first.assign(line, b1 - line);
			second.assign(b1 + 1, b2 - b1 - 1);
			thrid.assign(b2 + 1);
			return true;
		}

		bool getParameter(std::unordered_multimap<std::string, std::string>& parameters) const
		{
			if (m_state != state_done)
				return false;
			const char *ptr = (const char *)CrecvBuffer::buffer().getData();
			for (const auto& lineInfo : m_lines)
			{
				if ((size_t)-1 == lineInfo.second) // Unknown length.
					continue;
				const char *name_head = ptr + lineInfo.first;
				while (isspace(*name_head))
					++name_head;
				const char *colon = strchr(name_head, ':');
				if (nullptr == colon || colon == name_head) // Colon not found or empty header.
					continue;
				const char *name_tail = colon;
				while (isspace(*(name_tail - 1)))
					--name_tail;
				const char *value_head = colon + 1;
				while (isspace(*value_head))
					++value_head;
				const char *value_tail = ptr + lineInfo.first + lineInfo.second;
				if (value_tail == value_head) // Empty value.
					continue;
				while (isspace(*(value_tail - 1)))
					--value_tail;
				parameters.insert(std::make_pair(std::string(name_head, name_tail - name_head), std::string(value_head, value_tail - value_head)));
			}
			return true;
		}

		// Return content or merged chunk.
		bool getContent(Cbuffer& buffer)
		{
			if (m_state != state_done)
				return false;
			size_t total = 0;
			for (const auto& pair : m_chunks)
				total += pair.second;
			buffer.resize(total);
			char *src = (char *)CrecvBuffer::buffer().getData();
			char *dst = (char *)buffer.getData();
			for (const auto& pair : m_chunks)
			{
				memcpy(dst, src + pair.first, pair.second);
				dst += pair.second;
			}
			return true;
		}

		bool reinitForNext(ChttpContext& former)
		{
			if (m_state != state_done)
				return false;

			// Move current to former.
			former.maxBufferSize() = CrecvBuffer::maxBufferSize();

			former.buffer().set(CrecvBuffer::buffer().getData(), m_analysisIndex);
			former.nowIndex() = m_analysisIndex;

			former.m_analysisIndex = m_analysisIndex;
			former.m_state = state_done;
			former.m_lines = std::move(m_lines);
			former.m_headerSize = m_headerSize;
			former.m_bKeepAlive = m_bKeepAlive;
			former.m_bChunked = m_bChunked;
			former.m_contentLength = m_contentLength;
			former.m_nowChunkSize = 0;
			former.m_bChunkSizeStart = false;
			former.m_bChunkSizeDone = false;
			former.m_chunks = std::move(m_chunks);

			// Move extra.
			size_t extra = CrecvBuffer::nowIndex() - m_analysisIndex;
			char *ptr = (char *)CrecvBuffer::buffer().getData();
			memmove(ptr, ptr + m_analysisIndex, extra);
			CrecvBuffer::nowIndex() = extra;

			// Set others.
			init();
			return true;
		}
	};
}
