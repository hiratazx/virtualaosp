package dev.itzkaguya.aospcontainer.rootfs

import android.content.Context
import android.os.Build
import android.util.Log
import dev.itzkaguya.aospcontainer.core.ContainerCore
import org.tukaani.xz.XZInputStream
import java.io.File
import java.io.FileOutputStream
import java.io.InputStream
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream

/**
 * Imports container rootfs archives (.tar.xz or .zip) into app-private
 * storage with path-traversal protection, manifest validation, and
 * optional interpreter migration.
 *
 * Layout: <filesDir>/rootfs/<manifest.id>/
 */
object RootfsImporter {

    private const val TAG = "ac.import"
    private const val BLOCK = 512

    fun interface ProgressListener {
        /** [totalBytes] is -1 for streaming formats where size is unknown. */
        fun onProgress(extractedFiles: Int, totalBytes: Long, currentFile: String)
    }

    fun import(
        context: Context,
        archive: InputStream,
        progress: ProgressListener? = null,
    ): RootfsManifest {
        val stagingRoot = File(context.filesDir, "rootfs")
        stagingRoot.mkdirs()
        val staging = File(stagingRoot, ".staging-${System.currentTimeMillis()}")
        try {
            val (format, stream) = detectFormat(archive.buffered())
            val manifestJson = when (format) {
                Format.TAR_XZ -> extractTarXz(stream, staging, progress)
                Format.ZIP -> extractZip(stream, staging, progress)
            } ?: throw ImportException("archive does not contain a manifest.json")

            val manifest = RootfsManifest.parse(manifestJson)
            validateAbi(manifest)
            validateLayout(staging, manifest)

            if (manifest.patchInterp) {
                migrateInterpreters(staging, manifest)
            }

            val target = File(stagingRoot, manifest.id)
            if (target.exists()) target.deleteRecursively()
            if (!staging.renameTo(target)) {
                throw ImportException("failed to finalize import directory")
            }
            Log.i(TAG, "imported '${manifest.name}' -> ${target.path}")
            return manifest
        } finally {
            staging.deleteRecursively()
        }
    }

    /* ---------------------------------------------------------------- */
    /* formats                                                          */
    /* ---------------------------------------------------------------- */

    private enum class Format { TAR_XZ, ZIP }
    private const val XZ_MAGIC_0_5 = "FD377A585A00"
    private const val ZIP_MAGIC_PREFIX = "504B"

    /** Sniffs the container format and returns a stream positioned at 0. */
    private fun detectFormat(src: InputStream): Pair<Format, InputStream> {
        val pushback = java.io.PushbackInputStream(src, 6)
        val header = ByteArray(6)
        var off = 0
        while (off < header.size) {
            val n = pushback.read(header, off, header.size - off)
            if (n <= 0) throw ImportException("archive too small to be a rootfs image")
            off += n
        }
        pushback.unread(header)
        val hex = header.joinToString("") { "%02X".format(it) }
        return when {
            hex == XZ_MAGIC_0_5 -> Format.TAR_XZ to pushback
            hex.startsWith(ZIP_MAGIC_PREFIX) -> Format.ZIP to pushback
            else -> throw ImportException(
                "unsupported archive format (expected .tar.xz or .zip)",
            )
        }
    }

    private fun extractTarXz(src: InputStream, destDir: File, progress: ProgressListener?): String? =
        XZInputStream(src).use { extractTar(it, destDir, progress) }

    /* ---------------------------------------------------------------- */
    /* tar                                                              */
    /* ---------------------------------------------------------------- */

