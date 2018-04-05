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

#include "network_type.h"
#include "network_setting.h"
#include "network_node.h"

namespace NETWORK_POOL
{
	class CtcpCallback
	{
	public:
		typedef std::unique_ptr<CtcpCallback> ptr;

		virtual ~CtcpCallback() {}

		// 'allocateForPacket' will be called before 'packet', only packet which dataLength > 0 call the 'packet' function.
		virtual void allocateForPacket(const size_t suggestedSize, void *& buffer, size_t& length) = 0;
		virtual void deallocateForPacket(void * const buffer, const size_t length, const size_t dataLength) = 0;
		virtual void packet(const void * const data, const size_t length) = 0;

		virtual const preferred_tcp_settings& getSettings() = 0;
		virtual const preferred_tcp_timeout_settings& getTimeoutSettings() = 0;

		virtual void startup(const socket_id socketId, const Csockaddr& remote) = 0;
		virtual void shutdown() = 0;

		virtual void drop(const void * const data, const size_t length) = 0;
	};

	class CudpCallback
	{
	public:
		typedef std::unique_ptr<CudpCallback> ptr;

		virtual ~CudpCallback() {}

		// 'allocateForPacket' will be called before 'packet', only packet which dataLength > 0 call the 'packet' function.
		virtual void allocateForPacket(const size_t suggestedSize, void *& buffer, size_t& length) = 0;
		virtual void deallocateForPacket(void * const buffer, const size_t length, const size_t dataLength) = 0;
		virtual void packet(const Csockaddr& remote, const void * const data, const size_t length) = 0;

		virtual const preferred_udp_settings& getSettings() = 0;

		virtual void startup(const socket_id socketId, const Csockaddr& local) = 0;
		virtual void shutdown() = 0;

		virtual void sendError(const int err) {}
		virtual void recvError(const int err) {}
	};

	class CtcpServerCallback
	{
	public:
		typedef std::unique_ptr<CtcpServerCallback> ptr;

		virtual ~CtcpServerCallback() {}

		virtual const preferred_tcp_server_settings& getSettings() = 0;

		virtual CtcpCallback::ptr newTcpCallback() = 0;

		virtual void startup(const socket_id socketId, const Csockaddr& local) = 0;
		virtual void shutdown() = 0;

		virtual void listenError(const int err) {}
	};
}
