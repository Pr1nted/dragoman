# The locator file

A game says where it is, so the other one does not have to go looking.

## Why

Translating a map means writing it where the other game will find it, which
means knowing where the other game is. Both sides currently guess: they check a
fixed list of the places a game is normally installed, one level deep, and only
when a player presses a button that says so. That is deliberately shallow — it
is somebody's home directory — and it means the common case, *"I installed it
somewhere else"*, ends in a folder picker.

A game already knows exactly where it is. It only has to write it down.

## The file

On startup, a game writes one small JSON file naming itself and its own
directory:

| | |
| --- | --- |
| Linux | `$XDG_DATA_HOME/game-locators/` (default `~/.local/share/game-locators/`) |
| macOS | `~/Library/Application Support/game-locators/` |
| Windows | `%LOCALAPPDATA%\game-locators\` |

The filename is the game's id, lowercase, with `.json` appended:
`open-doctrines.json`, `greater-diplomacy-5.json`.

```json
{
  "locator": 1,
  "id": "open-doctrines",
  "name": "Open Doctrines",
  "path": "/home/vlad/Games/OpenDoctrines",
  "version": "1.0.2a",
  "updated": "2026-08-16T09:41:02Z"
}
```

- **`locator`** — format version. A reader that does not know the number stops
  rather than guessing at the fields.
- **`id`** — stable, lowercase, hyphenated. It is the filename, so a reader can
  look for one game without listing the directory.
- **`path`** — the game's own root, absolute. Where a translated map goes is
  derived from this by whoever writes one, not stored here.
- **`version`**, **`updated`** — informational. A stale `updated` is a hint
  that the game has not run in a while, not a reason to ignore the file.

## Rules

**Writing.** Write your own file, and only your own. Rewrite it when the path
or version changes; a write per startup is cheap enough not to bother
optimising. A game that fails to write it carries on silently — this is a
convenience, and a read-only or missing directory is not an error worth
interrupting anyone for.

**Moving and deleting.** Both happen, and neither is signalled to anyone.

A game that is *moved* fixes itself: the next time it starts it writes the file
again with its new path. Between the move and that next launch the file is
wrong, and nothing can be done about that from the other side.

A game that is *deleted* never runs again, so its locator is never corrected
and never removed. Stale files are therefore the normal steady state, not an
edge case — treat every locator as possibly pointing at nothing.

**Reading.** A path from a locator is a *claim*, not a fact. It may name a game
that has since been deleted, moved, or replaced by something else entirely. So:

1. Check the directory still exists and still looks like that game, by whatever
   marks that game has. Open Doctrines is recognised by `data/STDmaps` holding
   at least one `.odmap`; Greater Diplomacy 5 by `base_maps/` beside
   `data/json/research_template.json`.
2. If it fails that check, ignore the file. Do not delete it — it is not yours.
3. Show the player what you found before you write anything into it.

4. Re-check it before each use, not once at startup. A screen that found a
   game when it opened may still be on screen an hour later, offering to write
   into a folder that stopped existing in between. It is one `stat` against a
   directory, against the cost of writing a map somewhere that is no longer
   that game.

**Never** treat a locator as permission. It says where a game is, not that
anyone may write there.

## What this replaces

Nothing, on purpose. The search stays: a player who has never launched the
other game has no locator to read, and a locator that fails its check falls
back to the same search. This only removes the guessing when the answer is
already known.
