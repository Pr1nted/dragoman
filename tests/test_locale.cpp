/* The library must not change its answers when the caller has a locale.
 *
 * WHY THIS FILE EXISTS. Everything here passed for months, and ten of Greater
 * Diplomacy 5's twelve base maps still could not be converted by anything
 * written in Python.
 *
 * A C++ program starts in the "C" locale and never leaves it unless it says
 * so, and <cctype>'s isalpha/toupper answer according to that locale. So the
 * command line tool -- and every test, which is also a C++ program -- saw
 * ASCII rules and was correct. Python calls setlocale(LC_ALL, "") at startup.
 * Under C.UTF-8 isalpha() accepts bytes above 0x7F and toupper() maps them to
 * other bytes above 0x7F, so walking a nation's name byte by byte to build an
 * ISO code produced three mangled UTF-8 continuation bytes, and the JSON
 * writer rejected them: "invalid UTF-8 byte at index 2".
 *
 * The test suite could not have caught it, because the test suite never set a
 * locale. So this file sets one, and every check below runs twice.
 */
#include <clocale>
#include <string>

#include "Check.h"
#include "Fixture.h"

using namespace dragoman;

/* Names with bytes above 0x7F, which is the whole point: these are ordinary
 * nations, not hostile input. GD5's own maps are full of them. */
static void testIsoCodesAreAscii() {
    const char* names[] = {
        "\xC3\x85land",                     /* Åland   */
        "\xC3\x96sterreich",                /* Österreich */
        "C\xC3\xB4te d'Ivoire",             /* Côte d'Ivoire */
        "T\xC3\xBCrkiye",                   /* Türkiye */
        "\xD0\xA0\xD0\xBE\xD1\x81\xD1\x81\xD0\xB8\xD1\x8F",  /* Россия */
    };
    for (const char* name : names) {
        const std::string iso = synthesiseIso(name, {});
        /* An ISO 3166 alpha-3 code is three ASCII letters. Nothing else is a
         * valid answer, and anything else is what broke the JSON writer. */
        CHECK(iso.size() == 3);
        for (char c : iso) {
            CHECK(static_cast<unsigned char>(c) < 0x80);
            CHECK(c >= 'A' && c <= 'Z');
        }
    }
}

/* A name that is entirely non-ASCII has no letters to take initials from, and
 * must still produce a usable code rather than an empty or malformed one. */
static void testAllNonAsciiStillYieldsACode() {
    const std::string iso = synthesiseIso("\xD0\xA0\xD0\xBE\xD1\x81\xD1\x81\xD0\xB8\xD1\x8F", {});
    CHECK(iso.size() == 3);
    for (char c : iso) CHECK(c >= 'A' && c <= 'Z');
}

/* Case-insensitive matching against the built-in table is the other place the
 * locale used to reach, and it must find the same row either way. */
static void testNameMatchingIsUnchanged() {
    CHECK(isoForName("United States of America") == isoForName("UNITED STATES OF AMERICA"));
    CHECK(!isoForName("Germany").empty());
}

/* Two nations that differ only above 0x7F must not collide onto one code by
 * having their non-ASCII bytes folded together. */
static void testDistinctNamesDoNotCollide() {
    std::vector<std::string> taken;
    const std::string a = synthesiseIso("\xC3\x85land Islands", taken);
    taken.push_back(a);
    const std::string b = synthesiseIso("\xC3\x96land Islands", taken);
    CHECK(a != b);
}

static void runAll() {
    testIsoCodesAreAscii();
    testAllNonAsciiStillYieldsACode();
    testNameMatchingIsUnchanged();
    testDistinctNamesDoNotCollide();
}

int main() {
    /* Once in the locale a C++ program starts in -- which is what every other
     * test here runs under, and what made this bug invisible. */
    runAll();

    /* And again in the caller's, which is what Python, and therefore Greater
     * Diplomacy 5, actually has. An empty string means "whatever the
     * environment says"; if the runner has no UTF-8 locale the call fails and
     * the second pass simply repeats the first, which is not a false pass --
     * the first pass still ran. */
    const char* got = std::setlocale(LC_ALL, "");
    if (got) std::printf("  note  second pass under locale \"%s\"\n", got);
    runAll();

    /* Named explicitly as well, so a runner whose environment is plain C is
     * still made to try a UTF-8 one. */
    for (const char* name : {"C.UTF-8", "en_US.UTF-8", "C.utf8"}) {
        if (std::setlocale(LC_ALL, name)) {
            std::printf("  note  third pass under locale \"%s\"\n", name);
            runAll();
            break;
        }
    }

    return check::finish("test_locale");
}
