"""Dragoman -- translate maps between Open Doctrines and Greater Diplomacy 5.

    import dragoman

    report = dragoman.convert("1914.odmap", "base_maps/1914", to="gd5")
    for entry in report.warnings:
        print(entry.code, entry.message)

    world = dragoman.load("1914.odmap")
    print(world.name, len(world.provinces), "provinces")

The whole model is available as plain Python data through `World.to_dict()`,
so a map can be inspected or edited here and written back out without this
package needing an accessor for every field the two games have.
"""

from __future__ import annotations

import ctypes
import json
from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Union

from ._ffi import Options, declare, load_library
from ._version import __version__

__all__ = [
    "__version__",
    "abi_version",
    "library_version",
    "Format",
    "Severity",
    "Diagnostic",
    "Report",
    "World",
    "DragomanError",
    "detect",
    "load",
    "convert",
    "roundtrip_check",
]

_lib = None


def _library():
    global _lib
    if _lib is None:
        _lib = declare(load_library())
    return _lib


class DragomanError(RuntimeError):
    """A conversion that could not be completed.

    Carries the report as well as the message, because the reason a map failed
    is usually in a diagnostic rather than in the final error string.
    """

    def __init__(self, message: str, report: Optional["Report"] = None):
        super().__init__(message)
        self.report = report


class Format:
    UNKNOWN = 0
    ODMAP = 1
    GD5 = 2

    _BY_NAME = {"odmap": ODMAP, "od": ODMAP, "gd5": GD5, "gd": GD5, "unknown": UNKNOWN}

    @classmethod
    def parse(cls, value: Union[int, str, None]) -> int:
        if value is None:
            return cls.UNKNOWN
        if isinstance(value, int):
            return value
        key = value.strip().lower()
        if key not in cls._BY_NAME:
            raise ValueError(f"unknown format {value!r}; use 'odmap' or 'gd5'")
        return cls._BY_NAME[key]

    @staticmethod
    def name(value: int) -> str:
        return _library().dg_format_name(value).decode()


class Severity:
    INFO = 0
    WARNING = 1
    ERROR = 2


@dataclass(frozen=True)
class Diagnostic:
    severity: int
    code: str
    message: str

    @property
    def is_warning(self) -> bool:
        return self.severity == Severity.WARNING

    @property
    def is_error(self) -> bool:
        return self.severity == Severity.ERROR

    def __str__(self) -> str:
        label = {0: "note", 1: "warning", 2: "error"}[self.severity]
        return f"{label} {self.code}: {self.message}"


class Report:
    """What the translation could not do exactly, and why."""

    def __init__(self, entries: List[Diagnostic]):
        self._entries = entries

    def __iter__(self):
        return iter(self._entries)

    def __len__(self) -> int:
        return len(self._entries)

    def __repr__(self) -> str:
        return f"<Report {len(self._entries)} entries, worst={self.worst}>"

    @property
    def entries(self) -> List[Diagnostic]:
        return list(self._entries)

    @property
    def warnings(self) -> List[Diagnostic]:
        return [e for e in self._entries if e.severity == Severity.WARNING]

    @property
    def errors(self) -> List[Diagnostic]:
        return [e for e in self._entries if e.severity == Severity.ERROR]

    @property
    def worst(self) -> int:
        return max((e.severity for e in self._entries), default=Severity.INFO)

    @property
    def ok(self) -> bool:
        return self.worst < Severity.ERROR


def _take_report(handle) -> Report:
    """Read a dg_report out and free it. Always called, even on failure."""
    lib = _library()
    entries: List[Diagnostic] = []
    if handle:
        for i in range(lib.dg_report_count(handle)):
            entries.append(
                Diagnostic(
                    severity=lib.dg_report_severity(handle, i),
                    code=lib.dg_report_code(handle, i).decode(),
                    message=lib.dg_report_message(handle, i).decode(),
                )
            )
        lib.dg_report_free(handle)
    return Report(entries)


def _take_string(pointer) -> str:
    """Read a char* the library allocated and hand it back to the library."""
    if not pointer:
        return ""
    lib = _library()
    text = ctypes.cast(pointer, ctypes.c_char_p).value or b""
    lib.dg_string_free(pointer)
    return text.decode()


def _options(
    carry_sidecar: bool = True,
    derive_geometry: bool = True,
    translate_scripts: bool = True,
    strict: bool = False,
    reencode_images: bool = False,
) -> Options:
    opts = Options()
    _library().dg_options_defaults(ctypes.byref(opts))
    opts.carry_sidecar = int(carry_sidecar)
    opts.derive_geometry = int(derive_geometry)
    opts.translate_scripts = int(translate_scripts)
    opts.strict = int(strict)
    opts.reencode_images = int(reencode_images)
    return opts


