"""Finding and declaring the shared library.

ctypes rather than a compiled extension on purpose. Greater Diplomacy 5 is a
pygame project whose players install it by unzipping it, and asking that
audience for a C compiler to convert a map is asking too much. ctypes needs
only the .so/.dylib/.dll, which CI builds for each platform and the wheel
carries.

The search order is: an explicit DRAGOMAN_LIBRARY, then a copy shipped inside
this package (how a wheel is laid out), then a CMake build tree beside the
checkout (how a contributor runs it), then the system loader.
"""

import ctypes
import os
import sys
from pathlib import Path

_LIB_NAMES = {
    "win32": ["dragoman.dll", "libdragoman.dll"],
    "darwin": ["libdragoman.dylib"],
}
_DEFAULT_NAMES = ["libdragoman.so"]


def _candidate_names():
    return _LIB_NAMES.get(sys.platform, _DEFAULT_NAMES)


def _candidate_paths():
    override = os.environ.get("DRAGOMAN_LIBRARY")
    if override:
        yield Path(override)

    here = Path(__file__).resolve().parent
    for name in _candidate_names():
        yield here / name

    # A CMake build tree beside the repository, which is what a contributor
    # who just ran `cmake --build build` has.
    repo = here.parents[2]
    for build in ("build", "cmake-build-debug", "cmake-build-release", "out"):
        for name in _candidate_names():
            yield repo / build / name
            yield repo / build / "Release" / name
            yield repo / build / "Debug" / name


def load_library():
    tried = []
    for path in _candidate_paths():
        tried.append(str(path))
        if path.exists():
            return ctypes.CDLL(str(path))

    # Last resort: let the platform loader look on its own search path.
    for name in _candidate_names():
        try:
            return ctypes.CDLL(name)
        except OSError:
            tried.append(name)

    raise OSError(
        "could not find the dragoman shared library. Build it with\n"
        "    cmake -S . -B build && cmake --build build\n"
        "or set DRAGOMAN_LIBRARY to its path. Looked in:\n  "
        + "\n  ".join(tried)
    )


class Options(ctypes.Structure):
    _fields_ = [
        ("carry_sidecar", ctypes.c_int),
        ("derive_geometry", ctypes.c_int),
        ("translate_scripts", ctypes.c_int),
        ("strict", ctypes.c_int),
        ("reencode_images", ctypes.c_int),
    ]


def declare(lib):
    """Give every symbol its signature.

    Without this ctypes assumes an int return, which silently truncates a
    64-bit pointer on the first call that returns a handle -- the failure
    looks like a corrupt map rather than a binding mistake, so it is worth
    being exhaustive here.
    """
    c = ctypes
    p = c.c_void_p
    s = c.c_char_p

    sigs = {
        "dg_version_string": ([], s),
        "dg_version_major": ([], c.c_int),
        "dg_version_minor": ([], c.c_int),
        "dg_version_patch": ([], c.c_int),
        "dg_abi_version": ([], c.c_int),
        "dg_format_name": ([c.c_int], s),
        "dg_detect": ([s], c.c_int),
        "dg_options_defaults": ([c.POINTER(Options)], None),
        "dg_load": ([s, c.c_int, c.POINTER(Options), c.POINTER(p)], p),
        "dg_save": ([p, s, c.c_int, c.POINTER(Options), c.POINTER(p)], c.c_int),
        "dg_convert": ([s, s, c.c_int, c.POINTER(Options), c.POINTER(p)], c.c_int),
        "dg_roundtrip_check": ([s, c.c_int, c.POINTER(Options), c.POINTER(p)], c.c_int),
        "dg_world_free": ([p], None),
        "dg_world_province_count": ([p], c.c_int),
        "dg_world_nation_count": ([p], c.c_int),
        "dg_world_script_count": ([p], c.c_int),
        "dg_world_name": ([p], s),
        "dg_world_origin": ([p], c.c_int),
        "dg_world_to_json": ([p], p),
        "dg_world_from_json": ([s, c.POINTER(p)], p),
        "dg_report_count": ([p], c.c_int),
        "dg_report_severity": ([p, c.c_int], c.c_int),
        "dg_report_code": ([p, c.c_int], s),
        "dg_report_message": ([p, c.c_int], s),
        "dg_report_worst": ([p], c.c_int),
        "dg_report_to_json": ([p], p),
        "dg_report_free": ([p], None),
        "dg_string_free": ([p], None),
        "dg_last_error": ([], s),
    }
    for name, (argtypes, restype) in sigs.items():
        fn = getattr(lib, name)
        fn.argtypes = argtypes
        fn.restype = restype
    return lib
