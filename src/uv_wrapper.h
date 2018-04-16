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

#include <memory>

#include "uv.h"

#include "network_type.h"
#include "network_setting.h"
#include "network_callback.h"
#include "cached_allocator.h"

namespace NETWORK_POOL
{
	template<class T>
	struct __uv_wrapper_deleter
	{
		void operator()(T * const p)
		{
			T::close(p);
		}
	};

	#define PRIVATE_CLASS(_class) \
		private: \
			_class() {} \
			~_class() {} \
			_class(const _class& another) = delete; \
			_class(_class&& another) = delete; \
			const _class& operator=(const _class& another) = delete; \
			const _class& operator=(_class&& another) = delete;
	#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))

	class CnetworkPool;

	class Casync : public CcachedAllocator
	{
		PRIVATE_CLASS(Casync)
	private:
		uv_async_t m_async;
		CnetworkPool *m_pool;

		static void close(Casync * const async)
		{
			if (nullptr == async)
				return;
			if (!async->isClosing())
			{
				uv_close((uv_handle_t *)&async->m_async,
					[](uv_handle_t *handle)
				{
					delete Casync::obtain(handle);
				});
			}
		}

		template<class T>
		friend struct __uv_wrapper_deleter;

	public:
		typedef std::unique_ptr<Casync, __uv_wrapper_deleter<Casync>> ptr;

		inline uv_async_t *getAsync()
		{
			return &m_async;
		}
		inline CnetworkPool *getPool() const
		{
			return m_pool;
		}

		inline bool isClosing() const
		{
			return uv_is_closing((const uv_handle_t *)&m_async) != 0;
		}

		static inline Casync *obtain(uv_handle_t * const handle)
		{
			return container_of(handle, Casync, m_async);
		}
		static inline Casync *obtain(uv_async_t * const async)
		{
			return container_of(async, Casync, m_async);
		}

		static ptr alloc(CnetworkPool * const pool, uv_loop_t * const loop, const uv_async_cb cb)
		{
			Casync *async = new (std::nothrow) Casync();
			if (nullptr == async)
				return std::move(ptr());
			async->m_pool = pool;
			if (uv_async_init(loop, &async->m_async, cb) != 0)
			{
				delete async;
				return std::move(ptr());
			}
			return std::move(ptr(async));
		}
	};

	class CtcpServer : public CcachedAllocator
	{
		PRIVATE_CLASS(CtcpServer)
	private:
		uv_tcp_t m_tcp;
		CnetworkPool *m_pool;
		CtcpServerCallback::ptr m_callback;
		socket_id m_socketId;

		static void close(CtcpServer * const tcpServer)
		{
			if (nullptr == tcpServer)
				return;
			if (!tcpServer->isClosing())
			{
				uv_close((uv_handle_t *)&tcpServer->m_tcp,
					[](uv_handle_t *handle)
				{
					delete CtcpServer::obtain(handle);
				});
			}
		}

		template<class T>
		friend struct __uv_wrapper_deleter;

	public:
		typedef std::unique_ptr<CtcpServer, __uv_wrapper_deleter<CtcpServer>> ptr;

		inline bool customize()
		{
			const preferred_tcp_server_settings& settings = m_callback->getSettings();
			if (uv_tcp_simultaneous_accepts(&m_tcp, settings.tcp_enable_simultaneous_accepts) != 0)
				return false;
			return true;
		}

		inline uv_tcp_t *getTcp()
		{
			return &m_tcp;
		}
		inline uv_stream_t *getStream()
		{
			return (uv_stream_t *)&m_tcp;
		}
		inline CnetworkPool *getPool() const
		{
			return m_pool;
		}
		inline const CtcpServerCallback::ptr& getCallback() const
		{
			return m_callback;
		}
		inline socket_id getSocketId() const
		{
			return m_socketId;
		}

		inline bool isClosing() const
		{
			return uv_is_closing((const uv_handle_t *)&m_tcp) != 0;
		}

		static inline CtcpServer *obtain(uv_handle_t * const handle)
		{
			return container_of(handle, CtcpServer, m_tcp);
		}
		static inline CtcpServer *obtain(uv_stream_t * const stream)
		{
			return container_of(stream, CtcpServer, m_tcp);
		}
		static inline CtcpServer *obtain(uv_tcp_t * const tcp)
		{
			return container_of(tcp, CtcpServer, m_tcp);
		}

