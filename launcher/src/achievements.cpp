#include "achievements.h"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <sstream>

#include "launcher_config.h"

namespace {

// Just enough JSON for the two achievement files.
struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Json> array;
    std::map<std::string, Json> object;

    const Json* Get(const std::string& key) const {
        const auto it = object.find(key);
        return it == object.end() ? nullptr : &it->second;
    }
};

class Parser {
public:
    explicit Parser(const std::string& text) : s_(text) {}

    bool Parse(Json& out) {
        if (!Value(out, 0)) return false;
        Space();
        return i_ == s_.size();
    }

private:
    const std::string& s_;
    size_t i_ = 0;

    void Space() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_;
    }
    bool Literal(const char* word) {
        const size_t n = std::char_traits<char>::length(word);
        if (s_.compare(i_, n, word) != 0) return false;
        i_ += n;
        return true;
    }
    static void Utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += char(cp);
        } else if (cp < 0x800) {
            out += char(0xC0 | (cp >> 6));
            out += char(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += char(0xE0 | (cp >> 12));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        } else {
            out += char(0xF0 | (cp >> 18));
            out += char(0x80 | ((cp >> 12) & 0x3F));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        }
    }
    bool Hex4(unsigned& cp) {
        if (i_ + 4 > s_.size()) return false;
        cp = unsigned(std::strtoul(s_.substr(i_, 4).c_str(), nullptr, 16));
        i_ += 4;
        return true;
    }
    bool Str(std::string& out) {
        if (i_ >= s_.size() || s_[i_] != '"') return false;
        ++i_;
        while (i_ < s_.size()) {
            const char c = s_[i_++];
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i_ >= s_.size()) return false;
            const char e = s_[i_++];
            switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    unsigned cp = 0;
                    if (!Hex4(cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00 && s_.compare(i_, 2, "\\u") == 0) {
                        i_ += 2;
                        unsigned low = 0;
                        if (!Hex4(low)) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    }
                    Utf8(out, cp);
                    break;
                }
                default: out += e; break;   // \" \\ \/
            }
        }
        return false;
    }
    bool Value(Json& out, int depth) {
        if (depth > 32) return false;
        Space();
        if (i_ >= s_.size()) return false;
        const char c = s_[i_];
        if (c == '{') {
            out.type = Json::Object;
            ++i_;
            Space();
            if (i_ < s_.size() && s_[i_] == '}') { ++i_; return true; }
            for (;;) {
                Space();
                std::string key;
                if (!Str(key)) return false;
                Space();
                if (i_ >= s_.size() || s_[i_++] != ':') return false;
                if (!Value(out.object[key], depth + 1)) return false;
                Space();
                if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
                if (i_ < s_.size() && s_[i_] == '}') { ++i_; return true; }
                return false;
            }
        }
        if (c == '[') {
            out.type = Json::Array;
            ++i_;
            Space();
            if (i_ < s_.size() && s_[i_] == ']') { ++i_; return true; }
            for (;;) {
                out.array.emplace_back();
                if (!Value(out.array.back(), depth + 1)) return false;
                Space();
                if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
                if (i_ < s_.size() && s_[i_] == ']') { ++i_; return true; }
                return false;
            }
        }
        if (c == '"') {
            out.type = Json::String;
            return Str(out.string);
        }
        if (Literal("true")) { out.type = Json::Bool; out.boolean = true; return true; }
        if (Literal("false")) { out.type = Json::Bool; return true; }
        if (Literal("null")) { out.type = Json::Null; return true; }
        const char* start = s_.c_str() + i_;
        char* end = nullptr;
        out.number = std::strtod(start, &end);
        if (end == start) return false;
        out.type = Json::Number;
        i_ += size_t(end - start);
        return true;
    }
};

bool ReadJson(const std::filesystem::path& path, Json& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return Parser(text).Parse(out);
}

std::string StringField(const Json& object, const char* key) {
    const Json* value = object.Get(key);
    return value && value->type == Json::String ? value->string : std::string();
}

} // namespace

std::vector<Achievement> LoadAchievements() {
    std::vector<Achievement> list;
    const auto folder = launcher_config::UserDataDirectory() / "achievements";
    Json definitions;
    if (!ReadJson(folder / "definitions.json", definitions) || definitions.type != Json::Array) {
        return list;
    }
    Json unlocks;
    if (!ReadJson(folder / "unlocks.json", unlocks) || unlocks.type != Json::Object) {
        unlocks = Json();
    }
    for (const Json& entry : definitions.array) {
        if (entry.type != Json::Object) continue;
        Achievement a;
        a.id = StringField(entry, "id");
        if (a.id.empty()) continue;
        a.title = StringField(entry, "title");
        a.description = StringField(entry, "description");
        if (const Json* points = entry.Get("points"); points && points->type == Json::Number) {
            a.points = int(points->number);
        }
        if (const Json* unlock = unlocks.Get(a.id)) {
            a.unlocked = true;
            if (unlock->type == Json::Object) a.unlockedAt = StringField(*unlock, "unlocked_at");
        }
        list.push_back(std::move(a));
    }
    return list;
}
