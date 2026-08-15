#include "Raster.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

/* Declarations only. The implementations are compiled in
 * third_party/stb/stb_impl.c, so that this project's own files can be built
 * with warnings as errors. */
#define STBI_NO_STDIO
#include "stb_image.h"

#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

namespace dragoman {

bool decodePng(const std::vector<uint8_t>& bytes, Image& out) {
    if (bytes.empty()) return false;
    int w = 0, h = 0, comp = 0;
    /* Forced to 4 channels: an indexed PNG (which is how Open Doctrines packs
     * land_sea) and a truecolour one then look the same to everything above,
     * and the palette is expanded exactly as the game's own stb_image does. */
    unsigned char* data = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                                &w, &h, &comp, 4);
    if (!data) return false;
    out.width = w;
    out.height = h;
    out.channels = comp;
    out.rgba.assign(data, data + static_cast<size_t>(w) * h * 4);
    stbi_image_free(data);
    return true;
}

static void pngWriteCallback(void* ctx, void* data, int size) {
    auto* out = static_cast<std::vector<uint8_t>*>(ctx);
    const auto* p = static_cast<const uint8_t*>(data);
    out->insert(out->end(), p, p + size);
}

std::vector<uint8_t> encodePng(const Image& img) {
    std::vector<uint8_t> out;
    if (img.empty()) return out;

    bool opaque = true;
    for (size_t i = 3; i < img.rgba.size(); i += 4) {
        if (img.rgba[i] != 255) { opaque = false; break; }
    }

    if (opaque) {
        std::vector<uint8_t> rgb(static_cast<size_t>(img.width) * img.height * 3);
        for (size_t i = 0, j = 0; j < rgb.size(); i += 4, j += 3) {
            rgb[j] = img.rgba[i];
            rgb[j + 1] = img.rgba[i + 1];
            rgb[j + 2] = img.rgba[i + 2];
        }
        stbi_write_png_to_func(pngWriteCallback, &out, img.width, img.height, 3,
                               rgb.data(), img.width * 3);
    } else {
        stbi_write_png_to_func(pngWriteCallback, &out, img.width, img.height, 4,
                               img.rgba.data(), img.width * 4);
    }
    return out;
}

std::vector<uint8_t> encodePngGray(const std::vector<uint8_t>& gray, int w, int h) {
    std::vector<uint8_t> out;
    stbi_write_png_to_func(pngWriteCallback, &out, w, h, 1, gray.data(), w);
    return out;
}

/* ------------------------------------------------------------------ rasters */

std::vector<uint32_t> rasterFromOd(const Image& img) {
    std::vector<uint32_t> ids(static_cast<size_t>(img.width) * img.height, 0);
    for (size_t i = 0, p = 0; p < ids.size(); ++p, i += 4) {
        ids[p] = (uint32_t(img.rgba[i]) << 16) | (uint32_t(img.rgba[i + 1]) << 8)
                 | uint32_t(img.rgba[i + 2]);
    }
    return ids;
}

Image rasterToOd(const std::vector<uint32_t>& ids, int w, int h) {
    Image img;
    img.width = w;
    img.height = h;
    img.channels = 3;
    img.rgba.resize(ids.size() * 4);
    for (size_t p = 0, i = 0; p < ids.size(); ++p, i += 4) {
        const uint32_t v = ids[p];
        img.rgba[i] = uint8_t((v >> 16) & 0xff);
        img.rgba[i + 1] = uint8_t((v >> 8) & 0xff);
        img.rgba[i + 2] = uint8_t(v & 0xff);
        img.rgba[i + 3] = 255;
    }
    return img;
}

std::vector<uint32_t> rasterFromGd5(const Image& img) {
    std::vector<uint32_t> ids(static_cast<size_t>(img.width) * img.height, 0);
    for (size_t i = 0, p = 0; p < ids.size(); ++p, i += 4) {
        ids[p] = uint32_t(img.rgba[i]) | (uint32_t(img.rgba[i + 1]) << 8)
                 | (uint32_t(img.rgba[i + 2]) << 16);
    }
    return ids;
}

