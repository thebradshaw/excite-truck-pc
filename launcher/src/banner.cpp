#include "banner.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>

// opening.bnr is an IMET header followed by a U8 archive. Its meta/banner.bin is an IMD5 header,
// then "LZ77" and LZ77 (type 0x10) data, which unpacks to another U8 archive holding the TPL
// textures under arc/timg/.

namespace {

using Bytes = std::vector<unsigned char>;

uint32_t Be32(const Bytes& d, size_t o) {
    return o + 4 > d.size() ? 0 : uint32_t(d[o]) << 24 | uint32_t(d[o + 1]) << 16 | uint32_t(d[o + 2]) << 8 | d[o + 3];
}
uint16_t Be16(const Bytes& d, size_t o) { return o + 2 > d.size() ? 0 : uint16_t(d[o] << 8 | d[o + 1]); }

// Finds a file in a U8 archive by its name (without folders); returns its bytes.
bool U8Find(const Bytes& d, const std::string& wanted, Bytes& out) {
    static const unsigned char kMagic[4] = {0x55, 0xAA, 0x38, 0x2D};
    size_t base = 0;
    while (base + 4 <= d.size() && std::memcmp(&d[base], kMagic, 4) != 0) base += 4;
    if (base + 0x20 > d.size()) return false;
    const size_t root = base + Be32(d, base + 4);
    const uint32_t count = Be32(d, root + 8);
    const size_t names = root + size_t(count) * 12;
    if (count == 0 || names > d.size()) return false;
    for (uint32_t i = 1; i < count; ++i) {
        const size_t e = root + size_t(i) * 12;
        if (d[e] != 0) continue;   // folder
        const size_t nameAt = names + (Be32(d, e) & 0xFFFFFF);
        if (nameAt >= d.size()) return false;
        const char* name = reinterpret_cast<const char*>(&d[nameAt]);
        if (wanted != std::string(name, strnlen(name, d.size() - nameAt))) continue;
        const size_t at = base + Be32(d, e + 4), size = Be32(d, e + 8);
        if (at + size > d.size()) return false;
        out.assign(d.begin() + at, d.begin() + at + size);
        return true;
    }
    return false;
}

bool Unpack(const Bytes& in, Bytes& out) {
    size_t i = 0;
    if (in.size() >= 0x20 && std::memcmp(in.data(), "IMD5", 4) == 0) i = 0x20;
    if (i + 8 > in.size() || std::memcmp(&in[i], "LZ77", 4) != 0) {
        out.assign(in.begin() + i, in.end());
        return true;
    }
    i += 4;
    if (in[i] != 0x10) return false;
    const size_t size = in[i + 1] | in[i + 2] << 8 | in[i + 3] << 16;
    i += 4;
    out.clear();
    out.reserve(size);
    while (out.size() < size && i < in.size()) {
        const unsigned char flags = in[i++];
        for (int bit = 0; bit < 8 && out.size() < size; ++bit) {
            if (flags & (0x80 >> bit)) {
                if (i + 2 > in.size()) return false;
                const size_t n = (in[i] >> 4) + 3, back = ((in[i] & 0xF) << 8 | in[i + 1]) + 1;
                i += 2;
                if (back > out.size()) return false;
                for (size_t k = 0; k < n; ++k) out.push_back(out[out.size() - back]);
            } else {
                if (i >= in.size()) return false;
                out.push_back(in[i++]);
            }
        }
    }
    return out.size() == size;
}

// First image of a TPL: RGBA8 (format 6) or RGB5A3 (5) in 4x4 tiles, or the grey formats I4 (0),
// I8 (1), IA4 (2) and IA8 (3), whose tiles are 8x8, 8x4, 8x4 and 4x4.
bool DecodeTpl(const Bytes& d, Image& out) {
    if (Be32(d, 0) != 0x0020AF30 || Be32(d, 4) == 0) return false;
    const size_t header = Be32(d, Be32(d, 8));
    const int h = Be16(d, header), w = Be16(d, header + 2);
    const uint32_t format = Be32(d, header + 4);
    size_t src = Be32(d, header + 8);
    if (w <= 0 || h <= 0 || w > 2048 || h > 2048 || format > 6 || format == 4) return false;
    const int tileW = format == 0 || format == 1 || format == 2 ? 8 : 4;
    const int tileH = format == 0 ? 8 : 4;
    const size_t tileBytes = format == 6 ? 64 : 32;
    const size_t tiles = size_t((w + tileW - 1) / tileW) * size_t((h + tileH - 1) / tileH);
    if (src + tiles * tileBytes > d.size()) return false;
    out.width = w;
    out.height = h;
    out.rgba.assign(size_t(w) * h * 4, 0);
    auto put = [&](int x, int y, unsigned r, unsigned g, unsigned b, unsigned a) {
        if (x >= w || y >= h) return;
        unsigned char* p = &out.rgba[(size_t(y) * w + x) * 4];
        p[0] = (unsigned char)r; p[1] = (unsigned char)g; p[2] = (unsigned char)b; p[3] = (unsigned char)a;
    };
    for (int ty = 0; ty < h; ty += tileH) {
        for (int tx = 0; tx < w; tx += tileW) {
            for (int k = 0; k < tileW * tileH; ++k) {
                const int x = tx + k % tileW, y = ty + k / tileW;
                switch (format) {
                    case 0: {
                        const unsigned v = (k % 2 == 0 ? d[src + k / 2] >> 4 : d[src + k / 2] & 15) * 17;
                        put(x, y, v, v, v, v);
                        break;
                    }
                    case 1: put(x, y, d[src + k], d[src + k], d[src + k], d[src + k]); break;
                    case 2: {
                        const unsigned l = (d[src + k] & 15) * 17;
                        put(x, y, l, l, l, (d[src + k] >> 4) * 17);
                        break;
                    }
                    case 3: put(x, y, d[src + k * 2 + 1], d[src + k * 2 + 1], d[src + k * 2 + 1], d[src + k * 2]); break;
                    case 5: {
                        const unsigned v = d[src + k * 2] << 8 | d[src + k * 2 + 1];
                        if (v & 0x8000) {
                            put(x, y, ((v >> 10) & 31) * 255 / 31, ((v >> 5) & 31) * 255 / 31, (v & 31) * 255 / 31, 255);
                        } else {
                            put(x, y, ((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17, ((v >> 12) & 7) * 255 / 7);
                        }
                        break;
                    }
                    default:   // 6: AR pairs, then GB pairs
                        put(x, y, d[src + k * 2 + 1], d[src + 32 + k * 2], d[src + 32 + k * 2 + 1], d[src + k * 2]);
                        break;
                }
            }
            src += tileBytes;
        }
    }
    return true;
}

} // namespace

bool LoadBannerTexture(const std::filesystem::path& openingBnr, const std::string& name, Image& out) {
    std::ifstream in(openingBnr, std::ios::binary);
    if (!in) return false;
    const Bytes file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    Bytes packed, banner, texture;
    return U8Find(file, "banner.bin", packed) && Unpack(packed, banner) && U8Find(banner, name, texture) &&
           DecodeTpl(texture, out);
}
