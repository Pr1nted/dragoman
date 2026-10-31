#!/usr/bin/env python3
"""Every binding must name every format the C header does, with the same value.

Why this exists: the formats are declared six times -- once in the C header and
once in each binding -- and nothing connected them. When Unciv was added the
header gained DG_FORMAT_UNCIV and *none* of the five bindings did, so for a
release the library could convert to Unciv and nothing that called it could ask
for one. JavaScript did worse than omit it: its convert() checks the target
against a whitelist, so a caller passing the right integer was told it was not
a format.

Neither failure is a compile error in any of the six languages. This is the
only thing that would have caught either.

    python3 tools/check_binding_formats.py
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def norm(name):
    """One spelling for six conventions: DG_FORMAT_UNCIV_MOD, UNCIV_MOD,
    UncivMod and unciv_mod are the same format."""
    name = name.strip()
    if name.upper().startswith("DG_FORMAT_"):
        name = name[len("DG_FORMAT_"):]
    return name.replace("_", "").upper()


def read(rel):
    path = os.path.join(ROOT, rel)
    if not os.path.isfile(path):
        return None
    with open(path, encoding="utf-8") as f:
        return f.read()


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return text


def from_header():
    text = read("include/dragoman/dragoman.h")
    if text is None:
        sys.exit("cannot read the C header")
    # `typedef enum dg_format { ... } dg_format;` -- the tag is optional in C
    # and this header happens to use one.
    body = re.search(r"typedef\s+enum(?:\s+\w+)?\s*\{(.*?)\}\s*dg_format\s*;", text, re.S)
    if not body:
        sys.exit("no dg_format enum in the C header")
    out, nxt = {}, 0
    for name, value in re.findall(r"(DG_FORMAT_[A-Z0-9_]+)\s*(?:=\s*(\d+))?",
                                  strip_comments(body.group(1))):
        nxt = int(value) if value else nxt
        out[norm(name)] = nxt
        nxt += 1
    return out


def pairs(rel, pattern, section=None, swap=False):
    """name -> int, from a file, by a pattern with two groups.

    `section` first narrows the file to the format declaration. Without it the
    severity enum -- INFO/WARNING/ERROR, same shape, same file -- is read as
    three formats the header has never heard of.
    """
    text = read(rel)
    if text is None:
        return None
    if section is not None:
        found = re.search(section, text, re.S)
        if not found:
            return {}
        text = found.group(0)
    out = {}
    # re.M, or "^" only ever matches the start of the file and every
    # binding reads as empty -- which looks exactly like total drift.
    for a, b in re.findall(pattern, strip_comments(text), re.M):
        name, value = (b, a) if swap else (a, b)
        out[norm(name)] = int(value)
    return out


BINDINGS = [
    # (label, how to read it)
    ("python", lambda: pairs("bindings/python/dragoman/__init__.py",
                             r"^\s{4}([A-Z][A-Z0-9_]*)\s*=\s*(\d+)\s*$",
                             section=r"class\s+Format\b.*?(?=\nclass\s|\Z)")),
    ("js", lambda: pairs("bindings/js/index.js",
                         r"^\s{2}([A-Z][A-Z0-9_]*)\s*:\s*(\d+)\s*,",
                         section=r"(?:const|var|let)\s+Format\s*=\s*(?:Object\.freeze\()?\{.*?\n\}\)?;")),
    ("rust", lambda: pairs("bindings/rust/src/lib.rs",
                           r"Format::([A-Za-z0-9]+)\s*=>\s*(\d+)")),
    ("zig", lambda: pairs("bindings/zig/src/dragoman.zig",
                          r"^\s{4}([a-z][a-z0-9_]*)\s*=\s*(\d+)\s*,",
                          section=r"pub\s+const\s+Format\s*=\s*enum.*?\n\};")),
    ("jvm", lambda: pairs("bindings/jvm/src/main/java/com/opendoctrines/dragoman/Format.java",
                          r"^\s{4}([A-Z][A-Z0-9_]*)\s*\((\d+)\)")),
]


def main():
    want = from_header()
    if not want:
        sys.exit("the C header declared no formats, which cannot be right")
    print(f"C header declares {len(want)}: " +
          ", ".join(f"{k}={v}" for k, v in sorted(want.items(), key=lambda kv: kv[1])))

    failed = False
    for label, reader in BINDINGS:
        got = reader()
        if got is None:
            print(f"  {label:7} FAIL  cannot read its source")
            failed = True
            continue
        missing = {k: v for k, v in want.items() if k not in got}
        wrong = {k: (want[k], got[k]) for k in want if k in got and got[k] != want[k]}
        extra = {k: v for k, v in got.items() if k not in want}
        if missing or wrong or extra:
            failed = True
            print(f"  {label:7} FAIL")
            for k, v in sorted(missing.items()):
                print(f"            missing {k} = {v}")
            for k, (w, g) in sorted(wrong.items()):
                print(f"            {k} is {g}, the header says {w}")
            for k, v in sorted(extra.items()):
                print(f"            names {k} = {v}, which the header does not")
        else:
            print(f"  {label:7} ok    all {len(want)}")

    # JS rejects anything not on a whitelist, so naming the value is not enough.
    js = read("bindings/js/index.js")
    if js is not None:
        guard = re.search(r"if\s*\(to\s*!==.*?\)\s*\{", js, re.S)
        if guard:
            named = {norm(m) for m in re.findall(r"Format\.([A-Z0-9_]+)", guard.group(0))}
            # UNKNOWN is a detection result, never a conversion target.
            targets = {k for k in want if k != "UNKNOWN"}
            absent = targets - named
            if absent:
                failed = True
                print("  js      FAIL  convert() rejects " + ", ".join(sorted(absent)) +
                      " -- named in Format but not accepted as a target")
            else:
                print("  js      ok    convert() accepts every target format")

    if failed:
        print("\nbinding drift: a format the C header declares is missing, "
              "mis-numbered or refused somewhere.")
        return 1
    print("\nevery binding agrees with the C header")
    return 0


if __name__ == "__main__":
    sys.exit(main())
