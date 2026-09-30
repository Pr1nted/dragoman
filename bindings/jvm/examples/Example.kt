// The same binding from Kotlin. Nothing Kotlin-specific is needed: the Java API
// is built so that idiomatic Kotlin reads naturally against it -- enums, a
// fluent Options, and a Result that is a value rather than an out-parameter.
package com.opendoctrines.dragoman.example

import com.opendoctrines.dragoman.Dragoman
import com.opendoctrines.dragoman.Format
import com.opendoctrines.dragoman.Options
import com.opendoctrines.dragoman.Severity
import java.io.File

fun main(args: Array<String>) {
    println("open-dragoman ${Dragoman.version()} (abi ${Dragoman.abiVersion()})")

    if (args.isEmpty()) {
        println("usage: Example <map> [out]")
        return
    }

    val map = File(args[0])
    val from = Dragoman.detect(map)
    println("$map is $from")

    val to = if (from == Format.ODMAP) Format.GD5 else Format.ODMAP
    val out = File(if (args.size > 1) args[1] else "${map.path}.converted")

    val options = Options.defaults().strict(false).translateScripts(true)
    val result = Dragoman.convert(map, out, to, options)

    println("converted: ${result.ok()}, worst ${result.worst()}")
    result.problems()
        .filter { it.severity() == Severity.WARNING }
        .take(3)
        .forEach { println("  ${it.code()}: ${it.message().take(80)}") }
}
