#include "Support.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace dragoman {

namespace fs = std::filesystem;

nlohmann::ordered_json Report::toJson() const {
    nlohmann::ordered_json arr = nlohmann::ordered_json::array();
    for (const auto& d : m_entries) {
        arr.push_back({{"severity", d.severity == 0 ? "info" : d.severity == 1 ? "warning" : "error"},
                       {"code", d.code},
                       {"message", d.message}});
    }
    return arr;
}

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    if (size < 0) return false;
    f.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    if (size > 0) f.read(reinterpret_cast<char*>(out.data()), size);
    return static_cast<bool>(f);
}

bool writeFile(const std::string& path, const void* data, size_t size) {
    const fs::path p(path);
    if (p.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
    }
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    if (size > 0) f.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    return static_cast<bool>(f);
}

bool writeFile(const std::string& path, const std::vector<uint8_t>& data) {
    return writeFile(path, data.data(), data.size());
}

bool writeFile(const std::string& path, const std::string& data) {
    return writeFile(path, data.data(), data.size());
}

bool fileExists(const std::string& path) {
    std::error_code ec;
    return fs::exists(path, ec);
}

bool isDirectory(const std::string& path) {
    std::error_code ec;
    return fs::is_directory(path, ec);
}

bool makeDirectories(const std::string& path) {
    std::error_code ec;
    fs::create_directories(path, ec);
    return !ec || fs::is_directory(path, ec);
}

std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    return (fs::path(a) / b).string();
}

std::string toString(const std::vector<uint8_t>& bytes) {
    return std::string(bytes.begin(), bytes.end());
}

std::vector<uint8_t> toBytes(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '\n') {
            if (!cur.empty() && cur.back() == '\r') cur.pop_back();
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) {
        if (cur.back() == '\r') cur.pop_back();
        out.push_back(cur);
    }
    return out;
}

std::vector<std::string> splitOn(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) { out.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string toUpper(const std::string& s) {
    std::string o = s;
    std::transform(o.begin(), o.end(), o.begin(),
                   [](unsigned char c) { return asciiUpper(c); });
    return o;
}

std::string toLower(const std::string& s) {
    std::string o = s;
    std::transform(o.begin(), o.end(), o.begin(),
                   [](unsigned char c) { return asciiLower(c); });
    return o;
}

bool parseHexColor(const std::string& s, uint32_t& out) {
    std::string h = trim(s);
    if (!h.empty() && h[0] == '#') h = h.substr(1);
    if (h.size() != 6) return false;
    uint32_t v = 0;
    for (char c : h) {
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        v = (v << 4) | static_cast<uint32_t>(d);
    }
    out = v;
    return true;
}

std::string formatHexColor(uint32_t rgb) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%06x", rgb & 0xffffffu);
    return std::string(buf);
}

static const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const std::vector<uint8_t>& data) {
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 2 < data.size()) {
        const uint32_t n = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8) | data[i + 2];
        out += kB64[(n >> 18) & 63];
        out += kB64[(n >> 12) & 63];
        out += kB64[(n >> 6) & 63];
        out += kB64[n & 63];
        i += 3;
    }
    if (i + 1 == data.size()) {
        const uint32_t n = uint32_t(data[i]) << 16;
        out += kB64[(n >> 18) & 63];
        out += kB64[(n >> 12) & 63];
        out += "==";
    } else if (i + 2 == data.size()) {
        const uint32_t n = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8);
        out += kB64[(n >> 18) & 63];
        out += kB64[(n >> 12) & 63];
        out += kB64[(n >> 6) & 63];
        out += '=';
    }
    return out;
}

std::vector<uint8_t> base64Decode(const std::string& text) {
    int8_t table[256];
    for (int i = 0; i < 256; ++i) table[i] = -1;
    for (int i = 0; i < 64; ++i) table[static_cast<unsigned char>(kB64[i])] = static_cast<int8_t>(i);

    std::vector<uint8_t> out;
    uint32_t acc = 0;
    int      bits = 0;
    for (unsigned char c : text) {
        if (c == '=') break;
        const int8_t v = table[c];
        if (v < 0) continue;  /* whitespace and newlines inside the payload */
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((acc >> bits) & 0xff));
        }
    }
    return out;
}

static thread_local std::string g_lastError;

void setLastError(const std::string& message) { g_lastError = message; }
const char* lastError() { return g_lastError.c_str(); }

namespace {
double clampTo(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
int roundHalfAway(double v) { return static_cast<int>(v < 0 ? v - 0.5 : v + 0.5); }
}  // namespace

int gd5PoliticalValueFromAxis(double axis) {
    return roundHalfAway(clampTo(axis / 10.0, -10.0, 10.0));
}

double axisFromGd5PoliticalValue(double value) {
    return clampTo(value, -10.0, 10.0) * 10.0;
}

int odAuthFromAxis(double axis) {
    return roundHalfAway(clampTo(axis, -100.0, 100.0));
}

}  // namespace dragoman
