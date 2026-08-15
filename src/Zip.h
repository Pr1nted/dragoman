/* The `.odmap` container: an ordinary zip, read and written through miniz --
 * the same library Open Doctrines itself uses to open one, so an archive we
 * write cannot be one its loader cannot read.
 *
 * Member order and modification times are carried rather than regenerated.
 * They change nothing about how the game loads a map, but a diff of two
 * archives is far more useful when the only entries that moved are the ones
 * whose content did.
 */
#ifndef DRAGOMAN_ZIP_H
#define DRAGOMAN_ZIP_H

#include <cstdint>
#include <string>
#include <vector>

namespace dragoman {

struct ZipEntry {
    std::string          name;
    std::vector<uint8_t> data;
    bool                 is_dir = false;
    int64_t              mtime = 0;
};

struct Zip {
    std::vector<ZipEntry> entries;

    const ZipEntry* find(const std::string& name) const;
    bool            has(const std::string& name) const { return find(name) != nullptr; }
    std::string     text(const std::string& name) const;
    void            put(const std::string& name, std::vector<uint8_t> data);
    void            putText(const std::string& name, const std::string& text);
};

bool readZip(const std::string& path, Zip& out, std::string& error);
bool writeZip(const std::string& path, const Zip& zip, std::string& error);

/* Is this file a zip at all? Checked by magic rather than by extension, so a
 * `.odmap` renamed by a mod manager is still recognised. */
bool looksLikeZip(const std::string& path);

}  // namespace dragoman

#endif
