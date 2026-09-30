// The same binding from TypeScript. The types are the .d.ts beside index.js.
import { convert, detect, roundTripCheck, Format, Severity, type Note, type Options } from './index.js';

const map = process.argv[2];
const from: Format = detect(map);
console.log(`${map} is format ${from}`);

const options: Options = { strict: false, translateScripts: true };
const result = convert(map, '/tmp/ts_out', Format.GD5, options);

const problems: Note[] = result.notes.filter((n: Note) => n.severity !== Severity.INFO);
console.log(`ok=${result.ok}, ${problems.length} problem(s)`);
for (const p of problems.slice(0, 2)) console.log(`  ${p.code}: ${p.message.slice(0, 60)}`);

const rt = roundTripCheck(map, Format.GD5);
console.log(`round trip: ${rt.outcome} (held=${rt.held})`);
