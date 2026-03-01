/**
 * @file main_server.cpp
 * @brief Server app: UDP bind, tick loop 20 Hz, accept handshake, send empty snapshots (M13.1).
 */

#include "engine/network/Protocol.h"
#include "engine/network/ServerCore.h"
#include "engine/network/UdpSocket.h"

#include <chrono>
#include <thread>
#include <vector>

int main(int, char**) {
    if (!engine::network::NetworkInit()) return 1;
    int fd = engine::network::UdpSocketBind(engine::network::kDefaultServerPort);
    if (fd < 0) {
        engine::network::NetworkShutdown();
        return 1;
    }
    engine::network::UdpSetNonBlocking(fd);
    std::vector<engine::network::ServerClient> clients;
    uint32_t nextId = 0;
    uint32_t tick = 0;
    auto nextTick = std::chrono::steady_clock::now();
    const auto tickDur = std::chrono::duration<double>(engine::network::kServerTickInterval);
    for (;;) {
        auto now = std::chrono::steady_clock::now();
        while (now >= nextTick) {
            engine::network::ServerTick(fd, clients, nextId, tick);
            nextTick += tickDur;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
