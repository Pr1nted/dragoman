"""The `dragoman` command, for people who installed with pip.

The C++ build produces a command line tool of its own; this is the same set of
subcommands over the same library, so that `pip install dragoman` gives a
working command without anyone needing to know there is a binary underneath or
where it went.

    dragoman convert 1914.odmap base_maps/1914
    dragoman roundtrip 1914.odmap
    dragoman inspect 1914.odmap --json
    dragoman detect some-file
"""

from __future__ import annotations

import argparse
import json
import sys

from . import DragomanError, Severity, __version__, abi_version, convert
from . import detect as detect_format
from . import library_version, load, roundtrip_check

_LABEL = {Severity.INFO: "note", Severity.WARNING: "warning", Severity.ERROR: "error"}


def _print_report(report, quiet: bool) -> int:
    for entry in report:
        if quiet and entry.severity == Severity.INFO:
            continue
        stream = sys.stdout if entry.severity == Severity.INFO else sys.stderr
        print(f"  {_LABEL[entry.severity]:<7} {entry.code}: {entry.message}", file=stream)
    return report.worst


def _options(args) -> dict:
    return {
        "carry_sidecar": not args.no_sidecar,
        "derive_geometry": not args.no_geometry,
        "translate_scripts": not args.no_scripts,
        "synthesise_ocean": not args.no_ocean,
        "strict": args.strict,
    }


def _add_common(parser) -> None:
    parser.add_argument("--no-sidecar", action="store_true",
                        help="do not carry what the target format cannot hold")
    parser.add_argument("--no-geometry", action="store_true",
                        help="do not derive adjacency, centres or the land/sea mask")
    parser.add_argument("--no-scripts", action="store_true",
                        help="do not translate scripts or scripted events")
    parser.add_argument("--no-ocean", action="store_true",
                        help="do not invent sea provinces for a game that draws none")
    parser.add_argument("--strict", action="store_true",
                        help="treat any warning as a failure")
    parser.add_argument("--quiet", action="store_true", help="warnings and errors only")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        prog="dragoman",
        description="Translate maps between Open Doctrines and Greater Diplomacy 5.",
        epilog="Documentation: https://github.com/Pr1nted/dragoman/wiki",
    )
    parser.add_argument("--version", action="store_true", help="print versions and exit")
    sub = parser.add_subparsers(dest="command")

    p = sub.add_parser("convert", help="convert a map to the other format")
    p.add_argument("input")
    p.add_argument("output")
    p.add_argument("--to", choices=["odmap", "gd5"],
                   help="target format; inferred from the source when omitted")
    _add_common(p)

    p = sub.add_parser("roundtrip", help="convert to the other format and back, and compare")
    p.add_argument("map")
    p.add_argument("--to", choices=["odmap", "gd5"])
    _add_common(p)

    p = sub.add_parser("inspect", help="summarise a map, or dump the whole model")
    p.add_argument("map")
    p.add_argument("--json", action="store_true", help="print the interchange model")

    p = sub.add_parser("detect", help="say what a file is: odmap, gd5, or unknown")
    p.add_argument("path")

    args = parser.parse_args(argv)

    if args.version or args.command is None:
        if args.version:
            print(f"dragoman {__version__} (library {library_version()}, abi {abi_version()})")
            return 0
        parser.print_help()
        return 2

    try:
        if args.command == "detect":
            found = detect_format(args.path)
            print(found)
            return 1 if found == "unknown" else 0

        if args.command == "inspect":
            with load(args.map) as world:
                if args.json:
                    print(json.dumps(world.to_dict(), indent=1))
                else:
                    print(world.name)
                    print(f"  format     {world.origin}")
                    print(f"  provinces  {len(world.provinces)}")
                    print(f"  nations    {len(world.nations)}")
                    print(f"  scripts    {len(world.scripts) + len(world.events)}")
            return 0

        if args.command == "convert":
            report = convert(args.input, args.output, to=args.to, **_options(args))
            if not args.quiet:
                print(f"converted {args.input} -> {args.output}")
            return 1 if _print_report(report, args.quiet) >= Severity.ERROR else 0

        if args.command == "roundtrip":
            identical, report = roundtrip_check(args.map, to=args.to, **_options(args))
            print("round trip preserved every modelled field and the province raster"
                  if identical else "round trip changed the map -- see below")
            _print_report(report, args.quiet)
            return 0 if identical else 1

    except DragomanError as exc:
        print(f"dragoman: {exc}", file=sys.stderr)
        if exc.report is not None:
            _print_report(exc.report, quiet=False)
        return 1

    parser.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
