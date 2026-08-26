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
                            val destFile = File(targetDir, entry.name)

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
                            if (extractedCount % 25 == 0) {
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

        val hasValidEntrypoint = possibleInitBinaries.any { it.exists() || isSymlink(it) }

        if (!hasValidEntrypoint) {
            throw IllegalStateException("Invalid RootFS: missing init or sh entrypoint binary")
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

    private fun isSymlink(file: File): Boolean {
        return try {
            val stat = android.system.Os.lstat(file.absolutePath)
            android.system.OsConstants.S_ISLNK(stat.st_mode)
        } catch (e: Exception) {
            false
        }
    }
}
