#include "netplay/protocol.h"
#include "netplay/relay.h"

#include <cctype>

namespace netplay {

const char* ErrorText(ErrorCode code) {
    switch (code) {
        case ErrorCode::BadHello: return "The server didn't understand this game. Check the server address.";
        case ErrorCode::RoomExists: return "A room with that name is already open. Pick another name, or join it instead.";
        case ErrorCode::NoRoom: return "No room with that name is open. Check the spelling, or host it instead.";
        case ErrorCode::RoomFull: return "That room is full (6 players).";
        case ErrorCode::WrongPassword: return "Wrong room password.";
        case ErrorCode::WrongGame: return "That room is playing the other game.";
        case ErrorCode::WrongBuild: return "That room's host has a different version of Excite PC. Everyone needs the same version.";
        case ErrorCode::NameTaken: return "Someone in that room already uses that name.";
        case ErrorCode::WrongVersion: return "The server runs a different Network Play version.";
    }
    return "The server refused the connection.";
}

std::vector<uint8_t> EncodeHello(const Hello& h) {
    Writer w;
    w.u8(uint8_t(Msg::Hello)).u16(h.version).u8(h.create ? 1 : 0).str(h.room).str(h.password).str(h.name);
    w.u32(h.color).str(h.game).str(h.build);
    return std::move(w.b);
}

bool DecodeHello(Reader& r, Hello& h) {
    h.version = r.u16();
    h.create = r.u8() != 0;
    h.room = r.str();
    h.password = r.str();
    h.name = r.str();
    h.color = r.u32();
    h.game = r.str();
    h.build = r.str();
    return r.ok;
}

std::string CleanText(std::string_view text, size_t maxBytes) {
    std::string out;
    for (unsigned char c : text) {
        if (c < 0x20 || c == 0x7F) continue;
        out.push_back(char(c));
    }
    size_t a = 0, b = out.size();
    while (a < b && out[a] == ' ') ++a;
    while (b > a && out[b - 1] == ' ') --b;
    out = out.substr(a, b - a);
    if (out.size() > maxBytes) {
        size_t cut = maxBytes;
        while (cut > 0 && (uint8_t(out[cut]) & 0xC0) == 0x80) --cut;   // don't split a character
        out.resize(cut);
    }
    return out;
}

std::string RoomKey(std::string_view room) {
    std::string key = CleanText(room, kMaxRoomBytes);
    for (char& c : key) c = char(std::tolower(uint8_t(c)));
    return key;
}

// ---------------------------------------------------------------------------------------------

void RelayRooms::Fail(ConnId conn, ErrorCode code) {
    Writer w;
    w.u8(uint8_t(Msg::Error)).u8(uint8_t(code)).str(ErrorText(code));
    send_(conn, std::move(w.b));
    close_(conn);
}

void RelayRooms::OnMessage(ConnId conn, const uint8_t* data, size_t size) {
    Reader r(data, size);
    const Msg type = Msg(r.u8());
    auto it = conns_.find(conn);

    if (it == conns_.end()) {
        Hello h;
        if (type != Msg::Hello || !DecodeHello(r, h)) return Fail(conn, ErrorCode::BadHello);
        if (h.version != kProtocolVersion) return Fail(conn, ErrorCode::WrongVersion);
        const std::string key = RoomKey(h.room);
        const std::string name = CleanText(h.name, kMaxNameBytes);
        if (key.empty() || name.empty()) return Fail(conn, ErrorCode::BadHello);

        auto room = rooms_.find(key);
        if (h.create) {
            if (room != rooms_.end()) return Fail(conn, ErrorCode::RoomExists);
            room = rooms_.emplace(key, Room{h.password, h.game, h.build, {}, {}}).first;
        } else {
            if (room == rooms_.end()) return Fail(conn, ErrorCode::NoRoom);
            Room& rm = room->second;
            if (rm.password != h.password) return Fail(conn, ErrorCode::WrongPassword);
            if (rm.game != h.game) return Fail(conn, ErrorCode::WrongGame);
            if (rm.build != h.build) return Fail(conn, ErrorCode::WrongBuild);
            if (rm.members.size() >= size_t(kMaxPlayers)) return Fail(conn, ErrorCode::RoomFull);
            for (auto& [id, m] : rm.info) {
                if (RoomKey(m.name) == RoomKey(name)) return Fail(conn, ErrorCode::NameTaken);
            }
        }
        Room& rm = room->second;
        uint8_t id = 0;
        while (rm.members.count(id)) ++id;
        Member me{id, name, h.color};

        Writer welcome;
        welcome.u8(uint8_t(Msg::Welcome)).u8(id).u8(uint8_t(rm.info.size()));
        for (auto& [mid, m] : rm.info) welcome.u8(m.id).str(m.name).u32(m.color);
        send_(conn, std::move(welcome.b));

        Writer joined;
        joined.u8(uint8_t(Msg::Joined)).u8(id).str(name).u32(h.color);
        for (auto& [mid, c] : rm.members) send_(c, joined.b);

        rm.members[id] = conn;
        rm.info[id] = me;
        conns_[conn] = Conn{key, id};
        return;
    }

    if (type != Msg::Send) return;   // nothing else is valid once in a room; ignore it
    Room& rm = rooms_[it->second.roomKey];
    const uint8_t target = r.u8();
    if (!r.ok) return;
    Writer out;
    out.u8(uint8_t(Msg::Recv)).u8(it->second.id).bytes(data + r.at, size - r.at);
    if (target == kToEveryone) {
        for (auto& [id, c] : rm.members) {
            if (c != conn) send_(c, out.b);
        }
    } else if (auto m = rm.members.find(target); m != rm.members.end() && m->second != conn) {
        send_(m->second, std::move(out.b));
    }
}

void RelayRooms::OnClosed(ConnId conn) {
    auto it = conns_.find(conn);
    if (it == conns_.end()) return;
    const Conn c = it->second;
    conns_.erase(it);
    auto room = rooms_.find(c.roomKey);
    if (room == rooms_.end()) return;
    room->second.members.erase(c.id);
    room->second.info.erase(c.id);
    if (room->second.members.empty()) {
        rooms_.erase(room);
        return;
    }
    Writer left;
    left.u8(uint8_t(Msg::Left)).u8(c.id);
    for (auto& [id, other] : room->second.members) send_(other, left.b);
}

} // namespace netplay
