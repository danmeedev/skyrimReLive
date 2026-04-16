#pragma once

// Thin opaque wrapper around winsock UDP. This header deliberately exposes
// no Windows or winsock2 types — Socket.cpp owns those. Net.cpp (which uses
// CommonLib / RE / SKSE types) talks to the OS only through this surface.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace relive::sock {

    using Handle = std::uintptr_t;
    constexpr Handle kInvalid = ~Handle{0};

    // One-time WSAStartup. Idempotent. Returns false on failure.
    bool init();

    // Open a connected UDP socket. Returns kInvalid on failure.
    Handle udp_connect_ipv4(std::string_view host, std::uint16_t port);

    void close(Handle h);

    // Set recv timeout in ms. Subsequent recv() will return 0 on timeout.
    void set_recv_timeout(Handle h, std::uint32_t ms);

    // Sends `buf` once. Returns true if the kernel accepted the bytes.
    bool send_all(Handle h, std::span<const std::uint8_t> buf);

    // Recv up to `buf.size()` bytes. Returns the byte count, or 0 on timeout,
    // or -1 on error.
    int recv_one(Handle h, std::span<std::uint8_t> buf);

}
