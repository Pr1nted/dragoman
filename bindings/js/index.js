'use strict';
//
// open-dragoman for Node: Open Doctrines maps to Greater Diplomacy 5 maps, and
// back.
//
// koffi rather than a N-API addon, for the same reason the Python binding is
// ctypes and the JVM one is JNA: the ABI is six ints and some opaque pointers,
// and a hand-written addon would mean a compiler on every install for no gain.
//
// TWO OWNERSHIP RULES, both with a shape that hides the mistake.
//
//   A report must be freed. dg_convert hands one back whether it succeeded or
//   failed, and it leaks unless every path releases it -- including the error
//   path. `collect` is one function for that reason.
//
//   Some strings are ours and some are not. A `const char*` belongs to the
//   library; a plain `char*` (dg_report_to_json) is the CALLER's to release
//   with dg_string_free. Only the first kind is declared as `str` here.
//
const koffi = require('koffi');
const path = require('node:path');
const fs = require('node:fs');

/** The ABI this binding was written against. */
const REQUIRED_ABI = 2;

/** Which game's layout a map is in. */
const Format = Object.freeze({
  UNKNOWN: 0,
  /** Open Doctrines: one zip archive, `.odmap`. */
  ODMAP: 1,
  /** Greater Diplomacy 5: a directory of files. */
  GD5: 2,
});

/** How much a note matters. */
const Severity = Object.freeze({
  INFO: 0,
  /** Translated, but not exactly. The message says what was lost. */
  WARNING: 1,
  /** Not translated. */
  ERROR: 2,
});

/** Whether a map came back the way it set out. */
const RoundTrip = Object.freeze({
  IDENTICAL: 'identical',
  DIFFERED: 'differed',
  FAILED: 'failed',
});

const DgOptions = koffi.struct('dg_options', {
  carry_sidecar: 'int',
  derive_geometry: 'int',
  translate_scripts: 'int',
  strict: 'int',
  reencode_images: 'int',
  synthesise_ocean: 'int',
});

let lib = null;

function defaultLibraryNames() {
  if (process.platform === 'darwin') return ['libdragoman.dylib'];
  if (process.platform === 'win32') return ['dragoman.dll', 'libdragoman.dll'];
  return ['libdragoman.so'];
}

/**
 * Load the native library.
 *
 * Called automatically on first use. Pass a path, or set DRAGOMAN_LIBRARY, to
 * use a particular file rather than searching.
 *
 * The ABI is checked here, once: a library built against a different one would
 * otherwise answer calls with fields in the wrong places and return plausible
 * nonsense rather than failing.
 */
function load(libraryPath) {
  if (lib) return lib;

  const candidates = [];
  if (libraryPath) candidates.push(libraryPath);
  if (process.env.DRAGOMAN_LIBRARY) candidates.push(process.env.DRAGOMAN_LIBRARY);
  for (const name of defaultLibraryNames()) {
    candidates.push(path.join(__dirname, 'native', name));
    candidates.push(name);
  }

  let native = null;
  const tried = [];
  for (const c of candidates) {
    tried.push(c);
    try {
      native = koffi.load(c);
      break;
    } catch {
      /* next */
    }
  }
  if (!native) {
    throw new Error(
      'could not load the open-dragoman native library. Looked for: ' +
        tried.join(', ') +
        '. Set DRAGOMAN_LIBRARY=/path/to/libdragoman' +
        (process.platform === 'darwin' ? '.dylib' : process.platform === 'win32' ? '.dll' : '.so')
    );
  }

  const l = {
    dg_version_string: native.func('const char* dg_version_string()'),
    dg_abi_version: native.func('int dg_abi_version()'),
    dg_detect: native.func('int dg_detect(const char*)'),
    dg_options_defaults: native.func('void dg_options_defaults(_Out_ dg_options*)'),
    dg_convert: native.func('int dg_convert(const char*, const char*, int, const dg_options*, _Out_ void**)'),
    dg_roundtrip_check: native.func('int dg_roundtrip_check(const char*, int, const dg_options*, _Out_ void**)'),
    dg_report_count: native.func('int dg_report_count(void*)'),
    dg_report_severity: native.func('int dg_report_severity(void*, int)'),
    dg_report_code: native.func('const char* dg_report_code(void*, int)'),
    dg_report_message: native.func('const char* dg_report_message(void*, int)'),
    dg_report_worst: native.func('int dg_report_worst(void*)'),
    dg_report_free: native.func('void dg_report_free(void*)'),
    dg_last_error: native.func('const char* dg_last_error()'),
  };

  const abi = l.dg_abi_version();
  if (abi !== REQUIRED_ABI) {
    throw new Error(
      `open-dragoman ${l.dg_version_string()} speaks ABI ${abi}, and this binding was written ` +
        `against ABI ${REQUIRED_ABI}. They would appear to work and would disagree about what ` +
        'the bytes mean, so the load is refused instead.'
    );
  }
  lib = l;
  return lib;
}

