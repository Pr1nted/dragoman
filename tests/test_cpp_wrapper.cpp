/* The C++ wrapper, linked against the static library the way a consumer links
 * it.
 *
 * This test exists because nothing exercised that combination, and it was
 * broken from the day the wrapper was written. The header defines
 * dragoman::Options, Report, Diagnostic and World; the library's own
 * implementation already had types with those exact names, and the static
 * archive exports them. Two definitions of dragoman::Report in one program,
 * whose implicit copy constructors and destructors mangle identically and have
 * different layouts, and the linker keeps one of them. Constructing a World
 * and letting it go out of scope segfaulted, with the map's name sitting in
 * the bytes where the handle belonged.
 *
 * Every other suite here calls the C ABI, and the shared library exports
 * nothing else, so the Python binding and the CLI were fine and the fault hid
 * behind a passing suite. The wrapper is in an inline namespace now.
 *
 * Be clear about what this file does and does not do: it exercises the
 * wrapper, but it does NOT reproduce that collision -- removing the inline
 * namespace leaves these tests passing. What reproduces it is a project
 * outside this one, compiled against an installed Dragoman, which is what
 * bindings/cpp/CMakeLists.txt is for and what CI builds on every push.
 */
#include <dragoman/dragoman.hpp>

#include <filesystem>

#include "Check.h"
#include "fixture_writer.h"

namespace fs = std::filesystem;

/* Constructing and destroying a World is the whole bug: it crashed on the way
 * out, after everything had apparently worked. */
static void testWorldSurvivesItsOwnDestructor() {
    const std::string odmap = fixture_writer::writeOdmap("cpp.odmap");
    REQUIRE(!odmap.empty());

    dragoman::Options opts;
    {
        dragoman::World world(odmap, dragoman::Format::Unknown, opts);
        CHECK_EQ(world.provinceCount(), 6);
        CHECK_EQ(world.nationCount(), 4);
        CHECK_EQ(world.name(), std::string("Powder Keg Test"));
        CHECK(world.origin() == dragoman::Format::Odmap);
    }  /* <- here */
    CHECK(true);
}

static void testReportCopiesOutOfTheHandle() {
    const std::string odmap = fixture_writer::writeOdmap("cpp.odmap");
    REQUIRE(!odmap.empty());
    dragoman::Options opts;
    dragoman::Report loading;
    dragoman::World  world(odmap, dragoman::Format::Unknown, opts, &loading);

    /* The report is copied out of the C handle at construction, so it stays
     * readable after the call that produced it has freed the handle. */
    CHECK(!loading.entries().empty());
    CHECK(loading.ok());
    for (const auto& d : loading.entries()) CHECK(!d.code.empty());

    const dragoman::Report copied = loading;   /* the operation that crashed */
    CHECK_EQ(copied.entries().size(), loading.entries().size());
}

static void testConvertAndRoundTrip() {
    const std::string odmap = fixture_writer::writeOdmap("cpp.odmap");
    REQUIRE(!odmap.empty());
    const std::string gd5 = odmap + "-gd5";
    fs::remove_all(gd5);

    dragoman::Options opts;
    const dragoman::Report report = dragoman::convert(odmap, gd5, dragoman::Format::Gd5, opts);
    CHECK(report.ok());
    CHECK(fs::exists(gd5 + "/map_data.json"));

    CHECK(dragoman::roundtripCheck(odmap, dragoman::Format::Gd5, opts));
}

static void testFailureBecomesAnException() {
    dragoman::Options opts;
    bool threw = false;
    try {
        dragoman::World missing("/nonexistent/path/at/all", dragoman::Format::Unknown, opts);
    } catch (const dragoman::Error&) {
        threw = true;
    }
    CHECK(threw);
}

static void testVersionsMatchTheCAbi() {
    CHECK_EQ(dragoman::version(), std::string(dg_version_string()));
    CHECK_EQ(dragoman::abiVersion(), dg_abi_version());
    CHECK_EQ(dragoman::formatName(dragoman::Format::Gd5), std::string("gd5"));
}

int main() {
    testWorldSurvivesItsOwnDestructor();
    testReportCopiesOutOfTheHandle();
    testConvertAndRoundTrip();
    testFailureBecomesAnException();
    testVersionsMatchTheCAbi();
    return check::finish("test_cpp_wrapper");
}
