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

namespace NETWORK_POOL
{
	struct preferred_tcp_server_settings
	{
		int tcp_enable_simultaneous_accepts;
		int tcp_backlog;

		preferred_tcp_server_settings()
		{
			tcp_enable_simultaneous_accepts = 1;
			tcp_backlog = 128;
		}
	};

	struct preferred_tcp_settings
	{
		int tcp_enable_nodelay;
		int tcp_enable_keepalive;
		unsigned int tcp_keepalive_time_in_seconds;
		// Set 0 means use the system default value.
		// Note: Linux will set double the size of the original set value.
		int tcp_send_buffer_size;
		int tcp_recv_buffer_size;

		preferred_tcp_settings()
		{
			tcp_enable_nodelay = 1;
			tcp_enable_keepalive = 1;
			tcp_keepalive_time_in_seconds = 30;
			tcp_send_buffer_size = 0;
			tcp_recv_buffer_size = 0;
		}
	};

	struct preferred_tcp_timeout_settings
	{
		// Set 0 if you don't want timeout.
		unsigned int tcp_connect_timeout_in_seconds;
		unsigned int tcp_idle_timeout_in_seconds;
		unsigned int tcp_send_timeout_in_seconds;

		preferred_tcp_timeout_settings()
		{
			tcp_connect_timeout_in_seconds = 10;
			tcp_idle_timeout_in_seconds = 30;
			tcp_send_timeout_in_seconds = 30;
		}
	};

	struct preferred_udp_settings
	{
		int udp_ttl;

		preferred_udp_settings()
		{
			udp_ttl = 64;
		}
	};
}
