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
#include <deque>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <utility>

#include "uv.h"

#include "network_type.h"
#include "network_node.h"
#include "uv_wrapper.h"
#include "network_callback.h"
#include "buffer.h"

namespace NETWORK_POOL
{
	//
	// Caution! Program may cash when fail to allocate memory in critical step.
	// So be careful to check memory usage before pushing packet to network pool.
	//

	class CnetworkPool
	{
	public:
		struct __write_with_info
		{
			uv_write_t write;
			size_t num;
			uv_buf_t buf[1]; // Need free when complete request.
		};
		struct __udp_send_with_info
		{
			uv_udp_send_t udpSend;
			size_t num;
			uv_buf_t buf[1]; // Need free when complete request.
		};

	private:
		// Data which exchanged between internal and external.
		std::mutex m_lock;
		struct __pending_bind
		{
			CnetworkNode::protocol_type m_protocol;
			Csockaddr m_local;
			CtcpServerCallback::ptr m_tcpServerCallback;
			CudpCallback::ptr m_udpCallback;
			bool m_bBind;
			socket_id m_socketId;

			__pending_bind(const Csockaddr& local, CtcpServerCallback::ptr&& tcpServerCallback)
				:m_protocol(CnetworkNode::protocol_tcp), m_local(local), m_tcpServerCallback(std::forward<CtcpServerCallback::ptr>(tcpServerCallback)), m_bBind(true), m_socketId(SOCKET_ID_UNSPEC) {}
			__pending_bind(const Csockaddr& local, CudpCallback::ptr&& udpCallback)
				:m_protocol(CnetworkNode::protocol_udp), m_local(local), m_udpCallback(std::forward<CudpCallback::ptr>(udpCallback)), m_bBind(true), m_socketId(SOCKET_ID_UNSPEC) {}
			__pending_bind(const CnetworkNode::protocol_type protocol, socket_id socketId)
				:m_protocol(protocol), m_bBind(false), m_socketId(socketId) {}

			__pending_bind(const __pending_bind& another) = delete;
			__pending_bind(__pending_bind&& another)
				:m_protocol(another.m_protocol), m_local(std::move(another.m_local)), m_tcpServerCallback(std::move(another.m_tcpServerCallback)), m_udpCallback(std::move(another.m_udpCallback)), m_bBind(another.m_bBind), m_socketId(another.m_socketId) {}
			const __pending_bind& operator=(const __pending_bind& another) = delete;
			const __pending_bind& operator=(__pending_bind&& another) = delete;
		};
		std::deque<__pending_bind> m_pendingBind;
		struct __pending_send_tcp
		{
			socket_id m_socketId;
			Cbuffer m_data;

			__pending_send_tcp(const socket_id socketId, const void *data, const size_t length)
				:m_socketId(socketId), m_data(data, length) {}

			__pending_send_tcp(const __pending_send_tcp& another) = delete;
			__pending_send_tcp(__pending_send_tcp&& another)
				:m_socketId(another.m_socketId), m_data(std::move(another.m_data)) {}
			const __pending_send_tcp& operator=(const __pending_send_tcp& another) = delete;
			const __pending_send_tcp& operator=(__pending_send_tcp&& another) = delete;
		};
		std::deque<__pending_send_tcp> m_pendingSendTcp;
		struct __pending_send_udp
		{
			socket_id m_socketId;
			Csockaddr m_remote;
			Cbuffer m_data;

			__pending_send_udp(const socket_id socketId, const Csockaddr& remote, const void *data, const size_t length)
				:m_socketId(socketId), m_remote(remote), m_data(data, length) {}

			__pending_send_udp(const __pending_send_udp& another) = delete;
			__pending_send_udp(__pending_send_udp&& another)
				:m_socketId(another.m_socketId), m_remote(std::move(another.m_remote)), m_data(std::move(another.m_data)) {}
			const __pending_send_udp& operator=(const __pending_send_udp& another) = delete;
			const __pending_send_udp& operator=(__pending_send_udp&& another) = delete;
		};
		std::deque<__pending_send_udp> m_pendingSendUdp;
		struct __pending_connect
		{
			Csockaddr m_remote;
			CtcpCallback::ptr m_callback;

