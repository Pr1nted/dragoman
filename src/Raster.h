/* The province raster, and the things only the raster knows.
 *
 * Both games paint the same picture -- one province id per pixel -- and
 * disagree only on how a pixel spells the id. Open Doctrines packs it
 * big-endian (id 1 is #000001, so the colour *is* the id read as hex); GD5
 * packs it little-endian (id 1 is the pixel (1, 0, 0), and its province is
 * keyed by that triple as a string). Verified against every province of the
 * shipped maps on both sides: 1248 of Open Doctrines' and 2523 of GD5's, with
 * no exception. So the conversion between the two rasters is an exact swap of
 * the red and blue channels, and province identity survives it untouched.
 *
 * The rest of this header exists because the two games store different
 * *derived* facts. GD5 records each province's neighbours and centre and will
 * not run without them; Open Doctrines stores neither and recomputes them
 * from the raster at load. Going to GD5 therefore means deriving both, which
 * is what computeAdjacency() and computeCenters() are for.
 */
#ifndef DRAGOMAN_RASTER_H
#define DRAGOMAN_RASTER_H

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace dragoman {

struct Image {
    int                  width = 0;
    int                  height = 0;
    int                  channels = 0;  /* as decoded: 3 or 4 */
    std::vector<uint8_t> rgba;          /* always expanded to 4 bytes a pixel */

    bool empty() const { return width <= 0 || height <= 0; }
};

bool decodePng(const std::vector<uint8_t>& bytes, Image& out);

/* RGBA in, PNG out. The alpha channel is dropped when every pixel is opaque:
 * it carries nothing then, and on a map layer it is a byte a pixel -- 33 MB
 * of needless deflate input on an 8192x4096 world. This mirrors what Open
 * Doctrines' own packer does, so a layer we write is the size its packer
 * would have written. */
std::vector<uint8_t> encodePng(const Image& img);
std::vector<uint8_t> encodePngGray(const std::vector<uint8_t>& gray, int w, int h);

/* ------------------------------------------------------------------ rasters */

/* Open Doctrines' provinces.png: id = (R << 16) | (G << 8) | B. */
std::vector<uint32_t> rasterFromOd(const Image& img);
Image                 rasterToOd(const std::vector<uint32_t>& ids, int w, int h);

/* GD5's id_map.png: id = R | (G << 8) | (B << 16). */
std::vector<uint32_t> rasterFromGd5(const Image& img);
Image                 rasterToGd5(const std::vector<uint32_t>& ids, int w, int h);

/* Open Doctrines' land_sea.png answers one question per pixel and its loader
 * asks it as "red channel over 128", recolouring land to 200 and sea to 40.
 * Writing those two exact values keeps a layer we generate indistinguishable
 * from one the game's own editor saved. */
Image landSeaImage(const std::vector<uint32_t>& ids, int w, int h,
                   const std::set<uint32_t>& sea_ids);
std::set<uint32_t> seaIdsFromLandSea(const Image& land_sea,
                                     const std::vector<uint32_t>& ids);

/* A flat political layer, province id -> owner colour. GD5 ships one per map
 * and its map screen reads it; Open Doctrines treats the same layer as derived
 * and rebuilds it at load, which is why it is generated here rather than
 * carried. */
Image politicalImage(const std::vector<uint32_t>& ids, int w, int h,
                     const std::map<uint32_t, uint32_t>& owner_color, uint32_t fallback);

/* --------------------------------------------------------------- derivation */

/* Which provinces touch which, by four-connected scan of the raster. `wrap_x`
 * joins the left and right edges, which is what GD5's `loop_map` means and
 * what Open Doctrines' equirectangular world implies. Runs in one pass over
 * the pixels; on the 8192x4096 world map that is 33M comparisons, well under a
 * second, and it is done once per conversion. */
std::map<uint32_t, std::set<uint32_t>> computeAdjacency(
    const std::vector<uint32_t>& ids, int w, int h, bool wrap_x);

/* A representative interior point per province. The mean of a province's
 * pixels is the obvious answer and the wrong one for anything crescent-shaped
 * -- the centroid of Chile or of an archipelago falls in the sea, and GD5
 * draws a unit counter there. So the mean is computed and then, when it does
 * not land inside the province, replaced by the province pixel nearest to it.
 * Deterministic either way: same raster in, same centres out. */
std::map<uint32_t, std::pair<int, int>> computeCenters(
    const std::vector<uint32_t>& ids, int w, int h);

/* Give every unassigned pixel the province nearest to it.
 *
 * The two games mean different things by an id of zero. Open Doctrines means
 * water: 67% of its 1914 raster is zero and its land mask agrees to within a
 * rounding error, so province coverage and landmass are the same thing. GD5
 * means "not painted yet": its map painter samples every third pixel, so the
 * border between any two provinces is left unassigned, and 12% of its world
 * map and 37% of its 1914 scenario is border.
 *
 * Carried across unchanged, those borders become water -- a sea channel three
 * pixels wide along every provincial boundary in Europe. So they are filled
 * from the nearest painted province before an Open Doctrines map is written,
 * by a breadth-first expansion from every painted pixel at once, which reaches
 * each gap from its true nearest neighbour rather than from whichever it was
 * scanned from first. `gap_mask`, when given, records which pixels were filled
 * so the crossing back can restore them exactly.
 */
std::vector<uint32_t> fillGaps(const std::vector<uint32_t>& ids, int w, int h, bool wrap_x,
                               std::vector<uint8_t>* gap_mask, int max_distance = 8);

/* Provinces with at least one sea neighbour. Both games store the flag, and
 * neither stores it reliably enough to trust over the raster. */
std::set<uint32_t> computeCoastal(const std::map<uint32_t, std::set<uint32_t>>& adj,
                                  const std::set<uint32_t>& sea_ids);

/* Bilinear resample, for the one place a picture has to change size: GD5
 * stores a nation's flag as raw pixels at a fixed 60x40, so an Open Doctrines
 * flag PNG of any size has to be brought to exactly that. */
Image resizeImage(const Image& src, int w, int h);

/* Open Doctrines places ships by latitude and longitude on an equirectangular
 * full-globe projection: lon = x/w*360 - 180, lat = 90 - y/h*180. GD5 places
 * units in a province. These two convert between the pair. */
void        lonLatToPixel(double lon, double lat, int w, int h, int& x, int& y);
void        pixelToLonLat(int x, int y, int w, int h, double& lon, double& lat);
uint32_t    provinceAt(const std::vector<uint32_t>& ids, int w, int h, int x, int y);

}  // namespace dragoman

#endif
