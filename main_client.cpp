/**
 * @file main_client.cpp
 * @brief Client app: connect, send ClientInput (position), receive Spawn/Despawn/Snapshot, print stats (M13.1, M13.3).
 */

#include "engine/network/Protocol.h"
#include "engine/network/Replication.h"
#include "engine/network/UdpSocket.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <unordered_map>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

int main(int argc, char** argv) {
    if (!engine::network::NetworkInit())
        return 1;
    const char* host = (argc >= 2) ? argv[1] : "127.0.0.1";
    const uint16_t port = engine::network::kDefaultServerPort;
    int fd = engine::network::UdpSocketCreate();
    if (fd < 0) {
        engine::network::NetworkShutdown();
        return 1;
    }
    engine::network::PeerAddress serverAddr{};
#ifdef _WIN32
    sockaddr_in* sa = reinterpret_cast<sockaddr_in*>(serverAddr.data);
    sa->sin_family = AF_INET;
    sa->sin_port = htons(port);
    inet_pton(AF_INET, host, &sa->sin_addr);
#else
    struct sockaddr_in* sa = reinterpret_cast<struct sockaddr_in*>(serverAddr.data);
    sa->sin_family = AF_INET;
    sa->sin_port = htons(port);
    inet_pton(AF_INET, host, &sa->sin_addr);
#endif
    uint8_t conn[1] = { static_cast<uint8_t>(engine::network::MsgType::Connect) };
    if (engine::network::UdpSendTo(fd, conn, 1, &serverAddr) < 0) {
        engine::network::UdpSocketClose(fd);
        engine::network::NetworkShutdown();
        return 1;
    }
    if (!engine::network::UdpSetNonBlocking(fd)) {
        engine::network::UdpSocketClose(fd);
        engine::network::NetworkShutdown();
        return 1;
    }
    uint32_t clientId = 0xFFFFFFFFu;
    uint32_t lastTick = 0;
    uint32_t snapCount = 0;
    auto lastStats = std::chrono::steady_clock::now();
    auto lastInput = std::chrono::steady_clock::now();
    uint8_t buf[1024];
    engine::network::PeerAddress from{};
    std::unordered_map<uint64_t, engine::network::ReplicationEntityState> replicatedEntities;

    for (int wait = 0; wait < 500; ++wait) {
        int n = engine::network::UdpRecvFrom(fd, buf, sizeof(buf), &from);
        if (n >= 5 && buf[0] == static_cast<uint8_t>(engine::network::MsgType::ConnectAck)) {
            std::memcpy(&clientId, buf + 1, 4);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (clientId == 0xFFFFFFFFu) {
        std::fprintf(stderr, "client: no ConnectAck received\n");
        engine::network::UdpSocketClose(fd);
        engine::network::NetworkShutdown();
        return 1;
    }
    std::printf("client: connected, clientId=%u\n", clientId);

    float myPosition[3] = { 100.f * static_cast<float>(clientId), 0.f, 100.f };

    for (;;) {
        int n = engine::network::UdpRecvFrom(fd, buf, sizeof(buf), &from);
        if (n > 0) {
            uint8_t msgType = buf[0];
            if (msgType == static_cast<uint8_t>(engine::network::MsgType::Spawn) && n >= 41) {
                engine::network::ReplicationEntityState st;
                if (engine::network::ParseSpawn(buf + 1, static_cast<size_t>(n) - 1, st))
                    replicatedEntities[st.entityId] = st;
            } else if (msgType == static_cast<uint8_t>(engine::network::MsgType::Despawn) && n >= 9) {
                uint64_t eid;
                if (engine::network::ParseDespawn(buf + 1, static_cast<size_t>(n) - 1, eid))
                    replicatedEntities.erase(eid);
            } else if (msgType == static_cast<uint8_t>(engine::network::MsgType::Snapshot) && n >= 9) {
                uint32_t tickVal;
                std::vector<engine::network::ReplicationEntityState> states;
                if (engine::network::ParseSnapshotWithStates(buf + 1, static_cast<size_t>(n) - 1, tickVal, states)) {
                    snapCount++;
                    lastTick = tickVal;
                    for (const auto& st : states)
                        replicatedEntities[st.entityId] = st;
                }
            }
        }

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration<double>(now - lastInput).count() >= 0.05) {
            uint8_t input[13];
            input[0] = static_cast<uint8_t>(engine::network::MsgType::ClientInput);
            std::memcpy(input + 1, myPosition, 12);
            engine::network::UdpSendTo(fd, input, sizeof(input), &serverAddr);
            lastInput = now;
        }
        if (std::chrono::duration<double>(now - lastStats).count() >= 2.0) {
            std::printf("client: tick=%u snapshots=%u entities=%zu\n", lastTick, snapCount, replicatedEntities.size());
            lastStats = now;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    engine::network::UdpSocketClose(fd);
    engine::network::NetworkShutdown();
    return 0;
}
