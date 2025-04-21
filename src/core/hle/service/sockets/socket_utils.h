// SPDX-FileCopyrightText: Copyright 2025 sumi Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/common_types.h"
#include <array>
#include <cstring>

namespace Service::Sockets {

// Base socket structures and utilities for nn::socket

// in_addr struct similar to standard BSD/POSIX implementation
struct in_addr {
    u32 s_addr;
};

// sockaddr struct similar to standard BSD/POSIX implementation
struct sockaddr {
    u16 sa_family;
    char sa_data[14];
};

// sockaddr_in struct similar to standard BSD/POSIX implementation
struct sockaddr_in {
    u16 sin_family;
    u16 sin_port;
    in_addr sin_addr;
    char sin_zero[8];
};

// Socket configuration data based on LibraryConfigData from switchbrew
struct Config {
    u32 version;
    u32 tcp_tx_buf_size;
    u32 tcp_rx_buf_size;
    u32 tcp_tx_buf_max_size;
    u32 tcp_rx_buf_max_size;
    u32 udp_tx_buf_size;
    u32 udp_rx_buf_size;
    u32 sb_efficiency;
};

namespace nn::socket {

// InetAton converts an IPv4 address string to an in_addr structure
// Returns true on success, false on failure
bool InetAton(const char* ip, in_addr* addr);

// Connect to a remote host
// Returns 0 on success, -1 on failure
s32 Connect(s32 socket, const sockaddr* addr, u32 addr_len);

} // namespace nn::socket

} // namespace Service::Sockets