			__pending_connect(const Csockaddr& remote, CtcpCallback::ptr&& callback)
				:m_remote(remote), m_callback(std::forward<CtcpCallback::ptr>(callback)) {}

			__pending_connect(const __pending_connect& another) = delete;
			__pending_connect(__pending_connect&& another)
				:m_remote(std::move(another.m_remote)), m_callback(std::move(another.m_callback)) {}
			const __pending_connect& operator=(const __pending_connect& another) = delete;
			const __pending_connect& operator=(__pending_connect&& another) = delete;
		};
		std::deque<__pending_connect> m_pendingConnect;
		struct __pending_close
		{
			socket_id m_socketId;
			bool m_bForce;

			__pending_close(const socket_id socketId, const bool bForce)
				:m_socketId(socketId), m_bForce(bForce) {}

			__pending_close(const __pending_close& another) = delete;
			__pending_close(__pending_close&& another)
				:m_socketId(another.m_socketId), m_bForce(another.m_bForce) {}
			const __pending_close& operator=(const __pending_close& another) = delete;
			const __pending_close& operator=(__pending_close&& another) = delete;
		};
		std::deque<__pending_close> m_pendingClose;
		
		//
		// Following data must be accessed by internal thread.
		//

		// Counter to get next socket id.
		socket_id m_socketIdCounter;

		// Loop must be initialized in internal work thread.
		uv_loop_t m_loop;
		Casync::ptr m_wakeup;
		std::unordered_map<socket_id, CtcpServer::ptr> m_tcpServers;
		std::unordered_map<socket_id, Cudp::ptr> m_udpServers;
		std::unordered_map<socket_id, Ctcp::ptr> m_socketId2stream;
		std::unordered_map<socket_id, Ctcp::ptr> m_connecting;

		// Status of internal thread.
		volatile enum __internal_state
		{
			initializing = 0,
			good,
			bad
		} m_state;
		bool m_bWantExit;

		// Internal thread.
		std::unique_ptr<std::thread> m_thread;

		bool setTcpTimeout(Ctcp * const tcp, const unsigned int timeout_in_seconds);
		bool tcpReadWithTimeout(Ctcp * const tcp);
		bool tcpWriteWithTimeout(Ctcp * const tcp, Cbuffer * const data, const size_t number);
		CtcpServer::ptr bindAndListenTcp(const Csockaddr& local, CtcpServerCallback::ptr&& callback);
		Ctcp::ptr connectTcp(const Csockaddr& remote, CtcpCallback::ptr&& callback);

		bool udpSend(Cudp * const udp, const Csockaddr& remote, Cbuffer * const data, const size_t number);
		Cudp::ptr bindAndListenUdp(const Csockaddr& local, CudpCallback::ptr&& callback);

		void internalThread();

		// Caution! Call following function(s) may cause iterator of m_socketId2stream invalid.
		inline void startupTcpConnection(Ctcp::ptr&& tcp, const Csockaddr& remote);
		inline void shutdownTcpConnection(Ctcp * const tcp, const bool bShutdown = false);

		void bind(__pending_bind&& req)
		{
			{
				std::lock_guard<std::mutex> guard(m_lock); // Use guard in case of exception.
				m_pendingBind.push_back(std::forward<__pending_bind>(req));
			}
			uv_async_send(m_wakeup->getAsync());
		}

	public:
		// Throw when fail.
		CnetworkPool()
			:m_socketIdCounter(0), m_state(initializing), m_bWantExit(false), m_thread(new std::thread(&CnetworkPool::internalThread, this))
		{
			while (initializing == m_state)
				std::this_thread::yield();
			if (m_state != good)
				throw(-1);
		}
		~CnetworkPool()
		{
			m_bWantExit = true;
			// Use lock to keep safe.
			m_lock.lock();
			if (m_wakeup)
				uv_async_send(m_wakeup->getAsync());
			m_lock.unlock();
			m_thread->join();
		}

