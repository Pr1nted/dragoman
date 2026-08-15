#!/usr/bin/env python3
"""Push wiki/ to the repository's Wiki tab.

    python3 tools/publish_wiki.py             # publish
    python3 tools/publish_wiki.py --dry-run   # say what would change, touch nothing

The wiki is a SECOND GIT REPOSITORY that GitHub keeps beside this one, at
<repo>.wiki.git. That is the whole reason this script exists: nothing in a
normal push reaches it, so pages written here would sit in the tree, correct and
reviewed and linked to from the README, while the Wiki tab stayed empty.
Anything that has to be remembered by hand eventually is not.

Two things are rewritten on the way out:

  * `[text](Page-Name.md)` becomes `[text](Page-Name)`. The in-repo copy carries
    the `.md` so the pages also work when read in the Code tab; the Wiki tab
    serves pages without it.
  * README.md is not published. It is the folder's front page in the Code tab;
    the wiki's front page is Home.

FIRST RUN NEEDS ONE MANUAL STEP. GitHub does not create the wiki repository
until a first page exists, and there is no API for it: open the repository's
Wiki tab, click "Create the first page", save anything at all, then run this.
It is overwritten on the first publish.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WIKI = ROOT / "wiki"

# The Code tab's front page, and only that. Everything else in wiki/ is a page.
NOT_PUBLISHED = {"README.md"}


def run(cmd, cwd=None, check=True):
    r = subprocess.run(cmd, cwd=cwd, text=True, capture_output=True)
    if check and r.returncode != 0:
        sys.stderr.write((r.stdout or "") + (r.stderr or ""))
        raise SystemExit(f"failed: {' '.join(cmd)}")
    return r


def wiki_remote():
    """Where the wiki lives, derived from this checkout's own origin.

    Not hardcoded, so a fork publishes to its own wiki rather than failing on
    somebody else's. WIKI_REMOTE overrides it, which is how a test points this
    at a throwaway repository.
    """
    override = os.environ.get("WIKI_REMOTE")
    if override:
        return override
    origin = run(["git", "-C", str(ROOT), "remote", "get-url", "origin"]).stdout.strip()
    if not origin:
        raise SystemExit("this checkout has no origin remote to derive the wiki from")

    # A token-authenticated push in CI; ssh or https as configured locally.
    token = os.environ.get("GITHUB_TOKEN")
    if token and origin.startswith("https://"):
        origin = origin.replace("https://", f"https://x-access-token:{token}@", 1)
    return re.sub(r"(\.git)?$", ".wiki.git", origin, count=1)


def unlink_md(text):
    """`[text](Page.md)` -> `[text](Page)`, leaving real URLs alone."""
    return re.sub(r"\]\((?!https?://)([^)]+?)\.md((?:#[^)]*)?)\)", r"](\1\2)", text)


def pages():
    return sorted(p for p in WIKI.glob("*.md") if p.name not in NOT_PUBLISHED)


def main():
    dry_run = "--dry-run" in sys.argv
    found = pages()
    if not found:
        raise SystemExit(f"no pages in {WIKI}")

    remote = wiki_remote()
    scrubbed = re.sub(r"//[^@]+@", "//", remote)  # never print the token
    print(f"{len(found)} page(s) -> {scrubbed}")

    with tempfile.TemporaryDirectory() as tmp:
        clone = Path(tmp) / "wiki"
        result = run(["git", "clone", "--depth", "1", remote, str(clone)], check=False)
        if result.returncode != 0:
            sys.stderr.write(result.stderr or "")
            raise SystemExit(
                "could not clone the wiki repository.\n"
                "GitHub creates it with its first page and offers no API for that:\n"
                "open the repository's Wiki tab, click 'Create the first page',\n"
                "save anything, then run this again."
            )

        # Everything currently published that we are not about to write again.
        for old in clone.glob("*.md"):
            old.unlink()

        changed = []
        for page in found:
            text = unlink_md(page.read_text())
            target = clone / page.name
            if not target.exists() or target.read_text() != text:
                changed.append(page.name)
            target.write_text(text)

        status = run(["git", "status", "--porcelain"], cwd=clone).stdout.strip()
        if not status:
            print("wiki already up to date")
            return 0

        for line in status.splitlines():
            print(f"  {line}")
        if dry_run:
            print("(dry run, nothing published)")
            return 0

        run(["git", "config", "user.name", "dragoman-wiki"], cwd=clone)
        run(["git", "config", "user.email",
             "111568178+Pr1nted@users.noreply.github.com"], cwd=clone)
        run(["git", "add", "-A"], cwd=clone)
        run(["git", "commit", "-m", "Publish wiki from the tree"], cwd=clone)
        run(["git", "push", "origin", "HEAD"], cwd=clone)
        print(f"published {len(found)} page(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
