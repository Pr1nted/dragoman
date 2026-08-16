/* Diagnostics, file bytes and the small string helpers every reader needs. */
#ifndef DRAGOMAN_SUPPORT_H
#define DRAGOMAN_SUPPORT_H

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace dragoman {

/* A translation is rarely all-or-nothing: a map converts, and three of its
 * hundred scripted events name a condition the other game cannot check. The
 * report is how that is said out loud rather than swallowed. Every entry
 * carries a stable `code` as well as a sentence, so a caller can act on
 * "script.unsupported" without matching on English. */
struct Diagnostic {
    int         severity;   /* dg_severity */
    std::string code;
    std::string message;
};

class Report {
public:
    void info(std::string code, std::string message)    { add(0, std::move(code), std::move(message)); }
    void warn(std::string code, std::string message)    { add(1, std::move(code), std::move(message)); }
    void error(std::string code, std::string message)   { add(2, std::move(code), std::move(message)); }

    void add(int severity, std::string code, std::string message) {
        m_entries.push_back({severity, std::move(code), std::move(message)});
        if (severity > m_worst) m_worst = severity;
    }

    const std::vector<Diagnostic>& entries() const { return m_entries; }
    int  worst() const { return m_worst; }
    bool hasErrors() const { return m_worst >= 2; }
    bool hasWarnings() const { return m_worst >= 1; }

    nlohmann::ordered_json toJson() const;

private:
    std::vector<Diagnostic> m_entries;
    int                     m_worst = 0;
};

/* Reading and writing whole files, because every format here is small enough
 * to hold and both of them are archives of complete files anyway. */
bool readFile(const std::string& path, std::vector<uint8_t>& out);
bool writeFile(const std::string& path, const void* data, size_t size);
bool writeFile(const std::string& path, const std::vector<uint8_t>& data);
bool writeFile(const std::string& path, const std::string& data);

bool fileExists(const std::string& path);
bool isDirectory(const std::string& path);
bool makeDirectories(const std::string& path);
std::string joinPath(const std::string& a, const std::string& b);

std::string        toString(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> toBytes(const std::string& s);

std::string trim(const std::string& s);
std::vector<std::string> splitLines(const std::string& s);
std::vector<std::string> splitOn(const std::string& s, char sep);
bool startsWith(const std::string& s, const std::string& prefix);
/* ------------------------------------------------------------------ ascii
 *
 * Character classification that does NOT consult the locale, and the reason
 * is a bug that only appeared when somebody called this library from Python.
 *
 * <cctype>'s isalpha/toupper answer according to the current locale. A C++
 * program never sets one, so they behave as ASCII and every test here passed.
 * Python calls setlocale() at startup -- so under C.UTF-8, isalpha() accepts
 * bytes above 0x7F and toupper() maps them to OTHER bytes above 0x7F. Walking
 * a UTF-8 name byte by byte then produced a three-byte "ISO code" of mangled
 * continuation bytes, and the JSON writer refused it: "invalid UTF-8 byte at
 * index 2". Ten of Greater Diplomacy 5's twelve base maps could not be
 * converted by any Python caller, while the command line tool converted all
 * twelve.
 *
 * Everything these are used for -- ISO 3166 codes, file extensions, hex
 * colours, matching names against a table -- is defined in ASCII. So these
 * are the ASCII rules, spelled out, and no locale can change them. */
inline bool asciiAlpha(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}
inline bool asciiDigit(unsigned char c) { return c >= '0' && c <= '9'; }
inline bool asciiAlnum(unsigned char c) { return asciiAlpha(c) || asciiDigit(c); }
inline char asciiUpper(unsigned char c) {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : static_cast<char>(c);
}
inline char asciiLower(unsigned char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : static_cast<char>(c);
}

bool endsWith(const std::string& s, const std::string& suffix);
std::string toUpper(const std::string& s);
std::string toLower(const std::string& s);

/* "#8bc64b" <-> 0x8bc64b. Open Doctrines writes colours as hex strings and
 * GD5 as three integers, so both writers go through here. */
bool        parseHexColor(const std::string& s, uint32_t& out);
std::string formatHexColor(uint32_t rgb);

/* base64, for GD5's inline flag and portrait images. */
std::string          base64Encode(const std::vector<uint8_t>& data);
std::vector<uint8_t> base64Decode(const std::string& text);

/* The thread-local error the C ABI reports through dg_last_error(). */
void        setLastError(const std::string& message);
const char* lastError();

}  // namespace dragoman

#endif
