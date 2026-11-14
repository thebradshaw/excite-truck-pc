// A WebSocket client connection (ws:// or wss://) running on its own thread. Messages go in and
// come out through thread-safe queues, so the game can poll once a frame without ever blocking.
// Windows uses WinHTTP (system TLS, proxies); Linux uses sockets and mbed TLS for wss://.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace netplay {

struct Event {
    enum Kind { Connected, Message, Closed } kind = Closed;
    std::vector<uint8_t> data;   // Message
    std::string reason;          // Closed: why, in words for the player ("" = closed normally)
};

class Connection {
public:
    virtual ~Connection() = default;
    // Queues one binary message. Ignored once closed.
    virtual void Send(std::vector<uint8_t> message) = 0;
    // Takes the next event, if any.
    virtual bool Poll(Event& out) = 0;
    virtual void Close() = 0;

    // Starts connecting to ws://host[:port][/path] or wss://... . Never null: a bad URL arrives as
    // a Closed event.
    static std::unique_ptr<Connection> Open(const std::string& url);
};

struct Url {
    bool secure = false;
    std::string host;
    uint16_t port = 0;
    std::string path = "/";
};
// Accepts "wss://x", "ws://x:1234/p", "https://x" (as wss), "http://x" (as ws) and a bare
// "host[:port]" (ws, default port kDefaultPort).
bool ParseUrl(const std::string& text, Url& out);

} // namespace netplay