class World:
    """A loaded map, in neither game's layout but in the shared model."""

    def __init__(self, handle, report: Report):
        self._handle = handle
        self._dict: Optional[Dict[str, Any]] = None
        self.report = report

    def __del__(self):
        self.close()

    def __enter__(self) -> "World":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def close(self) -> None:
        if getattr(self, "_handle", None):
            _library().dg_world_free(self._handle)
            self._handle = None

    def _require(self):
        if not self._handle:
            raise DragomanError("this world has already been closed")
        return self._handle

    def __repr__(self) -> str:
        return (
            f"<World {self.name!r} {len(self.provinces)} provinces "
            f"{len(self.nations)} nations from {self.origin}>"
        )

    @property
    def name(self) -> str:
        return _library().dg_world_name(self._require()).decode()

    @property
    def origin(self) -> str:
        return Format.name(_library().dg_world_origin(self._require()))

    def to_dict(self) -> Dict[str, Any]:
        """The whole interchange model as plain Python data.

        Parsed once and kept, because on a world map this is a few megabytes
        of JSON and callers reach for `.provinces` in a loop.
        """
        if self._dict is None:
            self._dict = json.loads(_take_string(_library().dg_world_to_json(self._require())))
        return self._dict

    @property
    def provinces(self) -> List[Dict[str, Any]]:
        return self.to_dict().get("provinces", [])

    @property
    def nations(self) -> List[Dict[str, Any]]:
        return self.to_dict().get("nations", [])

    @property
    def events(self) -> List[Dict[str, Any]]:
        return self.to_dict().get("events", [])

    @property
    def scripts(self) -> List[Dict[str, Any]]:
        return self.to_dict().get("scripts", [])

    def save(self, path: str, to: Union[int, str], **options) -> Report:
        lib = _library()
        opts = _options(**options)
        handle = ctypes.c_void_p()
        rc = lib.dg_save(
            self._require(),
            str(path).encode(),
            Format.parse(to),
            ctypes.byref(opts),
            ctypes.byref(handle),
        )
        report = _take_report(handle)
        if rc != 0:
            raise DragomanError(lib.dg_last_error().decode(), report)
        return report


def library_version() -> str:
    """The version of the shared library, which a wheel may outlive."""
    return _library().dg_version_string().decode()


def abi_version() -> int:
    return _library().dg_abi_version()


def detect(path: str) -> str:
    """'odmap', 'gd5' or 'unknown', decided by content rather than by name."""
    return Format.name(_library().dg_detect(str(path).encode()))


def load(path: str, fmt: Union[int, str, None] = None, **options) -> World:
    lib = _library()
    opts = _options(**options)
    handle = ctypes.c_void_p()
    world = lib.dg_load(
        str(path).encode(), Format.parse(fmt), ctypes.byref(opts), ctypes.byref(handle)
    )
    report = _take_report(handle)
    if not world:
        raise DragomanError(lib.dg_last_error().decode(), report)
    return World(world, report)


def convert(src: str, dst: str, to: Union[int, str, None] = None, **options) -> Report:
    """Translate a map. With no `to`, converts to whichever format `src` is not."""
    lib = _library()
    if to is None:
        found = _library().dg_detect(str(src).encode())
        if found == Format.UNKNOWN:
            raise DragomanError(f"cannot tell what {src} is; pass to='odmap' or to='gd5'")
        to = Format.GD5 if found == Format.ODMAP else Format.ODMAP

    opts = _options(**options)
    handle = ctypes.c_void_p()
    rc = lib.dg_convert(
        str(src).encode(), str(dst).encode(), Format.parse(to), ctypes.byref(opts),
        ctypes.byref(handle),
    )
    report = _take_report(handle)
    if rc != 0:
        raise DragomanError(lib.dg_last_error().decode(), report)
    return report


def roundtrip_check(path: str, to: Union[int, str, None] = None, **options):
    """Convert to `to` and back; return (identical, report).

    'Identical' means every province, nation, relation, claim, script and
    carried file returned unchanged and the province raster hashes the same --
    not that the two container files are byte-for-byte equal, which deflate
    does not promise across implementations.
    """
    lib = _library()
    if to is None:
        found = lib.dg_detect(str(path).encode())
        to = Format.GD5 if found == Format.ODMAP else Format.ODMAP

    opts = _options(**options)
    handle = ctypes.c_void_p()
    rc = lib.dg_roundtrip_check(
        str(path).encode(), Format.parse(to), ctypes.byref(opts), ctypes.byref(handle)
    )
    report = _take_report(handle)
    if rc < 0:
        raise DragomanError(lib.dg_last_error().decode(), report)
    return rc == 1, report
