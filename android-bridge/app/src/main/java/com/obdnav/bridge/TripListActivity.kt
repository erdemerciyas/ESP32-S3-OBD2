package com.obdnav.bridge

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import android.view.View
import android.widget.LinearLayout
import android.widget.ScrollView
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/** Kayıtlı sürüşler (en yenisi üstte); süren sürüş varsa en başta. */
class TripListActivity : Activity() {
    private lateinit var list: LinearLayout

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Ui.applyWindow(this)
        val root = Ui.column(this)
        root.addView(Ui.topBar(this, "Sürüş geçmişi"))
        list = Ui.column(this, 16)
        root.addView(ScrollView(this).apply { addView(list) })
        setContentView(root)
    }

    override fun onResume() {
        super.onResume()
        list.removeAllViews()
        val df = SimpleDateFormat("d MMMM yyyy, HH:mm", Locale("tr"))

        TripRecorder.current?.let {
            list.addView(item("Şu anki sürüş", "%.1f km · %d dk · kayıt sürüyor".format(it.distM / 1000, it.durationS / 60),
                Ui.OK, null), Ui.lp(this, bottomDp = 10))
        }
        val files = TripRecorder.list(this)
        for (f in files) {
            val t = TripRecorder.load(f) ?: continue
            val src = t.src.replaceFirstChar { c -> c.uppercase() }
            list.addView(item(df.format(Date(t.start)),
                "%.1f km · %d dk · %s · %d uyarı".format(t.distM / 1000, t.durationS / 60, src, t.alerts.size),
                Ui.PRIMARY, f), Ui.lp(this, bottomDp = 10))
        }
        if (TripRecorder.current == null && files.isEmpty()) {
            list.addView(Ui.card(this).apply {
                addView(Ui.text(context, "Henüz kayıtlı sürüş yok", 17f, Ui.TEXT, true))
                addView(Ui.text(context, "Navigasyonda rota başlattığınızda sürüş otomatik kaydedilir.", 14f, Ui.DIM)
                    .apply { setPadding(0, Ui.dp(context, 4), 0, 0) })
            })
        }
    }

    private fun item(title: String, sub: String, accent: Int, f: File?): View = Ui.card(this).apply {
        addView(Ui.row(context).apply {
            addView(Ui.dot(context, accent))
            addView(Ui.column(context).apply {
                addView(Ui.text(context, title, 16f, Ui.TEXT, true))
                addView(Ui.text(context, sub, 13f, Ui.DIM).apply { setPadding(0, Ui.dp(context, 2), 0, 0) })
            }, Ui.lp(context, 0, weight = 1f))
            addView(Ui.text(context, "›", 26f, Ui.DIM))
        })
        isClickable = true
        setOnClickListener {
            startActivity(Intent(context, TripViewActivity::class.java).apply {
                if (f != null) putExtra(TripViewActivity.EXTRA_FILE, f.path)
            })
        }
    }
}
