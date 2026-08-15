/* Writes the test fixture map, without exposing any of the library's internal
 * types to the caller.
 *
 * test_cpp_wrapper.cpp has to look exactly like somebody else's project: the
 * public headers and nothing else. It cannot include Fixture.h, because that
 * reaches Formats.h, whose dragoman::Options, Report, Diagnostic and World are
 * different types with the same names as the wrapper's -- which is the whole
 * bug that test is guarding against, and including both makes every one of
 * those names ambiguous. So the fixture is built in its own translation unit
 * and handed back as a path.
 */
#ifndef DRAGOMAN_TEST_FIXTURE_WRITER_H
#define DRAGOMAN_TEST_FIXTURE_WRITER_H

#include <string>

namespace fixture_writer {

/* Writes the shared fixture world to a scratch .odmap and returns its path,
 * or an empty string if it could not be written. */
std::string writeOdmap(const std::string& name);

}  // namespace fixture_writer

#endif
