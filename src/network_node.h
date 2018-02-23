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
#include <utility>

#include "uv.h"

#include "cached_allocator.h"

namespace NETWORK_POOL
{
	class Csockaddr
	{
	private:
		union __sockaddr_mix
		{
			unsigned short family;
			sockaddr_in sockaddr4;
			sockaddr_in6 sockaddr6;
		} m_sockaddr;

	public:
		inline void init()
		{
			m_sockaddr.family = 0;
		}

		inline bool init(const sockaddr * const raw, const size_t size)
		{
			if (size < 8) // family+port+ipv4
			{
				init();
				return false;
			}
			else
			{
				switch (raw->sa_family)
				{
				case AF_INET:
				{ // Size checked before.
					const sockaddr_in *in4 = (const sockaddr_in *)raw;
					m_sockaddr.sockaddr4.sin_family = AF_INET;
					m_sockaddr.sockaddr4.sin_port = in4->sin_port;
					uint32_t *addr0 = (uint32_t *)&m_sockaddr.sockaddr4.sin_addr;
					uint32_t *addr1 = (uint32_t *)&in4->sin_addr;
					*addr0 = *addr1;
				}
					break;

				case AF_INET6:
					if (size < sizeof(sockaddr_in6))
					{
						init();
						return false;
					}
					else
					{
						const sockaddr_in6 *in6 = (const sockaddr_in6 *)raw;
						m_sockaddr.sockaddr6.sin6_family = AF_INET6;
						m_sockaddr.sockaddr6.sin6_port = in6->sin6_port;
						uint32_t *addr0 = (uint32_t *)&m_sockaddr.sockaddr6.sin6_addr;
						uint32_t *addr1 = (uint32_t *)&in6->sin6_addr;
						for (int i = 0; i < 4; ++i)
							addr0[i] = addr1[i];
						m_sockaddr.sockaddr6.sin6_flowinfo = in6->sin6_flowinfo;
						m_sockaddr.sockaddr6.sin6_scope_id = in6->sin6_scope_id;
					}
					break;

				default:
					init();
					return false;
				}
			}
			return true;
		}

		inline bool init(const char * const ip, const unsigned short port)
		{
			if (strchr(ip, ':') == nullptr) // IPv4.
			{
				if (uv_ip4_addr(ip, port, &m_sockaddr.sockaddr4) != 0)
				{
					init();
					return false;
				}
			}
			else // IPv6.
			{
				if (uv_ip6_addr(ip, port, &m_sockaddr.sockaddr6) != 0)
				{
					init();
					return false;
				}
			}
			return true;
		}

		inline bool init(const Csockaddr& another)
		{
			switch (another.m_sockaddr.family)
			{
			case AF_INET:
			{
				m_sockaddr.sockaddr4.sin_family = AF_INET;
				m_sockaddr.sockaddr4.sin_port = another.m_sockaddr.sockaddr4.sin_port;
				uint32_t *addr0 = (uint32_t *)&m_sockaddr.sockaddr4.sin_addr;
				uint32_t *addr1 = (uint32_t *)&another.m_sockaddr.sockaddr4.sin_addr;
				*addr0 = *addr1;
			}
			break;

			case AF_INET6:
			{
				m_sockaddr.sockaddr6.sin6_family = AF_INET6;
				m_sockaddr.sockaddr6.sin6_port = another.m_sockaddr.sockaddr6.sin6_port;
				uint32_t *addr0 = (uint32_t *)&m_sockaddr.sockaddr6.sin6_addr;
				uint32_t *addr1 = (uint32_t *)&another.m_sockaddr.sockaddr6.sin6_addr;
				for (int i = 0; i < 4; ++i)
					addr0[i] = addr1[i];
				m_sockaddr.sockaddr6.sin6_flowinfo = another.m_sockaddr.sockaddr6.sin6_flowinfo;
				m_sockaddr.sockaddr6.sin6_scope_id = another.m_sockaddr.sockaddr6.sin6_scope_id;
			}
			break;

			default:
				init();
				return false;
			}
			return true;
		}

		Csockaddr()
		{
			init();
		}
		Csockaddr(const sockaddr * const raw, const size_t size)
		{
			init(raw, size);
		}
		Csockaddr(const char * const ip, const unsigned short port)
		{
			init(ip, port);
		}
		Csockaddr(const Csockaddr& another)
		{
			init(another);
		}
		Csockaddr(Csockaddr&& another)
		{
			init(another);
		}

