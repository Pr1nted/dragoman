# open-dragoman for the JVM

Java and Kotlin, through the same flat C ABI the Python binding uses.

```java
Result r = Dragoman.convert(new File("world.odmap"), new File("out"), Format.GD5);
for (Note n : r.problems()) System.out.println(n);
```

```kotlin
val r = Dragoman.convert(File("world.odmap"), File("out"), Format.GD5)
r.problems().forEach(::println)
```

There is no Kotlin-specific module and none is needed: the Java API is shaped so
Kotlin reads naturally against it — enums rather than ints, a fluent `Options`,
and a `Result` that is a value rather than an out-parameter.

## Why JNA

| | floor | native code to build |
|---|---|---|
| **JNA** | Java 8 | none |
| Panama (`java.lang.foreign`) | Java 22 | none |
| JNI | Java 8 | a shim, per platform, in CI |

Panama is the better API and is final only in Java 22. Pinning the floor there
would exclude every Minecraft-era and Android-era JVM for no gain this binding
can use: the ABI is six ints and some opaque pointers, which JNA handles without
ceremony. JNI would mean maintaining and cross-compiling a C shim on top of the
library itself.

`Native.java` **is** the binding. There is no generated code.

## Finding the native library

JNA looks in `jna.library.path`, then the usual system directories, then the
classpath — a jar carrying `darwin-aarch64/libdragoman.dylib` and friends works
with nothing installed. To point at one file:

```
-Ddragoman.library.path=/path/to/libdragoman.dylib
```

**The ABI is checked once, at load.** A library built against a different ABI
would otherwise answer calls with fields in the wrong places and return
plausible nonsense; the load is refused instead.

## Two ownership rules the API hides

Both have a shape that hides the mistake, which is why they are handled in one
place rather than at each call site.

**A report must be freed.** `dg_convert` returns one whether it succeeded or
failed. `Dragoman.collect` reads it and frees it in a `finally`.

**Some strings are the caller's.** A `const char*` belongs to the library and
JNA may map it to `String`, which copies. A plain `char*` — `dg_report_to_json`,
`dg_world_to_json` — must be released with `dg_string_free`, and mapping one to
`String` leaks it silently with nothing to see at the call site. Those are
declared as `Pointer` for that reason alone.

## Zero is success

`dg_convert` returns 0 on success, the C convention. Reading it the other way
makes a finished map look like a failure — a bug this library's own Python
binding once shipped. `testAFailedConversionSaysWhy` fails if the polarity is
inverted; it was checked by inverting it.

## Getting it

`dragoman-jvm.jar` is attached to each release, with every platform's native
library inside it. Drop it on the classpath alongside
[JNA](https://github.com/java-native-access/jna) 5.14 and there is nothing to
install and nothing to build.

There is deliberately no Gradle or Maven build here. The binding is nine files
and one dependency, CI builds the jar with `javac`, and a build script that
nothing runs is a build script that rots. If you want it in a repository rather
than as a release asset, that is a publishing decision with credentials
attached, not a missing file.

## Running the tests

They need a real native library, so they are a `main()` with plain assertions
rather than a framework:

```
javac -cp jna.jar -d out $(find src -name '*.java')
java -cp out:jna.jar \
     -Ddragoman.library.path=/path/to/libdragoman.dylib \
     -Ddragoman.test.map=/path/to/world.odmap \
     com.opendoctrines.dragoman.DragomanTest
```

Without `dragoman.test.map` it runs everything that does not need a map and
**says which part it skipped**, rather than passing quietly.
