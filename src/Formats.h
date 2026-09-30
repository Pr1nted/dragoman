/* One reader and one writer per game, both against the interchange model. */
#ifndef DRAGOMAN_FORMATS_H
#define DRAGOMAN_FORMATS_H

#include <string>

#include "Model.h"
#include "Support.h"
#include "Zip.h"

namespace dragoman {

struct Options {
    bool carry_sidecar    = true;
    bool derive_geometry  = true;
    bool translate_scripts = true;
    bool strict           = false;
    bool reencode_images  = false;
    bool synthesise_ocean = true;
};

/* ----------------------------------------------------------------- research
 *
 * Open Doctrines names what a country has researched; GD5 gives each of its
 * technologies a level. Only the numbered ladders correspond -- see
 * Research.cpp for what is translated and, more importantly, what is not.
 * Both need GD5's tech tree, because a GD5 level means nothing without the
 * ceiling it is measured against.
 */
std::vector<std::string> researchNodesFromGd5(const Json& research, const Json& tech_tree);
Json researchGd5FromNodes(const std::vector<std::string>& nodes, const Json& tech_tree);

/* GD5's tech tree, from the installation beside a map directory, or an empty
 * object when there is none to read. */
Json readTechTree(const std::string& gd5_map_dir);

/* Where the synthesised sea provinces are recorded, so the crossing back can
 * delete exactly the ones this library invented and nothing else. */
extern const char* kSyntheticOceanKey;

/* Does this map draw its water as provinces?
 *
 * The single question both writers turn on, and they must answer it the same
 * way or one will fill in borders the other has just invented an ocean over.
 * Decided by comparing how much of the map is covered by sea provinces against
 * how much is left unpainted, because neither format states it outright.
 *
 * The two populations are not close. Greater Diplomacy 5's maps run from 0.35
 * to 7.8 on that ratio; Open Doctrines' shipped maps all sit at 0.0001 -- a
 * handful of coastal provinces whose pixels happen to fall mostly under the
 * land/sea mask, some three thousand pixels against twenty-two million blank.
 * Anything between those is a map this library has not seen.
 */
bool waterIsProvinced(const std::vector<uint32_t>& raster,
                      const std::vector<Province>& provinces);

bool readOdMap(const std::string& path, const Options& opt, World& world, Report& report);
bool writeOdMap(const std::string& path, const World& world, const Options& opt, Report& report);

bool readGd5Map(const std::string& path, const Options& opt, World& world, Report& report);
bool writeGd5Map(const std::string& path, const World& world, const Options& opt, Report& report);

/* Unciv. A resampling rather than a field mapping -- see UncivMap.cpp. */
bool readUncivMap(const std::string& path, const Options& opt, World& world, Report& report);
bool writeUncivMap(const std::string& path, const World& world, const Options& opt, Report& report);

/* Which game a path holds, decided by content. */
int detectFormat(const std::string& path);

/* ------------------------------------------------------------- nation keys
 *
 * The two games identify a nation differently: Open Doctrines by ISO 3166
 * alpha-3, GD5 by the display name that is also its key. One direction is
 * free -- Open Doctrines already carries the display name GD5 wants -- and
 * the other has to invent a code. It is invented once, recorded in the
 * sidecar, and reused from there on every later conversion, so a nation does
 * not acquire a second code the second time a map crosses.
 */
std::string isoForName(const std::string& display_name);
std::string synthesiseIso(const std::string& display_name,
                          const std::vector<std::string>& taken);

/* Open Doctrines writes "July 1914 AD"; GD5 keeps day, month and year. */
bool        parseOdDate(const std::string& text, Date& out);
std::string formatOdDate(const Date& d);

/* Terrain vocabularies. GD5 names a terrain per province ("coastal_sea",
 * "hills", "forest"); Open Doctrines stores only land or sea in a raster and
 * infers the rest. Tokens that have no partner survive in the sidecar. */
bool        isSeaTerrain(const std::string& gd5_terrain);
std::string defaultTerrainFor(bool is_sea, bool is_coastal);

/* ------------------------------------------------------------------ sidecar
 *
 * Everything the destination game has no field for, written beside the map it
 * cannot hold it in. Both loaders read a fixed list of names and ignore
 * anything else in the archive or the folder, which is what makes this safe:
 * a GD5 map carrying Open Doctrines' ethnic minorities loads in GD5 exactly
 * as it would without them.
 */
extern const char* kSidecarDir;    /* inside a GD5 map directory */
extern const char* kSidecarMember; /* inside a .odmap archive    */

/* ------------------------------------------------------------------ scripts
 *
 * Each direction is a separate translation, because the two systems overlap
 * only partly -- see Scripts.cpp for what crosses and what is reported.
 */
void scriptsToEvents(const std::vector<ScriptSource>& scripts, const std::string& default_owner,
                     std::vector<Event>& out, Report& report);
void eventsToScripts(const std::vector<Event>& events, std::vector<ScriptSource>& out,
                     Report& report);
void eventsToGd5(const std::vector<Event>& events,
                 const std::map<std::string, std::string>& key_to_name,
                 Json& nation_data, Report& report);
void eventsFromGd5(const Json& raw, const std::string& owner, std::vector<Event>& out,
                   Report& report);

void writeSidecarInto(Zip& zip, const World& world,
                      const std::map<std::string, std::vector<uint8_t>>& extra_blobs = {});
void writeSidecarInto(const std::string& dir, const World& world,
                      const Json& extra_data = Json::object());
/* The name -> ISO pairings a previous crossing settled on, read on their own
 * and before anything else. The GD5 reader needs them while it is still
 * deciding what to call each nation: by the time the whole sidecar is applied
 * it has already resolved every province owner and every relation, and
 * correcting the codes afterwards leaves all of those pointing at the codes it
 * guessed. That is how a map came home with Czechoslovakia renamed from CZE to
 * CSK and its province owners left behind. */
std::map<std::string, std::string> readSidecarNationKeys(const std::string& dir);

void readSidecarFrom(const Zip& zip, World& world);
void readSidecarFrom(const std::string& dir, World& world);

/* Put back the fields the format just read has no way to hold, from the model
 * the sidecar recorded on the way out. Called by both readers, after the
 * sidecar. Only fields the current format cannot express are touched, so a
 * change made in the destination game always wins over what was carried. */
void restoreUnrepresentable(World& world, Report& report);

}  // namespace dragoman

#endif
