/* Dragoman for C++ callers: the same ABI, with the frees done for you.
 *
 * Header-only and a thin wrapper by design. It owns handles and turns
 * failures into exceptions; it does not add behaviour, so anything it can do
 * a C caller can do too. Open Doctrines itself links the static library and
 * uses this.
 *
 *     dragoman::Options opts;
 *     auto report = dragoman::convert("1914.odmap", "base_maps/1914",
 *                                     dragoman::Format::Gd5, opts);
 *     for (const auto& d : report.warnings()) std::cerr << d.message << "\n";
 */
#ifndef DRAGOMAN_HPP
#define DRAGOMAN_HPP

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <dragoman/dragoman.h>

namespace dragoman {

/* Everything below is in an inline namespace, so a caller still writes
 * dragoman::World and dragoman::Options while the symbols mangle as
 * dragoman::api::World and dragoman::api::Options.
 *
 * That is not decoration. The library's own implementation uses this same
 * namespace and already has types called Options, Report, Diagnostic and
 * World -- different types, same names -- and the static archive exports them:
 * dragoman::Report::add, dragoman::Report::info and the rest are all in there.
 * A consumer that included this header and linked the static library therefore
 * had two definitions of dragoman::Report in one program, whose implicit copy
 * constructors and destructors mangle identically and have different layouts.
 * The linker picks one. The result was a segfault on destruction, with the
 * map's name sitting where the world handle should have been.
 *
 * It only bit the static library, because the shared one is built with hidden
 * visibility and exports nothing but the C ABI -- which is why the Python
 * binding and the command line tool were never affected, and why this went
 * unnoticed.
 */
inline namespace api {

enum class Format { Unknown = DG_FORMAT_UNKNOWN, Odmap = DG_FORMAT_ODMAP, Gd5 = DG_FORMAT_GD5 };
enum class Severity { Info = DG_INFO, Warning = DG_WARNING, Error = DG_ERROR };

class Error : public std::runtime_error {
public:
    explicit Error(const std::string& what) : std::runtime_error(what) {}
};

struct Diagnostic {
    Severity    severity;
    std::string code;
    std::string message;
};

/* A report is copied out of the C handle at construction rather than held,
 * so it stays valid however the caller stores it. */
class Report {
public:
    Report() = default;

    explicit Report(dg_report* handle) {
        if (!handle) return;
        const int n = dg_report_count(handle);
        m_entries.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            m_entries.push_back({static_cast<Severity>(dg_report_severity(handle, i)),
                                 dg_report_code(handle, i), dg_report_message(handle, i)});
        }
        dg_report_free(handle);
    }

    const std::vector<Diagnostic>& entries() const { return m_entries; }

    std::vector<Diagnostic> warnings() const { return filter(Severity::Warning); }
    std::vector<Diagnostic> errors() const { return filter(Severity::Error); }

    bool ok() const {
        for (const auto& d : m_entries) {
            if (d.severity == Severity::Error) return false;
        }
        return true;
    }

private:
    std::vector<Diagnostic> filter(Severity s) const {
        std::vector<Diagnostic> out;
        for (const auto& d : m_entries) {
            if (d.severity == s) out.push_back(d);
        }
        return out;
    }

    std::vector<Diagnostic> m_entries;
};

struct Options : dg_options {
    Options() { dg_options_defaults(this); }
};

inline std::string version() { return dg_version_string(); }
inline int         abiVersion() { return dg_abi_version(); }
inline Format      detect(const std::string& path) {
    return static_cast<Format>(dg_detect(path.c_str()));
}
inline std::string formatName(Format f) {
    return dg_format_name(static_cast<dg_format>(f));
}

class World {
public:
    World(const std::string& path, Format fmt, const Options& opts, Report* out = nullptr) {
        dg_report* report = nullptr;
        dg_world*  handle = dg_load(path.c_str(), static_cast<dg_format>(fmt), &opts, &report);
        Report taken(report);
        if (out) *out = taken;
        if (!handle) throw Error(dg_last_error());
        m_handle.reset(handle);
    }

    int         provinceCount() const { return dg_world_province_count(m_handle.get()); }
    int         nationCount() const { return dg_world_nation_count(m_handle.get()); }
    int         scriptCount() const { return dg_world_script_count(m_handle.get()); }
    std::string name() const { return dg_world_name(m_handle.get()); }
    Format      origin() const { return static_cast<Format>(dg_world_origin(m_handle.get())); }

    /* The whole interchange model as JSON, for callers that want a field this
     * wrapper has no accessor for. */
    std::string toJson() const {
        char* s = dg_world_to_json(m_handle.get());
        if (!s) throw Error(dg_last_error());
        std::string out(s);
        dg_string_free(s);
        return out;
    }

    Report save(const std::string& path, Format fmt, const Options& opts) const {
        dg_report* report = nullptr;
        const int  rc = dg_save(m_handle.get(), path.c_str(), static_cast<dg_format>(fmt), &opts,
                                &report);
        Report taken(report);
        if (rc != 0) throw Error(dg_last_error());
        return taken;
    }

private:
    struct Deleter {
        void operator()(dg_world* w) const { dg_world_free(w); }
    };
    std::unique_ptr<dg_world, Deleter> m_handle;
};

inline Report convert(const std::string& in, const std::string& out, Format to,
                      const Options& opts = Options()) {
    dg_report* report = nullptr;
    const int  rc = dg_convert(in.c_str(), out.c_str(), static_cast<dg_format>(to), &opts, &report);
    Report taken(report);
    if (rc != 0) throw Error(dg_last_error());
    return taken;
}

/* True when converting to `to` and back preserves every modelled field and
 * the province raster. See docs/roundtrip.md for exactly what that covers. */
inline bool roundtripCheck(const std::string& path, Format to, const Options& opts = Options(),
                           Report* out = nullptr) {
    dg_report* report = nullptr;
    const int  rc = dg_roundtrip_check(path.c_str(), static_cast<dg_format>(to), &opts, &report);
    Report taken(report);
    if (out) *out = taken;
    if (rc < 0) throw Error(dg_last_error());
    return rc == 1;
}

}  // namespace api
}  // namespace dragoman

#endif
