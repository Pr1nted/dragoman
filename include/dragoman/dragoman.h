/* Dragoman -- the C ABI.
 *
 * A dragoman was the interpreter attached to an embassy: the person through
 * whom two powers who shared no language could none the less sign something.
 * This library is that, for map files. It reads an Open Doctrines `.odmap`
 * and writes a Greater Diplomacy 5 map directory, and the other way round.
 *
 * Everything crossing this boundary is a plain C type, so a caller in Python,
 * Rust, Go, C# or Java can bind it with the FFI it already has and never see a
 * C++ symbol. The surface is deliberately narrow: load, save, convert, and one
 * escape hatch (`dg_world_to_json`) that hands the caller the whole
 * interchange model as text when it wants to inspect or edit a map rather
 * than merely move one.
 *
 * Threading: no global mutable state except the thread-local last-error
 * buffer, so distinct `dg_world` values may be used from distinct threads.
 * Ownership: every pointer this header hands back is freed by the matching
 * `*_free` below, never by the caller's allocator.
 */
#ifndef DRAGOMAN_H
#define DRAGOMAN_H

#include <stddef.h>

/* The shared library is built with hidden visibility, so that the only things
 * a caller can reach are the ones marked here -- no miniz, no stb, no C++
 * runtime. Every symbol in this header therefore has to say so explicitly;
 * one that forgets does not fail to build, it fails to load, at the first
 * dlsym a binding does. */
#if defined(_WIN32)
#  if defined(DRAGOMAN_SHARED) && defined(DRAGOMAN_BUILDING)
#    define DG_API __declspec(dllexport)
#  elif defined(DRAGOMAN_SHARED)
#    define DG_API __declspec(dllimport)
#  else
#    define DG_API
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define DG_API __attribute__((visibility("default")))
#else
#  define DG_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- version */

/* The library version is semver and moves with the release. The ABI version
 * is a separate integer that moves only when an existing symbol changes
 * meaning, so a binding can refuse to load a library it cannot speak to
 * without having to parse a version string. */
#define DRAGOMAN_VERSION_MAJOR 0
#define DRAGOMAN_VERSION_MINOR 2
#define DRAGOMAN_VERSION_PATCH 2
#define DRAGOMAN_VERSION_STRING "0.2.2"
#define DRAGOMAN_ABI_VERSION 2

DG_API const char* dg_version_string(void);
DG_API int         dg_version_major(void);
DG_API int         dg_version_minor(void);
DG_API int         dg_version_patch(void);
DG_API int         dg_abi_version(void);

/* ---------------------------------------------------------------- formats */

typedef enum dg_format {
    DG_FORMAT_UNKNOWN = 0,
    DG_FORMAT_ODMAP   = 1,  /* Open Doctrines: one zip archive, `.odmap`      */
    DG_FORMAT_GD5     = 2   /* Greater Diplomacy 5: a directory of files      */
} dg_format;

/* Which of the two a path holds, by looking at it rather than at its name --
 * a `.odmap` is recognised by its zip magic and a GD5 map by the members its
 * loader insists on. DG_FORMAT_UNKNOWN if neither. */
DG_API dg_format dg_detect(const char* path);

DG_API const char* dg_format_name(dg_format fmt);

/* ---------------------------------------------------------------- options */