		const Csockaddr& operator=(const Csockaddr& another)
		{
			init(another);
			return *this;
		}
		const Csockaddr& operator=(Csockaddr&& another)
		{
			init(another);
			return *this;
		}

		bool operator<(const Csockaddr& another) const
		{
			if (m_sockaddr.family != another.m_sockaddr.family)
				return m_sockaddr.family < another.m_sockaddr.family;
			switch (m_sockaddr.family)
			{
			case AF_INET:
			{
				if (m_sockaddr.sockaddr4.sin_port != another.m_sockaddr.sockaddr4.sin_port)
					return m_sockaddr.sockaddr4.sin_port < another.m_sockaddr.sockaddr4.sin_port;
				uint32_t *addr0 = (uint32_t *)&m_sockaddr.sockaddr4.sin_addr;
				uint32_t *addr1 = (uint32_t *)&another.m_sockaddr.sockaddr4.sin_addr;
				return *addr0 < *addr1;
			}
			break;

			case AF_INET6:
			{
				if (m_sockaddr.sockaddr6.sin6_port != another.m_sockaddr.sockaddr6.sin6_port)
					return m_sockaddr.sockaddr6.sin6_port < another.m_sockaddr.sockaddr6.sin6_port;
				uint32_t *addr0 = (uint32_t *)&m_sockaddr.sockaddr6.sin6_addr;
				uint32_t *addr1 = (uint32_t *)&another.m_sockaddr.sockaddr6.sin6_addr;
				for (int i = 0; i < 4; ++i)
				{
					if (addr0[i] != addr1[i])
						return addr0[i] < addr1[i];
				}
				if (m_sockaddr.sockaddr6.sin6_flowinfo != another.m_sockaddr.sockaddr6.sin6_flowinfo)
					return m_sockaddr.sockaddr6.sin6_flowinfo < another.m_sockaddr.sockaddr6.sin6_flowinfo;
				return m_sockaddr.sockaddr6.sin6_scope_id < another.m_sockaddr.sockaddr6.sin6_scope_id;
			}
			break;

			default:
				return false;
			}
		}

		bool operator==(const Csockaddr& another) const
		{
			if (m_sockaddr.family != another.m_sockaddr.family)
				return false;
			switch (m_sockaddr.family)
			{
			case AF_INET:
			{
				if (m_sockaddr.sockaddr4.sin_port != another.m_sockaddr.sockaddr4.sin_port)
					return false;
				uint32_t *addr0 = (uint32_t *)&m_sockaddr.sockaddr4.sin_addr;
				uint32_t *addr1 = (uint32_t *)&another.m_sockaddr.sockaddr4.sin_addr;
				return *addr0 == *addr1;
			}
			break;

			case AF_INET6:
			{
				if (m_sockaddr.sockaddr6.sin6_port != another.m_sockaddr.sockaddr6.sin6_port)
					return false;
				uint32_t *addr0 = (uint32_t *)&m_sockaddr.sockaddr6.sin6_addr;
				uint32_t *addr1 = (uint32_t *)&another.m_sockaddr.sockaddr6.sin6_addr;
				for (int i = 0; i < 4; ++i)
				{
					if (addr0[i] != addr1[i])
						return false;
				}
				if (m_sockaddr.sockaddr6.sin6_flowinfo != another.m_sockaddr.sockaddr6.sin6_flowinfo)
					return false;
				return m_sockaddr.sockaddr6.sin6_scope_id == another.m_sockaddr.sockaddr6.sin6_scope_id;
			}
			break;

			default:
				return true;
			}
		}

		bool operator!=(const Csockaddr& another) const
		{
			return !operator==(another);
		}

		inline const sockaddr *getSockaddr() const
		{
			return (const sockaddr *)&m_sockaddr;
		}

