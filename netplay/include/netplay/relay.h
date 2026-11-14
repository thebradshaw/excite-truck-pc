// The room logic of a relay, without any sockets. RelayServer (relay_server.h) drives it for the
// "host on this PC" option; worker.js does the same in the cloud.
#pragma once

#include "netplay/protocol.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace netplay {

class RelayRooms {
public:
    using ConnId = uint64_t;
    // Sends one binary message to a connection.
    using SendFn = std::function<void(ConnId, std::vector<uint8_t>)>;
    // Closes a connection (after anything already sent to it).
    using CloseFn = std::function<void(ConnId)>;

    RelayRooms(SendFn send, CloseFn close) : send_(std::move(send)), close_(std::move(close)) {}

    void OnMessage(ConnId conn, const uint8_t* data, size_t size);
    void OnClosed(ConnId conn);

    size_t RoomCount() const { return rooms_.size(); }

private:
    struct Room {
        std::string password, game, build;
        std::map<uint8_t, ConnId> members;     // id -> connection
        std::map<uint8_t, Member> info;
    };
    struct Conn {
        std::string roomKey;
        uint8_t id = 0;
    };

    void Fail(ConnId conn, ErrorCode code);

    SendFn send_;
    CloseFn close_;
    std::map<std::string, Room> rooms_;
    std::map<ConnId, Conn> conns_;   // only connections that are in a room
};

} // namespace netplay
