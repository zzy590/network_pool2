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

#include "network_pool.h"
#include "cached_allocator.h"
#include "http_context.h"
#include "mt_shared_ptr.h"
#include "work_queue.h"

namespace NETWORK_POOL
{
	class ChttpTask : public Ctask, public CcachedAllocator
	{
	private:
		CnetworkPool& m_pool;
		socket_id m_socketId;
		CmtSharedPtr<ChttpContext> m_context;

	public:
		ChttpTask(CnetworkPool& pool, socket_id socketId, CmtSharedPtr<ChttpContext> context)
			:m_pool(pool), m_socketId(socketId), m_context(context) {}

		void run()
		{
			if (m_context.unique())
				return;
			std::lock_guard<std::mutex> guard(m_context->getContextLock());
			bool bAgain;
			do
			{
				bAgain = false;
				m_context->merge();
				if (!m_context.unique() && m_context->analysis())
				{
					if (!m_context.unique() && m_context->isGood())
					{
						// Deal with the request.

						static const std::string resp("HTTP/1.1 200 OK\r\nConnection:Keep-Alive\r\nContent-Length: 10\r\n\r\n0123456789");
						m_pool.sendTcp(m_socketId, resp.data(), resp.length());
						if (!m_context->isKeepAlive())
						{
							m_pool.close(m_socketId);
							break;
						}

						m_context->clear();
						bAgain = true;
					}
					else
						m_pool.close(m_socketId);
				}
			} while (bAgain);
		}
	};

	class ChttpSession : public CtcpCallback, public CcachedAllocator
	{
	private:
		preferred_tcp_settings m_defaultSettings;
		preferred_tcp_timeout_settings m_defaultTimeout;

		CnetworkPool& m_pool;
		CworkQueue& m_workQueue;

		socket_id m_socketId;
		CmtSharedPtr<ChttpContext> m_context;

	public:
		ChttpSession(CnetworkPool& pool, CworkQueue& workQueue)
			:m_pool(pool), m_workQueue(workQueue) {}

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
			m_context->pushBuffer(data, length);
			m_workQueue.pushTask(std::move(Ctask::ptr(new ChttpTask(m_pool, m_socketId, m_context))));
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
			m_socketId = socketId;
			m_context.reset(new ChttpContext());
		}
		void shutdown()
		{
			m_context.reset();
		}

		void drop(const void * const data, const size_t length)
		{
		}
	};

	class ChttpServer : public CtcpServerCallback, public CcachedAllocator
	{
	private:
		preferred_tcp_server_settings m_defaultSettings;

		CnetworkPool& m_pool;
		CworkQueue& m_workQueue;

	public:
		ChttpServer(CnetworkPool& pool, CworkQueue& workQueue)
			:m_pool(pool), m_workQueue(workQueue)
		{
			__dynamic_set_cache(sizeof(ChttpSession), 16384);
			__dynamic_set_cache(sizeof(ChttpTask), 16384);
		}

		const preferred_tcp_server_settings& getSettings()
		{
			return m_defaultSettings;
		}

		CtcpCallback::ptr newTcpCallback()
		{
			return std::move(CtcpCallback::ptr(new ChttpSession(m_pool, m_workQueue)));
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
