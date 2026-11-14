// The "host on this PC" relay: a plain ws:// WebSocket server on its own thread. The game starts
// one when the launcher passes WC_NET_HOST_PORT, and ExciteRelay runs one on its own.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace netplay {

class RelayServer {
public:
    RelayServer();
    ~RelayServer();
    RelayServer(const RelayServer&) = delete;
    RelayServer& operator=(const RelayServer&) = delete;

    // Listens on every interface. False with a message if the port can't be opened.
    bool Start(uint16_t port, std::string* error);
    void Stop();
    bool Running() const { return running_; }
    uint16_t Port() const { return port_; }

private:
    void Run();
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    uint16_t port_ = 0;
};

// This PC's IPv4 addresses that friends could connect to (LAN first). For the launcher.
std::vector<std::string> LocalAddresses();

} // namespace netplay
