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
        SOVERSION 1                     # the ABI version, not the release
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
