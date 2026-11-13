#include "netplay/relay_server.h"
#include "netplay/protocol.h"
#include "netplay/relay.h"
#include "sock.h"
#include "ws_util.h"

#include <chrono>
#include <map>

#ifndef _WIN32
#include <ifaddrs.h>
#endif

namespace netplay {

namespace {

using Clock = std::chrono::steady_clock;

struct Client {
    sock::Socket s = sock::kInvalid;
    std::string head;          // HTTP request until the upgrade is done
    bool open = false;         // WebSocket established
    bool closing = false;      // close once `out` is written
    ws::Parser parser{kMaxMessageBytes + 16};
    std::vector<uint8_t> out;
    Clock::time_point lastHeard = Clock::now();
    Clock::time_point lastPing = Clock::now();
};

} // namespace

struct RelayServer::Impl {
    sock::Socket listener = sock::kInvalid;
    std::map<uint64_t, Client> clients;
    uint64_t nextId = 1;
    RelayRooms rooms{[this](uint64_t id, std::vector<uint8_t> msg) { Queue(id, ws::Binary, msg); },
                     [this](uint64_t id) { if (auto c = clients.find(id); c != clients.end()) c->second.closing = true; }};

    void Queue(uint64_t id, uint8_t opcode, const std::vector<uint8_t>& msg) {
        auto c = clients.find(id);
        if (c == clients.end() || !c->second.open) return;
        auto f = ws::Frame(opcode, msg.data(), msg.size(), false);
        c->second.out.insert(c->second.out.end(), f.begin(), f.end());
    }

    void Handshake(uint64_t id, Client& c) {
        const size_t end = c.head.find("\r\n\r\n");
        if (end == std::string::npos) {
            if (c.head.size() > 8192) c.closing = true;
            return;
        }
        const std::string head = c.head.substr(0, end + 2);
        const std::string rest = c.head.substr(end + 4);
        c.head.clear();
        const std::string key = ws::Header(head, "Sec-WebSocket-Key");
        if (key.empty()) {
            // A browser or a health check: say what this is.
            const std::string body = "Excite PC Network Play relay\n";
            const std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " +
                                     std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
            c.out.insert(c.out.end(), resp.begin(), resp.end());
            c.closing = true;
            return;
        }
        const std::string resp = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                                 "Sec-WebSocket-Accept: " + ws::AcceptKey(key) + "\r\n\r\n";
        c.out.insert(c.out.end(), resp.begin(), resp.end());
        c.open = true;
        if (!rest.empty()) c.parser.Feed(reinterpret_cast<const uint8_t*>(rest.data()), rest.size());
        Drain(id, c);
    }

    void Drain(uint64_t id, Client& c) {
        ws::Message m;
        for (;;) {
            const int r = c.parser.Next(m);
            if (r == 0) return;
            if (r < 0) { c.closing = true; return; }
            switch (m.opcode) {
                case ws::Binary: rooms.OnMessage(id, m.data.data(), m.data.size()); break;
                case ws::Ping: Queue(id, ws::Pong, m.data); break;
                case ws::Close: Queue(id, ws::Close, {}); c.closing = true; return;
                default: break;
            }
            if (clients.find(id) == clients.end()) return;
        }
    }

    void Drop(uint64_t id) {
        auto c = clients.find(id);
        if (c == clients.end()) return;
        sock::Close(c->second.s);
        clients.erase(c);
        rooms.OnClosed(id);
    }
};

RelayServer::RelayServer() : impl_(std::make_unique<Impl>()) {}
RelayServer::~RelayServer() { Stop(); }

bool RelayServer::Start(uint16_t port, std::string* error) {
    Stop();
    sock::Startup();
    sock::Socket s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == sock::kInvalid) {
        if (error) *error = "Could not open a network socket.";
        return false;
    }
    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&on), sizeof on);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || ::listen(s, 16) != 0) {
        sock::Close(s);
        if (error) *error = "Port " + std::to_string(port) + " is already in use on this PC. Close the other program, or pick another port.";
        return false;
    }
    sock::SetNonBlocking(s);
    impl_->listener = s;
    port_ = port;
    running_ = true;
    thread_ = std::thread([this] { Run(); });
    return true;
}

void RelayServer::Stop() {
    if (!running_) return;
    running_ = false;
    if (thread_.joinable()) thread_.join();
    for (auto& [id, c] : impl_->clients) sock::Close(c.s);
    impl_->clients.clear();
    sock::Close(impl_->listener);
    impl_->listener = sock::kInvalid;
    impl_ = std::make_unique<Impl>();
}