typedef struct dg_options {
    /* Carry everything the destination game has no field for in a sidecar
     * beside the map, so converting back restores it. Both games ignore files
     * they do not know, so this costs the destination nothing and is what
     * makes a round trip lossless rather than merely close. Default 1. */
    int carry_sidecar;

    /* Compute what the destination needs and the source never stored --
     * province adjacency and centroids for GD5, the land/sea mask for Open
     * Doctrines -- from the province raster. Off, those fields are left empty
     * and the map may not load. Default 1. */
    int derive_geometry;

    /* Translate scripts as well as map data: Open Doctrines' line-based
     * language to GD5's scripted events and back, as far as each side can
     * express the other. Default 1. */
    int translate_scripts;

    /* Treat any warning as a failure. For CI, where a silent partial
     * translation is worse than a stopped one. Default 0. */
    int strict;

    /* Re-encode images rather than passing the original bytes through. Only
     * useful for shrinking a map; it makes round trips non-identical at the
     * byte level. Default 0. */
    int reencode_images;

    /* Cut the water into sea provinces when converting to GD5 from a game that
     * does not draw any. Open Doctrines leaves its oceans unpainted, and GD5
     * can neither render nor sail across what is not a province, so without
     * this the sea arrives black and no fleet can move. The invented provinces
     * are recorded in the sidecar and removed again on the way back, so the
     * round trip is unaffected either way. Default 1. */
    int synthesise_ocean;
} dg_options;

DG_API void dg_options_defaults(dg_options* out);

/* ---------------------------------------------------------------- reports */

typedef enum dg_severity {
    DG_INFO    = 0,
    DG_WARNING = 1,  /* translated, but not exactly -- see the message */
    DG_ERROR   = 2   /* not translated */
} dg_severity;

typedef struct dg_report dg_report;

DG_API int         dg_report_count(const dg_report* r);
DG_API int         dg_report_severity(const dg_report* r, int index);
DG_API const char* dg_report_code(const dg_report* r, int index);     /* stable id, e.g. "script.unsupported" */
DG_API const char* dg_report_message(const dg_report* r, int index);  /* human sentence */
DG_API int         dg_report_worst(const dg_report* r);
/* The whole report as a JSON array; free with dg_string_free. */
DG_API char*       dg_report_to_json(const dg_report* r);
DG_API void        dg_report_free(dg_report* r);

/* ----------------------------------------------------------------- worlds */

/* A map held in the interchange model: neither game's layout, but the union
 * of what both describe. */
typedef struct dg_world dg_world;

/* `fmt` may be DG_FORMAT_UNKNOWN to detect it. `out_report` may be NULL; when
 * it is not, the caller owns the report and frees it even on failure.
 * Returns NULL on failure -- see dg_last_error(). */
DG_API dg_world* dg_load(const char* path, dg_format fmt,
                         const dg_options* opts, dg_report** out_report);

/* Returns 0 on success, non-zero on failure. */
DG_API int dg_save(const dg_world* w, const char* path, dg_format fmt,
                   const dg_options* opts, dg_report** out_report);

/* Load, translate and save in one call -- the common case, and the one the
 * CLI and every binding wrap. */
DG_API int dg_convert(const char* in_path, const char* out_path, dg_format to,
                      const dg_options* opts, dg_report** out_report);

/* Convert to `to` and back, and report whether the result is byte-identical
 * to the input. Returns 1 for identical, 0 for a difference, -1 on failure.
 * This is the property the test suite asserts, exposed so callers can assert
 * it too before trusting a conversion. */
DG_API int dg_roundtrip_check(const char* path, dg_format to,
                              const dg_options* opts, dg_report** out_report);

DG_API void dg_world_free(dg_world* w);

DG_API int         dg_world_province_count(const dg_world* w);
DG_API int         dg_world_nation_count(const dg_world* w);
DG_API int         dg_world_script_count(const dg_world* w);
DG_API const char* dg_world_name(const dg_world* w);
DG_API dg_format   dg_world_origin(const dg_world* w);

/* The entire interchange model as JSON; free with dg_string_free. This is the
 * escape hatch: rather than grow an accessor per field, a binding that wants
 * to read or rewrite a map parses this, edits it, and hands it back. The
 * schema is documented in docs/model.md and versioned with the ABI. */
DG_API char*     dg_world_to_json(const dg_world* w);
DG_API dg_world* dg_world_from_json(const char* json, dg_report** out_report);

/* ------------------------------------------------------------------ error */

/* The last failure on this thread, or "" if none. Valid until the next call
 * that fails on the same thread. */
DG_API const char* dg_last_error(void);

DG_API void dg_string_free(char* s);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* DRAGOMAN_H */
