# wiki/

The pages behind this repository's **Wiki** tab, kept in the tree so they are
reviewed with the code that makes them true.

The wiki is a second git repository (`<repo>.wiki.git`) that a normal push does
not touch, which is why `tools/publish_wiki.py` exists: without it, pages get
written, merged, and never appear. Publishing now happens on every push to
`main` that changes `wiki/` — see `.github/workflows/publish-wiki.yml`.

```bash
python3 tools/publish_wiki.py --dry-run   # say what would change
python3 tools/publish_wiki.py             # publish
```

**The wiki repository must exist before the first publish.** GitHub creates it
with its first page and offers no API for that, so it is one manual click, once:
open the repository's Wiki tab, "Create the first page", save anything. The
first publish overwrites it.

## What goes where

This wiki is **task-oriented** — what the thing is, and how to use it. Reference
material lives in `docs/` and is linked to rather than copied, so the two cannot
drift:

| `wiki/` | `docs/` |
|---|---|
| Getting Started, Troubleshooting | — |
| What Crosses (the short version) | `mapping.md` (field by field, with reasons) |
| Round Trips | `roundtrip.md` |
| Scripts and Events | `scripting.md` |
| Languages | `abi.md`, `model.md`, `versioning.md` |
| Diagnostics | — |

Pages link to each other as `[text](Page-Name.md)`. The `.md` is kept so the
pages also read correctly in the Code tab; the publisher strips it on the way
out, because the Wiki tab serves pages without it.

`README.md` is this file and is not published — the wiki's front page is
`Home.md`.
