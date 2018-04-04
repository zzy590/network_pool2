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

#include "network_pool.h"
#include "cached_allocator.h"
#include "http_context.h"

namespace NETWORK_POOL
{
	class ChttpSession : public CtcpCallback, public CcachedAllocator
	{
	private:
		preferred_tcp_settings m_defaultSettings;
		preferred_tcp_timeout_settings m_defaultTimeout;
		ChttpContext m_context;

		CnetworkPool *m_pool;
		socket_id m_id;

	public:
		ChttpSession(CnetworkPool *pool)
			:m_pool(pool) {}

		void allocateForPacket(const size_t suggestedSize, void *& buffer, size_t& length)
		{
			ChttpContext::allocateBuffer(suggestedSize, buffer, length);
		}
		void deallocateForPacket(void * const buffer, const size_t length, const size_t dataLength)
		{
			ChttpContext::deallocateBuffer(buffer, length, dataLength);
		}
		void packet(const void * const data, const size_t length)
		{
			m_context.pushBuffer(data, length);
			m_context.merge();
			bool bAgain;
			do
			{
				bAgain = false;
				if (m_context.analysis())
				{
					if (m_context.isGood())
					{
						ChttpContext readyContext;
						m_context.reinitForNext(readyContext);
						// Deal with request.
						static const std::string resp("HTTP/1.1 200 OK\r\nConnection:Keep-Alive\r\nContent-Length: 10\r\n\r\n0123456789");
						m_pool->sendTcp(m_id, resp.data(), resp.length());
						if (!readyContext.isKeepAlive())
							m_pool->close(m_id);
						bAgain = true;
					}
					else
						m_pool->close(m_id);
				}
			} while (bAgain);
		}

		const preferred_tcp_settings& getSettings()
		{
			return m_defaultSettings;
		}
		const preferred_tcp_timeout_settings& getTimeoutSettings()
		{
			return m_defaultTimeout;
		}

		void startup(const socket_id socketId, const Csockaddr& remote)
		{
			m_id = socketId;
		}
		void shutdown()
		{
		}

		void drop(const void * const data, const size_t length)
		{
		}
	};

	class ChttpServer : public CtcpServerCallback, public CcachedAllocator
	{
	private:
		preferred_tcp_server_settings m_defaultSettings;

		CnetworkPool *m_pool;

	public:
		ChttpServer(CnetworkPool *pool)
			:m_pool(pool) {}

		const preferred_tcp_server_settings& getSettings()
		{
			return m_defaultSettings;
		}

		CtcpCallback::ptr newTcpCallback()
		{
			return std::move(CtcpCallback::ptr(new ChttpSession(m_pool)));
		}

		void startup(const socket_id socketId, const Csockaddr& local)
		{
		}
		void shutdown()
		{
		}

		void listenError(const int err)
		{
		}
	};
}
