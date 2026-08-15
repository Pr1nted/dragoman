#include "Zip.h"

#include <cstring>
#include <fstream>

#include "miniz.h"
#include "miniz_zip.h"

namespace dragoman {

const ZipEntry* Zip::find(const std::string& name) const {
    for (const auto& e : entries) {
        if (!e.is_dir && e.name == name) return &e;
    }
    return nullptr;
}

std::string Zip::text(const std::string& name) const {
    const ZipEntry* e = find(name);
    if (!e) return std::string();
    return std::string(e->data.begin(), e->data.end());
}

void Zip::put(const std::string& name, std::vector<uint8_t> data) {
    for (auto& e : entries) {
        if (!e.is_dir && e.name == name) { e.data = std::move(data); return; }
    }
    ZipEntry e;
    e.name = name;
    e.data = std::move(data);
    entries.push_back(std::move(e));
}

void Zip::putText(const std::string& name, const std::string& text) {
    put(name, std::vector<uint8_t>(text.begin(), text.end()));
}

bool readZip(const std::string& path, Zip& out, std::string& error) {
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, path.c_str(), 0)) {
        error = "not a readable zip archive: " + path;
        return false;
    }

    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    out.entries.clear();
    out.entries.reserve(count);

    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;

        ZipEntry e;
        e.name = st.m_filename;
        e.mtime = static_cast<int64_t>(st.m_time);
        e.is_dir = mz_zip_reader_is_file_a_directory(&zip, i) != 0;

        if (!e.is_dir) {
            size_t size = 0;
            void*  data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
            if (!data) {
                mz_zip_reader_end(&zip);
                error = "could not extract " + e.name + " from " + path;
                return false;
            }
            const auto* p = static_cast<const uint8_t*>(data);
            e.data.assign(p, p + size);
            mz_free(data);
        }
        out.entries.push_back(std::move(e));
    }

    mz_zip_reader_end(&zip);
    return true;
}

bool writeZip(const std::string& path, const Zip& zip, std::string& error) {
    /* Written to a temporary beside the target and moved into place, so an
     * interrupted run leaves the previous map intact rather than a truncated
     * archive the game will try to load. Open Doctrines' own packer takes the
     * same precaution for the same reason. */
    const std::string tmp = path + ".dragoman-tmp";

    mz_zip_archive out;
    std::memset(&out, 0, sizeof(out));
    if (!mz_zip_writer_init_file(&out, tmp.c_str(), 0)) {
        error = "could not open " + tmp + " for writing";
        return false;
    }

    for (const auto& e : zip.entries) {
        MZ_TIME_T t = static_cast<MZ_TIME_T>(e.mtime);
        const bool ok = mz_zip_writer_add_mem_ex_v2(
            &out, e.name.c_str(), e.is_dir ? nullptr : e.data.data(),
            e.is_dir ? 0 : e.data.size(), nullptr, 0,
            e.is_dir ? 0 : MZ_BEST_COMPRESSION, 0, 0,
            e.mtime ? &t : nullptr, nullptr, 0, nullptr, 0);
        if (!ok) {
            mz_zip_writer_end(&out);
            std::remove(tmp.c_str());
            error = "could not add " + e.name + " to the archive";
            return false;
        }
    }

    if (!mz_zip_writer_finalize_archive(&out)) {
        mz_zip_writer_end(&out);
        std::remove(tmp.c_str());
        error = "could not finalise the archive";
        return false;
    }
    mz_zip_writer_end(&out);

    std::remove(path.c_str());
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        error = "could not move the finished archive into place at " + path;
        return false;
    }
    return true;
}

bool looksLikeZip(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char magic[4] = {0, 0, 0, 0};
    f.read(magic, 4);
    if (!f) return false;
    return magic[0] == 'P' && magic[1] == 'K'
           && (magic[2] == 3 || magic[2] == 5 || magic[2] == 7);
}

}  // namespace dragoman