Image rasterToGd5(const std::vector<uint32_t>& ids, int w, int h) {
    Image img;
    img.width = w;
    img.height = h;
    img.channels = 3;
    img.rgba.resize(ids.size() * 4);
    for (size_t p = 0, i = 0; p < ids.size(); ++p, i += 4) {
        const uint32_t v = ids[p];
        img.rgba[i] = uint8_t(v & 0xff);
        img.rgba[i + 1] = uint8_t((v >> 8) & 0xff);
        img.rgba[i + 2] = uint8_t((v >> 16) & 0xff);
        img.rgba[i + 3] = 255;
    }
    return img;
}

Image landSeaImage(const std::vector<uint32_t>& ids, int w, int h,
                   const std::set<uint32_t>& sea_ids) {
    Image img;
    img.width = w;
    img.height = h;
    img.channels = 3;
    img.rgba.resize(ids.size() * 4);
    for (size_t p = 0, i = 0; p < ids.size(); ++p, i += 4) {
        const bool land = ids[p] != 0 && sea_ids.find(ids[p]) == sea_ids.end();
        const uint8_t v = land ? 200 : 40;
        img.rgba[i] = img.rgba[i + 1] = img.rgba[i + 2] = v;
        img.rgba[i + 3] = 255;
    }
    return img;
}

std::set<uint32_t> seaIdsFromLandSea(const Image& land_sea,
                                     const std::vector<uint32_t>& ids) {
    /* A province is sea when its pixels are, and a coastline is drawn at
     * pixel resolution in one layer and province resolution in the other, so
     * the two disagree along every shore. Deciding by majority rather than by
     * first pixel keeps a province whose border pixels bleed into the sea on
     * the land side, where its owner put it. */
    std::map<uint32_t, std::pair<long, long>> tally;  /* id -> (land, total) */
    const size_t n = std::min(ids.size(), land_sea.rgba.size() / 4);
    for (size_t p = 0; p < n; ++p) {
        if (ids[p] == 0) continue;
        auto& t = tally[ids[p]];
        if (land_sea.rgba[p * 4] > 128) ++t.first;
        ++t.second;
    }
    std::set<uint32_t> sea;
    for (const auto& kv : tally) {
        if (kv.second.first * 2 <= kv.second.second) sea.insert(kv.first);
    }
    return sea;
}

Image politicalImage(const std::vector<uint32_t>& ids, int w, int h,
                     const std::map<uint32_t, uint32_t>& owner_color) {
    Image img;
    img.width = w;
    img.height = h;
    img.channels = 3;
    img.rgba.resize(ids.size() * 4);
    for (size_t p = 0, i = 0; p < ids.size(); ++p, i += 4) {
        uint32_t rgb = 0x0a1428;  /* the shade both games use for open water */
        const auto it = owner_color.find(ids[p]);
        if (it != owner_color.end()) rgb = it->second;
        img.rgba[i] = uint8_t((rgb >> 16) & 0xff);
        img.rgba[i + 1] = uint8_t((rgb >> 8) & 0xff);
        img.rgba[i + 2] = uint8_t(rgb & 0xff);
        img.rgba[i + 3] = 255;
    }
    return img;
}

/* --------------------------------------------------------------- derivation */

std::map<uint32_t, std::set<uint32_t>> computeAdjacency(
    const std::vector<uint32_t>& ids, int w, int h, bool wrap_x) {
    std::map<uint32_t, std::set<uint32_t>> adj;
    if (w <= 0 || h <= 0) return adj;

    auto link = [&adj](uint32_t a, uint32_t b) {
        if (a == 0 || b == 0 || a == b) return;
        adj[a].insert(b);
        adj[b].insert(a);
    };

    for (int y = 0; y < h; ++y) {
        const size_t row = static_cast<size_t>(y) * w;
        for (int x = 0; x < w; ++x) {
            const uint32_t here = ids[row + x];
            if (here == 0) continue;
            if (x + 1 < w) link(here, ids[row + x + 1]);
            if (y + 1 < h) link(here, ids[row + w + x]);
        }
        /* The map is a cylinder: the province at longitude 180 borders the one
         * at -180. Open Doctrines wraps because its projection is the whole
         * globe; GD5 wraps when its `loop_map` says so. */
        if (wrap_x && w > 1) link(ids[row], ids[row + w - 1]);
    }
    return adj;
}