		static ptr alloc(CnetworkPool * const pool, uv_loop_t * const loop, CtcpServerCallback::ptr&& callback, const socket_id socketId)
		{
			CtcpServer *tcpServer = new (std::nothrow) CtcpServer();
			if (nullptr == tcpServer)
				return std::move(ptr());
			tcpServer->m_pool = pool;
			tcpServer->m_callback = std::forward<CtcpServerCallback::ptr>(callback);
			tcpServer->m_socketId = socketId;
			if (uv_tcp_init(loop, &tcpServer->m_tcp) != 0)
			{
				delete tcpServer;
				return std::move(ptr());
			}
			return std::move(ptr(tcpServer));
		}
	};

	class Ctcp : public CcachedAllocator
	{
		PRIVATE_CLASS(Ctcp)
	private:
		uv_tcp_t m_tcp;
		uv_timer_t m_timer;
		bool m_tcpInited;
		bool m_timerInited;
		bool m_closing;
		bool m_shutdown;
		CnetworkPool *m_pool;
		CtcpCallback::ptr m_callback;
		socket_id m_socketId;

		static void close(Ctcp * const tcp)
		{
			if (nullptr == tcp)
				return;
			if (tcp->m_tcpInited || tcp->m_timerInited)
			{
				if (!tcp->m_closing)
				{
					// Both close callback should be set if initialized.
					if (tcp->m_tcpInited)
						uv_close((uv_handle_t *)&tcp->m_tcp,
						[](uv_handle_t *handle)
					{
						Ctcp *tcp = Ctcp::obtainFromTcp(handle);
						tcp->m_tcpInited = false;
						if (!tcp->m_timerInited)
							delete tcp;
					});
					if (tcp->m_timerInited)
						uv_close((uv_handle_t *)&tcp->m_timer,
						[](uv_handle_t *handle)
					{
						Ctcp *tcp = Ctcp::obtainFromTimer(handle);
						tcp->m_timerInited = false;
						if (!tcp->m_tcpInited)
							delete tcp;
					});
					tcp->m_closing = true;
				}
			}
			else
				delete tcp;
		}

		template<class T>
		friend struct __uv_wrapper_deleter;

	public:
		typedef std::unique_ptr<Ctcp, __uv_wrapper_deleter<Ctcp>> ptr;

		inline bool customize()
		{
			const preferred_tcp_settings& settings = m_callback->getSettings();
			if (uv_tcp_nodelay(&m_tcp, settings.tcp_enable_nodelay) != 0)
				return false;
			if (uv_tcp_keepalive(&m_tcp, settings.tcp_enable_keepalive, settings.tcp_keepalive_time_in_seconds) != 0)
				return false;
			// Set Tx & Rx buffer size.
			int tmpSz = settings.tcp_send_buffer_size;
			if (tmpSz != 0)
				uv_send_buffer_size((uv_handle_t *)&m_tcp, &tmpSz); // It's just prefer, so ignore the return.
			tmpSz = settings.tcp_recv_buffer_size;
			if (tmpSz != 0)
				uv_recv_buffer_size((uv_handle_t *)&m_tcp, &tmpSz); // It's just prefer, so ignore the return.
			return true;
		}

		inline uv_tcp_t *getTcp()
		{
			return &m_tcp;
		}
		inline uv_stream_t *getStream()
		{
			return (uv_stream_t *)&m_tcp;
		}
		inline uv_timer_t *getTimer()
		{
			return &m_timer;
		}
		inline CnetworkPool *getPool() const
		{
			return m_pool;
		}
		inline const CtcpCallback::ptr& getCallback() const
		{
			return m_callback;
		}
		inline socket_id getSocketId() const
		{
			return m_socketId;
		}

		inline bool isClosing() const
		{
			return m_closing;
		}
		inline bool isShutdown() const
		{
			return m_shutdown;
		}

		static inline Ctcp *obtainFromTcp(uv_handle_t * const handle)
		{
			return container_of(handle, Ctcp, m_tcp);
		}
		static inline Ctcp *obtainFromTimer(uv_handle_t * const handle)
		{
			return container_of(handle, Ctcp, m_timer);
		}
		static inline Ctcp *obtain(uv_stream_t * const stream)
		{
			return container_of(stream, Ctcp, m_tcp);
		}
		static inline Ctcp *obtain(uv_tcp_t * const tcp)
		{
			return container_of(tcp, Ctcp, m_tcp);
		}
		static inline Ctcp *obtain(uv_timer_t * const timer)
		{
			return container_of(timer, Ctcp, m_timer);
		}