		inline size_t getHash(size_t hash) const
		{
			switch (m_sockaddr.family)
			{
			case AF_INET:
				hash += *(uint32_t *)&m_sockaddr.sockaddr4.sin_addr;
				hash = (hash << 16) | m_sockaddr.sockaddr4.sin_port;
				break;

			case AF_INET6:
			{
				uint32_t *addr = (uint32_t *)&m_sockaddr.sockaddr6.sin6_addr;
				hash += addr[0];
				hash = hash * 0xFFFF + addr[1];
				hash = hash * 0xFFFF + addr[2];
				hash = hash * 0xFFFF + addr[3];
				hash = (hash << 16) | m_sockaddr.sockaddr6.sin6_port;
			}
			break;

			default:
				hash += m_sockaddr.family;
				break;
			}
			return hash;
		}

		inline std::string getIp() const
		{
			char ip[64];
			switch (m_sockaddr.family)
			{
			case AF_INET:
				if (uv_ip4_name(&m_sockaddr.sockaddr4, ip, sizeof(ip)) != 0)
					ip[0] = 0;
				break;

			case AF_INET6:
				if (uv_ip6_name(&m_sockaddr.sockaddr6, ip, sizeof(ip)) != 0)
					ip[0] = 0;
				break;

			default:
				ip[0] = 0;
				break;
			}
			return std::move(std::string(ip));
		}

		inline unsigned short getPort() const
		{
			switch (m_sockaddr.family)
			{
			case AF_INET:
				return ntohs(m_sockaddr.sockaddr4.sin_port);

			case AF_INET6:
				return ntohs(m_sockaddr.sockaddr6.sin6_port);

			default:
				return 0;
			}
		}

		inline bool isIpv6() const
		{
			return m_sockaddr.family == AF_INET6;
		}

		inline bool valid() const
		{
			return AF_INET == m_sockaddr.family || AF_INET6 == m_sockaddr.family;
		}
	};

	class CnetworkNode : public CcachedAllocator
	{
	public:
		enum protocol_type
		{
			protocol_tcp = 0,
			protocol_udp
		};

	private:
		protocol_type m_protocol;
		Csockaddr m_sockaddr;

		size_t m_hash;

		inline void rehash()
		{
			m_hash = m_sockaddr.getHash(m_protocol * 31);
		}

	public:
		CnetworkNode()
			:m_protocol(protocol_tcp), m_hash(0) {} // All zero and hash is 0.
		CnetworkNode(const protocol_type protocol, const sockaddr * const raw, const size_t size)
			:m_protocol(protocol), m_sockaddr(raw, size) { rehash(); }
		CnetworkNode(const protocol_type protocol, const char * const ip, const unsigned short port)
			:m_protocol(protocol), m_sockaddr(ip, port) { rehash(); }
		CnetworkNode(const CnetworkNode& another)
			:m_protocol(another.m_protocol), m_sockaddr(another.m_sockaddr), m_hash(another.m_hash) {}
		CnetworkNode(CnetworkNode&& another) // Move is copy, and another.m_hash will not change.
			:m_protocol(another.m_protocol), m_sockaddr(std::move(another.m_sockaddr)), m_hash(another.m_hash) {}

		const CnetworkNode& operator=(const CnetworkNode& another)
		{
			m_protocol = another.m_protocol;
			m_sockaddr = another.m_sockaddr;
			m_hash = another.m_hash;
			return *this;
		}
		const CnetworkNode& operator=(CnetworkNode&& another) // Move is copy, and another.m_hash will not change.
		{
			m_protocol = another.m_protocol;
			m_sockaddr = another.m_sockaddr;
			m_hash = another.m_hash;
			return *this;
		}

		bool operator<(const CnetworkNode& another) const
		{
			if (m_hash != another.m_hash)
				return m_hash < another.m_hash;
			if (m_protocol != another.m_protocol)
				return m_protocol < another.m_protocol;
			return m_sockaddr < another.m_sockaddr;
		}
		bool operator==(const CnetworkNode& another) const
		{
			return m_hash == another.m_hash && m_protocol == another.m_protocol && m_sockaddr == another.m_sockaddr;
		}
		bool operator!=(const CnetworkNode& another) const
		{
			return !operator==(another);
		}

		inline bool set(const protocol_type protocol, const sockaddr * const raw, const size_t size)
		{
			m_protocol = protocol;
			bool bRet = m_sockaddr.init(raw, size);
			rehash();
			return bRet;
		}
		inline bool set(const protocol_type protocol, const char * const ip, const unsigned short port)
		{
			m_protocol = protocol;
			bool bRet = m_sockaddr.init(ip, port);
			rehash();
			return bRet;
		}

