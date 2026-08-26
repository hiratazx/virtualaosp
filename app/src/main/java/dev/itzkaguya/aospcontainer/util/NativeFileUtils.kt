package dev.itzkaguya.aospcontainer.util

import android.system.ErrnoException
import android.system.Os
import android.system.OsConstants
import java.io.File

object NativeFileUtils {
    fun setPosixPermissions(file: File, mode: Int) {
        try {
            Os.chmod(file.absolutePath, mode and 0xFFF)
        } catch (e: ErrnoException) {
            file.setReadable(true, false)
            file.setWritable(true, true)
            if ((mode and 0b001_000_000) != 0) file.setExecutable(true, false)
        }
    }

    fun createSymlink(targetPath: String, linkPath: String) {
        val linkFile = File(linkPath)
        if (linkFile.exists() || isSymlink(linkFile)) linkFile.delete()
        try {
            Os.symlink(targetPath, linkPath)
        } catch (e: ErrnoException) {
            e.printStackTrace()
        }
    }

    private fun isSymlink(file: File): Boolean = try {
        OsConstants.S_ISLNK(Os.lstat(file.absolutePath).st_mode)
    } catch (e: Exception) { false }
}
