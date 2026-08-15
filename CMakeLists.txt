cmake_minimum_required(VERSION 3.16)

# The version lives in one file, is read here, and is checked against the C
# header and the Python package by tests/test_version.cpp and the CI job. A
# release that forgets one of the three fails before it is tagged.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" DRAGOMAN_VERSION_RAW)
string(STRIP "${DRAGOMAN_VERSION_RAW}" DRAGOMAN_VERSION_RAW)

project(dragoman VERSION ${DRAGOMAN_VERSION_RAW} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(DRAGOMAN_BUILD_TESTS "Build the test suite" ON)
option(DRAGOMAN_BUILD_CLI "Build the dragoman command line tool" ON)
option(DRAGOMAN_BUILD_SHARED "Build a shared library as well as a static one" ON)
option(DRAGOMAN_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(DRAGOMAN_PYTHON_WHEEL "Install only the shared library, into the Python package" OFF)

# --------------------------------------------------------------- third party
#
# miniz for the zip container and stb for PNG, both taken from the versions
# Open Doctrines itself builds against, so an archive this library writes is
# read back by exactly the code that will open it in the game. nlohmann/json
# is the same single header the game uses. All three are MIT or public domain.
add_library(dragoman_thirdparty STATIC
    third_party/miniz/miniz.c
    third_party/miniz/miniz_tdef.c
    third_party/miniz/miniz_tinfl.c
    third_party/miniz/miniz_zip.c
    third_party/stb/stb_impl.c
)
target_include_directories(dragoman_thirdparty PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/json"
)
# nlohmann/json ships as json.hpp; the library includes it by its usual path.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/thirdparty_include/nlohmann")
configure_file(third_party/json/json.hpp
               "${CMAKE_CURRENT_BINARY_DIR}/thirdparty_include/nlohmann/json.hpp" COPYONLY)
target_include_directories(dragoman_thirdparty PUBLIC
    "${CMAKE_CURRENT_BINARY_DIR}/thirdparty_include")

if(NOT MSVC)
    target_compile_options(dragoman_thirdparty PRIVATE -w)
endif()

# ------------------------------------------------------------------- library

set(DRAGOMAN_SOURCES
    src/Api.cpp
    src/Common.cpp
    src/Gd5Map.cpp
    src/ModelJson.cpp
    src/OdMap.cpp
    src/Raster.cpp
    src/Scripts.cpp
    src/Support.cpp
    src/Zip.cpp
)

add_library(dragoman STATIC ${DRAGOMAN_SOURCES})
add_library(dragoman::dragoman ALIAS dragoman)

# On Windows a DLL also produces an import library, and dragoman_shared is
# named `dragoman` -- so both targets wanted to write dragoman.lib into the same
# directory. The shared one won, the tests linked the DLL's import library
# instead of the static archive, and every internal C++ symbol came back
# unresolved: readOdMap, startsWith, World::findProvince, all of it. Only the C
# ABI is exported from the DLL, which is the point of it, so nothing else was
# there to find. Renaming the static archive keeps dragoman.lib meaning what a
# DLL consumer expects it to mean.
if(WIN32)
    set_target_properties(dragoman PROPERTIES OUTPUT_NAME dragoman_static)
endif()
target_include_directories(dragoman
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(dragoman PRIVATE dragoman_thirdparty)
target_compile_definitions(dragoman PRIVATE DRAGOMAN_BUILDING)

if(MSVC)
    target_compile_options(dragoman PRIVATE /W4)
    if(DRAGOMAN_WARNINGS_AS_ERRORS)
        target_compile_options(dragoman PRIVATE /WX)
    endif()
else()
    target_compile_options(dragoman PRIVATE -Wall -Wextra)
    if(DRAGOMAN_WARNINGS_AS_ERRORS)
        target_compile_options(dragoman PRIVATE -Werror)
    endif()
endif()

# The shared build is what every non-C binding loads: Python via ctypes, Go
# via cgo, C# via P/Invoke. It exports the C ABI and nothing else.
if(DRAGOMAN_BUILD_SHARED)
    add_library(dragoman_shared SHARED ${DRAGOMAN_SOURCES})
    set_target_properties(dragoman_shared PROPERTIES
        OUTPUT_NAME dragoman
        VERSION ${PROJECT_VERSION}
        SOVERSION 2                     # the ABI version, not the release
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
    )
    target_include_directories(dragoman_shared
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(dragoman_shared PRIVATE dragoman_thirdparty)
    target_compile_definitions(dragoman_shared PRIVATE DRAGOMAN_BUILDING DRAGOMAN_SHARED)

    # The C++ runtime is linked in statically on Linux. Nothing about this
    # library's interface is C++ -- it is loaded by ctypes, by cgo, by P/Invoke,
    # from processes that have no reason to have libstdc++ at the version it was
    # built against. Carrying it removes the one dependency a caller could not
    # have predicted from the header.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_link_options(dragoman_shared PRIVATE -static-libstdc++ -static-libgcc)
    endif()
endif()

# ----------------------------------------------------------------------- cli

if(DRAGOMAN_BUILD_CLI)
    add_executable(dragoman_cli tools/dragoman_cli.cpp)
    set_target_properties(dragoman_cli PROPERTIES OUTPUT_NAME dragoman)
    target_link_libraries(dragoman_cli PRIVATE dragoman)
endif()

# --------------------------------------------------------------------- tests

if(DRAGOMAN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ------------------------------------------------------------------- install

# Building a Python wheel: the shared library goes *inside* the package, beside
# __init__.py, which is the first place bindings/python/dragoman/_ffi.py looks.
# That is what makes `pip install dragoman` a complete install with no compiler
# and no separate library to find -- the audience for this includes people who
# install Greater Diplomacy 5 by unzipping it.
if(DRAGOMAN_PYTHON_WHEEL)
    if(NOT DRAGOMAN_BUILD_SHARED)
        message(FATAL_ERROR "DRAGOMAN_PYTHON_WHEEL needs DRAGOMAN_BUILD_SHARED")
    endif()
    # COMPONENT is repeated per artifact on purpose. Written once at the end it
    # binds only to the group it follows, so on Linux and macOS -- where a
    # shared library is a LIBRARY artifact, not a RUNTIME one -- the .so landed
    # in the default component and `--install --component python` installed
    # nothing at all. The wheel built, imported, and had no library in it.
    install(TARGETS dragoman_shared
            LIBRARY DESTINATION dragoman COMPONENT python
            RUNTIME DESTINATION dragoman COMPONENT python)
    return()   # a wheel wants the library and nothing else -- no headers, no CLI
endif()

include(GNUInstallDirs)
install(TARGETS dragoman
        EXPORT dragomanTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
if(DRAGOMAN_BUILD_SHARED)
    install(TARGETS dragoman_shared
            EXPORT dragomanTargets
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(DRAGOMAN_BUILD_CLI)
    install(TARGETS dragoman_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
