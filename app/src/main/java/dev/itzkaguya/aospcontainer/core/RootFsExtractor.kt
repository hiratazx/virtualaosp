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
        if (!targetDir.exists()) targetDir.mkdirs()
        var manifest: ContainerManifest? = null

        try {
            BufferedInputStream(inputStream).use { bufferedIn ->
                XZInputStream(bufferedIn).use { xzIn ->
                    TarArchiveInputStream(xzIn).use { tarIn ->
                        var entry: TarArchiveEntry? = tarIn.nextTarEntry
                        var extractedCount = 0

                        while (entry != null) {
                            val destFile = File(targetDir, entry.name)
                            if (!destFile.canonicalPath.startsWith(targetDir.canonicalPath)) {
                                throw SecurityException("Directory traversal in archive: ${entry.name}")
                            }

                            // Flattened APEX archives (e.g.
                            // com.android.ondevicepersonalization/) often
                            // carry directory entries whose typeflag is not
                            // LF_DIR but whose name ends in '/'; some also
                            // collide with directories created implicitly by
                            // earlier parents. Detect all three shapes and
                            // never open a FileOutputStream on a directory,
                            // which surfaces as EISDIR.
                            val isDirectory = entry.isDirectory ||
                                entry.name.endsWith("/") ||
                                entry.linkFlag == TarConstants.LF_DIR ||
                                destFile.isDirectory

                            if (isDirectory) {
                                destFile.mkdirs()
                                NativeFileUtils.setPosixPermissions(destFile, entry.mode)
                            } else if (entry.isSymbolicLink) {
                                NativeFileUtils.createSymlink(entry.linkName, destFile.absolutePath)
                            } else {
                                destFile.parentFile?.mkdirs()
                                FileOutputStream(destFile).use { out ->
                                    tarIn.copyTo(out, 64 * 1024)
                                }
                                NativeFileUtils.setPosixPermissions(destFile, entry.mode)
                                if (destFile.name == "manifest.json" && destFile.parentFile == targetDir) {
                                    manifest = gson.fromJson(destFile.readText(), ContainerManifest::class.java)
                                }
                            }
                            extractedCount++
                            if (extractedCount % 20 == 0) emit(ExtractionState.Progress(-1, entry.name))
                            entry = tarIn.nextTarEntry
                        }
                    }
                }
            }
            emit(ExtractionState.Completed(targetDir, manifest ?: verifyFallback(targetDir)))
        } catch (t: Throwable) {
            emit(ExtractionState.Error(t))
        }
    }.flowOn(Dispatchers.IO)

    private fun verifyFallback(dir: File): ContainerManifest {
        if (!File(dir, "system").exists() && !File(dir, "bin").exists()) {
            throw IllegalStateException("Invalid RootFS structure: missing /system and /bin")
        }
        return ContainerManifest("AOSP RootFS", null, "1.0", "arm64-v8a", 34, 1)
    }
}
