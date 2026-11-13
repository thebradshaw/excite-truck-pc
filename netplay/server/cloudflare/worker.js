// Excite PC Network Play relay for Cloudflare Workers. The same rooms protocol as the C++ relay
// (netplay/include/netplay/protocol.h, netplay/src/protocol.cpp RelayRooms). Keep them in step.
//
// All rooms live in one Durable Object, so every player reaches the same instance.

const PROTOCOL_VERSION = 1;
const MAX_PLAYERS = 6;
const MAX_NAME = 24;
const MAX_ROOM = 32;
const MAX_MESSAGE = 16 * 1024;
const TO_EVERYONE = 0xff;

const Msg = { Hello: 1, Welcome: 2, Error: 3, Joined: 4, Left: 5, Send: 6, Recv: 7 };
const Err = {
  BadHello: 1, RoomExists: 2, NoRoom: 3, RoomFull: 4, WrongPassword: 5,
  WrongGame: 6, WrongBuild: 7, NameTaken: 8, WrongVersion: 9,
};
const ErrText = {
  1: "The server didn't understand this game. Check the server address.",
  2: "A room with that name is already open. Pick another name, or join it instead.",
  3: "No room with that name is open. Check the spelling, or host it instead.",
  4: "That room is full (6 players).",
  5: "Wrong room password.",
  6: "That room is playing the other game.",
  7: "That room's host has a different version of Excite PC. Everyone needs the same version.",
  8: "Someone in that room already uses that name.",
  9: "The server runs a different Network Play version.",
};

const enc = new TextEncoder();
const dec = new TextDecoder();

class Writer {
  constructor() { this.b = []; }
  u8(v) { this.b.push(v & 0xff); return this; }
  u16(v) { return this.u8(v).u8(v >> 8); }
  u32(v) { return this.u16(v & 0xffff).u16((v >>> 16) & 0xffff); }
  str(s) {
    const bytes = enc.encode(s).slice(0, 0xffff);
    this.u16(bytes.length);
    for (const x of bytes) this.b.push(x);
    return this;
  }
  bytes(arr) { for (const x of arr) this.b.push(x); return this; }
  done() { return new Uint8Array(this.b); }
}

class Reader {
  constructor(u8) { this.p = u8; this.at = 0; this.ok = true; }
  need(k) { if (!this.ok || this.p.length - this.at < k) { this.ok = false; return false; } return true; }
  u8() { return this.need(1) ? this.p[this.at++] : 0; }
  u16() { const lo = this.u8(); return lo | (this.u8() << 8); }
  u32() { const lo = this.u16(); return (lo | (this.u16() << 16)) >>> 0; }
  str() {
    const n = this.u16();
    if (!this.need(n)) return "";
    const s = dec.decode(this.p.subarray(this.at, this.at + n));
    this.at += n;
    return s;
  }
}

function cleanText(text, maxBytes) {
  let s = "";
  for (const ch of text) {
    const c = ch.codePointAt(0);
    if (c < 0x20 || c === 0x7f) continue;
    s += ch;
  }
  s = s.replace(/^ +| +$/g, "");
  while (enc.encode(s).length > maxBytes) s = Array.from(s).slice(0, -1).join("");
  return s;
}

const roomKey = (r) => cleanText(r, MAX_ROOM).toLowerCase();

export class Relay {
  constructor(state, env) {
    this.state = state;
    this.rooms = new Map();   // key -> {password, game, build, members: Map(id -> {ws, name, color})}
    this.conns = new Map();   // ws -> {key, id}
  }

  async fetch(request) {
    if (request.headers.get("Upgrade") !== "websocket") {
      return new Response("Excite PC Network Play relay\n", { headers: { "content-type": "text/plain" } });
    }
    const pair = new WebSocketPair();
    const [client, server] = Object.values(pair);
    server.accept();
    server.addEventListener("message", (e) => this.onMessage(server, e.data));
    server.addEventListener("close", () => this.onClosed(server));
    server.addEventListener("error", () => this.onClosed(server));
    return new Response(null, { status: 101, webSocket: client });
  }

