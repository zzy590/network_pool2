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

#include "network_pool.h"
#include "cached_allocator.h"
#include "np_dbg.h"

#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))
#define on_uv_error_goto_label(_expr, _str, _label) if ((_expr) != 0) { NP_FPRINTF(_str); goto _label; }
#define goto_label(_str, _label) { NP_FPRINTF(_str); goto _label; }

namespace NETWORK_POOL
{
	//
	// CnetworkPool
	//

	bool CnetworkPool::setTcpTimeout(Ctcp * const tcp, const unsigned int timeout_in_seconds)
	{
		return 0 == uv_timer_start(tcp->getTimer(), 
			[](uv_timer_t *handle)
		{
			Ctcp *tcp = Ctcp::obtain(handle);
			tcp->getPool()->shutdownTcpConnection(tcp);
		}, timeout_in_seconds * 1000, 0);
	}

	bool CnetworkPool::tcpReadWithTimeout(Ctcp * const tcp)
	{
		if (!setTcpTimeout(tcp, tcp->getCallback()->getTimeoutSettings().tcp_idle_timeout_in_seconds))
			return false;
		return 0 == uv_read_start(tcp->getStream(),
			[](uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
		{
			Ctcp *tcp = Ctcp::obtainFromTcp(handle);
			// Every tcp_alloc_buffer will follow a on_tcp_read, so we don't care about the closing.
			void *buffer = nullptr;
			size_t length = 0;
			tcp->getCallback()->allocateForPacket(suggested_size, buffer, length);
			buf->base = (char *)buffer;
		#ifdef _MSC_VER
			buf->len = (ULONG)length;
		#else
			buf->len = length;
		#endif
		},
			[](uv_stream_t *client, ssize_t nread, const uv_buf_t *buf)
		{
			Ctcp *tcp = Ctcp::obtain(client);
			CnetworkPool *pool = tcp->getPool();
			if (nread > 0)
			{
				// Report message.
				tcp->getCallback()->packet(buf->base, nread);
				tcp->getCallback()->deallocateForPacket(buf->base, buf->len, nread);
				// Reset idle close.
				if (!tcp->isClosing() && !tcp->isShutdown() && 0 == uv_stream_get_write_queue_size(tcp->getStream()))
				{
					if (!pool->setTcpTimeout(tcp, tcp->getCallback()->getTimeoutSettings().tcp_idle_timeout_in_seconds))
						pool->shutdownTcpConnection(tcp);
				}
			}
			else
			{
				tcp->getCallback()->deallocateForPacket(buf->base, buf->len, 0);
				if (nread < 0)
				{
					if (nread != UV_EOF)
						NP_FPRINTF((stderr, "Read error %s.\n", uv_err_name((int)nread)));
					// Shutdown connection.
					pool->shutdownTcpConnection(tcp);
				}
			}
		});
	}

	bool CnetworkPool::tcpWriteWithTimeout(Ctcp * const tcp, Cbuffer * const data, const size_t number)
	{
		__write_with_info *writeInfo = (__write_with_info *)__alloc(sizeof(__write_with_info) + sizeof(uv_buf_t) * (number - 1));
		if (nullptr == writeInfo)
		{
			NP_FPRINTF((stderr, "Send tcp error with insufficient memory.\n"));
			for (size_t i = 0; i < number; ++i)
				tcp->getCallback()->drop(data[i].getData(), data[i].getLength());
			return false;
		}
		if (!setTcpTimeout(tcp, tcp->getCallback()->getTimeoutSettings().tcp_send_timeout_in_seconds))
		{
			NP_FPRINTF((stderr, "Send tcp error with set timer error.\n"));
			for (size_t i = 0; i < number; ++i)
				tcp->getCallback()->drop(data[i].getData(), data[i].getLength());
			__free(writeInfo);
			return false;
		}
		writeInfo->num = number;
		for (size_t i = 0; i < number; ++i)
			data[i].transfer(writeInfo->buf[i]);
		if (uv_write(&writeInfo->write, tcp->getStream(), writeInfo->buf, (unsigned int)writeInfo->num,
			[](uv_write_t *req, int status)
		{
			__write_with_info *writeInfo = container_of(req, __write_with_info, write);
			Ctcp *tcp = Ctcp::obtain(req->handle);
			CnetworkPool *pool = tcp->getPool();
			if (status != 0)
			{
				NP_FPRINTF((stderr, "Tcp write error %s.\n", uv_strerror(status)));
				// Notify the message drop.
				for (size_t i = 0; i < writeInfo->num; ++i)
					tcp->getCallback()->drop(writeInfo->buf[i].base, writeInfo->buf[i].len);
				// Shutdown connection.
				pool->shutdownTcpConnection(tcp);
			}
			else if (!tcp->isClosing() && !tcp->isShutdown() && 0 == uv_stream_get_write_queue_size(tcp->getStream()))
			{
				if (!pool->setTcpTimeout(tcp, tcp->getCallback()->getTimeoutSettings().tcp_idle_timeout_in_seconds))
					pool->shutdownTcpConnection(tcp);
			}
			// Free write buffer.
			for (size_t i = 0; i < writeInfo->num; ++i)
				__free(writeInfo->buf[i].base);
			__free(writeInfo);
		}) != 0)
		{
			for (size_t i = 0; i < writeInfo->num; ++i)
			{
				tcp->getCallback()->drop(writeInfo->buf[i].base, writeInfo->buf[i].len);
				__free(writeInfo->buf[i].base);
			}
			__free(writeInfo);
			return false;
		}
		return true;
	}
	
	CtcpServer::ptr CnetworkPool::bindAndListenTcp(const Csockaddr& local, CtcpServerCallback::ptr&& callback)
	{
		CtcpServer::ptr tcpServer = CtcpServer::alloc(this, &m_loop, std::forward<CtcpServerCallback::ptr>(callback), ++m_socketIdCounter);
		if (!tcpServer)
			goto_label((stderr, "Bind and listen tcp error with insufficient memory.\n"), _ec);
		on_uv_error_goto_label(
			uv_tcp_bind(tcpServer->getTcp(), local.getSockaddr(), 0),
			(stderr, "Bind and listen tcp bind error.\n"), _ec);
		if (!tcpServer->customize())
			goto_label((stderr, "Bind and listen tcp customize error.\n"), _ec);
		// Get binded local.
		sockaddr_storage realLocal;
		int len;
		len = sizeof(realLocal);
		on_uv_error_goto_label(
			uv_tcp_getsockname(tcpServer->getTcp(), (sockaddr *)&realLocal, &len),
			(stderr, "Bind and listen tcp getsockname error.\n"), _ec);
		on_uv_error_goto_label(
			uv_listen(tcpServer->getStream(), tcpServer->getCallback()->getSettings().tcp_backlog,
			[](uv_stream_t *server, int status)
		{
			CtcpServer *tcpServer = CtcpServer::obtain(server);
			CnetworkPool *pool = tcpServer->getPool();
			if (status != 0)
			{
				// WTF? Listen fail?
				NP_FPRINTF((stderr, "Tcp listen error %s.\n", uv_strerror(status)));
				tcpServer->getCallback()->listenError(status);
				return;
			}
			// Prepare for the new connection.
			CtcpCallback::ptr clientCallback = tcpServer->getCallback()->newTcpCallback();
			if (!clientCallback)
			{
				NP_FPRINTF((stderr, "New incoming connection tcp callback allocation error.\n"));
				return;
			}
			Ctcp::ptr clientTcp = Ctcp::alloc(pool, &pool->m_loop, std::move(clientCallback), ++pool->m_socketIdCounter);
			if (!clientTcp)
			{
				NP_FPRINTF((stderr, "New incoming connection tcp allocation error.\n"));
				// Auto free cb if not moved.
				return;
			}
			on_uv_error_goto_label(
				uv_accept(server, clientTcp->getStream()),
				(stderr, "New incoming connection tcp accept error.\n"), _iec);
			// Customize.
			if (!clientTcp->customize())
				goto_label((stderr, "New incoming connection tcp customize error.\n"), _iec);
			// Get peer.
			sockaddr_storage peer;
			int len;
			len = sizeof(peer);
			on_uv_error_goto_label(
				uv_tcp_getpeername(clientTcp->getTcp(), (sockaddr *)&peer, &len),
				(stderr, "New incoming connection tcp getpeername error.\n"), _iec);
			// Start read with timeout.
			if (!pool->tcpReadWithTimeout(clientTcp.get()))
				goto_label((stderr, "New incoming connection tcp read start error.\n"), _iec);
			// Startup connection.
			pool->startupTcpConnection(std::move(clientTcp), Csockaddr((const sockaddr *)&peer, len));
			return;
		_iec:; // Auto free tcp and cb if not moved.
		}),
			(stderr, "Bind and listen tcp listen error.\n"), _ec);
		tcpServer->getCallback()->startup(tcpServer->getSocketId(), Csockaddr((const sockaddr *)&realLocal, len));
		return std::move(tcpServer);
	_ec:
		// Auto free server ptr if not moved.
		return std::move(CtcpServer::ptr());
	}

	Ctcp::ptr CnetworkPool::connectTcp(const Csockaddr& remote, CtcpCallback::ptr&& callback)
	{
		uv_connect_t *connect = (uv_connect_t *)__alloc(sizeof(uv_connect_t));
		if (nullptr == connect)
		{
			NP_FPRINTF((stderr, "Connect tcp error with insufficient memory.\n"));
			return std::move(Ctcp::ptr());
		}
		Ctcp::ptr tcp = Ctcp::alloc(this, &m_loop, std::forward<CtcpCallback::ptr>(callback), ++m_socketIdCounter);
		if (!tcp)
			goto_label((stderr, "Connect tcp error with insufficient memory.\n"), _ec);
		if (!setTcpTimeout(tcp.get(), tcp->getCallback()->getTimeoutSettings().tcp_connect_timeout_in_seconds))
			goto_label((stderr, "Connect tcp error with set timeout error.\n"), _ec);
		on_uv_error_goto_label(
			uv_tcp_connect(connect, tcp->getTcp(), remote.getSockaddr(),
			[](uv_connect_t *req, int status)
		{
			// First take ptr from connecting map.
			Ctcp *_tcp = Ctcp::obtain(req->handle);
			CnetworkPool *pool = _tcp->getPool();
			auto it = pool->m_connecting.find(_tcp->getSocketId());
			if (it == pool->m_connecting.end())
				return; // Already freed.
			Ctcp::ptr tcp(std::move(it->second));
			pool->m_connecting.erase(it);
			// Free request.
			__free(req);
			// Error?
			if (status < 0 || !tcp || tcp->isClosing())
				goto_label((stderr, "Connect tcp error %s.\n", uv_strerror(status)), _iec);
			// Customize.
			if (!tcp->customize())
				goto_label((stderr, "Connect tcp customize error.\n"), _iec);
			// Get peer.
			sockaddr_storage peer;
			int len;
			len = sizeof(peer);
			on_uv_error_goto_label(
				uv_tcp_getpeername(tcp->getTcp(), (sockaddr *)&peer, &len),
				(stderr, "New incoming connection tcp getpeername error.\n"), _iec);
			// Start read with timeout.
			if (!pool->tcpReadWithTimeout(tcp.get()))
				goto_label((stderr, "Connect tcp read start error.\n"), _iec);
			// Startup connection.
			pool->startupTcpConnection(std::move(tcp), Csockaddr((const sockaddr *)&peer, len));
			return;
		_iec:; // Auto free tcp if notr moved.
		}),
			(stderr, "Connect tcp error with connect error.\n"), _ec);
		return std::move(tcp);
	_ec:
		// Auto free tcp if not moved.
		__free(connect);
		return std::move(Ctcp::ptr());
	}

	bool CnetworkPool::udpSend(Cudp * const udp, const Csockaddr& remote, Cbuffer * const data, const size_t number)
	{
		__udp_send_with_info *udpSendInfo = (__udp_send_with_info *)__alloc(sizeof(__udp_send_with_info) + sizeof(uv_buf_t) * (number - 1));
		if (nullptr == udpSendInfo)
		{
			NP_FPRINTF((stderr, "Send udp error with insufficient memory.\n"));
			udp->getCallback()->sendError(ENOMEM);
			return false;
		}
		udpSendInfo->num = number;
		for (size_t i = 0; i < number; ++i)
			data[i].transfer(udpSendInfo->buf[i]);
		int iRet = uv_udp_send(&udpSendInfo->udpSend, udp->getUdp(), udpSendInfo->buf, (unsigned int)udpSendInfo->num, remote.getSockaddr(),
			[](uv_udp_send_t *req, int status)
		{
			if (status != 0)
			{
				NP_FPRINTF((stderr, "Udp write error %s.\n", uv_strerror(status)));
				Cudp::obtain(req->handle)->getCallback()->sendError(status);
			}
			// Free udp send buffer.
			__udp_send_with_info *udpSendInfo = container_of(req, __udp_send_with_info, udpSend);
			for (size_t i = 0; i < udpSendInfo->num; ++i)
				__free(udpSendInfo->buf[i].base);
			__free(udpSendInfo);
		});
		if (iRet != 0)
		{
			// Send fail.
			udp->getCallback()->sendError(iRet);
			// Free udp send buffer.
			for (size_t i = 0; i < udpSendInfo->num; ++i)
				__free(udpSendInfo->buf[i].base);
			__free(udpSendInfo);
			return false;
		}
		return true;
	}

	Cudp::ptr CnetworkPool::bindAndListenUdp(const Csockaddr& local, CudpCallback::ptr&& callback)
	{
		Cudp::ptr udp = Cudp::alloc(this, &m_loop, std::forward<CudpCallback::ptr>(callback), ++m_socketIdCounter);
		if (!udp)
			goto_label((stderr, "Bind and listen udp error with insufficient memory.\n"), _ec);
		on_uv_error_goto_label(
			uv_udp_bind(udp->getUdp(), local.getSockaddr(), 0),
			(stderr, "Bind and listen udp bind error.\n"), _ec);
		// Get binded local.
		sockaddr_storage realLocal;
		int len;
		len = sizeof(realLocal);
		on_uv_error_goto_label(
			uv_udp_getsockname(udp->getUdp(), (sockaddr *)&realLocal, &len),
			(stderr, "Bind and listen udp getsockname error.\n"), _ec);
		// Start recv.(we cannot use micro because of '#ifdef')
		if (uv_udp_recv_start(udp->getUdp(),
			[](uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
		{
			Cudp *udp = Cudp::obtain(handle);
			// Every udp_alloc_buffer will follow a on_udp_read, so we don't care about the closing.
			void *buffer = nullptr;
			size_t length = 0;
			udp->getCallback()->allocateForPacket(suggested_size, buffer, length);
			buf->base = (char *)buffer;
		#ifdef _MSC_VER
			buf->len = (ULONG)length;
		#else
			buf->len = length;
		#endif
		},
			[](uv_udp_t *handle, ssize_t nread, const uv_buf_t *buf, const struct sockaddr *addr, unsigned flags)
		{
			Cudp *udp = Cudp::obtain(handle);
			if (nread < 0)
			{
				udp->getCallback()->deallocateForPacket(buf->base, buf->len);
				NP_FPRINTF((stderr, "Recv udp error %s.\n", uv_err_name((int)nread)));
				// Just report this error.
				udp->getCallback()->recvError((int)nread);
			}
			else if (addr != nullptr)
			{
				// Report message.
				udp->getCallback()->packet(Csockaddr(addr, sizeof(sockaddr_storage)), buf->base, nread);
				udp->getCallback()->deallocateForPacket(buf->base, buf->len);
			}
			else
				udp->getCallback()->deallocateForPacket(buf->base, buf->len);
		}) != 0)
			goto_label((stderr, "Bind and listen udp listen error.\n"), _ec);
		udp->getCallback()->startup(udp->getSocketId(), Csockaddr((const sockaddr *)&realLocal, len));
		return std::move(udp);
	_ec:
		return std::move(Cudp::ptr());
	}

	void CnetworkPool::internalThread()
	{
		// Init loop.
		if (uv_loop_init(&m_loop) != 0)
		{
			m_state = bad;
			return;
		}
		m_wakeup = std::move(Casync::alloc(this, &m_loop,
			[](uv_async_t *async)
		{
			CnetworkPool *pool = Casync::obtain(async)->getPool();
			// Copy pending to local first.
			if (!pool->m_lock.try_lock())
			{
				uv_async_send(async); // Deal when next loop.
				return;
			}
			// Just use lock and unlock, because we never get exception here(fatal error).
			std::deque<__pending_bind> bindCopy(std::move(pool->m_pendingBind));
			std::deque<__pending_send_tcp> sendTcpCopy(std::move(pool->m_pendingSendTcp));
			std::deque<__pending_send_udp> sendUdpCopy(std::move(pool->m_pendingSendUdp));
			std::deque<__pending_connect> connectCopy(std::move(pool->m_pendingConnect));
			std::deque<__pending_close> closeCopy(std::move(pool->m_pendingClose));
			pool->m_pendingBind.clear();
			pool->m_pendingSendTcp.clear();
			pool->m_pendingSendUdp.clear();
			pool->m_pendingConnect.clear();
			pool->m_pendingClose.clear();
			pool->m_lock.unlock();
			// Deal with request(s).
			if (pool->m_bWantExit)
			{
				//
				// Stop and free all resources.
				//
				// Async.(Free in lock for safety.)
				pool->m_lock.lock();
				pool->m_wakeup.reset();
				pool->m_lock.unlock();
				// TCP servers.
				std::unordered_map<socket_id, CtcpServer::ptr> tmpTcpServers(std::move(pool->m_tcpServers));
				pool->m_tcpServers.clear();
				for (const auto& pair : tmpTcpServers)
					pair.second->getCallback()->shutdown();
				tmpTcpServers.clear();
				// UDP servers.
				std::unordered_map<socket_id, Cudp::ptr> tmpUdpServers(std::move(pool->m_udpServers));
				pool->m_udpServers.clear();
				for (const auto& pair : tmpUdpServers)
				{
					uv_udp_recv_stop(pair.second->getUdp()); // Ignore the result.
					pair.second->getCallback()->shutdown();
				}
				tmpUdpServers.clear();
				// TCP connections.
				std::unordered_map<socket_id, Ctcp::ptr> tmpSocketId2stream(std::move(pool->m_socketId2stream));
				pool->m_socketId2stream.clear();
				for (const auto& pair : tmpSocketId2stream)
					pair.second->getCallback()->shutdown();
				tmpSocketId2stream.clear();
				// TCP connecting will free by smart pointer.
				pool->m_connecting.clear(); // No startup so no need to call shutdown.
				// All callback in copy will free by smart pointer.
			}
			else
			{
				//
				// Bind, send, connect & close.
				//
				// Bind.
				for (auto& req : bindCopy)
				{
					if (req.m_bBind)
					{
						const Csockaddr& local = req.m_local;
						if (req.m_tcpServerCallback)
						{
							CtcpServer::ptr tcpServer = pool->bindAndListenTcp(local, std::move(req.m_tcpServerCallback));
							if (tcpServer)
								pool->m_tcpServers.insert(std::make_pair(tcpServer->getSocketId(), std::move(tcpServer)));
						}
						else if (req.m_udpCallback)
						{
							Cudp::ptr udp = pool->bindAndListenUdp(local, std::move(req.m_udpCallback));
							if (udp)
								pool->m_udpServers.insert(std::make_pair(udp->getSocketId(), std::move(udp)));
						}
					}
					else
					{
						const socket_id& socketId = req.m_socketId;
						switch (req.m_protocol)
						{
						case CnetworkNode::protocol_tcp:
						{
							auto it = pool->m_tcpServers.find(socketId);
							if (it != pool->m_tcpServers.end())
							{
								CtcpServer::ptr tcpServers(std::move(it->second));
								pool->m_tcpServers.erase(it);
								tcpServers->getCallback()->shutdown();
								// Auto free.
							}
						}
							break;

						case CnetworkNode::protocol_udp:
						{
							auto it = pool->m_udpServers.find(socketId);
							if (it != pool->m_udpServers.end())
							{
								Cudp::ptr udp(std::move(it->second));
								pool->m_udpServers.erase(it);
								uv_udp_recv_stop(udp->getUdp()); // Ignore the result.
								udp->getCallback()->shutdown();
								// Auto free.
							}
						}
							break;

						default:
							break;
						}
					}
				}
				// Send.
				for (auto& req : sendTcpCopy)
				{
					auto it = pool->m_socketId2stream.find(req.m_socketId);
					if (it == pool->m_socketId2stream.end())
						continue;
					Ctcp *tcp = it->second.get();
					if (!pool->tcpWriteWithTimeout(tcp, &req.m_data, 1))
						pool->shutdownTcpConnection(tcp);
				}
				for (auto& req : sendUdpCopy)
				{
					auto it = pool->m_udpServers.find(req.m_socketId);
					if (it == pool->m_udpServers.end())
						continue;
					pool->udpSend(it->second.get(), req.m_remote, &req.m_data, 1);
				}
				// Connect.
				for (auto& req : connectCopy)
				{
					Ctcp::ptr tcp = pool->connectTcp(req.m_remote, std::move(req.m_callback));
					if (tcp)
						pool->m_connecting.insert(std::make_pair(tcp->getSocketId(), std::move(tcp)));
				}
				// Close.
				for (auto& req : closeCopy)
				{
					auto it = pool->m_socketId2stream.find(req.m_socketId);
					if (it == pool->m_socketId2stream.end())
						continue;
					Ctcp *tcp = it->second.get();
					// No force close means shutdown, and it's a type of send.
					if (!req.m_bForce && pool->setTcpTimeout(tcp, tcp->getCallback()->getTimeoutSettings().tcp_send_timeout_in_seconds))
						pool->shutdownTcpConnection(tcp, true); // Timer still working until close, so timeout when shutdown will force close the connection.
					else
						pool->shutdownTcpConnection(tcp); // Force close.
				}
			}
		}));
		if (!m_wakeup)
		{
			uv_loop_close(&m_loop);
			m_state = bad;
			return;
		}
		m_state = good;
		uv_run(&m_loop, UV_RUN_DEFAULT);
		uv_loop_close(&m_loop);
	}

	void CnetworkPool::startupTcpConnection(Ctcp::ptr&& tcp, const Csockaddr& remote)
	{
		// Add map.
		auto ib = m_socketId2stream.insert(std::make_pair(tcp->getSocketId(), std::forward<Ctcp::ptr>(tcp)));
		if (ib.second)
			ib.first->second->getCallback()->startup(ib.first->second->getSocketId(), remote);
	}

	// This function is idempotent, and can be called any time when tcp is valid(closing is also ok).
	void CnetworkPool::shutdownTcpConnection(Ctcp * const tcp, const bool bShutdown)
	{
		// Clean map.
		auto it = m_socketId2stream.find(tcp->getSocketId());
		if (it != m_socketId2stream.end())
		{
			Ctcp::ptr tcp(std::move(it->second));
			m_socketId2stream.erase(it);
			if (bShutdown)
				Ctcp::shutdown_and_close(std::move(tcp));
			// Or auto free with close.
		}
	}
}
