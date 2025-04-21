// SPDX-FileCopyrightText: Copyright 2025 sumi Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <charconv>
#include <cstring>
#include <string>
#include <string_view>

#include "common/logging/log.h"
#include "core/hle/service/sockets/bsd.h"
#include "core/hle/service/sockets/socket_utils.h"

namespace Service::Sockets::nn::socket {

bool InetAton(const char* ip, in_addr* addr) {
    if (ip == nullptr || addr == nullptr) {
        return false;
    }

    std::string_view ip_view(ip);

    // Count the number of dots to validate IPv4 format
    size_t dots = std::count(ip_view.begin(), ip_view.end(), '.');
    if (dots != 3) {
        return false;
    }

    // Parse the IP address in standard dotted-decimal notation
    u32 result = 0;
    size_t pos = 0;

    for (int i = 0; i < 4; i++) {
        size_t next_dot = ip_view.find('.', pos);
        std::string_view octet_view;

        if (i < 3) {
            if (next_dot == std::string_view::npos) {
                return false;
            }
            octet_view = ip_view.substr(pos, next_dot - pos);
            pos = next_dot + 1;
        } else {
            octet_view = ip_view.substr(pos);
        }

        u32 octet;
        auto [ptr, ec] = std::from_chars(octet_view.data(), octet_view.data() + octet_view.size(), octet);
        if (ec != std::errc() || octet > 255 || (ptr != octet_view.data() + octet_view.size())) {
            return false;
        }

        result = (result << 8) | octet;
    }

    addr->s_addr = result;
    return true;
}

s32 Connect(s32 socket, const sockaddr* addr, u32 addr_len) {
    if (addr == nullptr || addr_len < sizeof(sockaddr)) {
        LOG_ERROR(Service_BSD, "Invalid address pointer or length");
        // Set errno to EINVAL (Invalid argument)
        errno = static_cast<u32>(Errno::INVAL);
        return -1;
    }

    // Create a BSD-compliant sockaddr_in from our sockaddr
    SockAddrIn bsd_addr{};
    bsd_addr.len = sizeof(SockAddrIn);
    // Cast explicitly with a mask to ensure valid range conversion
    bsd_addr.family = static_cast<u8>(addr->sa_family & 0xFF);

    if (addr->sa_family == 2) { // AF_INET
        const auto* addr_in = reinterpret_cast<const sockaddr_in*>(addr);
        bsd_addr.portno = addr_in->sin_port;

        // Copy IPv4 address (in network byte order)
        const u32 ip_addr = addr_in->sin_addr.s_addr;
        bsd_addr.ip[0] = static_cast<u8>((ip_addr >> 24) & 0xFF);
        bsd_addr.ip[1] = static_cast<u8>((ip_addr >> 16) & 0xFF);
        bsd_addr.ip[2] = static_cast<u8>((ip_addr >> 8) & 0xFF);
        bsd_addr.ip[3] = static_cast<u8>(ip_addr & 0xFF);
    } else {
        LOG_ERROR(Service_BSD, "Unsupported address family: {}", addr->sa_family);
        // Set errno to EAFNOSUPPORT (Address family not supported)
        errno = static_cast<u32>(Errno::INVAL); // Using INVAL as a substitute for EAFNOSUPPORT
        return -1;
    }

    // Forward to the BSD socket implementation
    return BSD::Connect(socket, bsd_addr);
}

} // namespace Service::Sockets::nn::socket