  send(ws, bytes) {
    try { ws.send(bytes); } catch (_) { /* closing */ }
  }

  fail(ws, code) {
    this.send(ws, new Writer().u8(Msg.Error).u8(code).str(ErrText[code] || "Refused.").done());
    try { ws.close(1000, "refused"); } catch (_) {}
  }

  onMessage(ws, data) {
    if (typeof data === "string") return;
    const bytes = new Uint8Array(data);
    if (bytes.length > MAX_MESSAGE + 16) return;
    const r = new Reader(bytes);
    const type = r.u8();
    const conn = this.conns.get(ws);

    if (!conn) {
      if (type !== Msg.Hello) return this.fail(ws, Err.BadHello);
      const h = {
        version: r.u16(), create: r.u8() !== 0, room: r.str(), password: r.str(), name: r.str(),
        color: r.u32(), game: r.str(), build: r.str(),
      };
      if (!r.ok) return this.fail(ws, Err.BadHello);
      if (h.version !== PROTOCOL_VERSION) return this.fail(ws, Err.WrongVersion);
      const key = roomKey(h.room);
      const name = cleanText(h.name, MAX_NAME);
      if (!key || !name) return this.fail(ws, Err.BadHello);

      let room = this.rooms.get(key);
      if (h.create) {
        if (room) return this.fail(ws, Err.RoomExists);
        room = { password: h.password, game: h.game, build: h.build, members: new Map() };
        this.rooms.set(key, room);
      } else {
        if (!room) return this.fail(ws, Err.NoRoom);
        if (room.password !== h.password) return this.fail(ws, Err.WrongPassword);
        if (room.game !== h.game) return this.fail(ws, Err.WrongGame);
        if (room.build !== h.build) return this.fail(ws, Err.WrongBuild);
        if (room.members.size >= MAX_PLAYERS) return this.fail(ws, Err.RoomFull);
        for (const m of room.members.values()) {
          if (roomKey(m.name) === roomKey(name)) return this.fail(ws, Err.NameTaken);
        }
      }
      let id = 0;
      while (room.members.has(id)) id++;

      const welcome = new Writer().u8(Msg.Welcome).u8(id).u8(room.members.size);
      for (const [mid, m] of [...room.members.entries()].sort((a, b) => a[0] - b[0])) {
        welcome.u8(mid).str(m.name).u32(m.color);
      }
      this.send(ws, welcome.done());
      const joined = new Writer().u8(Msg.Joined).u8(id).str(name).u32(h.color).done();
      for (const m of room.members.values()) this.send(m.ws, joined);
      room.members.set(id, { ws, name, color: h.color });
      this.conns.set(ws, { key, id });
      return;
    }

    if (type !== Msg.Send) return;
    const room = this.rooms.get(conn.key);
    if (!room) return;
    const target = r.u8();
    if (!r.ok) return;
    const out = new Writer().u8(Msg.Recv).u8(conn.id).bytes(bytes.subarray(2)).done();
    if (target === TO_EVERYONE) {
      for (const [id, m] of room.members) if (id !== conn.id) this.send(m.ws, out);
    } else if (target !== conn.id && room.members.has(target)) {
      this.send(room.members.get(target).ws, out);
    }
  }

  onClosed(ws) {
    const conn = this.conns.get(ws);
    if (!conn) return;
    this.conns.delete(ws);
    const room = this.rooms.get(conn.key);
    if (!room) return;
    room.members.delete(conn.id);
    if (room.members.size === 0) {
      this.rooms.delete(conn.key);
      return;
    }
    const left = new Writer().u8(Msg.Left).u8(conn.id).done();
    for (const m of room.members.values()) this.send(m.ws, left);
  }
}

export default {
  async fetch(request, env) {
    const id = env.RELAY.idFromName("all-rooms");
    return env.RELAY.get(id).fetch(request);
  },
};
