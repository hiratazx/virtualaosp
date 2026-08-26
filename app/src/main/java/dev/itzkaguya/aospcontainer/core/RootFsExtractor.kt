package dev.itzkaguya.aospcontainer.core

import android.content.Context
import com.google.gson.Gson
import dev.itzkaguya.aospcontainer.model.ContainerManifest
import dev.itzkaguya.aospcontainer.util.NativeFileUtils
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.flowOn
import org.apache.commons.compress.archivers.tar.TarArchiveEntry
import org.apache.commons.compress.archivers.tar.TarArchiveInputStream
import org.apache.commons.compress.archivers.tar.TarConstants
import org.tukaani.xz.XZInputStream
import java.io.BufferedInputStream
import java.io.File
import java.io.FileOutputStream
import java.io.InputStream

sealed class ExtractionState {
    data class Progress(val percentage: Int, val currentFile: String) : ExtractionState()
    data class Completed(val destinationDir: File, val manifest: ContainerManifest) : ExtractionState()
    data class Error(val throwable: Throwable) : ExtractionState()
}

class RootFsExtractor(private val context: Context) {

    private val gson = Gson()

    fun extractRootFs(
        inputStream: InputStream,
        targetDir: File = File(context.filesDir, "rootfs")
    ): Flow<ExtractionState> = flow {
        if (!targetDir.exists()) {
            targetDir.mkdirs()
        }

        var manifest: ContainerManifest? = null

        try {
            BufferedInputStream(inputStream).use { bufferedIn ->
                XZInputStream(bufferedIn).use { xzIn ->
                    TarArchiveInputStream(xzIn).use { tarIn ->
                        var entry: TarArchiveEntry? = tarIn.nextTarEntry
                        var extractedCount = 0

                        while (entry != null) {
                            // Normalize archive paths: strip leading "./"
                            // and "/" so entries like "./system/bin/init" or
                            // "/system/bin/init" land under targetDir
                            // consistently instead of relying on File's
                            // join semantics for absolute children.
                            val entryName = entry.name.removePrefix("./").removePrefix("/")
                            val destFile = File(targetDir, entryName)

                            // Prevent Zip-Slip directory traversal
                            val canonicalDest = destFile.canonicalPath
                            if (!canonicalDest.startsWith(targetDir.canonicalPath)) {
                                throw SecurityException("Archive entry attempted directory traversal: ${entry.name}")
                            }

                            val isDir = entry.isDirectory ||
                                    entry.name.endsWith("/") ||
                                    entry.linkFlag == TarConstants.LF_DIR

                            if (isDir) {
                                destFile.mkdirs()
                                NativeFileUtils.setPosixPermissions(destFile, entry.mode)
                            } else if (entry.isSymbolicLink) {
                                destFile.parentFile?.mkdirs()
                                NativeFileUtils.createSymlink(entry.linkName, destFile.absolutePath)
                            } else {
                                if (destFile.exists() && destFile.isDirectory) {
                                    NativeFileUtils.setPosixPermissions(destFile, entry.mode)
                                } else {
                                    destFile.parentFile?.mkdirs()
                                    FileOutputStream(destFile).use { out ->
                                        tarIn.copyTo(out, bufferSize = 64 * 1024)
                                    }
                                    NativeFileUtils.setPosixPermissions(destFile, entry.mode)

                                    if (destFile.name == "manifest.json" && destFile.parentFile == targetDir) {
                                        val jsonContent = destFile.readText()
                                        manifest = gson.fromJson(jsonContent, ContainerManifest::class.java)
                                    }
                                }
                            }

                            extractedCount++
                            if (extractedCount % 75 == 0) {
                                emit(ExtractionState.Progress(-1, entry.name))
                            }

                            entry = tarIn.nextTarEntry
                        }
                    }
                }
            }

            // Post-extraction verification and manifest standardization
            val finalManifest = manifest?.let { parsed ->
                if (parsed.id.isNullOrBlank()) {
                    parsed.copy(id = "aosp_container_guest")
                } else {
                    parsed
                }
            } ?: verifyAndGenerateFallbackManifest(targetDir)

            emit(ExtractionState.Completed(targetDir, finalManifest))

        } catch (e: Throwable) {
            emit(ExtractionState.Error(e))
        }
    }.flowOn(Dispatchers.IO)

    private fun verifyAndGenerateFallbackManifest(rootFsDir: File): ContainerManifest {
        val possibleInitBinaries = listOf(
            File(rootFsDir, "init"),
            File(rootFsDir, "system/bin/init"),
            File(rootFsDir, "bin/init"),
            File(rootFsDir, "system/bin/sh"),
            File(rootFsDir, "bin/sh")
        )

        val hasValidEntrypoint = possibleInitBinaries.any { nodeExists(it) }

        if (!hasValidEntrypoint) {
            throw IllegalStateException("Invalid RootFS: missing init or sh entrypoint binary")
        }

        if (!hasLinker(rootFsDir)) {
            throw IllegalStateException(
                "Invalid RootFS: missing dynamic linker64 " +
                    "(checked system/bin, bin and apex/com.android.runtime)",
            )
        }

        return ContainerManifest(
            id = "aosp_container_guest",
            name = "Generic AOSP RootFS",
            version = "1.0.0",
            arch = "arm64-v8a",
            androidApi = 34,
            minAppVersion = 1
        )
    }

    /**
     * Modern AOSP images ship the dynamic linker inside the runtime APEX
     * rather than (only) at system/bin/linker64. Accept every known
     * layout; Os.lstat lets valid symlinks into the APEX runtime pass
     * even when their target resolves outside app-readable paths.
     */
    private fun hasLinker(targetDir: File): Boolean {
        val candidates = listOf(
            "system/bin/linker64",
            "bin/linker64",
            "apex/com.android.runtime/bin/linker64",
            "system/apex/com.android.runtime/bin/linker64",
            "system_ext/apex/com.android.runtime/bin/linker64"
        )
        return candidates.any { path ->
            val file = File(targetDir, path)
            file.exists() || try {
                val stat = android.system.Os.lstat(file.absolutePath)
                android.system.OsConstants.S_ISREG(stat.st_mode) ||
                        android.system.OsConstants.S_ISLNK(stat.st_mode)
            } catch (e: Exception) {
                false
            }
        }
    }

    /**
     * Node-level existence probe via Os.lstat: succeeds for regular files
     * AND symlinks (even dangling ones whose target is not extracted
     * yet), whereas File.exists() follows the link and can report false
     * under host SELinux restrictions or broken link targets.
     */
    private fun nodeExists(file: File): Boolean {
        return try {
            android.system.Os.lstat(file.absolutePath)
            true
        } catch (e: Exception) {
            false
        }
    }

    private fun isSymlink(file: File): Boolean {
        return try {
            val stat = android.system.Os.lstat(file.absolutePath)
            android.system.OsConstants.S_ISLNK(stat.st_mode)
        } catch (e: Exception) {
            false
        }
    }
}