std::map<uint32_t, std::pair<int, int>> computeCenters(
    const std::vector<uint32_t>& ids, int w, int h) {
    struct Acc { double sx = 0, sy = 0; long n = 0; };
    std::map<uint32_t, Acc> acc;

    for (int y = 0; y < h; ++y) {
        const size_t row = static_cast<size_t>(y) * w;
        for (int x = 0; x < w; ++x) {
            const uint32_t id = ids[row + x];
            if (id == 0) continue;
            Acc& a = acc[id];
            a.sx += x;
            a.sy += y;
            ++a.n;
        }
    }

    std::map<uint32_t, std::pair<int, int>> mean;
    for (const auto& kv : acc) {
        if (kv.second.n == 0) continue;
        mean[kv.first] = {static_cast<int>(std::lround(kv.second.sx / kv.second.n)),
                          static_cast<int>(std::lround(kv.second.sy / kv.second.n))};
    }

    /* Which of those means actually landed on their own province. The ones
     * that did not need the nearest pixel that belongs to it, which is one
     * more pass over the raster rather than a search per province. */
    std::map<uint32_t, bool> outside;
    for (const auto& kv : mean) {
        const int x = kv.second.first, y = kv.second.second;
        const bool inside = x >= 0 && x < w && y >= 0 && y < h
                            && ids[static_cast<size_t>(y) * w + x] == kv.first;
        if (!inside) outside[kv.first] = true;
    }
    if (outside.empty()) return mean;

    std::map<uint32_t, double> best;
    std::map<uint32_t, std::pair<int, int>> bestpx;
    for (int y = 0; y < h; ++y) {
        const size_t row = static_cast<size_t>(y) * w;
        for (int x = 0; x < w; ++x) {
            const uint32_t id = ids[row + x];
            if (id == 0 || !outside.count(id)) continue;
            const auto& m = mean[id];
            const double dx = double(x) - m.first, dy = double(y) - m.second;
            const double d = dx * dx + dy * dy;
            auto it = best.find(id);
            if (it == best.end() || d < it->second) {
                best[id] = d;
                bestpx[id] = {x, y};
            }
        }
    }
    for (const auto& kv : bestpx) mean[kv.first] = kv.second;
    return mean;
}

std::set<uint32_t> computeCoastal(const std::map<uint32_t, std::set<uint32_t>>& adj,
                                  const std::set<uint32_t>& sea_ids) {
    std::set<uint32_t> coastal;
    for (const auto& kv : adj) {
        if (sea_ids.count(kv.first)) continue;
        for (uint32_t n : kv.second) {
            if (sea_ids.count(n)) { coastal.insert(kv.first); break; }
        }
    }
    return coastal;
}

void lonLatToPixel(double lon, double lat, int w, int h, int& x, int& y) {
    x = static_cast<int>(std::lround((lon + 180.0) / 360.0 * w));
    y = static_cast<int>(std::lround((90.0 - lat) / 180.0 * h));
    if (x < 0) x = 0;
    if (x >= w) x = w - 1;
    if (y < 0) y = 0;
    if (y >= h) y = h - 1;
}

void pixelToLonLat(int x, int y, int w, int h, double& lon, double& lat) {
    lon = (static_cast<double>(x) / w) * 360.0 - 180.0;
    lat = 90.0 - (static_cast<double>(y) / h) * 180.0;
}

uint32_t provinceAt(const std::vector<uint32_t>& ids, int w, int h, int x, int y) {
    if (x < 0 || x >= w || y < 0 || y >= h) return 0;
    const size_t i = static_cast<size_t>(y) * w + x;
    return i < ids.size() ? ids[i] : 0;
}

}  // namespace dragoman