void RelayServer::Run() {
    Impl& im = *impl_;
    std::vector<sock::PollFd> fds;
    std::vector<uint64_t> ids;
    uint8_t buf[16384];
    while (running_) {
        fds.clear();
        ids.clear();
        fds.push_back({im.listener, POLLIN, 0});
        ids.push_back(0);
        for (auto& [id, c] : im.clients) {
            short ev = POLLIN;
            if (!c.out.empty()) ev |= POLLOUT;
            fds.push_back({c.s, ev, 0});
            ids.push_back(id);
        }
        sock::Poll(fds.data(), (unsigned long)fds.size(), 50);

        if (fds[0].revents & POLLIN) {
            for (;;) {
                sock::Socket s = ::accept(im.listener, nullptr, nullptr);
                if (s == sock::kInvalid) break;
                sock::SetNonBlocking(s);
                int on = 1;
                setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof on);
                Client c;
                c.s = s;
                im.clients.emplace(im.nextId++, std::move(c));
            }
        }

        const auto now = Clock::now();
        for (size_t i = 1; i < fds.size(); ++i) {
            const uint64_t id = ids[i];
            auto it = im.clients.find(id);
            if (it == im.clients.end()) continue;
            Client& c = it->second;
            bool dead = (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 && !(fds[i].revents & POLLIN);
            if (!dead && (fds[i].revents & POLLIN)) {
                const sock::ssize n = ::recv(c.s, reinterpret_cast<char*>(buf), sizeof buf, 0);
                if (n <= 0) {
                    dead = n == 0 || !sock::WouldBlock();
                } else {
                    c.lastHeard = now;
                    if (!c.open) {
                        c.head.append(reinterpret_cast<char*>(buf), size_t(n));
                        im.Handshake(id, c);
                    } else {
                        c.parser.Feed(buf, size_t(n));
                        im.Drain(id, c);
                    }
                }
            }
            it = im.clients.find(id);
            if (it == im.clients.end()) continue;
            if (!dead && it->second.open && now - it->second.lastPing > std::chrono::seconds(10)) {
                it->second.lastPing = now;
                im.Queue(id, ws::Ping, {});
            }
            Client& cc = it->second;
            if (!dead && !cc.out.empty()) {
                const sock::ssize n = ::send(cc.s, reinterpret_cast<const char*>(cc.out.data()), int(cc.out.size()), sock::kNoSignal);
                if (n > 0) cc.out.erase(cc.out.begin(), cc.out.begin() + n);
                else if (n < 0 && !sock::WouldBlock()) dead = true;
                if (cc.out.size() > 4 * 1024 * 1024) dead = true;   // a client that stopped reading
            }
            if (now - cc.lastHeard > std::chrono::seconds(30)) dead = true;   // pings answered count
            if (dead || (cc.closing && cc.out.empty())) im.Drop(id);
        }
    }
}

std::vector<std::string> LocalAddresses() {
    std::vector<std::string> lan, other;
    auto add = [&](const in_addr& a) {
        char text[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &a, text, sizeof text);
        const uint32_t h = ntohl(a.s_addr);
        if ((h >> 24) == 127) return;
        const bool priv = (h >> 24) == 10 || (h >> 20) == 0xAC1 || (h >> 16) == 0xC0A8;
        auto& list = priv ? lan : other;
        for (auto& s : list) if (s == text) return;
        list.emplace_back(text);
    };
#ifdef _WIN32
    sock::Startup();
    char host[256] = {};
    if (gethostname(host, sizeof host) == 0) {
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        if (getaddrinfo(host, nullptr, &hints, &res) == 0) {
            for (addrinfo* p = res; p; p = p->ai_next) add(reinterpret_cast<sockaddr_in*>(p->ai_addr)->sin_addr);
            freeaddrinfo(res);
        }
    }
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) == 0) {
        for (ifaddrs* p = list; p; p = p->ifa_next) {
            if (p->ifa_addr && p->ifa_addr->sa_family == AF_INET) add(reinterpret_cast<sockaddr_in*>(p->ifa_addr)->sin_addr);
        }
        freeifaddrs(list);
    }
#endif
    lan.insert(lan.end(), other.begin(), other.end());
    return lan;
}

} // namespace netplay
