# A consumer, deliberately standing outside the project.
#
# Not part of the main build. It is configured separately against an *installed*
# Dragoman, through find_package, exactly as somebody else's project would be:
#
#     cmake --install build --prefix /tmp/dgi
#     cmake -S bindings/cpp -B /tmp/consumer -DCMAKE_PREFIX_PATH=/tmp/dgi
#     cmake --build /tmp/consumer
#
# CI runs those three lines. It is the only thing here that catches a fault in
# how the library is *installed* rather than how it is built -- a missing
# package config, headers that are not where the export says, a static archive
# that does not carry its own third-party objects. It is also the only thing
# that reproduced the namespace collision between this header's types and the
# library's internal ones, which every in-tree test passed straight through.
cmake_minimum_required(VERSION 3.16)
project(dragoman_cpp_example LANGUAGES CXX)

find_package(dragoman REQUIRED)

add_executable(dragoman_cpp_example example.cpp)
target_link_libraries(dragoman_cpp_example PRIVATE dragoman::dragoman)