/** The native library's version, e.g. "0.5.0". */
function version() {
  return load().dg_version_string();
}

/** The ABI the loaded library speaks. */
function abiVersion() {
  return load().dg_abi_version();
}

/**
 * The library's own defaults, asked of the library rather than repeated here --
 * so an option added upstream arrives with whatever value that release
 * considers sensible.
 */
function defaultOptions() {
  const raw = {};
  load().dg_options_defaults(raw);
  return {
    carrySidecar: !!raw.carry_sidecar,
    deriveGeometry: !!raw.derive_geometry,
    translateScripts: !!raw.translate_scripts,
    strict: !!raw.strict,
    reencodeImages: !!raw.reencode_images,
    synthesiseOcean: !!raw.synthesise_ocean,
  };
}

function toNative(options) {
  const o = { ...defaultOptions(), ...(options || {}) };
  return {
    carry_sidecar: o.carrySidecar ? 1 : 0,
    derive_geometry: o.deriveGeometry ? 1 : 0,
    translate_scripts: o.translateScripts ? 1 : 0,
    strict: o.strict ? 1 : 0,
    reencode_images: o.reencodeImages ? 1 : 0,
    synthesise_ocean: o.synthesiseOcean ? 1 : 0,
  };
}

/** Which game's layout is at this path, if either. */
function detect(p) {
  return load().dg_detect(String(p));
}

/** Reads the report, then frees it. The free is why this is one function. */
function collect(raw) {
  const l = load();
  const notes = [];
  let worst = Severity.INFO;
  try {
    if (raw) {
      const n = l.dg_report_count(raw);
      for (let i = 0; i < n; i++) {
        notes.push({
          severity: l.dg_report_severity(raw, i),
          code: l.dg_report_code(raw, i),
          message: l.dg_report_message(raw, i),
        });
      }
      worst = l.dg_report_worst(raw);
    }
  } finally {
    if (raw) l.dg_report_free(raw);
  }
  return { notes, worst };
}

/**
 * Convert a map to `to`.
 *
 * Returns { ok, notes, worst }. A conversion can succeed with plenty of notes:
 * that is the normal case, because the two games do not hold the same facts.
 */
function convert(input, output, to, options) {
  const l = load();
  if (to !== Format.ODMAP && to !== Format.GD5) {
    throw new Error('a target format is required (Format.ODMAP or Format.GD5)');
  }
  const out = [null];
  // ZERO IS SUCCESS. It is the C convention, and reading it the other way makes
  // a finished map look like a failure -- a bug this library's own Python
  // binding once shipped.
  const rc = l.dg_convert(String(input), String(output), to, toNative(options), out);
  const { notes, worst } = collect(out[0]);
  if (rc !== 0 && notes.length === 0) {
    const err = l.dg_last_error();
    notes.push({
      severity: Severity.ERROR,
      code: 'dragoman.failed',
      message: err || `the conversion failed without saying why (code ${rc})`,
    });
    return { ok: false, notes, worst: Severity.ERROR };
  }
  return { ok: rc === 0, notes, worst };
}

/**
 * Convert a map out and back and check it returned unchanged, writing nothing
 * permanent.
 *
 * NOT the same convention as convert(): dg_roundtrip_check returns 1 for
 * identical, 0 for a difference and -1 for a check that could not run. Reading
 * it as 0-for-success calls a holding round trip a failure.
 */
function roundTripCheck(p, to, options) {
  const l = load();
  const out = [null];
  const rc = l.dg_roundtrip_check(String(p), to, toNative(options), out);
  const { notes, worst } = collect(out[0]);
  const outcome = rc === 1 ? RoundTrip.IDENTICAL : rc === 0 ? RoundTrip.DIFFERED : RoundTrip.FAILED;
  return { outcome, held: outcome === RoundTrip.IDENTICAL, notes, worst };
}

module.exports = {
  Format,
  Severity,
  RoundTrip,
  REQUIRED_ABI,
  load,
  version,
  abiVersion,
  defaultOptions,
  detect,
  convert,
  roundTripCheck,
};
