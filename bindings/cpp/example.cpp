/* The same conversion in C++, through the header-only RAII wrapper.
 *
 *     c++ -std=c++17 example.cpp -I../../include -L../../build -ldragoman \
 *         -Wl,-rpath,"$PWD/../../build" -o example
 *     ./example 1914.odmap base_maps/1914
 *
 * The wrapper owns the handles and turns failures into exceptions; it adds no
 * behaviour of its own, so everything here a C caller can do too.
 */
#include <dragoman/dragoman.hpp>

#include <iostream>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: " << argv[0] << " <input map> <output map>\n";
        return 2;
    }

    try {
        std::cout << "dragoman " << dragoman::version()
                  << " (abi " << dragoman::abiVersion() << ")\n";

        const auto from = dragoman::detect(argv[1]);
        if (from == dragoman::Format::Unknown) {
            std::cerr << argv[1] << " is neither an .odmap nor a GD5 map directory\n";
            return 1;
        }
        const auto to = from == dragoman::Format::Odmap ? dragoman::Format::Gd5
                                                        : dragoman::Format::Odmap;

        dragoman::Options opts;
        dragoman::Report  loading;
        dragoman::World   world(argv[1], from, opts, &loading);

        std::cout << world.name() << ": " << world.provinceCount() << " provinces, "
                  << world.nationCount() << " nations\n";

        const auto report = world.save(argv[2], to, opts);
        for (const auto& d : report.warnings()) {
            std::cerr << "  warning " << d.code << ": " << d.message << "\n";
        }

        /* Worth asserting before trusting the result: convert it back and check
         * that nothing was lost on the way. */
        if (dragoman::roundtripCheck(argv[1], to, opts)) {
            std::cout << "round trip preserves this map\n";
        } else {
            std::cout << "round trip loses something -- see the report\n";
        }

        std::cout << "wrote " << argv[2] << "\n";
        return 0;
    } catch (const dragoman::Error& e) {
        std::cerr << "dragoman: " << e.what() << "\n";
        return 1;
    }
}
