package com.obdnav.bridge

import android.util.Log
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/** Ekranda gösterilen kısa olay günlüğü (son 60 satır). */
object BridgeLog {
    private val lines = ArrayDeque<String>()
    private val fmt = SimpleDateFormat("HH:mm:ss", Locale.US)

    @Volatile
    var listener: (() -> Unit)? = null

    @Synchronized
    fun add(msg: String) {
        Log.i("OBDNav", msg)
        lines.addLast("${fmt.format(Date())}  $msg")
        while (lines.size > 60) lines.removeFirst()
        listener?.invoke()
    }

    @Synchronized
    fun text(): String = lines.reversed().joinToString("\n")
}
