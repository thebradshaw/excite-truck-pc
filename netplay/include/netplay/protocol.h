// Network Play wire format, shared by the games, the launcher, the PC relay and (in JavaScript)
// the cloud relay in netplay/server/cloudflare/worker.js. Keep the two in step.
//
// Every WebSocket message is one binary frame: a type byte, then fields. Integers and floats are
// little endian; a string is a u16 byte count and UTF-8 bytes.
//
// The relay only knows rooms and members. Everything about races travels as an opaque payload in
// Send / Recv (see session.h), so the relays stay small and never need updating for game changes.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace netplay {

constexpr uint16_t kProtocolVersion = 1;
constexpr int kMaxPlayers = 6;            // per room, and so per race
constexpr uint16_t kDefaultPort = 47630;  // "host on this PC"
constexpr uint8_t kToEveryone = 0xFF;
constexpr size_t kMaxNameBytes = 24;
constexpr size_t kMaxRoomBytes = 32;
constexpr size_t kMaxMessageBytes = 16 * 1024;

enum class Msg : uint8_t {
    Hello = 1,    // C->S  u16 version, u8 create, str room, str password, str name, u32 color, str game, str build
    Welcome = 2,  // S->C  u8 your id, u8 count, count x {u8 id, str name, u32 color}
    Error = 3,    // S->C  u8 code, str text; the relay closes the connection after it
    Joined = 4,   // S->C  u8 id, str name, u32 color
    Left = 5,     // S->C  u8 id
    Send = 6,     // C->S  u8 target (kToEveryone or an id), payload to the end
    Recv = 7,     // S->C  u8 from, payload to the end
};

enum class ErrorCode : uint8_t {
    BadHello = 1,
    RoomExists = 2,
    NoRoom = 3,
    RoomFull = 4,
    WrongPassword = 5,
    WrongGame = 6,
    WrongBuild = 7,
    NameTaken = 8,
    WrongVersion = 9,
};

// Text the launcher and the game show for a relay error.
const char* ErrorText(ErrorCode code);

struct Writer {
    std::vector<uint8_t> b;
    Writer& u8(uint8_t v) { b.push_back(v); return *this; }
    Writer& u16(uint16_t v) { u8(uint8_t(v)); return u8(uint8_t(v >> 8)); }
    Writer& u32(uint32_t v) { u16(uint16_t(v)); return u16(uint16_t(v >> 16)); }
    Writer& i32(int32_t v) { return u32(uint32_t(v)); }
    Writer& f32(float v) { uint32_t u; std::memcpy(&u, &v, 4); return u32(u); }
    Writer& str(std::string_view s) {
        const size_t n = s.size() > 0xFFFF ? 0xFFFF : s.size();
        u16(uint16_t(n));
        b.insert(b.end(), s.begin(), s.begin() + n);
        return *this;
    }
    Writer& bytes(const uint8_t* p, size_t n) { b.insert(b.end(), p, p + n); return *this; }
};

struct Reader {
    const uint8_t* p;
    size_t n;
    size_t at = 0;
    bool ok = true;
    Reader(const uint8_t* data, size_t size) : p(data), n(size) {}
    explicit Reader(const std::vector<uint8_t>& v) : p(v.data()), n(v.size()) {}
    bool Need(size_t k) { if (!ok || n - at < k) { ok = false; return false; } return true; }
    uint8_t u8() { if (!Need(1)) return 0; return p[at++]; }
    uint16_t u16() { const uint16_t lo = u8(); return uint16_t(lo | (u8() << 8)); }
    uint32_t u32() { const uint32_t lo = u16(); return lo | (uint32_t(u16()) << 16); }
    int32_t i32() { return int32_t(u32()); }
    float f32() { const uint32_t u = u32(); float f; std::memcpy(&f, &u, 4); return f; }
    std::string str() {
        const uint16_t len = u16();
        if (!Need(len)) return {};
        std::string s(reinterpret_cast<const char*>(p + at), len);
        at += len;
        return s;
    }
    std::vector<uint8_t> rest() {
        std::vector<uint8_t> v(p + at, p + n);
        at = n;
        return v;
    }
    size_t left() const { return n - at; }
};

struct Member {
    uint8_t id = 0;
    std::string name;
    uint32_t color = 0xFFFFFFFF;   // 0xRRGGBBAA
};

struct Hello {
    uint16_t version = kProtocolVersion;
    bool create = false;
    std::string room, password, name;
    uint32_t color = 0xFFFFFFFF;
    std::string game;    // "excite_truck" or "excitebots"
    std::string build;   // players in one room must run the same build
};

std::vector<uint8_t> EncodeHello(const Hello& h);
bool DecodeHello(Reader& r, Hello& h);

// Rooms are matched without regard to case or surrounding spaces.
std::string RoomKey(std::string_view room);
// Names and room names: trimmed, control characters removed, cut to max bytes on a UTF-8 boundary.
std::string CleanText(std::string_view text, size_t maxBytes);

} // namespace netplay
