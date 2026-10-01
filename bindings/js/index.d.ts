/**
 * open-dragoman: Open Doctrines maps to Greater Diplomacy 5 maps, and back.
 *
 * Hand-written rather than generated. The surface is a dozen functions over a
 * flat C ABI, and a generator here would be a second source of truth that can
 * disagree with the header.
 */

/** Which game's layout a map is in. */
export const Format: {
  readonly UNKNOWN: 0;
  /** Open Doctrines: one zip archive, `.odmap`. */
  readonly ODMAP: 1;
  /** Greater Diplomacy 5: a directory of files. */
  readonly GD5: 2;
  /** Unciv: one JSON file holding a hex grid. Crossing is a resampling. */
  readonly UNCIV: 3;
};
export type Format = 0 | 1 | 2 | 3;

/** How much a note matters. */
export const Severity: {
  readonly INFO: 0;
  /** Translated, but not exactly. The message says what was lost. */
  readonly WARNING: 1;
  /** Not translated. */
  readonly ERROR: 2;
};
export type Severity = 0 | 1 | 2;

/** Whether a map came back the way it set out. */
export const RoundTrip: {
  readonly IDENTICAL: 'identical';
  readonly DIFFERED: 'differed';
  readonly FAILED: 'failed';
};
export type RoundTrip = 'identical' | 'differed' | 'failed';

/** The ABI this binding was written against. */
export const REQUIRED_ABI: number;

/**
 * One thing the library has to say about a conversion.
 *
 * `code` is the stable half -- `"script.unsupported"`, `"gd5.ocean"` -- and is
 * what to branch on. `message` is a sentence for a person and is reworded
 * between releases.
 */
export interface Note {
  severity: Severity;
  code: string;
  message: string;
}

/** What a conversion should and should not do. Omitted fields take the library's default. */
export interface Options {
  /** Carry what the destination has no field for, so converting back restores it. */
  carrySidecar?: boolean;
  /** Compute what the destination needs and the source never stored. */
  deriveGeometry?: boolean;
  /** Translate scripts as well as map data. */
  translateScripts?: boolean;
  /** Treat any warning as a failure. */
  strict?: boolean;
  /** Re-encode images rather than passing the original bytes through. */
  reencodeImages?: boolean;
  /** Cut the water into sea provinces when converting to GD5. */
  synthesiseOcean?: boolean;
}

/**
 * What a conversion did.
 *
 * `ok` and `notes.length === 0` are different questions: a conversion can
 * succeed with plenty of notes, and normally does.
 */
export interface Result {
  ok: boolean;
  notes: Note[];
  worst: Severity;
}

/** What a round-trip check found. */
export interface RoundTripResult {
  outcome: RoundTrip;
  /** `outcome === 'identical'`, which is the question most callers are asking. */
  held: boolean;
  notes: Note[];
  worst: Severity;
}

/** Load the native library. Called automatically on first use. */
export function load(libraryPath?: string): unknown;

/** The native library's version, e.g. `"0.5.0"`. */
export function version(): string;

/** The ABI the loaded library speaks. */
export function abiVersion(): number;

/** The library's own defaults, asked of the library. */
export function defaultOptions(): Required<Options>;

/** Which game's layout is at this path, if either. */
export function detect(path: string): Format;

/** Convert a map to `to`. */
export function convert(input: string, output: string, to: Format, options?: Options): Result;

/**
 * Convert to Unciv's format, choosing the hex grid. Unciv's own sizes run from
 * 24x15 (Tiny) to 80x50 (Huge); zero for either takes the default of 80x50.
 * Both are clamped to 4..200.
 */
export function convertUnciv(
  input: string, output: string, columns?: number, rows?: number, options?: Options
): Result;

/**
 * Convert a map out and back and check it returned unchanged.
 *
 * Note the separate result type: the underlying call does NOT share
 * {@link convert}'s convention -- 1 means identical, 0 means differed.
 */
export function roundTripCheck(path: string, to: Format, options?: Options): RoundTripResult;
