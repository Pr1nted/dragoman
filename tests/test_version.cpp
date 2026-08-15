/* The version lives in four places -- VERSION, the C header, the Python
 * package and the CMake project -- and a release that updates three of them
 * ships a library that lies about what it is. This test is the thing that
 * stops that, which is why it reads the files rather than trusting constants.
 */
#include <dragoman/dragoman.h>

#include <fstream>
#include <sstream>
#include <string>

#include "Check.h"
#include "Support.h"

using namespace dragoman;

static std::string readTextFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return std::string();
    std::ostringstream os;
    os << f.rdbuf();
    return trim(os.str());
}

static void testVersionFileMatchesHeader() {
    const std::string fromFile = readTextFile(std::string(DRAGOMAN_SOURCE_DIR) + "/VERSION");
    REQUIRE(!fromFile.empty());
    CHECK_EQ(fromFile, std::string(DRAGOMAN_VERSION_STRING));
    CHECK_EQ(std::string(dg_version_string()), fromFile);

    /* And the string agrees with the three integers a caller might branch on. */
    const std::string composed = std::to_string(dg_version_major()) + "."
                                 + std::to_string(dg_version_minor()) + "."
                                 + std::to_string(dg_version_patch());
    CHECK_EQ(composed, std::string(DRAGOMAN_VERSION_STRING));
}

static void testPythonPackageMatches() {
    const std::string py = readTextFile(std::string(DRAGOMAN_SOURCE_DIR)
                                        + "/bindings/python/dragoman/_version.py");
    REQUIRE(!py.empty());
    /* The file is one line: __version__ = "0.1.0" */
    const size_t open = py.find('"');
    const size_t close = py.rfind('"');
    REQUIRE(open != std::string::npos && close > open);
    CHECK_EQ(py.substr(open + 1, close - open - 1), std::string(DRAGOMAN_VERSION_STRING));
}

/* The ABI version is deliberately not the library version. It moves only when
 * an existing symbol changes meaning, so a binding can refuse to load a
 * library it cannot speak to without parsing semver. */
static void testAbiVersionIsIndependent() {
    CHECK_EQ(dg_abi_version(), DRAGOMAN_ABI_VERSION);
    CHECK(dg_abi_version() >= 1);
}

int main() {
    testVersionFileMatchesHeader();
    testPythonPackageMatches();
    testAbiVersionIsIndependent();
    return check::finish("test_version");
}
