/**
 * @file UdpSocket.cpp
 * @brief UDP socket implementation (Windows Winsock) for M13.1.
 */

#include "engine/network/UdpSocket.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#endif

#include <cstring>

namespace engine::network {

namespace {
bool s_networkInitialized = false;
}

bool NetworkInit() {
    if (s_networkInitialized) return true;
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return false;
#endif
    s_networkInitialized = true;
    return true;
}

void NetworkShutdown() {
    if (!s_networkInitialized) return;
#ifdef _WIN32
    WSACleanup();
#endif
    s_networkInitialized = false;
}

int UdpSocketBind(uint16_t port) {
#ifdef _WIN32
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return -1;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closesocket(s);
        return -1;
    }
    return static_cast<int>(s);
#else
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return -1;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(s);
        return -1;
    }
    return s;
#endif
}

int UdpSocketCreate() {
#ifdef _WIN32
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    return (s == INVALID_SOCKET) ? -1 : static_cast<int>(s);
#else
    return socket(AF_INET, SOCK_DGRAM, 0);
#endif
}

void UdpSocketClose(int fd) {
    if (fd < 0) return;
#ifdef _WIN32
    closesocket(static_cast<SOCKET>(fd));
#else
    close(fd);
#endif
}

int UdpSendTo(int fd, const void* data, size_t len, const PeerAddress* peer) {
    if (fd < 0 || !data || !peer) return -1;
#ifdef _WIN32
    return sendto(static_cast<SOCKET>(fd), static_cast<const char*>(data), static_cast<int>(len), 0,
                  reinterpret_cast<const sockaddr*>(peer->data), static_cast<int>(sizeof(sockaddr_in)));
#else
    return static_cast<int>(sendto(fd, data, len, 0, reinterpret_cast<const sockaddr*>(peer->data), sizeof(sockaddr_in)));
#endif
}

int UdpRecvFrom(int fd, void* data, size_t len, PeerAddress* peer) {
    if (fd < 0 || !data || !peer) return -1;
#ifdef _WIN32
    int addrLen = sizeof(sockaddr_in);
    int n = recvfrom(static_cast<SOCKET>(fd), static_cast<char*>(data), static_cast<int>(len), 0,
                     reinterpret_cast<sockaddr*>(peer->data), &addrLen);
    return n;
#else
    socklen_t addrLen = sizeof(sockaddr_in);
    return static_cast<int>(recvfrom(fd, data, len, 0, reinterpret_cast<sockaddr*>(peer->data), &addrLen));
#endif
}

bool UdpSetNonBlocking(int fd) {
    if (fd < 0) return false;
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(static_cast<SOCKET>(fd), FIONBIO, &mode) == 0;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

} // namespace engine::network