    /**
     * Minimal USTAR/GNU tar reader. Returns manifest.json content when a
     * regular file entry named (anywhere as) manifest.json is present.
     */
    private fun extractTar(input: InputStream, destDir: File, progress: ProgressListener?): String? {
        var manifest: String? = null
        var files = 0
        var pendingLongName: String? = null

        while (true) {
            val block = ByteArray(BLOCK)
            if (!readFully(input, block)) break /* EOF */
            if (block.all { it == 0.toByte() }) break /* end-of-archive marker */

            fun ascii(off: Int, len: Int): String =
                String(block, off, len, Charsets.US_ASCII).trimEnd('\u0000', ' ')
            fun octal(off: Int, len: Int): Long =
                ascii(off, len).toLongOrNull(8) ?: 0L

            var name = ascii(0, 100)
            val size = octal(124, 12)
            val typeFlag = block[156].toInt() and 0xFF

            when (typeFlag) {
                'L'.code -> { /* GNU long name: next real entry uses it */
                    val buf = ByteArray(size.toInt().coerceAtMost(1 shl 20))
                    readFully(input, buf)
                    skipPadding(input, size)
                    pendingLongName = buf.toString(Charsets.UTF_8).trimEnd('\u0000')
                    continue
                }
                'x'.code, 'g'.code -> { /* PAX metadata: skipped */
                    copyAndDiscard(input, size)
                    skipPadding(input, size)
                    continue
                }
            }

            pendingLongName?.let {
                name = it
                pendingLongName = null
            }

            val safeRel = sanitizePath(name)
            if (safeRel == null) {
                Log.w(TAG, "skipping unsafe tar entry '$name'")
                copyAndDiscard(input, size)
                skipPadding(input, size)
                continue
            }

            when (typeFlag) {
                '5'.code -> File(destDir, safeRel).mkdirs()
                '0'.code, '7'.code -> {
                    val outFile = File(destDir, safeRel)
                    outFile.parentFile?.mkdirs()
                    FileOutputStream(outFile).use { out ->
                        copyExactly(input, out, size)
                    }
                    skipPadding(input, size)
                    files++
                    progress?.onProgress(files, -1L, safeRel)
                    if (safeRel == "manifest.json") {
                        manifest = outFile.readText()
                    } else if (manifest == null && safeRel.endsWith("/manifest.json")) {
                        manifest = outFile.readText()
                    }
                }
                else -> {
                    /* symlinks/devices/fifos carry no payload here; their
                     * metadata is not replicated into the container */
                    copyAndDiscard(input, size)
                    skipPadding(input, size)
                    if (size > 0 && typeFlag == '2'.code) {
                        Log.d(TAG, "skipped symlink $name (recreated by ROM helpers)")
                    }
                }
            }
        }
        return manifest
    }

    /** Fills [buf] completely; false means truncated/corrupt archive. */
    private fun readFully(input: InputStream, buf: ByteArray): Boolean {
        var off = 0
        while (off < buf.size) {
            val n = input.read(buf, off, buf.size - off)
            if (n <= 0) return false
            off += n
        }
        return true
    }

    private fun skipPadding(input: InputStream, entrySize: Long) {
        val remainder = (entrySize % BLOCK).let { if (it == 0L) 0L else BLOCK - it }
        var left = remainder
        while (left > 0) {
            if (input.read() < 0) break
            left--
        }
    }

    private fun copyExactly(input: InputStream, out: FileOutputStream, size: Long) {
        val buf = ByteArray(64 * 1024)
        var remaining = size
        while (remaining > 0) {
            val want = minOf(buf.size.toLong(), remaining).toInt()
            val n = input.read(buf, 0, want)
            if (n <= 0) throw ImportException("unexpected EOF inside tar entry")
            out.write(buf, 0, n)
            remaining -= n
        }
    }

    private fun copyAndDiscard(input: InputStream, size: Long) {
        val buf = ByteArray(64 * 1024)
        var remaining = size
        while (remaining > 0) {
            val want = minOf(buf.size.toLong(), remaining).toInt()
            val n = input.read(buf, 0, want)
            if (n <= 0) break
            remaining -= n
        }
    }

    /* ---------------------------------------------------------------- */
    /* zip                                                              */
    /* ---------------------------------------------------------------- */

