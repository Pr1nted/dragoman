# One executable per concern, each registered with CTest. No test framework:
# the suite is small, the assertions are plain, and a dependency here would be
# one more thing a contributor has to install before `ctest` works.

set(DRAGOMAN_TEST_DATA "${CMAKE_CURRENT_SOURCE_DIR}/data")

function(dragoman_test name)
    add_executable(${name} ${name}.cpp ${ARGN})
    target_link_libraries(${name} PRIVATE dragoman dragoman_thirdparty_headers)
    target_include_directories(${name} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src")
    target_compile_definitions(${name} PRIVATE
        DRAGOMAN_TEST_DATA="${DRAGOMAN_TEST_DATA}"
        DRAGOMAN_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        DRAGOMAN_BINARY_DIR="${CMAKE_BINARY_DIR}")
    add_test(NAME ${name} COMMAND ${name})
endfunction()

dragoman_test(test_version)
dragoman_test(test_raster)
dragoman_test(test_scripts)
dragoman_test(test_locale)
dragoman_test(test_research)
dragoman_test(test_roundtrip)
dragoman_test(test_forts)
dragoman_test(test_politics)
dragoman_test(test_abi)
dragoman_test(test_cpp_wrapper fixture_writer.cpp)

# The real maps are not in this repository -- Open Doctrines' are its own and
# Greater Diplomacy 5's are GPL -- so the conformance suite runs only when it
# is pointed at a local copy of each. CI sets neither and skips both; a
# developer who has the games sets them and gets the stronger check.
if(DEFINED ENV{DRAGOMAN_OD_MAP} OR DEFINED ENV{DRAGOMAN_GD5_MAP})
    set_tests_properties(test_roundtrip PROPERTIES
        ENVIRONMENT "DRAGOMAN_OD_MAP=$ENV{DRAGOMAN_OD_MAP};DRAGOMAN_GD5_MAP=$ENV{DRAGOMAN_GD5_MAP}")
endif()