		inline protocol_type getProtocol() const
		{
			return m_protocol;
		}
		inline const Csockaddr& getSockaddr() const
		{
			return m_sockaddr;
		}

		inline size_t getHash() const
		{
			return m_hash;
		}
	};

	class CnetworkPair : public CcachedAllocator
	{
	private:
		CnetworkNode::protocol_type m_protocol;
		Csockaddr m_local;
		Csockaddr m_remote;

		size_t m_hash;

		inline void rehash()
		{
			m_hash = m_remote.getHash(m_local.getHash(m_protocol * 31));
		}

	public:
		CnetworkPair() :m_protocol(CnetworkNode::protocol_tcp), m_hash(0) {} // All zero and hash is 0.
		CnetworkPair(const CnetworkNode::protocol_type protocol, const Csockaddr& local, const Csockaddr& remote)
			:m_protocol(protocol), m_local(local), m_remote(remote) { rehash(); }
		CnetworkPair(const CnetworkNode::protocol_type protocol, Csockaddr&& local, const Csockaddr& remote)
			:m_protocol(protocol), m_local(local), m_remote(remote) { rehash(); }
		CnetworkPair(const CnetworkNode::protocol_type protocol, const Csockaddr& local, Csockaddr&& remote)
			:m_protocol(protocol), m_local(local), m_remote(remote) { rehash(); }
		CnetworkPair(const CnetworkNode::protocol_type protocol, Csockaddr&& local, Csockaddr&& remote)
			:m_protocol(protocol), m_local(local), m_remote(remote) { rehash(); }
		CnetworkPair(const CnetworkPair& another)
			:m_protocol(another.m_protocol), m_local(another.m_local), m_remote(another.m_remote), m_hash(another.m_hash) {}
		CnetworkPair(CnetworkPair&& another) // Move is copy, and another.m_hash will not change.
			:m_protocol(another.m_protocol), m_local(std::move(another.m_local)), m_remote(std::move(another.m_remote)), m_hash(another.m_hash) {}

		const CnetworkPair& operator=(const CnetworkPair& another)
		{
			m_local = another.m_local;
			m_remote = another.m_remote;
			m_hash = another.m_hash;
			return *this;
		}
		const CnetworkPair& operator=(CnetworkPair&& another) // Move is copy, and another.m_hash will not change.
		{
			m_local = std::move(another.m_local);
			m_remote = std::move(another.m_remote);
			m_hash = another.m_hash;
			return *this;
		}
		bool operator<(const CnetworkPair& another) const
		{
			if (m_hash != another.m_hash)
				return m_hash < another.m_hash;
			if (m_remote != another.m_remote) // Compare remote address(usually different).
				return m_remote < another.m_remote;
			return m_local < another.m_local;
		}
		bool operator==(const CnetworkPair& another) const
		{
			return m_hash == another.m_hash && m_remote == another.m_remote && m_local == another.m_local; // Compare remote address(usually different).
		}
		bool operator!=(const CnetworkPair& another) const
		{
			return !operator==(another);
		}

		inline void setProtocol(const CnetworkNode::protocol_type protocol)
		{
			m_protocol = protocol;
			rehash();
		}
		inline void setLocal(const Csockaddr& local)
		{
			m_local = local;
			rehash();
		}
		inline void setLocal(Csockaddr&& local)
		{
			m_local = local;
			rehash();
		}
		inline void setRemote(const Csockaddr& remote)
		{
			m_remote = remote;
			rehash();
		}
		inline void setRemote(Csockaddr&& remote)
		{
			m_remote = remote;
			rehash();
		}

		inline CnetworkNode::protocol_type getProtocol() const
		{
			return m_protocol;
		}
		inline const Csockaddr& getLocal() const
		{
			return m_local;
		}
		inline const Csockaddr& getRemote() const
		{
			return m_remote;
		}

		inline size_t getHash() const
		{
			return m_hash;
		}
	};

	struct __network_hash
	{
		size_t operator()(const CnetworkNode& k) const
		{
			return k.getHash();
		}
		size_t operator()(const CnetworkPair& k) const
		{
			return k.getHash();
		}
	};
}