    private fun extractZip(src: InputStream, destDir: File, progress: ProgressListener?): String? {
        var manifest: String? = null
        var files = 0
        ZipInputStream(src).use { zip ->
            var e: ZipEntry? = zip.nextEntry
            while (e != null) {
                val safeRel = sanitizePath(e.name)
                if (safeRel == null) {
                    Log.w(TAG, "skipping unsafe zip entry '${e.name}'")
                } else if (e.isDirectory) {
                    File(destDir, safeRel).mkdirs()
                } else {
                    val outFile = File(destDir, safeRel)
                    outFile.parentFile?.mkdirs()
                    FileOutputStream(outFile).use { zip.copyTo(it) }
                    if (safeRel == "manifest.json" ||
                        (manifest == null && safeRel.endsWith("/manifest.json"))) {
                        manifest = outFile.readText()
                    }
                    files++
                    progress?.onProgress(files, e.size, safeRel)
                }
                e = zip.nextEntry
            }
        }
        return manifest
    }

    /* ---------------------------------------------------------------- */
    /* safety + validation                                              */
    /* ---------------------------------------------------------------- */

    internal fun sanitizePath(raw: String): String? {
        if (raw.startsWith("/")) return null
        if (raw.contains('\u0000')) return null
        val parts = raw.split('/').filter { it.isNotEmpty() && it != "." }
        if (parts.isEmpty() || parts.any { it == ".." }) return null
        return parts.joinToString("/")
    }

    private fun validateAbi(manifest: RootfsManifest) {
        if (manifest.arch !in Build.SUPPORTED_ABIS.toSet()) {
            throw ImportException(
                "ROM arch '${manifest.arch}' incompatible with device ABIs " +
                    Build.SUPPORTED_ABIS.contentToString(),
            )
        }
    }

    private fun validateLayout(rootDir: File, manifest: RootfsManifest) {
        val init = findUnder(rootDir, manifest.initPath.trimStart('/'))
            ?: throw ImportException("init binary ${manifest.initPath} missing from archive")
        findUnder(rootDir, "system/bin/linker64")
            ?: throw ImportException("system/bin/linker64 missing from archive")

        val interp = ContainerCore.nativeReadInterp(init.absolutePath)
        if (interp.isEmpty()) {
            Log.w(TAG, "init carries no PT_INTERP (static build?)")
        } else {
            Log.i(TAG, "init interp: $interp")
        }
    }

    /**
     * Rewrites PT_INTERP of dynamic executables under the standard bin
     * directories to the sandbox-absolute linker path. Entries whose
     * .interp segment cannot fit the replacement are reported (-ENOSPC)
     * and left untouched so ROM builders get actionable feedback.
     */
    private fun migrateInterpreters(rootDir: File, manifest: RootfsManifest) {
        val newInterp = rootDir.absolutePath + "/system/bin/linker64"
        val targets = sequenceOf(
            manifest.initPath.trimStart('/'),
            "bin", "sbin", "system/bin", "system/xbin", "vendor/bin",
        ).mapNotNull { findUnder(rootDir, it) }

        var patched = 0
        var failed = 0
        for (target in targets) {
            val candidates = if (target.isDirectory) {
                target.listFiles { f -> f.isFile }?.asList().orEmpty()
            } else {
                listOf(target)
            }
            for (exe in candidates) {
                val current = ContainerCore.nativeReadInterp(exe.absolutePath)
                if (current.isEmpty() || current == newInterp) continue
                when (ContainerCore.nativePatchInterp(exe.absolutePath, newInterp)) {
                    0 -> patched++
                    else -> failed++
                }
            }
        }
        Log.i(TAG, "interp migration: $patched patched, $failed need shorter paths")
    }

    private fun findUnder(root: File, relative: String): File? {
        File(root, relative).takeIf { it.exists() }?.let { return it }
        /* tolerate archives wrapped in one top-level folder */
        val single = root.listFiles()?.singleOrNull { it.isDirectory }
        if (single != null) File(single, relative).takeIf { it.exists() }?.let { return it }
        return null
    }
}
