// Where to find the library, and nothing more.
//
// This does NOT compile the C++ -- that is CMake's job, and duplicating it here
// would give two build systems that can disagree about what was built. It only
// tells the linker where to look:
//
//     DRAGOMAN_LIB_DIR=/path/to/build   cargo build
//
// Without it, the usual system paths are searched, which is right for a machine
// that has installed the library.
fn main() {
    if let Ok(dir) = std::env::var("DRAGOMAN_LIB_DIR") {
        println!("cargo:rustc-link-search=native={dir}");
        println!("cargo:rerun-if-env-changed=DRAGOMAN_LIB_DIR");
    }
    println!("cargo:rustc-link-lib=dylib=dragoman");
}
