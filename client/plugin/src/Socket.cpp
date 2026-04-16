// This TU intentionally does NOT include the project PCH. CommonLib's
// REX/W32/BASE.h #errors out if any Windows API has been included before it,
// and winsock2.h pulls in <windows.h> transitively. Keeping winsock isolated
// to this file means the rest of the plugin (which uses RE/SKSE types) can
// include CommonLib normally.

#include "Socket.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <string>

#pragma comment(lib, "ws2_32.lib")

namespace relive::sock {

    namespace {
        std::atomic<bool> g_wsa_inited{false};
    }

    bool init() {
        if (g_wsa_inited.load(std::memory_order_acquire)) return true;
        WSADATA d{};
        if (WSAStartup(MAKEWORD(2, 2), &d) != 0) return false;
        g_wsa_inited.store(true, std::memory_order_release);
        return true;
    }

    Handle udp_connect_ipv4(std::string_view host, std::uint16_t port) {
        if (!init()) return kInvalid;

        const SOCKET sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock == INVALID_SOCKET) return kInvalid;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        const std::string host_s{host};
        if (inet_pton(AF_INET, host_s.c_str(), &addr.sin_addr) != 1) {
            closesocket(sock);
            return kInvalid;
        }
        if (::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            closesocket(sock);
            return kInvalid;
        }
        return static_cast<Handle>(sock);
    }

    void close(Handle h) {
        if (h == kInvalid) return;
        closesocket(static_cast<SOCKET>(h));
    }

    void set_recv_timeout(Handle h, std::uint32_t ms) {
        const SOCKET sock = static_cast<SOCKET>(h);
        const DWORD t = ms;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&t), sizeof(t));
    }

    bool send_all(Handle h, std::span<const std::uint8_t> buf) {
        const SOCKET sock = static_cast<SOCKET>(h);
        const int sent = ::send(sock, reinterpret_cast<const char*>(buf.data()),
                                static_cast<int>(buf.size()), 0);
        return sent > 0 && static_cast<std::size_t>(sent) == buf.size();
    }

    int recv_one(Handle h, std::span<std::uint8_t> buf) {
        const SOCKET sock = static_cast<SOCKET>(h);
        const int n = ::recv(sock, reinterpret_cast<char*>(buf.data()),
                             static_cast<int>(buf.size()), 0);
        if (n > 0) return n;
        if (n == 0) return 0;
        const int err = WSAGetLastError();
        if (err == WSAETIMEDOUT) return 0;
        return -1;
    }

}
