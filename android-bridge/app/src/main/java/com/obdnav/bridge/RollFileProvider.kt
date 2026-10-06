package com.obdnav.bridge

import android.content.ContentProvider
import android.content.ContentValues
import android.content.Context
import android.database.Cursor
import android.database.MatrixCursor
import android.net.Uri
import android.os.ParcelFileDescriptor
import android.provider.OpenableColumns
import java.io.File
import java.io.FileNotFoundException

/**
 * ROLL kayıtlarını ve sonuç CSV'lerini paylaşım için salt okunur sunar (androidx FileProvider yerine).
 * exported=false + grantUriPermissions: yalnız izin verilen URI'ye, yalnız filesDir/roll (sonuçlar: roll/res).
 */
class RollFileProvider : ContentProvider() {
    override fun onCreate() = true

    private fun fileFor(uri: Uri): File? {
        val name = uri.lastPathSegment ?: return null
        val ctx = context ?: return null
        val f = when {
            NAME.matches(name) -> File(Roll.dir(ctx), name)
            RES.matches(name) -> File(resDir(ctx), name)
            else -> return null
        }
        return if (f.isFile) f else null
    }

    override fun getType(uri: Uri) = "text/csv"

    override fun openFile(uri: Uri, mode: String): ParcelFileDescriptor {
        if (mode != "r") throw SecurityException("Salt okunur")
        val f = fileFor(uri) ?: throw FileNotFoundException(uri.toString())
        return ParcelFileDescriptor.open(f, ParcelFileDescriptor.MODE_READ_ONLY)
    }

    override fun query(uri: Uri, projection: Array<out String>?, selection: String?,
                       selectionArgs: Array<out String>?, sortOrder: String?): Cursor? {
        val f = fileFor(uri) ?: return null
        val cols = projection ?: arrayOf(OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE)
        return MatrixCursor(cols, 1).apply {
            addRow(cols.map {
                when (it) {
                    OpenableColumns.DISPLAY_NAME -> f.name
                    OpenableColumns.SIZE -> f.length()
                    else -> null
                }
            })
        }
    }

    override fun insert(uri: Uri, values: ContentValues?): Uri? = null
    override fun delete(uri: Uri, selection: String?, selectionArgs: Array<out String>?) = 0
    override fun update(uri: Uri, values: ContentValues?, selection: String?, selectionArgs: Array<out String>?) = 0

    companion object {
        private val NAME = Regex("roll_[0-9_]+\\.csv")
        private val RES = Regex("roll_[0-9_]+_sonuc\\.csv")

        /** Sonuç CSV'leri ayrı alt klasörde: kayıt listesine ve otomatik silmeye karışmaz. */
        fun resDir(ctx: Context) = File(Roll.dir(ctx), "res").apply { mkdirs() }

        fun resultsFile(ctx: Context, raw: File) = File(resDir(ctx), raw.name.removeSuffix(".csv") + "_sonuc.csv")

        fun uri(ctx: Context, f: File): Uri = Uri.parse("content://${ctx.packageName}.rollfiles/${f.name}")
    }
}
