#pragma once

/**
 * @file UdpSocket.h
 * @brief UDP socket: bind (server), sendto, recvfrom (M13.1). Platform: Windows Winsock, Unix BSD.
 */

#include <cstddef>
#include <cstdint>

namespace engine::network {

/** @brief Opaque peer address (sockaddr_in). */
struct PeerAddress {
    char data[28] = {};
};

/** @brief Creates bound UDP socket (server). Port 0 = any. Returns -1 on failure. */
int UdpSocketBind(uint16_t port);
/** @brief Creates unbound UDP socket (client). */
int UdpSocketCreate();
/** @brief Closes socket. Safe with -1. */
void UdpSocketClose(int fd);
/** @brief Sends to peer. Returns bytes sent or -1. */
int UdpSendTo(int fd, const void* data, size_t len, const PeerAddress* peer);
/** @brief Receives datagram. Non-blocking if set. Returns bytes received, 0 if none, -1 on error. */
int UdpRecvFrom(int fd, void* data, size_t len, PeerAddress* peer);
/** @brief Sets socket non-blocking. */
bool UdpSetNonBlocking(int fd);
/** @brief Platform init (e.g. WSAStartup). Call once. */
bool NetworkInit();
/** @brief Platform shutdown (e.g. WSACleanup). */
void NetworkShutdown();

} // namespace engine::network
