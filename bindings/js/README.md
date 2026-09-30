# open-dragoman for Node

```js
const { convert, Format } = require('open-dragoman');

const r = convert('world.odmap', 'out', Format.GD5);
for (const n of r.notes) console.log(`[${n.code}] ${n.message}`);
```

TypeScript types ship beside it in `index.d.ts` — hand-written, because the
surface is a dozen functions over a flat C ABI and a generator would be a
second source of truth that can disagree with the header.

```ts
import { convert, Format, type Note } from 'open-dragoman';

const result = convert(map, 'out', Format.GD5, { strict: false });
const problems: Note[] = result.notes.filter(n => n.severity !== 0);
```

## Why koffi and not an addon

The ABI is six ints and some opaque pointers. A hand-written N-API addon would
mean a compiler on every install, and a build to maintain per platform, for
nothing this binding can use. Same bargain as the Python binding's ctypes and
the JVM binding's JNA.

## Finding the native library

In order: the path passed to `load()`, `$DRAGOMAN_LIBRARY`, `native/` beside the
package, then the system paths.

**The ABI is checked once, at load.** A library built against a different one
would otherwise answer calls with fields in the wrong places and return
plausible nonsense.

## Two return conventions, not one

Worth stating plainly, because both readings look like working code:

| call | success is |
|---|---|
| `convert` | `ok === true`, from a C return of **0** |
| `roundTripCheck` | `outcome === 'identical'`, from a C return of **1** |

`dg_roundtrip_check` returns 1 for identical, 0 for a difference and -1 for a
check that could not run. Reading it the way `convert` is read calls a holding
round trip a failure — a bug that shipped in the JVM binding before a real map
exposed it.

## Tests

```
DRAGOMAN_LIBRARY=/path/to/libdragoman.dylib \
DRAGOMAN_TEST_MAP=/path/to/world.odmap \
npm test
```

Without `DRAGOMAN_TEST_MAP` the conversion test **reports a skip**, rather than
passing quietly and implying coverage it does not have.
