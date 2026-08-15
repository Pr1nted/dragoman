/* The half that knows about the library's internals. Kept apart from
 * test_cpp_wrapper.cpp on purpose -- see fixture_writer.h. */
#include "fixture_writer.h"

#include "Fixture.h"

namespace fixture_writer {

std::string writeOdmap(const std::string& name) {
    const std::string path = fixture::scratch(name);
    dragoman::World w = fixture::makeWorld();
    dragoman::Options opt;
    dragoman::Report report;
    if (!dragoman::writeOdMap(path, w, opt, report)) return std::string();
    return path;
}

}  // namespace fixture_writer
