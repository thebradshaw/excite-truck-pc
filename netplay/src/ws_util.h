// Small WebSocket helpers (RFC 6455): SHA-1 + base64 for the handshake, frame encode and parse.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace netplay::ws {

enum Opcode : uint8_t { Continuation = 0, Text = 1, Binary = 2, Close = 8, Ping = 9, Pong = 10 };

std::string Base64(const uint8_t* data, size_t size);
// Sec-WebSocket-Accept for a Sec-WebSocket-Key.
std::string AcceptKey(std::string_view key);
std::string RandomKey();

// Clients must mask what they send; servers must not.
std::vector<uint8_t> Frame(uint8_t opcode, const uint8_t* payload, size_t size, bool mask);

struct Message {
    uint8_t opcode = 0;   // Text, Binary, Close, Ping or Pong (fragments are joined)
    std::vector<uint8_t> data;
};

class Parser {
public:
    explicit Parser(size_t maxMessage) : max_(maxMessage) {}
    void Feed(const uint8_t* data, size_t size) { buf_.insert(buf_.end(), data, data + size); }
    // 1 = a message in `out`, 0 = need more bytes, -1 = protocol error or too large.
    int Next(Message& out);

private:
    size_t max_;
    std::vector<uint8_t> buf_;
    std::vector<uint8_t> partial_;
    uint8_t partialOp_ = 0;
};

// Case-insensitive header lookup in an HTTP head ("Name: value" lines).
std::string Header(std::string_view head, std::string_view name);

} // namespace netplay::ws
