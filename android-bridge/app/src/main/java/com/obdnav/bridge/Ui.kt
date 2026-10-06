package com.obdnav.bridge

import android.app.Activity
import android.content.Context
import android.content.res.ColorStateList
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.RippleDrawable
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.LinearLayout
import android.widget.TextView

/**
 * Ortak görünüm: ESP ekranıyla aynı palet (koyu zemin, camgöbeği vurgu),
 * yuvarlatılmış kartlar, dokunma dalgalı düğmeler. androidx olmadan.
 */
object Ui {
    const val BG = 0xFF05090E.toInt()
    const val SURFACE = 0xFF0D141D.toInt()
    const val SURFACE_HI = 0xFF152131.toInt()
    const val BORDER = 0xFF1F2D3F.toInt()
    const val PRIMARY = 0xFF00E5FF.toInt()
    const val ACCENT = 0xFFFF9100.toInt()
    const val OK = 0xFF00E676.toInt()
    const val WARN = 0xFFFFC400.toInt()
    const val CRIT = 0xFFFF5252.toInt()
    const val TEXT = 0xFFEEF3F8.toInt()
    const val DIM = 0xFF7C8DA3.toInt()

    enum class Style { PRIMARY, SECONDARY, GHOST }

    fun dp(ctx: Context, v: Number): Int = (v.toFloat() * ctx.resources.displayMetrics.density + 0.5f).toInt()

    fun rounded(ctx: Context, color: Int, radiusDp: Int, stroke: Int? = null, strokeDp: Int = 1) =
        GradientDrawable().apply {
            setColor(color)
            cornerRadius = dp(ctx, radiusDp).toFloat()
            stroke?.let { setStroke(dp(ctx, strokeDp), it) }
        }

    fun applyWindow(a: Activity) {
        a.window.statusBarColor = BG
        a.window.navigationBarColor = BG
        a.window.decorView.setBackgroundColor(BG)
    }

    fun lp(ctx: Context, w: Int = ViewGroup.LayoutParams.MATCH_PARENT, h: Int = ViewGroup.LayoutParams.WRAP_CONTENT,
           bottomDp: Int = 0, weight: Float = 0f) =
        LinearLayout.LayoutParams(w, h, weight).apply { bottomMargin = dp(ctx, bottomDp) }

    fun column(ctx: Context, padDp: Int = 0) = LinearLayout(ctx).apply {
        orientation = LinearLayout.VERTICAL
        val p = dp(ctx, padDp)
        setPadding(p, p, p, p)
    }

    fun row(ctx: Context) = LinearLayout(ctx).apply {
        orientation = LinearLayout.HORIZONTAL
        gravity = Gravity.CENTER_VERTICAL
    }

    fun card(ctx: Context, accentStroke: Int? = null) = column(ctx, 16).apply {
        background = rounded(ctx, SURFACE, 18, accentStroke ?: BORDER)
    }

    fun text(ctx: Context, s: CharSequence, sp: Float = 15f, color: Int = TEXT, bold: Boolean = false) =
        TextView(ctx).apply {
            text = s
            textSize = sp
            setTextColor(color)
            if (bold) typeface = Typeface.create("sans-serif-medium", Typeface.BOLD)
        }

    /** Küçük, aralıklı büyük harf bölüm başlığı. */
    fun section(ctx: Context, s: String) = text(ctx, s.uppercase(java.util.Locale("tr")), 11f, DIM, true).apply {
        letterSpacing = 0.12f
        setPadding(dp(ctx, 4), dp(ctx, 8), 0, dp(ctx, 8))
    }

    fun dot(ctx: Context, color: Int, sizeDp: Int = 10) = View(ctx).apply {
        background = GradientDrawable().apply { shape = GradientDrawable.OVAL; setColor(color) }
        layoutParams = LinearLayout.LayoutParams(dp(ctx, sizeDp), dp(ctx, sizeDp)).apply { marginEnd = dp(ctx, 10) }
    }

    fun setDot(v: View, color: Int) {
        (v.background as GradientDrawable).setColor(color)
    }

    fun button(ctx: Context, label: String, style: Style = Style.SECONDARY, onClick: () -> Unit) =
        TextView(ctx).apply {
            text = label
            textSize = 15f
            gravity = Gravity.CENTER
            typeface = Typeface.create("sans-serif-medium", Typeface.NORMAL)
            val (bg, fg, stroke) = when (style) {
                Style.PRIMARY -> Triple(PRIMARY, BG, null)
                Style.SECONDARY -> Triple(SURFACE_HI, TEXT, BORDER)
                Style.GHOST -> Triple(0x00000000, PRIMARY, null)
            }
            setTextColor(fg)
            val content = rounded(ctx, bg, 14, stroke)
            background = RippleDrawable(ColorStateList.valueOf(0x33FFFFFF), content, rounded(ctx, 0xFFFFFFFF.toInt(), 14))
            val v = dp(ctx, if (style == Style.GHOST) 10 else 14)
            setPadding(dp(ctx, 16), v, dp(ctx, 16), v)
            isClickable = true
            isFocusable = true
            setOnClickListener { onClick() }
        }

    /** Ekran başlığı (geri ok + başlık) — alt ekranlar için. */
    fun topBar(a: Activity, title: String): View = row(a).apply {
        setPadding(dp(a, 8), dp(a, 12), dp(a, 16), dp(a, 12))
        addView(text(a, "‹", 30f, PRIMARY).apply {
            setPadding(dp(a, 12), 0, dp(a, 16), dp(a, 4))
            isClickable = true
            setOnClickListener { a.finish() }
        })
        addView(text(a, title, 20f, TEXT, true))
    }
}
