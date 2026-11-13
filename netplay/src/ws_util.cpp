#include "ws_util.h"

#include <cctype>
#include <cstring>
#include <random>

namespace netplay::ws {

namespace {

struct Sha1 {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};

    static uint32_t Rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

    void Block(const uint8_t* p) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) w[i] = uint32_t(p[i * 4]) << 24 | uint32_t(p[i * 4 + 1]) << 16 | uint32_t(p[i * 4 + 2]) << 8 | p[i * 4 + 3];
        for (int i = 16; i < 80; ++i) w[i] = Rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            const uint32_t t = Rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = Rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }

    void Digest(const uint8_t* data, size_t size, uint8_t out[20]) {
        std::vector<uint8_t> m(data, data + size);
        const uint64_t bits = uint64_t(size) * 8;
        m.push_back(0x80);
        while (m.size() % 64 != 56) m.push_back(0);
        for (int i = 7; i >= 0; --i) m.push_back(uint8_t(bits >> (i * 8)));
        for (size_t i = 0; i < m.size(); i += 64) Block(&m[i]);
        for (int i = 0; i < 5; ++i) {
            out[i * 4] = uint8_t(h[i] >> 24); out[i * 4 + 1] = uint8_t(h[i] >> 16);
            out[i * 4 + 2] = uint8_t(h[i] >> 8); out[i * 4 + 3] = uint8_t(h[i]);
        }
    }
};

} // namespace

std::string Base64(const uint8_t* data, size_t size) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < size; i += 3) {
        uint32_t v = uint32_t(data[i]) << 16;
        if (i + 1 < size) v |= uint32_t(data[i + 1]) << 8;
        if (i + 2 < size) v |= data[i + 2];
        out.push_back(t[(v >> 18) & 63]);
        out.push_back(t[(v >> 12) & 63]);
        out.push_back(i + 1 < size ? t[(v >> 6) & 63] : '=');
        out.push_back(i + 2 < size ? t[v & 63] : '=');
    }
    return out;
}

std::string AcceptKey(std::string_view key) {
    std::string s(key);
    s += "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    uint8_t d[20];
    Sha1().Digest(reinterpret_cast<const uint8_t*>(s.data()), s.size(), d);
    return Base64(d, 20);
}

std::string RandomKey() {
    std::random_device rd;
    uint8_t k[16];
    for (auto& b : k) b = uint8_t(rd());
    return Base64(k, 16);
}

std::vector<uint8_t> Frame(uint8_t opcode, const uint8_t* payload, size_t size, bool mask) {
    std::vector<uint8_t> f;
    f.reserve(size + 14);
    f.push_back(uint8_t(0x80 | opcode));
    const uint8_t m = mask ? 0x80 : 0;
    if (size < 126) {
        f.push_back(uint8_t(m | size));
    } else if (size <= 0xFFFF) {
        f.push_back(uint8_t(m | 126));
        f.push_back(uint8_t(size >> 8));
        f.push_back(uint8_t(size));
    } else {
        f.push_back(uint8_t(m | 127));
        for (int i = 7; i >= 0; --i) f.push_back(uint8_t(uint64_t(size) >> (i * 8)));
    }
    uint8_t key[4] = {0, 0, 0, 0};
    if (mask) {
        static thread_local std::mt19937 rng{std::random_device{}()};
        const uint32_t r = rng();
        std::memcpy(key, &r, 4);
        f.insert(f.end(), key, key + 4);
    }
    const size_t at = f.size();
    f.insert(f.end(), payload, payload + size);
    if (mask) {
        for (size_t i = 0; i < size; ++i) f[at + i] ^= key[i & 3];
    }
    return f;
}

int Parser::Next(Message& out) {
    for (;;) {
        if (buf_.size() < 2) return 0;
        const uint8_t b0 = buf_[0], b1 = buf_[1];
        const bool fin = b0 & 0x80;
        const uint8_t op = b0 & 0x0F;
        const bool masked = b1 & 0x80;
        uint64_t len = b1 & 0x7F;
        size_t at = 2;
        if (len == 126) {
            if (buf_.size() < 4) return 0;
            len = uint64_t(buf_[2]) << 8 | buf_[3];
            at = 4;
        } else if (len == 127) {
            if (buf_.size() < 10) return 0;
            len = 0;
            for (int i = 0; i < 8; ++i) len = len << 8 | buf_[2 + i];
            at = 10;
        }
        if (len > max_) return -1;
        uint8_t key[4] = {0, 0, 0, 0};
        if (masked) {
            if (buf_.size() < at + 4) return 0;
            std::memcpy(key, &buf_[at], 4);
            at += 4;
        }
        if (buf_.size() < at + len) return 0;
        std::vector<uint8_t> payload(buf_.begin() + at, buf_.begin() + at + size_t(len));
        if (masked) {
            for (size_t i = 0; i < payload.size(); ++i) payload[i] ^= key[i & 3];
        }
        buf_.erase(buf_.begin(), buf_.begin() + at + size_t(len));

        if (op >= 8) {   // control frames are never fragmented
            out.opcode = op;
            out.data = std::move(payload);
            return 1;
        }
        if (op != Continuation) {
            partial_.clear();
            partialOp_ = op;
        }
        partial_.insert(partial_.end(), payload.begin(), payload.end());
        if (partial_.size() > max_) return -1;
        if (fin) {
            out.opcode = partialOp_;
            out.data = std::move(partial_);
            partial_.clear();
            return 1;
        }
    }
}

std::string Header(std::string_view head, std::string_view name) {
    size_t pos = 0;
    while (pos < head.size()) {
        size_t end = head.find("\r\n", pos);
        if (end == std::string_view::npos) end = head.size();
        const std::string_view line = head.substr(pos, end - pos);
        const size_t colon = line.find(':');
        if (colon == name.size()) {
            bool same = true;
            for (size_t i = 0; i < colon && same; ++i) {
                same = std::tolower(uint8_t(line[i])) == std::tolower(uint8_t(name[i]));
            }
            if (same) {
                std::string_view v = line.substr(colon + 1);
                while (!v.empty() && v.front() == ' ') v.remove_prefix(1);
                while (!v.empty() && v.back() == ' ') v.remove_suffix(1);
                return std::string(v);
            }
        }
        pos = end + 2;
    }
    return {};
}

} // namespace netplay::ws