		static ptr alloc(CnetworkPool * const pool, uv_loop_t * const loop, CtcpCallback::ptr&& callback, const socket_id socketId, const bool initTimer = true)
		{
			Ctcp *tcp = new (std::nothrow) Ctcp();
			if (nullptr == tcp)
				return std::move(ptr());
			tcp->m_tcpInited = false;
			tcp->m_timerInited = false;
			tcp->m_closing = false;
			tcp->m_shutdown = false;
			tcp->m_pool = pool;
			tcp->m_callback = std::forward<CtcpCallback::ptr>(callback);
			tcp->m_socketId = socketId;
			if (uv_tcp_init(loop, &tcp->m_tcp) != 0)
				goto _ec;
			tcp->m_tcpInited = true;
			if (initTimer)
			{
				if (uv_timer_init(loop, &tcp->m_timer) != 0)
					goto _ec;
				tcp->m_timerInited = true;
			}
			return std::move(ptr(tcp));
		_ec:
			close(tcp);
			return std::move(ptr());
		}

		// Wait send finish, shutdown and close.
		static void shutdown_and_close(ptr&& tcp)
		{
			Ctcp *_tcp = tcp.release();
			if (nullptr == _tcp || !_tcp->m_tcpInited)
				goto _ec;
			if (!_tcp->m_closing && !_tcp->m_shutdown)
			{
				uv_shutdown_t *shutdown = (uv_shutdown_t *)__alloc(sizeof(uv_shutdown_t));
				if (nullptr == shutdown)
					goto _ec;
				if (uv_shutdown(shutdown, (uv_stream_t *)&_tcp->m_tcp,
					[](uv_shutdown_t *req, int status)
				{
					close(Ctcp::obtain(req->handle));
					__free(req);
				}) != 0)
				{
					__free(shutdown);
					goto _ec;
				}
				_tcp->m_shutdown = true;
			}
			return;
		_ec:
			close(_tcp);
		}
	};

	class Cudp : public CcachedAllocator
	{
		PRIVATE_CLASS(Cudp)
	private:
		uv_udp_t m_udp;
		CnetworkPool *m_pool;
		CudpCallback::ptr m_callback;
		socket_id m_socketId;

		static void close(Cudp * const udp)
		{
			if (nullptr == udp)
				return;
			if (!udp->isClosing())
			{
				uv_close((uv_handle_t *)&udp->m_udp,
					[](uv_handle_t *handle)
				{
					delete Cudp::obtain(handle);
				});
			}
		}

		template<class T>
		friend struct __uv_wrapper_deleter;

	public:
		typedef std::unique_ptr<Cudp, __uv_wrapper_deleter<Cudp>> ptr;

		inline bool customize()
		{
			const preferred_udp_settings& settings = m_callback->getSettings();
			uv_udp_set_ttl(&m_udp, settings.udp_ttl); // It's just prefer, so ignore the return.
			return true;
		}

		inline uv_udp_t *getUdp()
		{
			return &m_udp;
		}
		inline CnetworkPool *getPool() const
		{
			return m_pool;
		}
		inline const CudpCallback::ptr& getCallback() const
		{
			return m_callback;
		}
		inline socket_id getSocketId() const
		{
			return m_socketId;
		}

		inline bool isClosing() const
		{
			return uv_is_closing((const uv_handle_t *)&m_udp) != 0;
		}

		static inline Cudp *obtain(uv_handle_t * const handle)
		{
			return container_of(handle, Cudp, m_udp);
		}
		static inline Cudp *obtain(uv_udp_t * const udp)
		{
			return container_of(udp, Cudp, m_udp);
		}

		static ptr alloc(CnetworkPool * const pool, uv_loop_t * const loop, CudpCallback::ptr&& callback, const socket_id socketId)
		{
			Cudp *udp = new (std::nothrow) Cudp();
			if (nullptr == udp)
				return std::move(ptr());
			udp->m_pool = pool;
			udp->m_callback = std::forward<CudpCallback::ptr>(callback);
			udp->m_socketId = socketId;
			if (uv_udp_init(loop, &udp->m_udp) != 0)
			{
				delete udp;
				return std::move(ptr());
			}
			return std::move(ptr(udp));
		}
	};

	#undef container_of
	#undef PRIVATE_CLASS
}
