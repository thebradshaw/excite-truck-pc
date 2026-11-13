// ExciteRelay: a Network Play room server for your own PC or a small server.
//   ExciteRelay [port]      (default 47630)
// Friends connect with ws://<this machine's address>:<port>.
#include "netplay/protocol.h"
#include "netplay/relay_server.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

int main(int argc, char** argv) {
    const int port = argc > 1 ? std::atoi(argv[1]) : netplay::kDefaultPort;
    if (port <= 0 || port > 65535) {
        std::fprintf(stderr, "usage: ExciteRelay [port]\n");
        return 2;
    }
    netplay::RelayServer server;
    std::string error;
    if (!server.Start(uint16_t(port), &error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    std::printf("Excite PC Network Play relay on port %d. Friends connect to:\n", port);
    for (auto& a : netplay::LocalAddresses()) std::printf("  ws://%s:%d\n", a.c_str(), port);
    std::fflush(stdout);
    for (;;) std::this_thread::sleep_for(std::chrono::hours(1));
}