		// No copy, no move.
		CnetworkPool(const CnetworkPool& another) = delete;
		CnetworkPool(CnetworkPool&& another) = delete;
		const CnetworkPool& operator=(const CnetworkPool& another) = delete;
		const CnetworkPool& operator=(CnetworkPool&& another) = delete;

		void bindTcp(const Csockaddr& local, CtcpServerCallback::ptr&& callback)
		{
			if (callback)
				bind(std::move(__pending_bind(local, std::forward<CtcpServerCallback::ptr>(callback))));
		}
		void unbindTcp(const socket_id socketId)
		{
			bind(std::move(__pending_bind(CnetworkNode::protocol_tcp, socketId)));
		}

		void bindUdp(const Csockaddr& local, CudpCallback::ptr&& callback)
		{
			if (callback)
				bind(std::move(__pending_bind(local, std::forward<CudpCallback::ptr>(callback))));
		}
		void unbindUdp(const socket_id socketId)
		{
			bind(std::move(__pending_bind(CnetworkNode::protocol_udp, socketId)));
		}

		void sendTcp(const socket_id socketId, const void *data, const size_t length, bool bAllowDirectCall = true)
		{
			if (0 == length || nullptr == data)
				return;
			if (bAllowDirectCall && std::this_thread::get_id() == m_thread->get_id())
			{
				// Direct send.
				auto it = m_socketId2stream.find(socketId);
				if (it != m_socketId2stream.end())
				{
					Ctcp *tcp = it->second.get();
					Cbuffer buf(data, length);
					if (!tcpWriteWithTimeout(tcp, &buf, 1))
						shutdownTcpConnection(tcp);
				}
			}
			else
			{
				__pending_send_tcp temp(socketId, data, length);
				{
					std::lock_guard<std::mutex> guard(m_lock); // Use guard in case of exception.
					m_pendingSendTcp.push_back(std::move(temp));
				}
				uv_async_send(m_wakeup->getAsync());
			}
		}
		void sendUdp(const socket_id socketId, const Csockaddr& remote, const void *data, const size_t length, bool bAllowDirectCall = true)
		{
			if (0 == length || nullptr == data || length > 65507)
				return;
			if (bAllowDirectCall && std::this_thread::get_id() == m_thread->get_id())
			{
				auto it = m_udpServers.find(socketId);
				if (it != m_udpServers.end())
				{
					Cbuffer buf(data, length);
					udpSend(it->second.get(), remote, &buf, 1);
				}
			}
			else
			{
				__pending_send_udp temp(socketId, remote, data, length);
				{
					std::lock_guard<std::mutex> guard(m_lock); // Use guard in case of exception.
					m_pendingSendUdp.push_back(std::move(temp));
				}
				uv_async_send(m_wakeup->getAsync());
			}
		}

		//
		// Following function(s) are only for tcp.
		//

		void connect(const Csockaddr& remote, CtcpCallback::ptr&& callback)
		{
			if (!callback)
				return;
			__pending_connect temp(remote, std::forward<CtcpCallback::ptr>(callback));
			{
				std::lock_guard<std::mutex> guard(m_lock); // Use guard in case of exception.
				m_pendingConnect.push_back(std::move(temp));
			}
			uv_async_send(m_wakeup->getAsync());
		}

		// It waits for pending write requests to complete if bForceClose == false.
		// Or close immediately if bForceClose == true.
		void close(const socket_id socketId, const bool bForceClose = false)
		{
			__pending_close temp(socketId, bForceClose);
			{
				std::lock_guard<std::mutex> guard(m_lock); // Use guard in case of exception.
				m_pendingClose.push_back(std::move(temp));
			}
			uv_async_send(m_wakeup->getAsync());
		}
	};
}
